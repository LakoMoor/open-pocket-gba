// ROMs built by fw/home/build.sh: the home screen (run while no game is loaded) and the
// Game Boy Printer viewer (Tools > Game Boy Printer).
    .section .rodata.home, "a"
    .global home_rom, home_rom_end, printer_rom, printer_rom_end
    .balign 4
home_rom:
    .incbin "home/home.gb"
home_rom_end:
    .balign 4
printer_rom:
    .incbin "home/printer.gb"
printer_rom_end:
