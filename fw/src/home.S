// The home screen ROM (fw/home/home.gb, built by fw/home/build.sh), run while no game is loaded.
    .section .rodata.home, "a"
    .global home_rom, home_rom_end
    .balign 4
home_rom:
    .incbin "home/home.gb"
home_rom_end:
