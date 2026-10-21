; Open Pocket printer viewer: shows what a Game Boy printed to the board (Tools > Game Boy
; Printer). The firmware copies this ROM to SDRAM, puts the picture (2bpp tiles, 20 per
; row) at $4000 and the parameters at $0150, then starts the core. The paper slides up out
; line by line under a sheet of blank paper (the window) with a motor sound, a long picture
; then scrolls to its end; afterwards up / down scroll it. CGB only: two VRAM banks give room
; for 25 tile rows (200 lines). Below the torn edge is the dark table.

DEF rP1   EQU $FF00
DEF rNR41 EQU $FF20
DEF rNR42 EQU $FF21
DEF rNR43 EQU $FF22
DEF rNR44 EQU $FF23
DEF rNR50 EQU $FF24
DEF rNR51 EQU $FF25
DEF rNR52 EQU $FF26
DEF rLCDC EQU $FF40
DEF rSCY  EQU $FF42
DEF rWY   EQU $FF4A
DEF rWX   EQU $FF4B
DEF rLY   EQU $FF44
DEF rVBK  EQU $FF4F
DEF rBCPS EQU $FF68
DEF rBCPD EQU $FF69
DEF rIF   EQU $FF0F
DEF rIE   EQU $FFFF

DEF MAXROWS EQU 25
DEF T_PAPER EQU 253             ; bank 1, palette 1: blank paper (the window)
DEF T_TEAR  EQU 254             ; bank 1, palette 1: the torn edge
DEF T_TABLE EQU 255             ; bank 1, palette 1: the table around the paper

SECTION "vblank", ROM0[$0040]
    reti                        ; VBlank only wakes the main loop from halt

SECTION "header", ROM0[$0100]
    nop
    jp Start
    ds $150 - @, 0

SECTION "params", ROM0[$0150]
PRows::    db 18                ; tile rows of the picture (8 lines each)
PPal::     db $E4               ; printer palette: colour n -> shade (bits 2n+1:2n)
PFlags::   db 1                 ; bit 0: feed animation

SECTION "hram", HRAM
hScy:    ds 1                   ; scroll: lines from the top of the picture, may be negative
hMax:    ds 1                   ; largest scroll
hFrame:  ds 1
hFeed:   ds 1                   ; 1: revealing (WY moves down), 2: scrolling to the end
hWy:     ds 1
hKeys:   ds 1

SECTION "main", ROM0[$0200]
Start:
    di
    ld sp, $E000
.wv:
    ldh a, [rLY]
    cp 144
    jr c, .wv
    xor a
    ldh [rLCDC], a

    ; clamp the row count
    ld a, [PRows]
    cp MAXROWS + 1
    jr c, .rows_ok
    ld a, MAXROWS
.rows_ok:
    ld [wRows], a

    ; tiles: picture tile i -> bank i / 256, index i % 256 at $8000
    ld a, [wRows]
    ld c, a
    ld b, 0
    ld h, b
    ld l, c
    add hl, hl
    add hl, hl
    add hl, bc                  ; rows * 5
    add hl, hl
    add hl, hl                  ; rows * 20 = tiles
    ld b, h
    ld c, l                     ; bc = tile count
    ld hl, $4000
    ld de, $8000
    xor a
    ldh [rVBK], a
.tile:
    ld a, b
    or c
    jr z, .tiles_done
    push bc
    ld b, 16
.byte:
    ld a, [hl+]
    ld [de], a
    inc de
    dec b
    jr nz, .byte
    pop bc
    dec bc
    ld a, d
    cp $90                      ; 256 tiles in this bank
    jr nz, .tile
    ld a, 1
    ldh [rVBK], a
    ld de, $8000
    jr .tile
.tiles_done:
    ; bank 1: paper, torn edge, table
    ld a, 1
    ldh [rVBK], a
    ld hl, $8000 + T_PAPER * 16
    ld de, PaperTiles
    ld b, 48
.pt:
    ld a, [de]
    inc de
    ld [hl+], a
    dec b
    jr nz, .pt

    ; maps: the table everywhere ($9800), paper in the window map ($9C00)
    ld hl, $9800
    ld bc, $800
.clr:
    ld a, h
    cp $9C
    ld a, T_TABLE
    jr c, .clr1
    ld a, T_PAPER
.clr1:
    ld [hl], a                  ; tile (bank 0 view)
    ld a, 1
    ldh [rVBK], a
    ld a, 9                     ; attribute: bank 1, palette 1
    ld [hl+], a
    xor a
    ldh [rVBK], a
    dec bc
    ld a, b
    or c
    jr nz, .clr
    ; picture rows
    ld hl, $9800
    ld de, 0                    ; tile number
    ld a, [wRows]
    ld b, a
.row:
    ld c, 20
.col:
    xor a
    ldh [rVBK], a
    ld a, e
    ld [hl], a
    ld a, 1
    ldh [rVBK], a
    ld a, d                     ; bank = tile / 256
    and 1
    add a
    add a
    add a
    ld [hl+], a
    xor a
    ldh [rVBK], a
    inc de
    dec c
    jr nz, .col
    push de
    ld de, 12
    add hl, de
    pop de
    dec b
    jr nz, .row
    ; torn edge below the picture
    ld c, 20
