// The Open Pocket board around the menu CPU, as rtl/sys/iosys.sv presents it:
//   0x0000_0000 SDRAM 8 MB (ROM at 0, framebuffers 0x700000, firmware 0x780000)
//   0x0100_0000 boot ROM (a jump here restarts the firmware from flash 0x100000)
//   0x0200_0000 I/O registers (see fw/src/hw.h)
//   0x0300_0000 HDMI OSD text (42 x 24), 0x0310_0000 LCD text (16 x 12)
// SPI devices: the W25Q flash (8 MB image), an SD card (FAT image), no cartridge flash.
'use strict';

const K = { B: 1, Y: 2, SEL: 4, START: 8, UP: 16, DOWN: 32, LEFT: 64, RIGHT: 128, A: 256, X: 512, L: 1024, R: 2048, S1: 4096, S2: 8192 };

// ---------------------------------------------------------------- W25Q flash
class SpiFlash {
  constructor(mem, id) { this.mem = mem; this.id = id; this.cs = false; this.cmd = []; this.out = []; this.wel = false; this.dirty = false; }
  select(on) {
    if (this.cs && !on) this.finish();
    if (!this.cs && on) { this.cmd = []; this.out = []; this.addr = 0; this.prog = null; }
    this.cs = on;
  }
  finish() {
    const c = this.cmd[0];
    if (c === 0x20 && this.wel && this.cmd.length >= 4) { const a = this.a24() & ~0xfff; this.mem.fill(0xff, a, a + 0x1000); this.wel = false; this.dirty = true; }
    if (c === 0xd8 && this.wel && this.cmd.length >= 4) { const a = this.a24() & ~0xffff; this.mem.fill(0xff, a, a + 0x10000); this.wel = false; this.dirty = true; }
    if (c === 0x02) { this.wel = false; this.dirty = true; }
  }
  a24() { return ((this.cmd[1] << 16) | (this.cmd[2] << 8) | this.cmd[3]) & (this.mem.length - 1); }
  xfer(b) {
    if (!this.mem || !this.cs) return 0xff;
    this.cmd.push(b);
    const c = this.cmd[0], n = this.cmd.length;
    switch (c) {
      case 0x9f: return n === 1 ? 0xff : [0, (this.id >> 16) & 255, (this.id >> 8) & 255, this.id & 255][Math.min(n - 1, 3)];
      case 0x05: return n === 1 ? 0xff : 0x00;                       // never busy
      case 0x06: this.wel = true; return 0xff;
      case 0x04: this.wel = false; return 0xff;
      case 0x03: if (n <= 4) return 0xff; return this.mem[(this.a24() + n - 5) & (this.mem.length - 1)];
      case 0x0b: if (n <= 5) return 0xff; return this.mem[(this.a24() + n - 6) & (this.mem.length - 1)];
      case 0x02: if (n > 4 && this.wel) { const a = (this.a24() & ~0xff) | ((this.cmd[3] + n - 5) & 0xff); this.mem[a] &= b; } return 0xff;
      case 0xab: return n >= 5 ? (this.id & 0xff) - 1 : 0xff;
      default: return 0xff;
    }
  }
}

// ---------------------------------------------------------------- SD card (SPI mode, SDHC)
class SdCard {
  constructor(img) { this.img = img; this.cs = false; this.q = []; this.cmd = []; this.state = 'idle'; this.acmd = false; this.dirty = false; }
  select(on) { this.cs = on; }
  sectors() { return this.img ? (this.img.length / 512) | 0 : 0; }
  block(lba) {
    const out = [0xfe];
    for (let i = 0; i < 512; i++) out.push(this.img[lba * 512 + i] ?? 0);
    out.push(0xff, 0xff);
    return out;
  }
  xfer(b) {
    if (!this.img || !this.cs) return 0xff;
    if (this.state === 'wtoken') {                       // waiting for a data token
      if (b === 0xfe || b === 0xfc) { this.state = 'wdata'; this.buf = []; return 0xff; }
      if (b === 0xfd) { this.state = 'idle'; this.q.push(0x00, 0xff); return 0xff; }
      return 0xff;
    }
    if (this.state === 'wdata') {
      this.buf.push(b);
      if (this.buf.length === 514) {                     // 512 bytes and the CRC
        this.img.set(this.buf.slice(0, 512), this.wlba * 512);
        this.dirty = true;
        this.wlba++;
        this.q.push(0x05, 0x00, 0xff);
        this.state = this.multi ? 'wtoken' : 'idle';
      }
      return 0xff;
    }
    if (this.cmd.length === 0 && (b & 0xc0) === 0x40) {   // a command starts (also stops a multi-block read)
      this.cmd = [b];
      if (b === 0x4c) { this.q = []; this.state = 'idle'; }
      return 0xff;
    }
    if (this.cmd.length) {
      this.cmd.push(b);
      if (this.cmd.length === 6) { this.command(this.cmd[0] & 0x3f, ((this.cmd[1] << 24) | (this.cmd[2] << 16) | (this.cmd[3] << 8) | this.cmd[4]) >>> 0); this.cmd = []; }
      return 0xff;
    }
    if (this.q.length) return this.q.shift();
    if (this.state === 'mread') { this.q = this.block(this.rlba++); return this.q.shift(); }
    return 0xff;
  }
  command(c, arg) {
    const a = this.acmd;
    this.acmd = false;
    this.q = [0xff];                                     // one byte of NCR
    switch (c) {
      case 0: this.q.push(0x01); break;
      case 8: this.q.push(0x01, 0x00, 0x00, 0x01, arg & 0xff); break;
      case 55: this.acmd = true; this.q.push(0x01); break;
      case 41: this.q.push(0x00); break;
      case 58: this.q.push(0x00, 0xc0, 0xff, 0x80, 0x00); break;
      case 16: case 23: this.q.push(0x00); break;
      case 9: {
        const csize = (this.sectors() >> 10) - 1, csd = new Array(16).fill(0);
        csd[0] = 0x40; csd[7] = (csize >> 16) & 0x3f; csd[8] = (csize >> 8) & 0xff; csd[9] = csize & 0xff;
        this.q.push(0x00, 0xfe, ...csd, 0xff, 0xff);
        break;
      }
      case 17: this.q.push(0x00, ...this.block(arg)); break;
      case 18: this.q.push(0x00); this.state = 'mread'; this.rlba = arg; break;
      case 12: this.q = [0xff, 0x00]; this.state = 'idle'; break;
      case 24: this.q.push(0x00); this.state = 'wtoken'; this.multi = false; this.wlba = arg; break;
      case 25: this.q.push(0x00); this.state = 'wtoken'; this.multi = true; this.wlba = arg; break;
      default: this.q.push(a ? 0x00 : 0x04); break;      // illegal command
    }
  }
}

// ---------------------------------------------------------------- the board
class Board {
  constructor(opts) {
    this.sdram = new Uint8Array(8 << 20);
    this.dv = new DataView(this.sdram.buffer);
    this.flash = new SpiFlash(opts.flash, 0xef4017);
    this.cart = new SpiFlash(null, 0xffffff);
    this.sd = new SdCard(opts.sd);
    this.osd = new Uint16Array(42 * 24);
    this.lcd = new Uint16Array(16 * 12 + 64);
    this.onUart = opts.onUart || (() => {});
    this.onBeep = opts.onBeep || (() => {});
    this.onCore = opts.onCore || (() => {});
    this.vms = 0;                                  // virtual milliseconds
    this.keys = 0;
    this.prevKeys = 0;
    this.ev = false;
    this.waitUntil = 0;
    this.ctrl = 0; this.cfg = 0;
    this.frames = 0;
    this.gpio = 0;
    this.spi = { sd: 0, fl: 0, ct: 0 };
    this.rebootReq = false;
  }

  ms() { return Math.floor(this.vms); }

  // virtual time: the CPU runs about IPS instructions per second; frames at 59.7 Hz
  advance(dt) {
    this.vms += dt;
    this.frames = Math.floor(this.vms / 16.75);
  }

  setKeys(k) {
    if (k !== this.keys) { this.keys = k; this.ev = true; }
  }

  // ---- CPU bus
  fetch(pc) {
    if (pc < 0x800000) return this.dv.getInt32(pc, true);
    if (pc >= 0x01000000 && pc < 0x01001000) { this.rebootReq = true; return 0x0000006f; }   // j . until the reboot
    throw new Error('fetch from ' + pc.toString(16));
  }

  load(a, size, signed) {
    if (a < 0x800000) {
      if (size === 4) return this.dv.getInt32(a, true);
      if (size === 2) return signed ? this.dv.getInt16(a, true) : this.dv.getUint16(a, true);
      return signed ? this.dv.getInt8(a) : this.sdram[a];
    }
    if (a >= 0x02000000 && a < 0x02000100) return this.io(a & 0xff);
    if (a >= 0x03000000 && a < 0x03000800) return this.osd[(a & 0x7ff) >> 1];
    if (a >= 0x03100000 && a < 0x03100400) return this.lcd[(a & 0x3ff) >> 1] || 0;
    return 0;
  }

  store(a, size, v) {
    if (a < 0x800000) {
      if (size === 4) this.dv.setInt32(a, v, true);
      else if (size === 2) this.dv.setInt16(a, v, true);
      else this.sdram[a] = v;
      return;
    }
    if (a >= 0x02000000 && a < 0x02000100) { this.iow(a & 0xff, v >>> 0); return; }
    if (a >= 0x03000000 && a < 0x03000800) { this.osd[(a & 0x7ff) >> 1] = v & 0xffff; return; }
    if (a >= 0x03100000 && a < 0x03100400) { const i = (a & 0x3ff) >> 1; if (i < this.lcd.length) this.lcd[i] = v & 0xffff; }
  }

  // ---- SPI ports: data (+0) exchanges a byte, word (+4) receives 4, ctrl (+8) bit 0 = CS
  dev(o) { return o < 0x20 ? this.sd : o < 0x30 ? this.flash : this.cart; }

  io(o) {
    switch (o) {
      case 0x00: return 0x4f470001;
      case 0x04: return this.keys;
      case 0x08: return this.ms();
      case 0x0c: {
        const gbc = this.coreGbc ? 1 : 0;
        return ((this.frames & 0xffff) << 16) | gbc;
      }
      case 0x10: case 0x20: case 0x50: return this.dev(o).last ?? 0xff;
      case 0x14: case 0x24: case 0x54: {
        const d = this.dev(o);
        let w = 0;
        for (let i = 0; i < 4; i++) w |= d.xfer(0xff) << (8 * i);
        return w;
      }
      case 0x30: return 0;
      case 0x34: case 0x48: {                                      // WAIT / DEEPWAIT: sleep until an input or the timeout
        if (this.ev || this.ms() >= this.waitUntil) { const e = this.ev; this.ev = false; return e ? 1 : 0; }
        return undefined;                                          // stall
      }
      case 0x38: return this.ctrl;
      case 0x3c: return this.cfg;
      case 0x60: return 0;                                         // printer shifter: nothing received
      case 0x64: return ((this.gpio & 1) ? 0 : 1) | ((this.gpio & 2) ? 0 : 2);   // I2C released lines read high, no charger
      default: return 0;
    }
  }

  iow(o, v) {
    switch (o) {
      case 0x10: case 0x20: case 0x50: { const d = this.dev(o); d.last = d.xfer(v & 0xff); return; }
      case 0x18: case 0x28: case 0x58: this.dev(o).select((v & 1) === 1); return;
      case 0x30: this.onUart(String.fromCharCode(v & 0xff)); return;
      case 0x34: case 0x48: this.waitUntil = this.ms() + (v & 0xffff); this.ev = false; return;
      case 0x38: {
        const prev = this.ctrl;
        this.ctrl = v;
        if ((prev & 1) && !(v & 1)) this.onCore('run');          // core reset released
        if (!(prev & 1) && (v & 1)) this.onCore('reset');
        return;
      }
      case 0x3c: this.cfg = v; return;
      case 0x4c: this.onBeep(v); return;
      case 0x64: this.gpio = v; return;
      default: return;
    }
  }

  // the boot ROM: copy u32 length + code from flash 0x100000 to SDRAM 0x780000
  boot() {
    const f = this.flash.mem;
    const len = f[0x100000] | (f[0x100001] << 8) | (f[0x100002] << 16) | (f[0x100003] << 24);
    if (len <= 0 || len > 0x70000) throw new Error('no firmware in flash at 0x100000');
    this.sdram.set(f.subarray(0x100004, 0x100004 + len), 0x780000);
    this.rebootReq = false;
    return 0x780000;
  }
}

if (typeof module !== 'undefined') module.exports = { Board, SpiFlash, SdCard, K };
