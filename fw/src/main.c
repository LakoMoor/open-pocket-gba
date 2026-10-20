// Open Pocket GBC menu firmware (FemtoRV32, runs from SDRAM).
//
// - Pocket-style menu on the HDMI OSD (42 x 24 cells) and the SPI LCD (16 x 16 cells)
// - games from the SD card (FAT32/exFAT, folders, long names) or the internal flash
// - cartridge saves: <rom>.sav on the SD card, or a slot in the SPI flash
// - settings stored in the SPI flash, so everything works without an SD card
// - power: the CPU sleeps in REG_WAIT, HDMI / LCD can be switched off, sleep timer
#include <string.h>
#include <strings.h>
#include "util.h"
#include "osd.h"
#include "ff.h"

#define FW_VERSION      "1.0"
#define GAMES_BASE      0x200000u
#define SAVE_BASE       0x600000u       // 15 slots of 0x22000: header sector + up to 128 KB
#define SAVE_SLOT       0x22000u
#define SET_MAGIC       0x3353504Fu     // "OPS3"
#define SET_MAGIC_V2    0x3253504Fu     // "OPS2": older layout, converted on load
#define CART_MAGIC      0x3143504Fu     // "OPC1"
#define CART_ROM        0x10000u        // ROM offset in the cartridge flash

// ================================================================ settings
typedef struct {
    uint32_t magic;
    uint8_t  disp_mode, dmg_pal, gbc_raw, lcd_crop;
    uint8_t  dbg_on, lcd_bri, volume, swap_ab;
    uint8_t  sys_mode, hdmi_off, lcd_off, led_en;
    uint8_t  autosave, toasts, boot_menu, sleep_min;
    uint8_t  last_src, last_flash, speaker, spk_boost;
    uint8_t  spk_mode, theme, sdram_pd, lcd_slow;      // theme: TH_*
    uint8_t  dim_idx, poll_slow, rsv[2];               // LCD auto-dim: off / 15 / 30 / 60 s
    char     last_path[216];
    uint32_t sum;
} settings_t;

typedef struct {                                       // "OPS2" layout
    uint32_t magic;
    uint8_t  b[20];                                    // disp_mode .. spk_boost, same order
    char     last_path[224];
    uint8_t  power_mode, spk_mode, rsv[2];
    uint32_t sum;
} settings_v2_t;

static settings_t set, set_stored;
static uint32_t set_addr;               // 0: flash has no room for settings / saves
static int      flash_saves;

static uint32_t sum32(const void *p, int n) {
    const uint8_t *b = p;
    uint32_t s = 0x1234;
    for (int i = 0; i < n; i++) s = s * 31 + b[i];
    return s;
}

static void set_defaults(void) {
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.disp_mode = 1;      // pixel grid, like the Pocket's original display mode
    set.dmg_pal = 2;        // Pocket
    set.lcd_bri = 3;
    set.volume = 4;
    set.autosave = 1;
    set.toasts = 1;
    set.spk_boost = 2;      // x4
    set.theme = 1;          // Ozone
}

static void settings_save(void) {
    set.sum = sum32(&set, sizeof set - 4);
    if (!set_addr || !memcmp(&set, &set_stored, sizeof set)) return;
    flash_write(set_addr, &set, sizeof set);
    set_stored = set;
    logf("settings saved\n");
}

// ================================================================ control registers
static uint32_t ctrl;

static void ctrl_write(void) { REG_CTRL = ctrl; }
static void ctrl_bits(uint32_t mask, int on) {
    if (on) ctrl |= mask; else ctrl &= ~mask;
    ctrl_write();
}

static const uint8_t bri_duty[4] = { 3, 7, 11, 15 };

static int lcd_dimmed;                  // LCD auto-dim active (no input for a while)
static uint8_t preset_sel;              // Power > Preset: 0 normal, 1 battery, 2 custom
static int preset_now(void);
static void preset_apply(int battery_preset);

static void apply_settings(void) {
    uint32_t duty = bri_duty[set.lcd_bri & 3];
    if (lcd_dimmed) duty = 1;
    REG_CFG = (set.disp_mode & 3) | ((set.dmg_pal & 7) << 2) | ((set.gbc_raw & 1) << 5) |
              ((set.lcd_crop & 1) << 6) | ((uint32_t)(set.dbg_on & 1) << 7) |
              (duty << 8) | ((uint32_t)(set.volume & 7) << 12) |
              ((uint32_t)(set.swap_ab & 1) << 15) | ((uint32_t)(set.speaker & 1) << 16) |
              ((uint32_t)(set.spk_boost & 3) << 17) | ((uint32_t)(set.spk_mode & 1) << 19);
    ctrl &= ~((3u << C_SYS_SHIFT) | C_HDMI_OFF | C_LCD_OFF | C_LED | C_SDRAM_PD | C_LCD_SLOW);
    ctrl |= ((uint32_t)set.sys_mode << C_SYS_SHIFT) | (set.hdmi_off ? C_HDMI_OFF : 0) |
            (set.lcd_off ? C_LCD_OFF : 0) | (set.led_en ? C_LED : 0) |
            (set.sdram_pd ? C_SDRAM_PD : 0) | (set.lcd_slow ? C_LCD_SLOW : 0);
    ctrl_write();
}

// ================================================================ storage
static FATFS fs;
static int   sd_mounted;

static void sd_mount(void) {
    sd_mounted = 0;
    if (sd_init() != 0) { logf("sd: no card\n"); return; }
    FRESULT r = f_mount(&fs, "", 1);
    sd_mounted = (r == FR_OK);
    logf("sd: %u MB, mount %d\n", sd_sectors() / 2048, r);
}

typedef struct { char name[24]; uint32_t ofs, size; } fl_entry_t;
static fl_entry_t fl_dir[15];
static int fl_count;

static void flash_dir_read(void) {
    uint32_t hdr[4];
    flash_read(GAMES_BASE, hdr, 16);
    fl_count = 0;
    if (hdr[0] != 0x50434247u) return;          // "GBCP"
    fl_count = hdr[1] > 15 ? 15 : hdr[1];
    flash_read(GAMES_BASE + 16, fl_dir, fl_count * sizeof(fl_entry_t));
    for (int i = 0; i < fl_count; i++) fl_dir[i].name[23] = 0;
}

// ================================================================ cartridge (external W25Qxx)
// layout: 0x000000 header sector, 0x010000 ROM, size - 0x21000 save header sector,
// size - 0x20000 save data (up to 128 KB)
typedef struct { uint32_t magic, rom_size, rsv[2]; char name[48]; } cart_hdr_t;
static uint32_t   cart_id, cart_size;
static cart_hdr_t cart;
static int        cart_valid;

static uint32_t cart_save_addr(void) { return cart_size - 0x21000; }
static uint32_t cart_rom_max(void) { return cart_size > 0x41000 ? cart_size - 0x21000 - CART_ROM : 0; }

static void cart_probe(void) {
    flash_select(1);
    flash_init();
    cart_id = flash_id();
    cart_size = flash_size(cart_id);
    cart_valid = 0;
    if (cart_size) {
        flash_read(0, &cart, sizeof cart);
        cart.name[47] = 0;
        cart_valid = cart.magic == CART_MAGIC && cart.rom_size >= 0x8000 && cart.rom_size <= cart_rom_max();
        flash_sleep();
    }
    flash_select(0);
    logf("cart: id %06x, %u KB, %s\n", cart_id, cart_size >> 10, cart_valid ? cart.name : "no game");
}

// ================================================================ game state
static struct {
    int      loaded, src, flash_idx;
    char     path[256];
    char     name[48];
    uint8_t  cart_type, rom_code, ram_code, cgb;
    uint32_t rom_bytes, save_bytes;
    int      battery;
    int      save_pending;
    uint32_t dirty_ms, saved_ms;
    int      saved_once;
} game;

static const char *mapper_name(uint8_t t) {
    switch (t) {
    case 0x00: case 0x08: case 0x09: return "ROM only";
    case 0x01: case 0x02: case 0x03: return "MBC1";
    case 0x05: case 0x06: return "MBC2";
    case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: return "MBC3";
    case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: return "MBC5";
    case 0x20: return "MBC6";
    case 0x22: return "MBC7";
    case 0xFC: return "Camera";
    case 0xFE: return "HuC3";
    case 0xFF: return "HuC1";
    }
    return "Unknown";
}

static int has_battery(uint8_t t) {
    switch (t) {
    case 0x03: case 0x06: case 0x09: case 0x0D: case 0x0F: case 0x10: case 0x13:
    case 0x1B: case 0x1E: case 0x22: case 0xFC: case 0xFD: case 0xFE: case 0xFF: return 1;
    }
    return 0;
}

static uint32_t ram_bytes(uint8_t type, uint8_t code) {
    if (type == 0x05 || type == 0x06) return 512;           // MBC2 internal RAM
    switch (code) {
    case 1: return 2048;
    case 2: return 8192;
    case 3: return 32768;
    case 4: return 131072;
    case 5: return 65536;
    }
    return 0;
}

// ================================================================ toasts and screens
static uint32_t toast_until;
static int menu_active;

static void hint(int x, int y, const char *key, const char *text, uint8_t bg) {
    osd_put(x, y, G_PILL_L | EXT, AT(CL_LINE, bg));
    x = osd_text(x + 1, y, key, AT(CL_WHITE, CL_LINE));
    osd_put(x, y, G_PILL_R | EXT, AT(CL_LINE, bg));
    osd_text(x + 2, y, text, AT(CL_LIGHT, bg));
}

static void toast(const char *msg, uint8_t icon, uint8_t colour) {
    if (menu_active) return;
    osd_clear(AT(CL_WHITE, CL_GAME));
    int w = (int)strlen(msg) + 6;
    int x = OSD_W - 2 - w;
    osd_pill(x, 1, w, CL_PANEL, CL_GAME);
    osd_put(x + 2, 1, icon, AT(colour, CL_PANEL));
    osd_text(x + 4, 1, msg, AT(CL_WHITE, CL_PANEL));
    osd_present();
    ctrl &= ~C_DIM;
    ctrl_bits(C_OSD, 1);
    toast_until = ms_now() + 1600;
}

// LCD: navy title bar on the top row, the rest cleared
static void lcd_header(const char *title) {
    lcd_clear(AT(CL_WHITE, CL_SHADE));
    for (int x = 0; x < LCD_W; x++) lcd_put(x, 0, ' ', AT(CL_WHITE, CL_NAVY));
    lcd_put(0, 0, G_STAR, AT(CL_BLUE, CL_NAVY));
    lcd_text(2, 0, title, AT(CL_WHITE, CL_NAVY));
}

static void progress_screen(const char *title, const char *name, uint32_t done, uint32_t total) {
    osd_clear(AT(CL_WHITE, CL_SHADE));
    osd_text(15, 9, title, AT(CL_GREY, CL_SHADE));
    int n = (int)strlen(name);
    if (n > 36) n = 36;
    osd_textn(21 - n / 2, 11, name, 36, AT(CL_WHITE, CL_SHADE));
    int w = 30, f = total ? (int)((uint64_t)done * (w - 2) / total) : 0;
    osd_put(6, 13, G_PILL_L | EXT, AT(CL_PANEL, CL_SHADE));
    osd_fill(7, 13, w - 2, ' ', AT(CL_WHITE, CL_PANEL));
    osd_put(6 + w - 1, 13, G_PILL_R | EXT, AT(CL_PANEL, CL_SHADE));
    if (f > 0) {
        osd_put(6, 13, G_PILL_L | EXT, AT(CL_BLUE, CL_SHADE));
        osd_fill(7, 13, f, ' ', AT(CL_WHITE, CL_BLUE));
    }
    osd_present();
    lcd_header(title);
    lcd_text(0, 4, name, AT(CL_WHITE, CL_SHADE));
    int lf = total ? (int)((uint64_t)done * 16 / total) : 0;
    lcd_pill(6, CL_PANEL, CL_SHADE);
    for (int x = 0; x < lf; x++)
        lcd_put(x, 6, x == 0 ? (G_PILL_L | EXT) : x == 15 ? (G_PILL_R | EXT) : ' ',
                x == 0 || x == 15 ? AT(CL_BLUE, CL_SHADE) : AT(CL_WHITE, CL_BLUE));
    char b[17];
    sfmt(b, sizeof b, "%u%%", total ? (unsigned)((uint64_t)done * 100 / total) : 0);
    lcd_text(8 - (int)strlen(b) / 2, 8, b, AT(CL_LIGHT, CL_SHADE));
    lcd_present();
}

static void message_screen(const char *title, const char *line) {
    osd_clear(AT(CL_WHITE, CL_SHADE));
    osd_text(21 - (int)strlen(title) / 2, 10, title, AT(CL_WHITE, CL_SHADE));
    osd_text(21 - (int)strlen(line) / 2, 12, line, AT(CL_GREY, CL_SHADE));
    osd_present();
    lcd_header(title);
    lcd_text(0, 5, line, AT(CL_LIGHT, CL_SHADE));
    if (strlen(line) > 16) lcd_text(0, 6, line + 16, AT(CL_LIGHT, CL_SHADE));
    lcd_present();
}

// ================================================================ saves
static void sav_path(char *out, const char *rom) {
    strcpy(out, rom);
    char *dot = strrchr(out, '.');
    char *sl = strrchr(out, '/');
    if (!dot || (sl && dot < sl)) dot = out + strlen(out);
    strcpy(dot, ".sav");
}

static uint32_t slot_addr(int idx) { return SAVE_BASE + (uint32_t)idx * SAVE_SLOT; }

// flash chip and header address of the save of a flash (1) or cartridge (3) game
static int save_slot(uint32_t *a) {
    if (game.src == 1 && flash_saves) { *a = slot_addr(game.flash_idx); return 0; }
    if (game.src == 3 && cart_size)   { *a = cart_save_addr(); return 1; }
    return -1;
}

static void save_load(void) {
    volatile uint8_t *cram = sdram(CRAM_BASE);
    for (uint32_t i = 0; i < 131072; i += 4) *(volatile uint32_t *)(cram + i) = 0xFFFFFFFFu;
    if (!game.battery || !game.save_bytes) return;
    if (game.src == 2 && sd_mounted) {
        char p[260];
        FIL f;
        sav_path(p, game.path);
        if (f_open(&f, p, FA_READ) == FR_OK) {
            UINT br;
            f_read(&f, (void *)cram, game.save_bytes, &br);
            f_close(&f);
            logf("save loaded: %s (%u)\n", p, br);
        }
    } else {
        uint32_t a, h[4];
        int chip = save_slot(&a);
        if (chip < 0) return;
        flash_select(chip);
        flash_read(a, h, 16);
        if (h[0] == 0x31564153u && h[1] == game.save_bytes && h[2] == sum32(game.name, 16)) {
            flash_read_sdram(a + 0x1000, CRAM_BASE, game.save_bytes);
            logf("save loaded from %s\n", chip ? "the cartridge" : "the flash");
        }
        flash_sleep();
        flash_select(0);
    }
}

static int save_write(void) {
    if (!game.loaded || !game.battery || !game.save_bytes) return -1;
    int ok = -1;
    if (game.src == 2 && sd_mounted) {
        char p[260];
        FIL f;
        sav_path(p, game.path);
        if (f_open(&f, p, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            UINT bw;
            f_write(&f, (const void *)sdram(CRAM_BASE), game.save_bytes, &bw);
            f_close(&f);
            ok = (bw == game.save_bytes) ? 0 : -1;
        }
    } else {
        uint32_t a, h[4] = { 0x31564153u, game.save_bytes, sum32(game.name, 16), 0 }, old[4];
        int chip = save_slot(&a);
        if (chip >= 0) {
            flash_select(chip);
            int n = flash_update_sdram(a + 0x1000, CRAM_BASE, game.save_bytes);
            flash_read(a, old, 16);
            if (memcmp(old, h, 16)) { flash_write(a, h, 16); n++; }
            flash_sleep();
            flash_select(0);
            logf("flash save: %d sectors\n", n);
            ok = n ? 0 : 1;                     // 1: nothing changed
        }
    }
    game.save_pending = 0;
    if (ok >= 0) { game.saved_ms = ms_now(); game.saved_once = 1; }
    logf("save written: %d\n", ok);
    return ok;
}

// ================================================================ loading
static void feed_header(uint32_t rom_bytes) {
    volatile uint8_t *rom = sdram(ROM_BASE);
    for (uint32_t bank = 0; bank < rom_bytes; bank += 0x4000) {
        for (uint32_t i = 0; i < 0x150; i += 2) {
            IOCTL_ADDR = bank + i;
            IOCTL_DATA = rom[bank + i] | (rom[bank + i + 1] << 8);
        }
    }
}

static void title_from_header(void) {
    volatile uint8_t *rom = sdram(ROM_BASE);
    int n = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t c = rom[0x134 + i];
        if (c == 0 || (i >= 11 && (c & 0x80))) break;
        game.name[n++] = (c >= 32 && c < 127) ? c : ' ';
    }
    while (n && game.name[n - 1] == ' ') n--;
    game.name[n] = 0;
}

static void name_from_path(const char *p) {
    const char *b = strrchr(p, '/');
    b = b ? b + 1 : p;
    int n = 0;
    while (b[n] && n < 47) { game.name[n] = b[n]; n++; }
    game.name[n] = 0;
    char *dot = strrchr(game.name, '.');
    if (dot && dot != game.name) *dot = 0;
}

static int cart_write_pending;           // write the loaded SD game to the cartridge

static void cart_progress(uint32_t done, uint32_t total) {
    progress_screen("Writing cartridge", game.name, done, total);
}

// writes the ROM in SDRAM to the cartridge flash
static int cart_write_rom(void) {
    if (!cart_size) { message_screen("No cartridge flash", "CS 51 CLK 48 DI 55 DO 49"); wait_ms(2000); return -1; }
    if (game.rom_bytes > cart_rom_max()) { message_screen("Cartridge too small", game.name); wait_ms(2000); return -1; }
    cart_hdr_t h;
    memset(&h, 0, sizeof h);
    flash_select(1);
    flash_init();
    flash_erase_range(0, 4096);                         // invalid until the ROM is complete
    flash_write_big(CART_ROM, ROM_BASE, game.rom_bytes, cart_progress);
    flash_erase_range(cart_save_addr(), 4096);          // drop the previous game's save
    h.magic = CART_MAGIC;
    h.rom_size = game.rom_bytes;
    strncpy(h.name, game.name, 47);
    flash_write(0, &h, sizeof h);
    // spot check: first and last 4 bytes of every 64 KB block
    int bad = 0;
    for (uint32_t o = 0; o < game.rom_bytes && !bad; o += 0x10000) {
        uint32_t v;
        flash_read(CART_ROM + o, &v, 4);
        if (v != *(volatile uint32_t *)sdram(ROM_BASE + o)) bad = 1;
    }
    flash_sleep();
    flash_select(0);
    if (bad) { message_screen("Cartridge write failed", "check the wiring"); wait_ms(2000); return -1; }
    cart = h;
    cart_valid = 1;
    logf("cartridge written: %s, %u bytes\n", h.name, h.rom_size);
    return 0;
}

// ---------------------------------------------------------------- home screen
extern const uint8_t home_rom[], home_rom_end[];
static int home_running;                // the home screen ROM runs in the core (no game loaded)

static void start_home(void) {
    uint32_t n = (uint32_t)(home_rom_end - home_rom);
    ctrl = (ctrl | C_CORE_RST | C_DOWNLOAD) & ~C_PAUSE;
    ctrl_write();
    memset(&game, 0, sizeof game);
    volatile uint8_t *rom = sdram(ROM_BASE);
    for (uint32_t i = 0; i < n; i++) rom[i] = home_rom[i];
    feed_header(n);
    ctrl_bits(C_DOWNLOAD, 0);
    wait_ms(2);
    ctrl_bits(C_CORE_RST, 0);
    home_running = 1;
    logf("home screen: %u bytes\n", n);
}

// src 1: flash index, src 2: SD path, src 3: cartridge
static int load_game_rom(int src, int idx, const char *path) {
    if (game.loaded && game.save_pending) save_write();
    // the core only resets while its clock enables run, so no pause during reset
    ctrl = (ctrl | C_CORE_RST | C_DOWNLOAD | C_OSD | C_DIM) & ~C_PAUSE;
    ctrl_write();
    game.loaded = 0;
    uint32_t size = 0;
    const char *nm = src == 1 ? fl_dir[idx].name : path;
    char shown[48];
    if (src == 1) strcpy(shown, fl_dir[idx].name);
    else if (src == 3) strcpy(shown, cart.name);
    else { const char *b = strrchr(path, '/'); strncpy(shown, b ? b + 1 : path, 47); shown[47] = 0; }
    (void)nm;

    if (src == 1) {
        size = fl_dir[idx].size;
        if (size > ROM_MAX) size = ROM_MAX;
        for (uint32_t o = 0; o < size; o += 0x10000) {
            progress_screen("Loading", shown, o, size);
            uint32_t n = size - o < 0x10000 ? size - o : 0x10000;
            flash_read_sdram(GAMES_BASE + fl_dir[idx].ofs + o, ROM_BASE + o, n);
        }
    } else if (src == 3) {
        if (!cart_valid) { message_screen("No game on the cartridge", ""); ctrl_bits(C_DOWNLOAD, 0); wait_ms(1500); return -1; }
        size = cart.rom_size;
        flash_select(1);
        for (uint32_t o = 0; o < size; o += 0x10000) {
            progress_screen("Loading cartridge", shown, o, size);
            uint32_t n = size - o < 0x10000 ? size - o : 0x10000;
            flash_read_sdram(CART_ROM + o, ROM_BASE + o, n);
        }
        flash_sleep();
        flash_select(0);
    } else {
        FIL f;
        if (!sd_mounted || f_open(&f, path, FA_READ) != FR_OK) {
            message_screen("Can't open file", shown);
            ctrl_bits(C_DOWNLOAD, 0);
            wait_ms(1500);
            return -1;
        }
        size = f_size(&f);
        if (size > ROM_MAX || size < 0x8000) {
            f_close(&f);
            message_screen(size < 0x8000 ? "File too small" : "ROM too large (6 MB max)", shown);
            ctrl_bits(C_DOWNLOAD, 0);
            wait_ms(1500);
            return -1;
        }
        for (uint32_t o = 0; o < size; o += 0x10000) {
            progress_screen("Loading", shown, o, size);
            UINT br;
            uint32_t n = size - o < 0x10000 ? size - o : 0x10000;
            if (f_read(&f, (void *)sdram(ROM_BASE + o), n, &br) != FR_OK || br != n) {
                f_close(&f);
                message_screen("Read error", shown);
            ctrl_bits(C_DOWNLOAD, 0);
                wait_ms(1500);
                return -1;
            }
        }
        f_close(&f);
    }
    progress_screen("Loading", shown, size, size);

    volatile uint8_t *rom = sdram(ROM_BASE);
    memset(&game, 0, sizeof game);
    game.src = src;
    game.flash_idx = idx;
    if (src == 2) { strncpy(game.path, path, 255); name_from_path(path); }
    else if (src == 3) strcpy(game.name, cart.name);
    else strcpy(game.name, fl_dir[idx].name);
    if (!game.name[0]) title_from_header();
    game.rom_bytes = size;
    game.cart_type = rom[0x147];
    game.rom_code  = rom[0x148];
    game.ram_code  = rom[0x149];
    game.cgb       = rom[0x143];
    game.battery   = has_battery(game.cart_type);
    game.save_bytes = ram_bytes(game.cart_type, game.ram_code);
    if (game.cart_type == 0x22) game.save_bytes = 256;      // MBC7 EEPROM (not kept)

    feed_header(size);
    save_load();
    int copy_save = 0;
    if (cart_write_pending) {
        cart_write_pending = 0;
        if (cart_write_rom() == 0) { game.src = src = 3; copy_save = 1; }
    }
    ctrl_bits(C_DOWNLOAD, 0);
    wait_ms(2);
    ctrl_bits(C_CORE_RST, 0);
    game.loaded = 1;
    if (copy_save && game.battery && game.save_bytes) save_write();    // the SD save goes along

    set.last_src = src;
    set.last_flash = idx;
    if (src == 2) { strncpy(set.last_path, path, sizeof set.last_path - 1); }
    settings_save();
    logf("loaded %s: %u bytes, type %x, save %u\n", game.name, size, game.cart_type, game.save_bytes);
    return 0;
}

static int load_game(int src, int idx, const char *path) {
    home_running = 0;
    int r = load_game_rom(src, idx, path);
    if (r) start_home();                // nothing runs after a failed load: back to the home screen
    return r;
}

static void reset_game(void) {
    if (!game.loaded) return;
    if (game.save_pending) save_write();
    uint32_t paused = ctrl & C_PAUSE;
    ctrl = (ctrl | C_CORE_RST) & ~C_PAUSE;
    ctrl_write();
    wait_ms(10);
    ctrl = (ctrl & ~C_CORE_RST) | paused;
    ctrl_write();
}

// ================================================================ input
static uint32_t held, rep_t;
static uint32_t last_input_ms;

static uint32_t poll_keys(uint32_t timeout) {
    uint32_t cur = REG_IN & 0xFFFF;
    if (cur == held) {
        uint32_t dp = cur & (K_UP | K_DOWN | K_LEFT | K_RIGHT);
        uint32_t t = timeout;
        if (dp) { int32_t d = (int32_t)(rep_t - ms_now()); t = d > 0 ? (uint32_t)d : 1; if (t > timeout) t = timeout; }
        REG_WAIT = t;
        (void)REG_WAIT;
        cur = REG_IN & 0xFFFF;
    }
    uint32_t now = ms_now();
    uint32_t press = cur & ~held;
    uint32_t dp = cur & (K_UP | K_DOWN | K_LEFT | K_RIGHT);
    if (press & dp) rep_t = now + 380;
    else if (dp && (int32_t)(now - rep_t) >= 0) { press |= dp; rep_t = now + 90; }
    if (cur != held) last_input_ms = now;
    held = cur;
    return press;
}

static void wait_release(void) {
    uint32_t end = ms_now() + 3000;
    while ((REG_IN & (K_PAD | K_S1 | K_S2)) && (int32_t)(ms_now() - end) < 0) { REG_WAIT = 50; (void)REG_WAIT; }
    held = REG_IN & 0xFFFF;
}

// ================================================================ sleep
static void do_sleep(void) {
    if (game.save_pending) save_write();
    settings_save();
    uint32_t saved = ctrl;
    ctrl = (ctrl | C_PAUSE | C_HDMI_OFF | C_LCD_OFF) & ~(C_LED | C_OSD);
    ctrl_write();
    flash_sleep();
    logf("sleep\n");
    wait_release();
    for (;;) {
        REG_DEEPWAIT = 60000;               // SDRAM in self refresh while waiting
        (void)REG_DEEPWAIT;
        if (REG_IN & (K_PAD | K_S1 | K_S2 | K_UMENU)) break;
    }
    wait_release();
    ctrl = saved;
    ctrl_write();
    last_input_ms = ms_now();
    logf("wake\n");
}

static const uint16_t sleep_minutes[4] = { 0, 5, 15, 30 };
static uint32_t sleep_after_ms(void) { return sleep_minutes[set.sleep_min & 3] * 60000u; }
static const uint8_t dim_seconds[4] = { 0, 15, 30, 60 };

// ================================================================ menu model
enum {
    P_MAIN, P_LIBRARY, P_BROWSER, P_DISPLAY, P_AUDIO, P_CONTROLS, P_SYSTEM, P_INFO,
    P_CART, P_FILEACT, P_GAME, P_POWER, P_SLEEP
};
enum {
    I_NONE, I_RESUME, I_LIBRARY, I_SAVE, I_RESET, I_PG_DISPLAY, I_PG_AUDIO, I_PG_CONTROLS,
    I_PG_SYSTEM, I_PG_INFO, I_SLEEP_NOW,
    I_SRC_SD, I_SRC_FLASH, I_FLASH_GAME, I_FILE, I_DIR, I_UP,
    I_OPT, I_DEFAULTS, I_INFO,
    I_SRC_CART, I_PLAY_CART, I_ERASE_CART, I_PLAY_SD, I_WRITE_CART, I_PG_POWER
};

typedef struct {
    uint8_t  id, icon;
    const char *label;
    const char *desc;
    uint8_t *val;                // option
    uint8_t  nopt;
    const char *const *opts;
    char     value[40];          // shown on the right
    int      arg;
} item_t;

#define MAX_ITEMS 520
static item_t items[MAX_ITEMS];
static int nitems;
static int page, cursor, scroll;
static char page_title[48];

static const char *const o_disp[]  = { "Original", "Pixel Grid", "Scanlines" };
static const char *const o_pal[]   = { "Gray", "Green", "Pocket", "Light", "Amber" };
static const char *const o_col[]   = { "Corrected", "Raw" };
static const char *const o_onoff[] = { "On", "Off" };       // value 0 = on (hdmi_off, lcd_off)
static const char *const o_offon[] = { "Off", "On" };
static const char *const o_crop[]  = { "Fit", "Crop" };
static const char *const o_bri[]   = { "25%", "50%", "75%", "100%" };
static const char *const o_vol[]   = { "Mute", "25%", "50%", "75%", "100%" };
static const char *const o_sys[]   = { "Auto", "Game Boy Color", "Game Boy" };
static const char *const o_boot[]  = { "Last Game", "Menu" };
static const char *const o_sleep[] = { "Off", "5 min", "15 min", "30 min" };
static const char *const o_boost[] = { "x1", "x2", "x4", "x8" };
static const char *const o_preset[] = { "Normal", "Battery", "Custom" };
static const char *const o_theme[] = { "Pocket", "Ozone (Switch)", "XMB (PS3)", "Game Boy", "iPod", "BIOS", "Terminal" };
static const char *const o_fps[]   = { "60 fps", "30 fps" };
static const char *const o_dim[]   = { "Off", "15 s", "30 s", "60 s" };
static const char *const o_poll[]  = { "200 ms", "500 ms" };
static const char *const o_spkmode[] = { "PWM 66 kHz", "Sigma-delta" };

static item_t *add(uint8_t id, uint8_t icon, const char *label, const char *desc) {
    if (nitems >= MAX_ITEMS) return &items[MAX_ITEMS - 1];
    item_t *it = &items[nitems++];
    memset(it, 0, sizeof *it);
    it->id = id; it->icon = icon; it->label = label; it->desc = desc;
    return it;
}
static void add_opt(uint8_t icon, const char *label, const char *desc, uint8_t *v, uint8_t n, const char *const *o) {
    item_t *it = add(I_OPT, icon, label, desc);
    it->val = v; it->nopt = n; it->opts = o;
}
static void add_info(const char *label, const char *fmt, ...) {
    item_t *it = add(I_INFO, 0, label, 0);
    va_list ap;
    va_start(ap, fmt);
    vsfmt(it->value, sizeof it->value, fmt, ap);
    va_end(ap);
}

// ---------------------------------------------------------------- SD browser
static char cur_dir[256] = "/";
static char act_path[260];              // file chosen in the SD browser
static int  br_cursor, br_scroll;
static char name_pool[48 * 1024];
static int  pool_used;

static int is_rom_name(const char *n) {
    const char *dot = strrchr(n, '.');
    if (!dot) return 0;
    return !strcasecmp(dot, ".gb") || !strcasecmp(dot, ".gbc") || !strcasecmp(dot, ".sgb");
}

static int item_less(const item_t *a, const item_t *b) {
    if (a->id != b->id) return a->id == I_DIR;
    return strcasecmp(a->label, b->label) < 0;
}

static void build_browser(void) {
    nitems = 0;
    pool_used = 0;
    if (strcmp(cur_dir, "/")) add(I_UP, G_CHEV_L, "..", "Back to the parent folder");
    DIR d;
    FILINFO fi;
    if (f_opendir(&d, cur_dir) != FR_OK) return;
    int first = nitems;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        if (fi.fname[0] == '.' || (fi.fattrib & (AM_HID | AM_SYS))) continue;
        int dir = (fi.fattrib & AM_DIR) != 0;
        if (!dir && !is_rom_name(fi.fname)) continue;
        int n = (int)strlen(fi.fname) + 1;
        if (pool_used + n > (int)sizeof name_pool || nitems >= MAX_ITEMS) break;
        char *s = name_pool + pool_used;
        memcpy(s, fi.fname, n);
        pool_used += n;
        item_t *it = add(dir ? I_DIR : I_FILE, dir ? G_FOLDER : G_CART, s, 0);
        if (!dir) {
            if (fi.fsize >= 1024 * 1024) sfmt(it->value, sizeof it->value, "%u.%u MB", fi.fsize >> 20, ((fi.fsize >> 10) & 1023) * 10 / 1024);
            else sfmt(it->value, sizeof it->value, "%u KB", fi.fsize >> 10);
        }
    }
    f_closedir(&d);
    // insertion sort: folders first, then by name
    for (int i = first + 1; i < nitems; i++) {
        item_t t = items[i];
        int j = i - 1;
        while (j >= first && item_less(&t, &items[j])) { items[j + 1] = items[j]; j--; }
        items[j + 1] = t;
    }
}

// ---------------------------------------------------------------- page builders
static void build_page(void) {
    nitems = 0;
    switch (page) {
    case P_MAIN: {
        strcpy(page_title, "Menu");
        if (game.loaded) {
            item_t *it = add(I_RESUME, G_PLAY, "Resume", "Return to the game.");
            strncpy(it->value, game.name, 30);
        }
        add(I_LIBRARY, G_LIBRARY, "Library", "Choose a game from the SD card or the internal flash.");
        if (game.loaded && game.battery && game.save_bytes) {
            item_t *it = add(I_SAVE, G_SAVE, "Save Game", "Write the cartridge save now. Autosave also does this on its own.");
            if (game.save_pending) strcpy(it->value, "Unsaved changes");
            else if (game.saved_once) sfmt(it->value, sizeof it->value, "Saved %us ago", (ms_now() - game.saved_ms) / 1000);
            else strcpy(it->value, game.src == 2 ? "SD card" : "Internal flash");
        }
        if (game.loaded) add(I_RESET, G_RESET, "Reset Game", "Restart the game. The save is written first.");
        add(I_PG_DISPLAY, G_DISPLAY, "Display", "Display mode, palettes, HDMI and LCD.");
        add(I_PG_AUDIO, G_AUDIO, "Audio", "Volume and the buzzer / speaker output.");
        add(I_PG_CONTROLS, G_PAD, "Controls", "Button layout and shortcuts.");
        add(I_PG_SYSTEM, G_GEAR, "System", "Theme, model, saves, start-up.");
        add(I_PG_POWER, G_POWER, "Power", "Battery preset and every power saving setting.");
        add(I_PG_INFO, G_INFO, "Info", "Game, storage and system information.");
        add(I_SLEEP_NOW, G_MOON, "Sleep", "Pause and switch off HDMI and the LCD. Press any button to wake up.");
        break;
    }
    case P_GAME: {
        strcpy(page_title, game.name);
        item_t *it = add(I_RESUME, G_PLAY, "Resume", "Return to the game.");
        (void)it;
        if (game.battery && game.save_bytes) {
            it = add(I_SAVE, G_SAVE, "Save Game", "Write the cartridge save now. Autosave also does this on its own.");
            if (game.save_pending) strcpy(it->value, "Unsaved");
            else if (game.saved_once) sfmt(it->value, sizeof it->value, "%us ago", (ms_now() - game.saved_ms) / 1000);
        }
        add(I_RESET, G_RESET, "Reset Game", "Restart the game. The save is written first.");
        break;
    }
    case P_SLEEP:
        strcpy(page_title, "Sleep");
        add(I_SLEEP_NOW, G_MOON, "Sleep Now", "Pause, switch off HDMI and the LCD, SDRAM in self refresh. Any button wakes up.");
        break;
    case P_POWER:
        strcpy(page_title, "Power");
        preset_sel = preset_now();
        add_opt(G_POWER, "Preset", "Normal or Battery sets everything below. Hold S2 to switch.", &preset_sel, 3, o_preset);
        add_opt(G_DISPLAY, "HDMI Output", "Off: PLL powered down, outputs tri-stated, no framebuffer traffic.", &set.hdmi_off, 2, o_onoff);
        add_opt(G_FLASH, "SDRAM Power-Down", "SDRAM sleeps between accesses (2 cycles more latency).", &set.sdram_pd, 2, o_offon);
        add_opt(G_DISPLAY, "LCD Frame Rate", "30 fps halves the SPI traffic to the LCD.", &set.lcd_slow, 2, o_fps);
        add_opt(G_POWER, "LCD Backlight", "The backlight is the largest consumer on the LCD.", &set.lcd_bri, 4, o_bri);
        add_opt(G_MOON, "LCD Auto-Dim", "Backlight almost off after this long without input.", &set.dim_idx, 4, o_dim);
        add_opt(G_POWER, "Activity LEDs", "Board LEDs while loading and in the menu.", &set.led_en, 2, o_offon);
        add_opt(G_MOON, "Sleep Timer", "Sleep after this long without a button press.", &set.sleep_min, 4, o_sleep);
        add_opt(G_GEAR, "Menu CPU Polling", "How often the menu CPU wakes up while a game runs.", &set.poll_slow, 2, o_poll);
        break;
    case P_LIBRARY: {
        strcpy(page_title, "Library");
        item_t *it = add(I_SRC_SD, G_SDCARD, "SD Card", ".gb / .gbc files on a FAT32 or exFAT card. Saves go next to the ROM.");
        strcpy(it->value, sd_mounted ? "Ready" : "No card");
        it = add(I_SRC_FLASH, G_FLASH, "Internal Flash", "Games written with make_games.py. Saves stay in the flash.");
        sfmt(it->value, sizeof it->value, "%d games", fl_count);
        it = add(I_SRC_CART, G_CART, "Cartridge", "External W25Q flash: play the game written to it from the SD card.");
        strncpy(it->value, cart_valid ? cart.name : cart_size ? "Empty" : "Not found", 22);
        break;
    }
    case P_CART: {
        strcpy(page_title, "Cartridge");
        if (cart_valid) {
            item_t *it = add(I_PLAY_CART, G_PLAY, "Play", "Load the game from the cartridge. Saves are kept on it.");
            strncpy(it->value, cart.name, 24);
        }
        if (cart_size) {
            add_info("Chip", "%06x, %u MB", cart_id, cart_size >> 20 ? cart_size >> 20 : 1);
            if (cart_valid) {
                add_info("ROM", "%u KB", cart.rom_size >> 10);
                add_info("Room for", "%u KB ROM", cart_rom_max() >> 10);
            } else add_info("Game", "none (write from SD)");
            add(I_ERASE_CART, G_RESET, "Erase Cartridge", "Remove the game and its save from the cartridge flash.");
        } else {
            add_info("Status", "no chip found");
            add_info("Wiring", "CS51 CLK48 DI55 DO49");
            add_info("", "WP, HOLD, VCC to 3V3");
        }
        break;
    }
    case P_FILEACT: {
        item_t *it = add(I_PLAY_SD, G_PLAY, "Play", "Load the game from the SD card. The save is <name>.sav next to it.");
        (void)it;
        it = add(I_WRITE_CART, G_CART, "Write to Cartridge", "Copy the game and its save to the W25Q flash, then play it from there.");
        strncpy(it->value, cart_size ? (cart_valid ? "replaces game" : "empty") : "no chip", 22);
        break;
    }
    case P_BROWSER:
        if (page_title[0] == 'I') {             // internal flash list
            for (int i = 0; i < fl_count; i++) {
                item_t *f = add(I_FLASH_GAME, G_CART, fl_dir[i].name, 0);
                f->arg = i;
                sfmt(f->value, sizeof f->value, "%u KB", fl_dir[i].size >> 10);
                if (game.loaded && game.src == 1 && game.flash_idx == i) strcpy(f->value, "Playing");
            }
        } else build_browser();
        break;
    case P_DISPLAY:
        strcpy(page_title, "Display");
        add_opt(G_DISPLAY, "Display Mode", "How each Game Boy pixel is drawn on HDMI. Pixel Grid imitates the LCD.", &set.disp_mode, 3, o_disp);
        add_opt(G_STAR, "DMG Palette", "Colours for original Game Boy games.", &set.dmg_pal, 5, o_pal);
        add_opt(G_STAR, "GBC Colors", "Corrected imitates the GBC screen. Raw shows the colours as stored.", &set.gbc_raw, 2, o_col);
        add_opt(G_DISPLAY, "HDMI Output", "Off powers down the HDMI clock to save energy. Hold S1 for 1 s to toggle.", &set.hdmi_off, 2, o_onoff);
        add_opt(G_DISPLAY, "SPI LCD", "The 1.44 inch 128x128 screen. Off stops it and its backlight.", &set.lcd_off, 2, o_onoff);
        add_opt(G_DISPLAY, "LCD Scaling", "Fit scales 160x144 to 128x115. Crop shows the centre 1:1.", &set.lcd_crop, 2, o_crop);
        add_opt(G_POWER, "LCD Backlight", "Lower brightness uses less power.", &set.lcd_bri, 4, o_bri);
        add_opt(G_INFO, "Debug Overlay", "FPS, buttons and cartridge info in the HDMI border.", &set.dbg_on, 2, o_offon);
        break;
    case P_AUDIO:
        strcpy(page_title, "Audio");
        add_opt(G_AUDIO, "Volume", "Volume for HDMI audio and the speaker.", &set.volume, 5, o_vol);
        add_opt(G_AUDIO, "Speaker", "Buzzer or small speaker on pins 76 / 75 (sigma-delta audio).", &set.speaker, 2, o_offon);
        add_opt(G_AUDIO, "Speaker Signal", "PWM suits buzzers and transistors; sigma-delta needs an RC filter or amp.", &set.spk_mode, 2, o_spkmode);
        add_opt(G_AUDIO, "Speaker Boost", "Extra gain for the speaker. Too high clips loud sounds.", &set.spk_boost, 4, o_boost);
        break;
    case P_CONTROLS:
        strcpy(page_title, "Controls");
        add_opt(G_PAD, "Swap A / B", "Exchange the A and B buttons.", &set.swap_ab, 2, o_offon);
        add_info("Open menu", "SELECT + START, S1");
        add_info("HDMI on / off", "Hold S1");
        add_info("Reset game", "S2");
        add_info("Battery preset", "Hold S2");
        add_info("Navigate", "D-pad, A, B");
        break;
    case P_SYSTEM:
        strcpy(page_title, "System");
        add_opt(G_STAR, "Theme", "Look of the menu on HDMI and on the LCD.", &set.theme, 7, o_theme);
        add_opt(G_CART, "Model", "Auto follows the cartridge header. Applies when the game is reset.", &set.sys_mode, 3, o_sys);
        add_opt(G_SAVE, "Autosave", "Write the save 2 s after the game changes it, and when the menu opens.", &set.autosave, 2, o_offon);
        add_opt(G_CHECK, "Save Notifications", "Show a short message when the save was written.", &set.toasts, 2, o_offon);
        add_opt(G_PLAY, "Start-up", "Start the last game or open the menu at power-up.", &set.boot_menu, 2, o_boot);
        add(I_DEFAULTS, G_RESET, "Reset Settings", "Restore all settings to their defaults.");
        break;
    case P_INFO: {
        strcpy(page_title, "Info");
        uint32_t st = REG_STAT;
        if (st & 4) { game.save_pending = 1; game.dirty_ms = ms_now(); }
        if (game.loaded) {
            add_info("Game", "%s", game.name);
            add_info("Cartridge", "%s%s", mapper_name(game.cart_type), game.battery ? " + battery" : "");
            add_info("ROM", "%u KB", game.rom_bytes >> 10);
            if (game.save_bytes) add_info("Save RAM", "%u KB%s", game.save_bytes >> 10 ? game.save_bytes >> 10 : 1, game.battery ? "" : " (not kept)");
            add_info("Running as", "%s%s", (st & 1) ? "Game Boy Color" : "Game Boy", (st & 2) ? " 2x" : "");
            add_info("Source", "%s", game.src == 2 ? "SD card" : game.src == 3 ? "Cartridge" : "Internal flash");
        } else add_info("Game", "none loaded");
        if (sd_mounted) add_info("SD card", "%u MB", sd_sectors() / 2048);
        else add_info("SD card", "not found");
        add_info("Flash", "%06x, %d games", flash_id(), fl_count);
        if (cart_size) add_info("Cartridge", "%u MB%s%s", cart_size >> 20 ? cart_size >> 20 : 1, cart_valid ? ", " : "", cart_valid ? cart.name : "");
        else add_info("Cartridge", "not connected");
        uint32_t s = ms_now() / 1000;
        add_info("Uptime", "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
        add_info("Firmware", "Open Pocket GBC " FW_VERSION);
        add_info("Menu CPU", "FemtoRV32 RV32E");
        break;
    }
    }
}

// ---------------------------------------------------------------- themes
// Pocket and Game Boy: one list per page. Ozone (sidebar) and XMB (category bar): the
// categories are a second level; focus 0 = categories, 1 = the page of the category.
enum { TH_POCKET, TH_OZONE, TH_XMB, TH_GAMEBOY, TH_IPOD, TH_BIOS, TH_TERM, TH_COUNT };
static int two_level(void) { return set.theme == TH_OZONE || set.theme == TH_XMB; }

typedef struct { uint8_t icon; const char *name; uint8_t page; } cat_t;
static cat_t cats[10];
static int ncats, cat, focus;

static void build_cats(void) {
    ncats = 0;
    if (game.loaded) cats[ncats++] = (cat_t){ G_PLAY, "Game", P_GAME };
    cats[ncats++] = (cat_t){ G_LIBRARY, "Library", P_LIBRARY };
    cats[ncats++] = (cat_t){ G_DISPLAY, "Display", P_DISPLAY };
    cats[ncats++] = (cat_t){ G_AUDIO, "Audio", P_AUDIO };
    cats[ncats++] = (cat_t){ G_PAD, "Controls", P_CONTROLS };
    cats[ncats++] = (cat_t){ G_GEAR, "System", P_SYSTEM };
    cats[ncats++] = (cat_t){ G_POWER, "Power", P_POWER };
    cats[ncats++] = (cat_t){ G_INFO, "Info", P_INFO };
    cats[ncats++] = (cat_t){ G_MOON, "Sleep", P_SLEEP };
    if (cat >= ncats) cat = 0;
}

static int selectable(int i) { return items[i].id != I_INFO; }

// value text of an item (options: the current choice)
static const char *item_value(item_t *it, int sel, char *buf, int n) {
    if (it->id == I_OPT) {
        if (sel) { sfmt(buf, n, "%c %s %c", G_CHEV_L, it->opts[*it->val], G_CHEV_R); return buf; }
        return it->opts[*it->val];
    }
    if (it->id == I_DIR || (it->id >= I_PG_DISPLAY && it->id <= I_PG_INFO) || it->id == I_PG_POWER ||
        it->id == I_LIBRARY || it->id == I_SRC_SD || it->id == I_SRC_FLASH) {
        if (!it->value[0] || it->id == I_DIR) { buf[0] = G_CHEV_R; buf[1] = 0; return buf; }
    }
    return it->value;
}

// word-wrapped description, up to `lines` lines of width w
static void draw_desc(int x, int y, int w, int lines, uint8_t attr) {
    if (cursor >= nitems || !items[cursor].desc) return;
    const char *d = items[cursor].desc;
    for (int line = 0; *d && line < lines; line++) {
        int n = (int)strlen(d), cut = n;
        if (n > w) { cut = w; while (cut > 0 && d[cut] != ' ') cut--; if (!cut) cut = w; }
        osd_textn(x, y + line, d, cut, attr);
        d += cut;
        while (*d == ' ') d++;
    }
}

static void upcase(char *d, const char *s, int n) {
    int i = 0;
    for (; s[i] && i < n - 1; i++) d[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
    d[i] = 0;
}
static void lowcase(char *d, const char *s, int n) {
    int i = 0;
    for (; s[i] && i < n - 1; i++) d[i] = (s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i];
    d[i] = 0;
}
static int is_file(const item_t *it) { return it->id == I_FILE || it->id == I_DIR || it->id == I_FLASH_GAME; }

// label in the theme's letter case: 1 upper, 2 lower (file names keep their case)
static const char *label_case(const item_t *it, int caps, char *buf, int n) {
    if (caps == 1) { upcase(buf, it->label, n); return buf; }
    if (caps == 2 && !is_file(it)) { lowcase(buf, it->label, n); return buf; }
    return it->label;
}

// Marquee: the selected text scrolls when it does not fit (pause, scroll, pause, again).
static uint32_t mq_t0;
static int mq_cursor = -1, mq_page = -1, mq_on;
static const char *marquee(const char *s, int w) {
    int n = (int)strlen(s);
    if (w <= 0 || n <= w) return s;
    mq_on = 1;
    int steps = n - w, p = (int)((ms_now() - mq_t0) / 250) % (steps + 8);
    return s + (p < 4 ? 0 : p - 4 > steps ? steps : p - 4);
}

// List drawing shared by the themes.
typedef struct {
    uint8_t bg, fg, label_sel, icon, icon_sel, val, val_sel, info_label, sel_fill;
    uint8_t pill, bar, caps;     // rounded selection, Ozone-style bar, 1 upper / 2 lower case labels
    uint8_t cursor;              // cursor glyph left of the selected row instead of a filled bar
    uint8_t noicon, brackets;    // no item icons; values as [value]
    uint8_t chev;                // replaces the page chevron (e.g. '/')
} lstyle_t;

static void draw_list(int x0, int y0, int w, int rows, const lstyle_t *st, int show_sel) {
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + rows) scroll = cursor - rows + 1;
    for (int r = 0; r < rows; r++) {
        int i = scroll + r;
        if (i >= nitems) break;
        item_t *it = &items[i];
        int y = y0 + r;
        int sel = show_sel && i == cursor && selectable(i);
        uint8_t tb = sel && !st->cursor ? st->sel_fill : st->bg;
        if (sel && st->cursor) osd_put(x0, y, st->cursor, AT(st->icon_sel, st->bg));
        else if (sel) {
            if (st->pill) osd_pill(x0, y, w, st->sel_fill, st->bg);
            else osd_fill(x0, y, w, ' ', AT(st->fg, st->sel_fill));
            if (st->bar) osd_put(x0, y, G_THUMB | EXT, AT(CL_BLUE, st->sel_fill));
        }
        int lx = x0 + 2;
        if (it->icon && !st->noicon) { osd_put(lx, y, it->icon, AT(sel ? st->icon_sel : st->icon, tb)); lx += 2; }
        char vb[44], lb[44];
        const char *val = item_value(it, sel, vb, sizeof vb);
        if (val[0] == G_CHEV_R && !val[1] && st->chev) { vb[0] = st->chev; vb[1] = 0; val = vb; }
        else if (st->brackets && it->id == I_OPT) { char t[44]; sfmt(t, sizeof t, "[%s]", it->opts[*it->val]); strcpy(vb, t); val = vb; }
        int vlen = (int)strlen(val);
        if (it->id == I_INFO) {
            osd_textn(lx, y, label_case(it, st->caps, lb, sizeof lb), 13, AT(st->info_label, tb));
            osd_textn(x0 + 15, y, val, w - 16, AT(sel ? st->val_sel : st->fg, tb));
            continue;
        }
        if (vlen > w - 12 && it->id == I_OPT) { val = it->opts[*it->val]; vlen = (int)strlen(val); }   // drop the arrows
        if (vlen > w - 12) vlen = w - 12;
        if (it->id == I_OPT && it->val == &set.volume)
            for (int k = 0; k < 4; k++)
                osd_put(x0 + w - 2 - vlen - 6 + k, y, k < set.volume ? G_SQ_FULL : G_SQ_EMPTY, AT(sel ? st->icon_sel : st->icon, tb));
        int maxl = x0 + w - 2 - lx - (vlen ? vlen + 2 : 0);
        const char *lab = label_case(it, st->caps, lb, sizeof lb);
        osd_textn(lx, y, sel ? marquee(lab, maxl) : lab, maxl, AT(sel ? st->label_sel : st->fg, tb));
        if (vlen) osd_textn(x0 + w - 2 - vlen, y, val, vlen, AT(sel ? st->val_sel : st->val, tb));
    }
    if (nitems > rows) {                         // scroll bar
        int th = rows * rows / nitems;
        if (th < 1) th = 1;
        int ty = (rows - th) * scroll / (nitems - rows);
        for (int r = 0; r < rows; r++)
            osd_put(x0 + w, y0 + r, (r >= ty && r < ty + th ? G_THUMB : G_TRACK) | EXT,
                    AT(r >= ty && r < ty + th ? st->val : st->info_label, st->bg));
    }
    if (nitems == 0) osd_text(x0 + 2, y0, page == P_BROWSER ? "No games in this folder" : "Empty", AT(st->val, st->bg));
}

static const char *title_now(char *buf, int n) {
    if (page == P_BROWSER && page_title[0] != 'I') {
        int l = (int)strlen(cur_dir);
        sfmt(buf, n, "SD %s", l > 20 ? cur_dir + l - 20 : cur_dir);
        return buf;
    }
    return page_title;
}

static void status_icons(int x, int y, uint8_t bg, uint8_t on, uint8_t off) {
    osd_put(x, y, G_SDCARD, AT(sd_mounted ? on : off, bg));
    osd_put(x + 2, y, G_DISPLAY, AT(set.hdmi_off ? off : on, bg));
}

// ---- Pocket
static void draw_pocket(void) {
    uint8_t bg = CL_SHADE;
    char tb[48];
    osd_clear(AT(CL_WHITE, bg));
    osd_put(2, 1, G_STAR, AT(CL_BLUE, bg));
    osd_text(4, 1, "Open Pocket", AT(CL_WHITE, bg));
    osd_rtext(36, 1, title_now(tb, sizeof tb), AT(CL_GREY, bg));
    status_icons(37, 1, bg, CL_GB3, CL_LINE);
    osd_fill(2, 2, 38, G_HLINE, AT(CL_LINE, bg));
    static const lstyle_t st = { CL_SHADE, CL_WHITE, CL_BLACK, CL_LIGHT, CL_BLUE, CL_LIGHT, CL_BLACK, CL_GREY, CL_WHITE, 1, 0, 0 };
    draw_list(2, 4, 38, 14, &st, 1);
    osd_fill(2, 19, 38, G_HLINE, AT(CL_LINE, bg));
    draw_desc(2, 20, 38, 2, AT(CL_LIGHT, bg));
    hint(2, 22, "A", "Select", bg);
    hint(13, 22, "B", page == P_MAIN ? "Close" : "Back", bg);
    if (page == P_MAIN && game.loaded) hint(23, 22, "START", "Resume", bg);
}

// ---- Game Boy (DMG greens, framed, upper case)
static void draw_gameboy(void) {
    uint8_t in = CL_GB3, fr = CL_GB0;
    char tb[48], up[48];
    osd_clear(AT(fr, fr));
    for (int y = 2; y < 21; y++) osd_fill(2, y, 38, ' ', AT(fr, in));
    upcase(up, page == P_MAIN ? "Open Pocket" : title_now(tb, sizeof tb), sizeof up);
    osd_text(21 - (int)strlen(up) / 2, 1, up, AT(in, fr));
    static const lstyle_t st = { CL_GB3, CL_GB0, CL_GB3, CL_GB1, CL_GB3, CL_GB1, CL_GB2, CL_GB1, CL_GB0, 0, 0, 1 };
    draw_list(2, 3, 38, 13, &st, 1);
    osd_fill(3, 17, 36, G_HLINE, AT(CL_GB1, in));
    draw_desc(3, 18, 36, 2, AT(CL_GB1, in));
    osd_text(3, 22, "A SELECT", AT(in, fr));
    osd_text(14, 22, page == P_MAIN ? "B CLOSE" : "B BACK", AT(in, fr));
    osd_text(28, 22, "OPEN POCKET", AT(CL_GB2, fr));
}

// ---- Ozone (Switch): sidebar with categories, page on the right
static void draw_ozone(void) {
    uint8_t sb = CL_PANEL, bg = CL_SHADE;
    char tb[48];
    osd_clear(AT(CL_WHITE, bg));
    for (int y = 0; y < OSD_H; y++) osd_fill(0, y, 11, ' ', AT(CL_WHITE, sb));
    for (int y = 0; y < OSD_H; y++) osd_put(11, y, G_TRACK | EXT, AT(CL_LINE, bg));
    osd_put(1, 1, G_STAR, AT(CL_BLUE, sb));
    osd_text(3, 1, "Pocket", AT(CL_WHITE, sb));
    for (int i = 0; i < ncats; i++) {
        int y = 4 + i * 2, s = (i == cat);
        uint8_t b = (s && focus == 0) ? CL_LINE : sb;
        if (s && focus == 0) osd_fill(0, y, 11, ' ', AT(CL_WHITE, b));
        if (s) osd_put(0, y, G_THUMB | EXT, AT(CL_BLUE, b));
        osd_put(2, y, cats[i].icon, AT(s ? CL_BLUE : CL_GREY, b));
        osd_text(4, y, cats[i].name, AT(s ? CL_BLUE : CL_LIGHT, b));
    }
    osd_textn(13, 1, title_now(tb, sizeof tb), 23, AT(CL_WHITE, bg));
    status_icons(37, 1, bg, CL_GB3, CL_LINE);
    osd_fill(13, 2, 28, G_HLINE, AT(CL_LINE, bg));
    static const lstyle_t st = { CL_SHADE, CL_WHITE, CL_WHITE, CL_GREY, CL_BLUE, CL_GREY, CL_BLUE, CL_GREY, CL_LINE, 0, 1, 0 };
    draw_list(13, 4, 27, 14, &st, focus == 1);
    osd_fill(13, 18, 28, G_HLINE, AT(CL_LINE, bg));
    if (focus == 1) draw_desc(13, 19, 28, 3, AT(CL_LIGHT, bg));
    else osd_text(13, 19, "Right / A: open", AT(CL_GREY, bg));
    hint(13, 22, "A", focus ? "OK" : "Open", bg);
    hint(23, 22, "B", focus ? "Back" : "Close", bg);
}

// ---- XMB (PS3 / PSP): category icons in a row, the page below the selected one
static void draw_xmb(void) {
    uint8_t bg = CL_NAVY;
    char tb[48];
    osd_clear(AT(CL_WHITE, bg));
    // a calm "wave" across the bottom
    for (int x = 0; x < OSD_W; x++) {
        osd_put(x, 20 + ((x / 7) & 1), G_HLINE, AT(CL_BLUE, bg));
        osd_put(x, 21 - ((x / 9) & 1), G_HLINE, AT(CL_LINE, bg));
    }
    osd_rtext(40, 1, game.loaded ? game.name : "Open Pocket", AT(CL_GREY, bg));
    const int cx = 8;                            // column of the selected category
    for (int i = 0; i < ncats; i++) {
        int x = cx + (i - cat) * 6;
        if (x < 1 || x > 39) continue;
        int s = (i == cat);
        osd_put(x, 3, cats[i].icon, AT(s ? CL_WHITE : CL_GREY, bg));
        if (s) osd_text(x - (int)strlen(cats[i].name) / 2 + 1 > 0 ? x - (int)strlen(cats[i].name) / 2 : 0, 4,
                        cats[i].name, AT(focus == 0 ? CL_WHITE : CL_LIGHT, bg));
    }
    if (page != cats[cat].page) osd_text(cx + 2, 6, title_now(tb, sizeof tb), AT(CL_LIGHT, bg));
    static const lstyle_t st = { CL_NAVY, CL_GREY, CL_WHITE, CL_GREY, CL_WHITE, CL_GREY, CL_WHITE, CL_GREY, CL_BLUE, 1, 0, 0 };
    draw_list(cx - 2, 7, 34, 11, &st, focus == 1);
    if (focus == 1) draw_desc(cx, 18, 32, 2, AT(CL_LIGHT, bg));
    osd_text(2, 22, focus ? "A ok   B back" : "\x05 \x04 category   A open", AT(CL_GREY, bg));
}

// ---- iPod (classic): grey title bar, white list, square blue selection, chevrons
static void draw_ipod(void) {
    uint8_t bg = CL_WHITE, hb = CL_LIGHT;
    char tb[48];
    osd_clear(AT(CL_BLACK, bg));
    for (int y = 0; y < 3; y++) osd_fill(0, y, OSD_W, ' ', AT(CL_BLACK, hb));
    osd_fill(0, 2, OSD_W, G_HLINE, AT(CL_GREY, hb));
    const char *t = page == P_MAIN ? "Open Pocket" : title_now(tb, sizeof tb);
    osd_text(21 - (int)strlen(t) / 2, 1, t, AT(CL_BLACK, hb));
    if (game.loaded) osd_put(2, 1, G_PLAY, AT(CL_BLUE, hb));
    status_icons(37, 1, hb, CL_BLUE, CL_GREY);
    static const lstyle_t st = { CL_WHITE, CL_BLACK, CL_WHITE, CL_GREY, CL_WHITE, CL_GREY, CL_WHITE, CL_GREY, CL_BLUE, 0, 0, 0 };
    draw_list(0, 3, 41, 16, &st, 1);
    osd_fill(0, 19, OSD_W, G_HLINE, AT(CL_LIGHT, bg));
    draw_desc(2, 20, 38, 2, AT(CL_GREY, bg));
    osd_text(2, 22, page == P_MAIN ? "A select   B close" : "A select   B back", AT(CL_GREY, bg));
    if (page == P_MAIN && game.loaded) osd_rtext(40, 22, "START resume", AT(CL_GREY, bg));
}

// ---- BIOS setup (Award / Phoenix): blue screen, red selection bar, [values], Item Help
static void draw_bios(void) {
    uint8_t bg = CL_BLUE;
    char tb[48];
    osd_clear(AT(CL_WHITE, bg));
    osd_fill(0, 0, OSD_W, ' ', AT(CL_BLUE, CL_LIGHT));
    static const char head[] = "Open Pocket CMOS Setup Utility";
    osd_text(21 - (int)(sizeof head - 1) / 2, 0, head, AT(CL_BLUE, CL_LIGHT));
    upcase(tb, page == P_MAIN ? "Main" : title_now(tb, sizeof tb), sizeof tb);
    osd_text(2, 1, tb, AT(CL_YELLOW, bg));
    osd_fill(0, 2, OSD_W, G_HLINE, AT(CL_WHITE, bg));
    osd_fill(0, 19, OSD_W, G_HLINE, AT(CL_WHITE, bg));
    for (int y = 3; y < 19; y++) osd_put(29, y, G_TRACK | EXT, AT(CL_WHITE, bg));
    static const lstyle_t st = { CL_BLUE, CL_WHITE, CL_WHITE, CL_LIGHT, CL_WHITE, CL_YELLOW, CL_YELLOW, CL_LIGHT, CL_RED,
                                 0, 0, 0, 0, 1, 1, 0 };
    draw_list(0, 3, 28, 16, &st, 1);
    osd_text(31, 3, "Item Help", AT(CL_YELLOW, bg));
    osd_fill(31, 4, 10, G_HLINE, AT(CL_LIGHT, bg));
    draw_desc(31, 5, 10, 13, AT(CL_WHITE, bg));
    osd_text(1, 20, "\x1a\x1b:Move  A:Select  \x05\x04:Value", AT(CL_WHITE, bg));
    osd_text(1, 21, page == P_MAIN ? "B:Exit" : "B:Back", AT(CL_WHITE, bg));
    if (game.loaded) osd_text(12, 21, "START:Resume", AT(CL_WHITE, bg));
    osd_fill(0, 23, OSD_W, ' ', AT(CL_BLUE, CL_LIGHT));
    osd_text(1, 23, "Open Pocket GBC " FW_VERSION, AT(CL_BLUE, CL_LIGHT));
}

// ---- Terminal: green phosphor shell, `ls` of the page, inverse selection
static const char *term_path(char *buf, int n) {
    char t[48];
    if (page == P_MAIN) sfmt(buf, n, "~");
    else if (page == P_BROWSER && page_title[0] != 'I') sfmt(buf, n, "/sd%s", strcmp(cur_dir, "/") ? cur_dir : "");
    else {
        lowcase(t, page_title, sizeof t);
        for (char *p = t; *p; p++) if (*p == ' ') *p = '-';
        sfmt(buf, n, "~/%s", t);
    }
    return buf;
}

static void draw_term(void) {
    uint8_t bg = CL_BLACK, g = CL_GB3, d = CL_GB1;
    char pb[48], line[64];
    osd_clear(AT(g, bg));
    term_path(pb, sizeof pb);
    int l = (int)strlen(pb);
    sfmt(line, sizeof line, "pocket@gbc:%s$ ls", l > 24 ? pb + l - 24 : pb);
    osd_text(1, 1, line, AT(g, bg));
    static const lstyle_t st = { CL_BLACK, CL_GB3, CL_BLACK, CL_GB1, CL_BLACK, CL_GB1, CL_BLACK, CL_GB1, CL_GB3,
                                 0, 0, 2, 0, 1, 0, '/' };
    draw_list(1, 3, 39, 15, &st, 1);
    if (cursor < nitems && items[cursor].desc) {
        osd_put(1, 19, '#', AT(d, bg));
        draw_desc(3, 19, 37, 2, AT(d, bg));
    }
    osd_text(1, 22, page == P_MAIN ? "# a: run   b: exit" : "# a: run   b: cd ..", AT(d, bg));
    int x = osd_text(1, 23, "pocket@gbc:", AT(g, bg));
    x = osd_text(x, 23, l > 24 ? pb + l - 24 : pb, AT(g, bg));
    x = osd_text(x, 23, "$ ", AT(g, bg));
    osd_put(x, 23, G_BLOCK | EXT, AT(g, bg));
}

// ---- SPI LCD (16 x 12): every theme has its own layout
enum { LS_PILL, LS_BAR, LS_CURSOR, LS_TEXT };
typedef struct {
    uint8_t bg, fg, sel_bg, sel_fg, icon, icon_sel, val, val_sel;
    uint8_t kind;       // LS_PILL rounded bar, LS_BAR square bar, LS_CURSOR glyph at x0, LS_TEXT colour only
    uint8_t cursor;     // LS_CURSOR glyph
    uint8_t caps;       // 1 upper, 2 lower case labels
    uint8_t icons;      // item icons
    uint8_t values;     // values on the row (when they fit)
    uint8_t chev;       // page marker on the right (0: none)
    uint8_t brackets;   // [value]
    uint8_t inline_sel; // no footer: the selected row always shows its value
} lcdst_t;

static int lscroll;

static void lcd_list(int x0, int y0, int w, int rows, const lcdst_t *st, int show_sel) {
    if (cursor < lscroll) lscroll = cursor;
    if (cursor >= lscroll + rows) lscroll = cursor - rows + 1;
    if (lscroll > nitems - rows) lscroll = nitems > rows ? nitems - rows : 0;
    char lb[44], vb[44];
    for (int r = 0; r < rows && lscroll + r < nitems; r++) {
        int i = lscroll + r, y = y0 + r;
        item_t *it = &items[i];
        int s = show_sel && i == cursor && selectable(i);
        int filled = s && (st->kind == LS_PILL || st->kind == LS_BAR);
        uint8_t tb = filled ? st->sel_bg : st->bg;
        if (s) {
            if (st->kind == LS_PILL) lcd_pillx(x0, y, w, st->sel_bg, st->bg);
            else if (st->kind == LS_BAR) lcd_fill(x0, y, w, ' ', AT(st->sel_fg, st->sel_bg));
            else if (st->kind == LS_CURSOR) lcd_put(x0, y, st->cursor, AT(st->icon_sel, st->bg));
        }
        int lx = x0 + 1, xr = x0 + w - (st->kind == LS_PILL ? 1 : 0);    // text ends before xr
        if (it->id == I_INFO) { lcd_textn(lx, y, it->value, xr - lx, AT(st->val, tb)); continue; }
        if (st->icons && it->icon) { lcd_put(lx, y, it->icon, AT(s ? st->icon_sel : st->icon, tb)); lx += 2; }
        const char *v = item_value(it, 0, vb, sizeof vb);
        if (v[0] == G_CHEV_R && !v[1]) { vb[0] = st->chev; vb[1] = 0; v = vb; }
        else if (!st->values) v = "";
        else if (st->brackets && it->id == I_OPT) { sfmt(lb, sizeof lb, "[%s]", v); strcpy(vb, lb); v = vb; }
        if (st->caps && v != vb && v[0]) { strcpy(vb, v); v = vb; }
        if (st->caps == 1 && v == vb) upcase(vb, vb, sizeof vb);
        if (st->caps == 2 && v == vb) lowcase(vb, vb, sizeof vb);
        const char *lab = label_case(it, st->caps, lb, sizeof lb);
        int vl = (int)strlen(v), room = xr - lx, ll = (int)strlen(lab);
        // the value only when the label still fits; with no footer the selected row shortens the label
        if (ll + 1 + vl > room && !(s && st->inline_sel && vl <= room - 4)) vl = 0;
        int lw = room - (vl ? vl + 1 : 0);
        lcd_textn(lx, y, s ? marquee(lab, lw) : lab, lw, AT(s ? st->sel_fg : st->fg, tb));
        if (vl) lcd_text(xr - vl, y, v, AT(s ? st->val_sel : st->val, tb));
    }
    if (!nitems) lcd_text(x0 + 1, y0, page == P_BROWSER ? "No games" : "Empty", AT(st->val, st->bg));
}

// y of the selected row in the last lcd_list
static int lcd_sel_row(int y0) { return y0 + cursor - lscroll; }

// footer: the selected option as < value >, or the item's value, or a key hint
static void lcd_footer(int x0, int w, int y, uint8_t bg, uint8_t vcol, uint8_t hcol, const char *hint, int caps, int show) {
    char b[44];
    int opt = 0;
    lcd_fill(x0, y, w, ' ', AT(hcol, bg));
    if (show && cursor < nitems) {
        item_t *it = &items[cursor];
        opt = it->id == I_OPT;
        if (opt) sfmt(b, sizeof b, "%c %s %c", G_CHEV_L, it->opts[*it->val], G_CHEV_R);
        else if (it->value[0] && it->id != I_INFO && !(it->value[0] == G_CHEV_R && !it->value[1])) sfmt(b, sizeof b, "%s", it->value);
        else sfmt(b, sizeof b, "%s", hint);
    } else sfmt(b, sizeof b, "%s", hint);
    if (caps == 1) upcase(b, b, sizeof b);
    int n = (int)strlen(b);
    if (opt && n > w) { memmove(b, b + 2, n - 4); b[n - 4] = 0; n -= 4; }    // drop the arrows
    if (n > w) { lcd_textn(x0, y, marquee(b, w), w, AT(opt ? vcol : hcol, bg)); return; }
    lcd_text(x0 + (w - n) / 2, y, b, AT(opt ? vcol : hcol, bg));
}

static const char *lcd_title(void) {
    if (page == P_MAIN) return "Open Pocket";
    if (page == P_BROWSER) return page_title[0] == 'I' ? "Flash" : "SD Card";
    return page_title;
}

static void lcd_pocket(void) {
    uint8_t bg = CL_SHADE, hb = CL_NAVY;
    lcd_clear(AT(CL_WHITE, bg));
    lcd_fill(0, 0, LCD_W, ' ', AT(CL_WHITE, hb));
    lcd_put(0, 0, G_STAR, AT(CL_BLUE, hb));
    lcd_textn(2, 0, lcd_title(), 12, AT(CL_WHITE, hb));
    lcd_put(15, 0, G_SDCARD, AT(sd_mounted ? CL_GB3 : CL_LINE, hb));
    static const lcdst_t st = { CL_SHADE, CL_WHITE, CL_WHITE, CL_BLACK, CL_GREY, CL_BLUE, CL_YELLOW, CL_BLUE,
                                LS_PILL, 0, 0, 1, 1, 0, 0 };
    lcd_list(0, 1, LCD_W, 10, &st, 1);
    lcd_footer(0, LCD_W, 11, bg, CL_YELLOW, CL_GREY, page == P_MAIN ? "A ok  B close" : "A ok  B back", 0, 1);
}

// Ozone: icon sidebar with the categories, the page beside it
static void lcd_ozone(void) {
    uint8_t sb = CL_PANEL, bg = CL_SHADE;
    lcd_clear(AT(CL_WHITE, bg));
    for (int y = 0; y < LCD_H; y++) lcd_fill(0, y, 3, ' ', AT(CL_WHITE, sb));
    lcd_put(1, 0, G_STAR, AT(CL_BLUE, sb));
    int top = cat > 10 ? cat - 10 : 0;
    for (int r = 0; r < 11 && top + r < ncats; r++) {
        int i = top + r, y = 1 + r, s = (i == cat);
        uint8_t b = (s && focus == 0) ? CL_LINE : sb;
        if (s && focus == 0) lcd_fill(0, y, 3, ' ', AT(CL_WHITE, b));
        if (s) lcd_put(0, y, G_THUMB | EXT, AT(CL_BLUE, b));
        lcd_put(1, y, cats[i].icon, AT(s ? CL_BLUE : CL_GREY, b));
    }
    lcd_textn(4, 0, focus == 0 ? cats[cat].name : lcd_title(), 12, AT(focus == 0 ? CL_BLUE : CL_WHITE, bg));
    static const lcdst_t st = { CL_SHADE, CL_WHITE, CL_LINE, CL_WHITE, CL_GREY, CL_BLUE, CL_BLUE, CL_BLUE,
                                LS_BAR, 0, 0, 0, 1, G_CHEV_R, 0 };
    lcd_list(3, 1, 13, 10, &st, focus == 1);
    if (focus == 1 && nitems) lcd_put(3, lcd_sel_row(1), G_THUMB | EXT, AT(CL_BLUE, CL_LINE));
    lcd_footer(3, 13, 11, bg, CL_BLUE, CL_GREY, focus ? "A ok  B back" : "\x04 open", 0, focus == 1);
}

// XMB: category icons in a row, the selected one at the left, its page underneath
static void lcd_xmb(void) {
    uint8_t bg = CL_NAVY;
    lcd_clear(AT(CL_WHITE, bg));
    lcd_textn(0, 0, game.loaded ? game.name : "Open Pocket", 16, AT(CL_GREY, bg));
    // icon row: the selected category at the left, the following ones to its right
    if (cat > 0) lcd_put(0, 2, G_CHEV_L, AT(CL_LINE, bg));
    lcd_put(1, 2, cats[cat].icon, AT(CL_WHITE, bg));
    for (int i = cat + 1, x = 4; i < ncats && x < 16; i++, x += 3) lcd_put(x, 2, cats[i].icon, AT(CL_GREY, bg));
    lcd_textn(1, 3, cats[cat].name, 15, AT(focus == 0 ? CL_WHITE : CL_LIGHT, bg));
    if (page != cats[cat].page) lcd_textn(1, 4, lcd_title(), 15, AT(CL_GREY, bg));
    static const lcdst_t st = { CL_NAVY, CL_GREY, CL_BLUE, CL_WHITE, CL_GREY, CL_WHITE, CL_GREY, CL_WHITE,
                                LS_TEXT, 0, 0, 1, 1, 0, 0 };
    lcd_list(0, 5, LCD_W, 6, &st, focus == 1);
    // the wave, or the value of the selected option on top of it
    for (int x = 0; x < LCD_W; x++) lcd_put(x, 11, G_HLINE, AT(((x / 4) & 1) ? CL_BLUE : CL_LINE, bg));
    if (focus == 1 && cursor < nitems && items[cursor].id == I_OPT) lcd_footer(0, LCD_W, 11, bg, CL_WHITE, CL_GREY, "", 0, 1);
}

// Game Boy: dark bezel bands, light screen with a thin frame, a cursor arrow, upper case
static void lcd_gameboy(void) {
    uint8_t in = CL_GB3, fr = CL_GB0;
    char b[24];
    lcd_clear(AT(CL_GB0, in));
    lcd_fill(0, 0, LCD_W, ' ', AT(in, fr));
    upcase(b, lcd_title(), 15);
    lcd_text(8 - (int)strlen(b) / 2, 0, b, AT(in, fr));
    for (int y = 1; y < 11; y++) {
        lcd_put(0, y, G_TRACK | EXT, AT(CL_GB1, in));
        lcd_put(15, y, G_TRACK | EXT, AT(CL_GB1, in));
    }
    static const lcdst_t st = { CL_GB3, CL_GB0, CL_GB3, CL_GB0, CL_GB1, CL_GB0, CL_GB1, CL_GB0,
                                LS_CURSOR, G_PLAY, 1, 0, 1, 0, 0 };
    lcd_list(1, 1, 14, 10, &st, 1);
    lcd_footer(0, LCD_W, 11, fr, CL_GB3, CL_GB2, page == P_MAIN ? "A OK  B CLOSE" : "A OK  B BACK", 1, 1);
}

// iPod: grey title bar, white list, square blue bar, chevrons, no footer
static void lcd_ipod(void) {
    uint8_t hb = CL_LIGHT;
    char b[24];
    lcd_clear(AT(CL_BLACK, CL_WHITE));
    lcd_fill(0, 0, LCD_W, ' ', AT(CL_BLACK, hb));
    sfmt(b, 13, "%s", lcd_title());
    lcd_text(8 - (int)strlen(b) / 2, 0, b, AT(CL_BLACK, hb));
    if (game.loaded) lcd_put(0, 0, G_PLAY, AT(CL_BLUE, hb));
    lcd_put(15, 0, G_SDCARD, AT(sd_mounted ? CL_BLUE : CL_GREY, hb));
    static const lcdst_t st = { CL_WHITE, CL_BLACK, CL_BLUE, CL_WHITE, CL_GREY, CL_WHITE, CL_GREY, CL_WHITE,
                                LS_BAR, 0, 0, 0, 1, G_CHEV_R, 0, 1 };
    lcd_list(0, 1, LCD_W, 11, &st, 1);
}

// BIOS: blue screen, grey bars, red selection, yellow [values]
static void lcd_bios(void) {
    uint8_t bg = CL_BLUE, hb = CL_LIGHT;
    char b[24];
    lcd_clear(AT(CL_WHITE, bg));
    lcd_fill(0, 0, LCD_W, ' ', AT(CL_BLUE, hb));
    upcase(b, page == P_MAIN ? "CMOS Setup" : lcd_title(), 15);
    lcd_text(8 - (int)strlen(b) / 2, 0, b, AT(CL_BLUE, hb));
    static const lcdst_t st = { CL_BLUE, CL_WHITE, CL_RED, CL_WHITE, CL_LIGHT, CL_WHITE, CL_YELLOW, CL_YELLOW,
                                LS_BAR, 0, 0, 0, 1, 0, 1 };
    lcd_list(0, 1, LCD_W, 10, &st, 1);
    lcd_footer(0, LCD_W, 11, hb, CL_RED, CL_BLUE, "\x1a\x1b Move  A Sel", 0, 1);
}

// Terminal: green on black, the path as a prompt, inverse selection, a block cursor
static void lcd_term(void) {
    uint8_t bg = CL_BLACK, g = CL_GB3;
    char pb[48], b[24];
    lcd_clear(AT(g, bg));
    term_path(pb, sizeof pb);
    int l = (int)strlen(pb);
    sfmt(b, sizeof b, "%s$", l > 14 ? pb + l - 14 : pb);
    lcd_text(0, 0, b, AT(g, bg));
    static const lcdst_t st = { CL_BLACK, CL_GB3, CL_GB3, CL_BLACK, CL_GB1, CL_BLACK, CL_GB1, CL_BLACK,
                                LS_BAR, 0, 2, 0, 1, '/', 0 };
    lcd_list(0, 1, LCD_W, 10, &st, 1);
    if (cursor < nitems && items[cursor].id == I_OPT) {
        lcd_footer(0, LCD_W, 11, bg, g, CL_GB1, "", 0, 1);
    } else {
        int x = lcd_text(0, 11, "$ ", AT(g, bg));
        lcd_put(x, 11, G_BLOCK, AT(g, bg));
    }
}

static void draw_lcd(void) {
    switch (set.theme) {
    case TH_OZONE:   lcd_ozone(); break;
    case TH_XMB:     lcd_xmb(); break;
    case TH_GAMEBOY: lcd_gameboy(); break;
    case TH_IPOD:    lcd_ipod(); break;
    case TH_BIOS:    lcd_bios(); break;
    case TH_TERM:    lcd_term(); break;
    default:         lcd_pocket(); break;
    }
    lcd_present();
}

static void draw_menu(void) {
    if (cursor != mq_cursor || page != mq_page) { mq_cursor = cursor; mq_page = page; mq_t0 = ms_now(); }
    mq_on = 0;
    switch (set.theme) {
    case TH_OZONE:   draw_ozone(); break;
    case TH_XMB:     draw_xmb(); break;
    case TH_GAMEBOY: draw_gameboy(); break;
    case TH_IPOD:    draw_ipod(); break;
    case TH_BIOS:    draw_bios(); break;
    case TH_TERM:    draw_term(); break;
    default:         draw_pocket(); break;
    }
    osd_present();
    draw_lcd();
}

// ---------------------------------------------------------------- navigation
static int sel_first(void) { for (int i = 0; i < nitems; i++) if (selectable(i)) return i; return 0; }

static void go(int p, int keep_cursor) {
    page = p;
    build_page();
    if (!keep_cursor) { cursor = sel_first(); scroll = 0; }
    if (cursor >= nitems) cursor = nitems ? nitems - 1 : 0;
}

// the top page: main list, or the root page of the current category
static void go_home(int keep) {
    if (two_level()) { build_cats(); go(cats[cat].page, keep); }
    else go(P_MAIN, keep);
}

// power presets
static void preset_apply(int battery_preset) {
    if (battery_preset) {
        set.hdmi_off = 1; set.sdram_pd = 1; set.lcd_slow = 1; set.lcd_bri = 0;
        set.dim_idx = 2; set.led_en = 0; set.sleep_min = 1; set.poll_slow = 1; set.dbg_on = 0;
    } else {
        set.hdmi_off = 0; set.sdram_pd = 0; set.lcd_slow = 0; set.lcd_bri = 3;
        set.dim_idx = 0; set.sleep_min = 0; set.poll_slow = 0;
    }
    lcd_dimmed = 0;
    apply_settings();
}
static int preset_now(void) {
    if (set.hdmi_off && set.sdram_pd && set.lcd_slow && set.lcd_bri == 0 && set.dim_idx == 2 && !set.led_en &&
        set.sleep_min == 1 && set.poll_slow) return 1;
    if (!set.hdmi_off && !set.sdram_pd && !set.lcd_slow && set.lcd_bri == 3 && !set.dim_idx && !set.sleep_min &&
        !set.poll_slow) return 0;
    return 2;
}

static int opt_changed(item_t *it) {
    if (it->val == &preset_sel) {
        if (preset_sel == 2) preset_sel = (it->nopt == 3) ? 0 : 0;   // "Custom" is not a target
        preset_apply(preset_sel == 1);
        go(P_POWER, 1);
        return 1;
    }
    if (it->val == &set.theme) { osd_invalidate(); build_cats(); }
    apply_settings();
    if (page == P_POWER) go(P_POWER, 1);              // refresh the preset value
    return 1;
}

// returns 1 when the menu should close
static int activate(item_t *it, int dir) {
    switch (it->id) {
    case I_RESUME: return 1;
    case I_LIBRARY: go(P_LIBRARY, 0); break;
    case I_SAVE: {
        message_screen("Saving", game.name);
        int r = save_write();
        message_screen(r >= 0 ? "Saved" : "Save failed", game.name);
        wait_ms(700);
        osd_invalidate();
        go(page, 1);
        break;
    }
    case I_RESET: reset_game(); return 1;
    case I_PG_DISPLAY: go(P_DISPLAY, 0); break;
    case I_PG_AUDIO: go(P_AUDIO, 0); break;
    case I_PG_CONTROLS: go(P_CONTROLS, 0); break;
    case I_PG_SYSTEM: go(P_SYSTEM, 0); break;
    case I_PG_INFO: go(P_INFO, 0); break;
    case I_PG_POWER: go(P_POWER, 0); break;
    case I_SLEEP_NOW: do_sleep(); osd_invalidate(); go(page, 1); break;
    case I_SRC_SD:
        if (!sd_mounted) { message_screen("Reading SD card", ""); sd_mount(); }
        if (!sd_mounted) { message_screen("No SD card", "Insert a FAT32 or exFAT card"); wait_ms(1500); osd_invalidate(); go(P_LIBRARY, 1); break; }
        page_title[0] = 'S';
        strcpy(cur_dir, "/");
        go(P_BROWSER, 0);
        break;
    case I_SRC_FLASH:
        strcpy(page_title, "Internal Flash");
        go(P_BROWSER, 0);
        break;
    case I_DIR: {
        int n = (int)strlen(cur_dir);
        if (n + strlen(it->label) + 2 < sizeof cur_dir) {
            if (cur_dir[n - 1] != '/') strcat(cur_dir, "/");
            strcat(cur_dir, it->label);
        }
        go(P_BROWSER, 0);
        break;
    }
    case I_UP: {
        char *sl = strrchr(cur_dir, '/');
        if (sl == cur_dir) cur_dir[1] = 0; else if (sl) *sl = 0;
        go(P_BROWSER, 0);
        break;
    }
    case I_FILE: {
        int n = (int)strlen(cur_dir);
        sfmt(act_path, sizeof act_path, "%s%s%s", cur_dir, cur_dir[n - 1] == '/' ? "" : "/", it->label);
        strncpy(page_title, it->label, sizeof page_title - 1);
        page_title[sizeof page_title - 1] = 0;
        br_cursor = cursor;
        br_scroll = scroll;
        cart_probe();
        go(P_FILEACT, 0);
        break;
    }
    case I_PLAY_SD:
    case I_WRITE_CART:
        cart_write_pending = (it->id == I_WRITE_CART);
        if (load_game(2, 0, act_path) == 0) return 1;
        cart_write_pending = 0;
        osd_invalidate();
        go(P_FILEACT, 1);
        break;
    case I_SRC_CART:
        message_screen("Reading cartridge", "");
        cart_probe();
        osd_invalidate();
        go(P_CART, 0);
        break;
    case I_PLAY_CART:
        if (load_game(3, 0, 0) == 0) return 1;
        osd_invalidate();
        go(P_CART, 1);
        break;
    case I_ERASE_CART:
        message_screen("Erasing cartridge", "");
        flash_select(1);
        flash_init();
        flash_erase_range(0, 4096);
        if (cart_size) flash_erase_range(cart_save_addr(), 4096);
        flash_sleep();
        flash_select(0);
        cart_probe();
        if (game.loaded && game.src == 3) game.battery = 0;    // nowhere to save any more
        osd_invalidate();
        go(P_CART, 0);
        break;
    case I_FLASH_GAME:
        if (load_game(1, it->arg, 0) == 0) return 1;
        osd_invalidate();
        go(P_BROWSER, 1);
        break;
    case I_OPT:
        if (dir < 0) *it->val = *it->val ? *it->val - 1 : it->nopt - 1;
        else *it->val = (*it->val + 1) % it->nopt;
        opt_changed(it);
        break;
    case I_DEFAULTS: {
        char lp[sizeof set.last_path];
        uint8_t ls = set.last_src, lf = set.last_flash;
        memcpy(lp, set.last_path, sizeof lp);
        set_defaults();
        set.last_src = ls; set.last_flash = lf;
        memcpy(set.last_path, lp, sizeof lp);
        apply_settings();
        message_screen("Settings restored", "");
        wait_ms(700);
        osd_invalidate();
        build_cats();
        go(P_SYSTEM, 1);
        break;
    }
    }
    return 0;
}

// B: one level up; returns 1 when the menu should close
static int back(void) {
    if (two_level() && page == cats[cat].page) { focus = 0; return 0; }
    switch (page) {
    case P_MAIN: return game.loaded || home_running;
    case P_BROWSER:
        if (page_title[0] != 'I' && strcmp(cur_dir, "/")) {
            char *sl = strrchr(cur_dir, '/');
            if (sl == cur_dir) cur_dir[1] = 0; else if (sl) *sl = 0;
            go(P_BROWSER, 0);
        } else { go(P_LIBRARY, 0); cursor = page_title[0] == 'I' ? 1 : 0; }
        break;
    case P_CART: go(P_LIBRARY, 0); cursor = 2; break;
    case P_FILEACT:
        page_title[0] = 'S';
        go(P_BROWSER, 0);
        cursor = br_cursor < nitems ? br_cursor : 0;
        scroll = br_scroll;
        break;
    default: {
        if (two_level()) { go_home(0); break; }
        int from = page;
        go(P_MAIN, 0);
        for (int i = 0; i < nitems; i++)
            if ((from == P_DISPLAY && items[i].id == I_PG_DISPLAY) || (from == P_AUDIO && items[i].id == I_PG_AUDIO) ||
                (from == P_CONTROLS && items[i].id == I_PG_CONTROLS) || (from == P_SYSTEM && items[i].id == I_PG_SYSTEM) ||
                (from == P_INFO && items[i].id == I_PG_INFO) || (from == P_POWER && items[i].id == I_PG_POWER) ||
                (from == P_LIBRARY && items[i].id == I_LIBRARY)) cursor = i;
    }
    }
    return 0;
}

static void menu_run(void) {
    menu_active = 1;
    if (set.autosave && game.save_pending) save_write();
    ctrl_bits(C_OSD | C_DIM, 1);
    if (!home_running) ctrl_bits(C_PAUSE, 1);      // the home screen keeps playing behind the menu
    osd_invalidate();
    wait_release();
    lcd_dimmed = 0;
    apply_settings();
    focus = 0;
    cat = 0;
    go_home(0);
    int quit = 0;
    uint32_t refresh = ms_now();
    while (!quit) {
        draw_menu();
        uint32_t k = poll_keys(mq_on ? 250 : page == P_INFO || page == P_GAME || page == P_MAIN ? 1000 : 5000);
        uint32_t now = ms_now();
        if (sleep_after_ms() && (now - last_input_ms) > sleep_after_ms()) { do_sleep(); osd_invalidate(); }
        if (!k) {
            if (now - refresh > 900 && (page == P_INFO || page == P_GAME || page == P_MAIN)) { build_page(); refresh = now; }
            continue;
        }
        if (k & K_SEL) { lcd_dump(); continue; }    // LCD screenshot for tools/lcd_shot.py
        if ((k & (K_START | K_S1 | K_UMENU)) && (game.loaded || home_running)) break;
        if (k & K_S2 || k & K_URESET) { reset_game(); break; }

        if (two_level() && focus == 0) {
            // category level: Ozone moves up / down, XMB left / right
            int prev = (set.theme == TH_OZONE) ? K_UP : K_LEFT;
            int next = (set.theme == TH_OZONE) ? K_DOWN : K_RIGHT;
            int enter = K_A | K_X | ((set.theme == TH_OZONE) ? K_RIGHT : K_DOWN);
            if (k & prev) { cat = cat ? cat - 1 : ncats - 1; go(cats[cat].page, 0); }
            if (k & next) { cat = (cat + 1) % ncats; go(cats[cat].page, 0); }
            if ((k & enter) && nitems) {
                focus = 1;
                // one-action categories run directly on A
                if ((k & (K_A | K_X)) && nitems == 1 && items[0].id != I_OPT) quit = activate(&items[0], 1);
            }
            if (k & (K_B | K_Y)) { if (game.loaded || home_running) break; }
            continue;
        }

        if (k & K_UP) {
            int c = cursor;
            do c = c ? c - 1 : nitems - 1; while (!selectable(c) && c != cursor);
            cursor = c;
        }
        if (k & K_DOWN) {
            int c = cursor;
            do c = (c + 1) % (nitems ? nitems : 1); while (!selectable(c) && c != cursor);
            cursor = c;
        }
        if ((k & K_L) && nitems) { cursor -= 14; if (cursor < 0) cursor = 0; }
        if ((k & K_R) && nitems) { cursor += 14; if (cursor >= nitems) cursor = nitems - 1; }
        if (nitems && cursor < nitems) {
            item_t *it = &items[cursor];
            if ((k & (K_LEFT | K_RIGHT)) && it->id == I_OPT) activate(it, (k & K_LEFT) ? -1 : 1);
            else if ((k & K_LEFT) && set.theme == TH_OZONE && page == cats[cat].page) focus = 0;
            else if (k & (K_A | K_X)) quit = activate(it, 1);
        }
        if (!quit && (k & (K_B | K_Y))) quit = back();
    }
    settings_save();
    draw_menu();
    wait_release();
    menu_active = 0;
    ctrl &= ~(C_OSD | C_DIM | C_PAUSE);
    ctrl_write();
    osd_invalidate();
    last_input_ms = ms_now();
}

// ================================================================ debug overlay
static uint16_t fps_frames;
static uint32_t fps_ms;
static unsigned fps;

static void dbg_line(int line, const char *s) {
    for (int i = 0; i < 42; i++) {
        char c = *s ? *s++ : ' ';
        LCD_TXT[256 + 64 * line + i] = ((uint16_t)AT(CL_WHITE, CL_GAME) << 8) | (uint8_t)c;
    }
}

static void dbg_update(uint32_t st) {
    uint32_t now = ms_now();
    uint16_t fr = st >> 16;
    if (now - fps_ms >= 1000) {
        fps = (uint16_t)(fr - fps_frames) * 1000u / (now - fps_ms);
        fps_frames = fr;
        fps_ms = now;
    }
    char b[48], pad[9];
    uint32_t in = REG_IN;
    const char *pk = "UDLRABsS";
    uint32_t bits[8] = { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_A | K_X, K_B | K_Y, K_SEL, K_START };
    for (int i = 0; i < 8; i++) pad[i] = (in & bits[i]) ? pk[i] : '-';
    pad[8] = 0;
    uint32_t s = now / 1000;
    sfmt(b, sizeof b, "FPS %2u  %s %s  PAD %s  UP %02u:%02u:%02u", fps, (st & 1) ? "GBC" : "DMG",
         (st & 2) ? "2X" : "1X", pad, s / 3600, (s / 60) % 60, s % 60);
    dbg_line(0, b);
    sfmt(b, sizeof b, "%-24s %-8s %uK %s", game.name, mapper_name(game.cart_type), game.rom_bytes >> 10,
         game.save_pending ? "*" : "");
    dbg_line(1, b);
    sfmt(b, sizeof b, "%2ufps   %s", fps, pad);
    dbg_line(2, b);
}

// ================================================================ main
int main(void) {
    logf("\nOpen Pocket GBC firmware " FW_VERSION "\n");
    ctrl = C_CORE_RST;
    ctrl_write();
    flash_init();
    uint32_t id = flash_id();
    uint8_t cap = id & 0xFF;
    logf("flash id %06x\n", id);
    if (cap >= 0x17 && cap < 0x20) {         // 8 MB or more
        set_addr = 0x7FF000;
        flash_saves = 1;
    }
    set_defaults();
    if (set_addr) {
        settings_t t;
        settings_v2_t o;
        flash_read(set_addr, &t, sizeof t);
        flash_read(set_addr, &o, sizeof o);
        if (t.magic == SET_MAGIC && t.sum == sum32(&t, sizeof t - 4)) set = t;
        else if (o.magic == SET_MAGIC_V2 && o.sum == sum32(&o, sizeof o - 4)) {
            memcpy(&set.disp_mode, o.b, 20);               // fields up to spk_boost
            strncpy(set.last_path, o.last_path, sizeof set.last_path - 1);
            set.spk_mode = o.spk_mode;
            if (o.power_mode) { set.sdram_pd = 1; set.lcd_slow = 1; set.dim_idx = 2; set.poll_slow = 1; set.hdmi_off = 1; }
        }
        if (set.theme >= 7) set.theme = 1;
        set_stored = set;
    }
    apply_settings();
    flash_dir_read();
    logf("flash games: %d\n", fl_count);
    sd_mount();
    cart_probe();
    lcd_clear(AT(CL_WHITE, CL_SHADE));
    lcd_present();
    osd_invalidate();

    int started = 0;
    logf("start-up: menu %d, last src %d idx %d, theme %d\n", set.boot_menu, set.last_src, set.last_flash, set.theme);
    if (!set.boot_menu) {
        if (set.last_src == 2 && sd_mounted && set.last_path[0]) started = load_game(2, 0, set.last_path) == 0;
        if (!started && set.last_src == 3 && cart_valid) started = load_game(3, 0, 0) == 0;
        if (!started && set.last_src == 1 && set.last_flash < fl_count) started = load_game(1, set.last_flash, 0) == 0;
        if (!started && fl_count) started = load_game(1, 0, 0) == 0;
    }
    ctrl &= ~(C_OSD | C_DIM);
    ctrl_write();
    if (!started) {
        start_home();
        // the intro plays for about 3.5 s, then the menu opens; a button opens it at once
        uint32_t t0 = ms_now();
        wait_release();
        while (ms_now() - t0 < 3500 && !(REG_IN & (K_PAD | K_S1))) { REG_WAIT = 100; (void)REG_WAIT; }
        menu_run();
    }
    flash_sleep();

    uint32_t s1_down = 0, s2_down = 0;
    int s1_long = 0, s2_long = 0;
    uint32_t dbg_ms = 0;
    last_input_ms = ms_now();
    held = REG_IN & 0xFFFF;
    for (;;) {
        REG_WAIT = (held & (K_S1 | K_S2)) ? 50 : set.poll_slow ? 500 : 200;
        (void)REG_WAIT;
        uint32_t in = REG_IN & 0xFFFF;
        uint32_t now = ms_now();
        uint32_t press = in & ~held;
        uint32_t rel = held & ~in;
        if (in != held) last_input_ms = now;
        held = in;

        // S1: short press = menu, hold 1 s = HDMI on / off
        if (press & K_S1) { s1_down = now; s1_long = 0; }
        if ((in & K_S1) && !s1_long && now - s1_down >= 1000) {
            s1_long = 1;
            set.hdmi_off ^= 1;
            apply_settings();
            settings_save();
            toast(set.hdmi_off ? "HDMI off" : "HDMI on", G_DISPLAY, CL_BLUE);
        }
        // S2: short press = reset game, hold 1 s = battery mode on / off
        if (press & K_S2) { s2_down = now; s2_long = 0; }
        if ((in & K_S2) && !s2_long && now - s2_down >= 1000) {
            s2_long = 1;
            int to_batt = preset_now() != 1;
            preset_apply(to_batt);
            settings_save();
            logf("power preset: %s\n", to_batt ? "battery" : "normal");
            if (!to_batt) toast("Normal power", G_POWER, CL_GB3);
        }
        int open = ((rel & K_S1) && !s1_long && now - s1_down >= 20) || (press & K_UMENU) ||
                   ((in & K_SEL) && (in & K_START) && (press & (K_SEL | K_START)));
        if (home_running && (press & (K_A | K_X | K_START))) open = 1;     // PRESS START
        if (((rel & K_S2) && !s2_long && now - s2_down >= 20) || (press & K_URESET)) reset_game();

        uint32_t st = REG_STAT;
        if (st & 4) { game.save_pending = 1; game.dirty_ms = now; }
        if (open) {
            if (toast_until) { toast_until = 0; ctrl_bits(C_OSD, 0); }
            menu_run();
            continue;
        }
        if (set.autosave && game.save_pending && now - game.dirty_ms > 2000) {
            if (save_write() == 0 && set.toasts) toast("Saved", G_CHECK, CL_GB3);
        }
        if (toast_until && (int32_t)(now - toast_until) > 0) {
            toast_until = 0;
            if (!menu_active) ctrl_bits(C_OSD, 0);
        }
        if (set.dbg_on && now - dbg_ms >= 250) { dbg_ms = now; dbg_update(st); }
        if (sleep_after_ms() && now - last_input_ms > sleep_after_ms()) do_sleep();
        // LCD auto-dim after a while without input, any button brings it back
        int dim = set.dim_idx && now - last_input_ms > dim_seconds[set.dim_idx & 3] * 1000u;
        if (dim != lcd_dimmed) { lcd_dimmed = dim; apply_settings(); }
    }
}