.tear:
    xor a
    ldh [rVBK], a
    ld a, T_TEAR
    ld [hl], a
    ld a, 1
    ldh [rVBK], a
    ld a, 9
    ld [hl+], a
    dec c
    jr nz, .tear
    xor a
    ldh [rVBK], a

    ; palette 0: printer palette onto paper colours
    ld a, $80
    ldh [rBCPS], a
    ld a, [PPal]
    ld b, a
    ld c, 4
.pal:
    ld a, b
    and 3
    add a
    ld e, a
    ld d, 0
    ld hl, Paper
    add hl, de
    ld a, [hl+]
    ldh [rBCPD], a
    ld a, [hl]
    ldh [rBCPD], a
    srl b
    srl b
    dec c
    jr nz, .pal
    ld hl, Paper1               ; palette 1: paper, -, -, table
    ld b, 8
.p1:
    ld a, [hl+]
    ldh [rBCPD], a
    dec b
    jr nz, .p1

    ; scroll: the picture starts below the screen when the feed animation runs
    ld a, [wRows]
    inc a                       ; the torn row too
    add a
    add a
    add a                       ; lines
    sub 144
    jr nc, .m
    xor a
.m:
    ldh [hMax], a
    xor a
    ldh [hScy], a
    ldh [hWy], a
    ld a, [PFlags]
    and 1
    ldh [hFeed], a
    ld b, %10010001             ; LCD on, BG, tiles at $8000, map $9800
    jr z, .nowin
    ld b, %11110001             ; + window from $9C00 (the blank sheet)
.nowin:
    ld a, b
    ld [wLcdc], a
    ld a, 7
    ldh [rWX], a
    xor a
    ldh [rWY], a

    ; sound: the noise channel for the motor
    ld a, $80
    ldh [rNR52], a
    ld a, $77
    ldh [rNR50], a
    ld a, $88
    ldh [rNR51], a

    xor a
    ldh [rSCY], a
    ld a, [wLcdc]
    ldh [rLCDC], a
    xor a
    ldh [rIF], a
    ld a, 1
    ldh [rIE], a
    ei

Loop:
    halt
    nop
    ldh a, [hFrame]
    inc a
    ldh [hFrame], a
    ldh a, [hFeed]
    and a
    jr z, .user
    cp 2
    jr z, .scroll
    ; reveal: the sheet slides down one line per frame
    ldh a, [hWy]
    inc a
    ldh [hWy], a
    ldh [rWY], a
    cp 144
    jr c, .buzz
    ld a, %10010001             ; window off
    ldh [rLCDC], a
    ld a, 2
    ldh [hFeed], a
    jr .buzz
.scroll:
    ; then a long picture moves on to its end
    ldh a, [hMax]
    ld c, a
    ldh a, [hScy]
    cp c
    jr nc, .stop
    inc a
    ldh [hScy], a
    jr .buzz
.stop:
    xor a
    ldh [hFeed], a
    ldh [rNR42], a              ; motor off
    ld a, $80
    ldh [rNR44], a
    jr .set
.buzz:
    ldh a, [hFrame]
    and 7
    jr nz, .set
    xor a
    ldh [rNR41], a
    ld a, $52
    ldh [rNR42], a
    ld a, $21
    ldh [rNR43], a
    ld a, $80
    ldh [rNR44], a
    jr .set
.user:
    ; d-pad: up / down scroll 2 lines per frame
    ld a, $20
    ldh [rP1], a
    ldh a, [rP1]
    ldh a, [rP1]
    cpl
    and $0F
    ld b, a
    ld a, $30
    ldh [rP1], a
    bit 3, b                    ; down
    jr z, .up
    ldh a, [hMax]
    ld c, a
    ldh a, [hScy]
    cp c
    jr nc, .set
    inc a
    inc a
    cp c
    jr c, .sd
    ld a, c
.sd:
    ldh [hScy], a
.up:
    bit 2, b                    ; up
    jr z, .set
    ldh a, [hScy]
    and a
    jr z, .set
    dec a
    jr z, .su
    dec a
.su:
    ldh [hScy], a
.set:
    ldh a, [hScy]
    ldh [rSCY], a
    jp Loop

; paper colours by shade: warm white paper, two greys, the thermal black
Paper:
    dw $73DE, $56B5, $2D6B, $0842

Paper1:                         ; palette 1: paper white, unused, unused, the dark table
    dw $73DE, $56B5, $2D6B, $0C63

PaperTiles:
    ds 16, $00                  ; T_PAPER: colour 0
    db $00, $00, $00, $00, $00, $00, $00, $00     ; T_TEAR: paper, then a zigzag into the table
    db $81, $81, $C3, $C3, $E7, $E7, $FF, $FF
    ds 16, $FF                  ; T_TABLE: colour 3

SECTION "wram", WRAM0
wRows: ds 1
wLcdc: ds 1
