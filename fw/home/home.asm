; Open Pocket home screen: the ROM the core runs while no game is loaded.
;
; A synthwave scene drawn with per-scanline tricks. A STAT (HBlank) interrupt on every
; line loads the next line's background colour (palette 0 and 1 colour 0) and SCX / SCY
; from four 256-byte tables. The main loop fills the back set of tables each frame and the
; VBlank handler swaps them. Sky gradient, moving floor grid, logo drop with a bounce,
; sunrise and the wobble all come from those tables. Music on all four APU channels.
; CGB in double speed; on a DMG the same tables drive BGP instead of colour palettes.

SECTION "gendata", ROM0
INCLUDE "build/data.inc"

DEF rP1   EQU $FF00
DEF rIF   EQU $FF0F
DEF rNR10 EQU $FF10
DEF rNR11 EQU $FF11
DEF rNR12 EQU $FF12
DEF rNR13 EQU $FF13
DEF rNR14 EQU $FF14
DEF rNR21 EQU $FF16
DEF rNR22 EQU $FF17
DEF rNR23 EQU $FF18
DEF rNR24 EQU $FF19
DEF rNR30 EQU $FF1A
DEF rNR31 EQU $FF1B
DEF rNR32 EQU $FF1C
DEF rNR33 EQU $FF1D
DEF rNR34 EQU $FF1E
DEF rNR41 EQU $FF20
DEF rNR42 EQU $FF21
DEF rNR43 EQU $FF22
DEF rNR44 EQU $FF23
DEF rNR50 EQU $FF24
DEF rNR51 EQU $FF25
DEF rNR52 EQU $FF26
DEF rLCDC EQU $FF40
DEF rSTAT EQU $FF41
DEF rSCY  EQU $FF42
DEF rSCX  EQU $FF43
DEF rLY   EQU $FF44
DEF rDMA  EQU $FF46
DEF rBGP  EQU $FF47
DEF rOBP0 EQU $FF48
DEF rOBP1 EQU $FF49
DEF rKEY1 EQU $FF4D
DEF rVBK  EQU $FF4F
DEF rBCPS EQU $FF68
DEF rBCPD EQU $FF69
DEF rOCPS EQU $FF6A
DEF rOCPD EQU $FF6B
DEF rIE   EQU $FFFF

DEF FLOOR_SCY EQU 48            ; floor rows 18..23 (lines 144..191) shown from line 96

; ---------------------------------------------------------------- RAM
SECTION "tables", WRAM0[$C000]
wTabA:   ds $400                ; colour lo, colour hi, SCX, SCY: one page each
wTabB:   ds $400
wOAM:    ds $A0
wFlags:  ds 48
wBase:   ds 2

SECTION "hram_dma", HRAM[$FF80]
hOAMDMA:   ds 10
hStatJump: ds 3

SECTION "hram_vars", HRAM
hCGB:      ds 1
hCur:      ds 1                 ; high byte of the table set the display uses
hReady:    ds 1
hVbl:      ds 1
hFrame:    ds 1
hIntroF:   ds 1
hIntroDone: ds 1
hFade:     ds 1
hLogoD:    ds 1
hSunS:     ds 1
hAmp:      ds 1
hAmpIdle:  ds 1
hWobP:     ds 1
hPhase:    ds 1
hDrift:    ds 1
hTmp:      ds 1
hC1:       ds 2
hC2:       ds 2
; music
hTick:     ds 1
hRow:      ds 2
hSong:     ds 2
hArpBase:  ds 1
hArpMaj:   ds 1
hArpStep:  ds 1

; ---------------------------------------------------------------- vectors
SECTION "rst", ROM0[$0000]
    ret

SECTION "vblank", ROM0[$0040]
    jp VBlankIsr

SECTION "stat", ROM0[$0048]
    jp hStatJump

SECTION "header", ROM0[$0100]
    nop
    jp Start
    ds $150 - @, 0

; ---------------------------------------------------------------- interrupts
SECTION "isr", ROM0

; HBlank of line LY: set up line LY + 1
StatCGB:
    push af
    push hl
    push bc
    ldh a, [rLY]
    inc a
    ld l, a
    ldh a, [hCur]
    ld h, a
    ld b, [hl]
    inc h
    ld c, [hl]
    inc h
    ld a, $80
    ldh [rBCPS], a
    ld a, b
    ldh [rBCPD], a
    ld a, c
    ldh [rBCPD], a
    ld a, $88
    ldh [rBCPS], a
    ld a, b
    ldh [rBCPD], a
    ld a, c
    ldh [rBCPD], a
    ld a, [hl]
    ldh [rSCX], a
    inc h
    ld a, [hl]
    ldh [rSCY], a
    pop bc
    pop hl
    pop af
    reti

StatDMG:
    push af
    push hl
    ldh a, [rLY]
    inc a
    ld l, a
    ldh a, [hCur]
    ld h, a
    ld a, [hl]
    ldh [rBGP], a
    inc h
    inc h
    ld a, [hl]
    ldh [rSCX], a
    inc h
    ld a, [hl]
    ldh [rSCY], a
    pop hl
    pop af
    reti

VBlankIsr:
    push af
    push bc
    push hl
    ldh a, [hReady]
    and a
    jr z, .keep
    ldh a, [hCur]
    xor $04
    ldh [hCur], a
    xor a
    ldh [hReady], a
.keep:
    ldh a, [hCur]
    ld h, a
    ld l, 0
    ldh a, [hCGB]
    and a
    jr z, .dmg
    ld b, [hl]
    inc h
    ld c, [hl]
    ld a, $80
    ldh [rBCPS], a
    ld a, b
    ldh [rBCPD], a
    ld a, c
    ldh [rBCPD], a
    ldh a, [hC1]                ; logo colours 1 and 2 follow
    ldh [rBCPD], a
    ldh a, [hC1 + 1]
    ldh [rBCPD], a
    ldh a, [hC2]
    ldh [rBCPD], a
    ldh a, [hC2 + 1]
    ldh [rBCPD], a
    ld a, $88
    ldh [rBCPS], a
    ld a, b
    ldh [rBCPD], a
    ld a, c
    ldh [rBCPD], a
    inc h
    jr .regs
.dmg:
    ld a, [hl]
    ldh [rBGP], a
    inc h
    inc h
.regs:
    ld a, [hl]
    ldh [rSCX], a
    inc h
    ld a, [hl]
    ldh [rSCY], a
    call hOAMDMA
    ld a, 1
    ldh [hVbl], a
    pop hl
    pop bc
    pop af
    reti

DMARoutine:
    ld a, HIGH(wOAM)
    ldh [rDMA], a
    ld a, 40
.wait:
    dec a
    jr nz, .wait
    ret
.end:

; ---------------------------------------------------------------- start-up
SECTION "main", ROM0

Start:
    di
    ld sp, $E000
    cp $11
    ld a, 0
    jr nz, .notcgb
    inc a
.notcgb:
    ldh [hCGB], a
    and a
    jr z, .speed_done
    ld a, 1                     ; double speed
    ldh [rKEY1], a
    ld a, $30
    ldh [rP1], a
    xor a
    ldh [rIE], a
    stop
.speed_done:
    ; LCD off in VBlank
.wv:
    ldh a, [rLY]
    cp 144
    jr c, .wv
    xor a
    ldh [rLCDC], a

    ; clear VRAM (both banks on CGB) and the tables
    ld hl, $8000
    ld bc, $2000
    call Clear
    ldh a, [hCGB]
    and a
    jr z, .vram_done
    ld a, 1
    ldh [rVBK], a
    ld hl, $8000
    ld bc, $2000
    call Clear
    xor a
    ldh [rVBK], a
.vram_done:
    ld hl, wTabA
    ld bc, $A00
    call Clear

    ; tiles: BG index 0..127 at $9000, 128..255 at $8800; sprites at $8000
    ld hl, BgTiles
    ld de, $9000
    ld bc, $800
    call Copy
    ld de, $8800
    ld bc, $800
    call Copy
    ld hl, ObjTiles
    ld de, $8000
    ld bc, ObjTilesEnd - ObjTiles
    call Copy

    ; maps
    ld hl, LogoMap
    ld de, $9800 + 32 * 1
    ld b, 6
    call CopyRows
    ld hl, SunMap
    ld de, $9800 + 32 * 12
    ld b, 5
    call CopyRows
    ld hl, FloorMap
    ld de, $9800 + 32 * 18
    ld b, 6
    call CopyRows

    ldh a, [hCGB]
    and a
    jr z, .dmg_pal
    ; sun rows use BG palette 1
    ld a, 1
    ldh [rVBK], a
    ld hl, $9800 + 32 * 12
    ld bc, 32 * 5
.attr:
    ld a, 1
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, .attr
    xor a
    ldh [rVBK], a
    ; palettes
    ld a, $80
    ldh [rBCPS], a
    ld hl, BgPalettes
    ld b, BgPalettesEnd - BgPalettes
.bp:
    ld a, [hl+]
    ldh [rBCPD], a
    dec b
    jr nz, .bp
    ld a, $80
    ldh [rOCPS], a
    ld hl, ObjPalettes
    ld b, ObjPalettesEnd - ObjPalettes
.op:
    ld a, [hl+]
    ldh [rOCPD], a
    dec b
    jr nz, .op
    ld hl, StatCGB
    jr .pal_done
.dmg_pal:
    ld a, $93
    ldh [rBGP], a
    ld a, $18
    ldh [rOBP0], a
    ld a, $0C
    ldh [rOBP1], a
    ld hl, StatDMG
.pal_done:
    ld a, $C3                   ; jp StatCGB / StatDMG
    ldh [hStatJump], a
    ld a, l
    ldh [hStatJump + 1], a
    ld a, h
    ldh [hStatJump + 2], a

    ; OAM DMA routine into HRAM
    ld hl, DMARoutine
    ld de, hOAMDMA
    ld b, DMARoutine.end - DMARoutine
.dma:
    ld a, [hl+]
    ld [de], a
    inc de
    dec b
    jr nz, .dma

    ; variables
    xor a
    ld hl, hCGB + 1
    ld b, hArpStep - hCGB
.zv:
    ld [hl+], a
    dec b
    jr nz, .zv
    ld a, HIGH(wTabA)
    ldh [hCur], a
    ld hl, Song
    ld a, l
    ldh [hSong], a
    ld a, h
    ldh [hSong + 1], a

    call SoundInit

    ld a, %10000011             ; LCD on, BG + OBJ, map $9800, tiles $8800 mode, BG priority
    ldh [rLCDC], a
    ld a, %00001000             ; STAT interrupt on HBlank
    ldh [rSTAT], a
    xor a
    ldh [rIF], a
    ld a, %00000011
    ldh [rIE], a
    ei

MainLoop:
    halt
    nop
    ldh a, [hVbl]
    and a
    jr z, MainLoop
    xor a
    ldh [hVbl], a
    call MusicTick
    call Animate
    call BuildTables
    call BuildSprites
    ld a, 1
    ldh [hReady], a
    jr MainLoop

; ---------------------------------------------------------------- helpers
Clear:                          ; hl = dst, bc = count
    xor a
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, Clear
    ret

Copy:                           ; hl = src, de = dst, bc = count (hl advances)
    ld a, [hl+]
    ld [de], a
    inc de
    dec bc
    ld a, b
    or c
    jr nz, Copy
    ret

CopyRows:                       ; b rows of 20 tiles from hl to the map at de
    ld c, 20
.t:
    ld a, [hl+]
    ld [de], a
    inc de
    dec c
    jr nz, .t
    ld a, e
    add 12
    ld e, a
    jr nc, .n
    inc d
.n:
    dec b
    jr nz, CopyRows
    ret

; hl = a * 192
Mul192:
    ld l, a
    ld h, 0
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl                  ; *64
    ld d, h
    ld e, l
    add hl, hl                  ; *128
    add hl, de
    ret

; ---------------------------------------------------------------- animation
Animate:
    ldh a, [hFrame]
    inc a
    ldh [hFrame], a
    ldh a, [hIntroDone]
    and a
    jr nz, .idle
    ldh a, [hIntroF]
    ld c, a
    ld b, 0
    ld hl, IntroFade
    add hl, bc
    ld a, [hl]
    ldh [hFade], a
    ld hl, IntroLogo
    add hl, bc
    ld a, [hl]
    ldh [hLogoD], a
    ld hl, IntroSun
    add hl, bc
    ld a, [hl]
    ldh [hSunS], a
    ld hl, IntroAmp
    add hl, bc
    ld a, [hl]
    ldh [hAmp], a
    ld a, c
    inc a
    ldh [hIntroF], a
    cp INTRO_LEN
    jr c, .common
    ld a, 1
    ldh [hIntroDone], a
    jr .common
.idle:
    ld a, 7
    ldh [hFade], a
    xor a
    ldh [hSunS], a
    ldh a, [hFrame]
    and 127
    ld c, a
    ld b, 0
    ld hl, Bob
    add hl, bc
    ld a, [hl]
    ldh [hLogoD], a
    ; a short wobble every 256 frames
    ldh a, [hFrame]
    and a
    jr nz, .nw
    ld a, 5
    ldh [hAmpIdle], a
.nw:
    ldh a, [hFrame]
    and 15
    jr nz, .na
    ldh a, [hAmpIdle]
    and a
    jr z, .na
    dec a
    ldh [hAmpIdle], a
.na:
    ldh a, [hAmpIdle]
    ldh [hAmp], a
.common:
    ldh a, [hWobP]
    inc a
    ldh [hWobP], a
    ldh a, [hPhase]
    inc a
    ldh [hPhase], a
    ldh a, [hFrame]
    and 3
    jr nz, .nd
    ldh a, [hDrift]
    dec a
    ldh [hDrift], a
.nd:
    ; logo colour cycle
    ldh a, [hFrame]
    rrca
    and 63
    add a
    ld c, a
    ld b, 0
    ld hl, LogoC1
    add hl, bc
    ld a, [hl+]
    ldh [hC1], a
    ld a, [hl]
    ldh [hC1 + 1], a
    ld hl, LogoC2
    add hl, bc
    ld a, [hl+]
    ldh [hC2], a
    ld a, [hl]
    ldh [hC2 + 1], a
    ret

; ---------------------------------------------------------------- raster tables
BuildTables:
    ; floor grid flags: a line wherever the depth crosses a multiple of 32
    ld hl, FloorZ
    ld de, wFlags
    ldh a, [hPhase]
    ld b, a
    ld a, [hl+]
    add b
    ld c, a
    ld a, 48
    ldh [hTmp], a
.fl:
    ld a, [hl+]
    add b
    push af
    xor c
    and $E0
    ld [de], a
    inc de
    pop af
    ld c, a
    ldh a, [hTmp]
    dec a
    ldh [hTmp], a
    jr nz, .fl

    ldh a, [hCur]
    xor $04
    ld d, a                     ; d = colour lo page of the back set
    ldh a, [hCGB]
    and a
    jp z, .dmg

    ; sky: 96 colours of the fade level
    push de
    ldh a, [hFade]
    call Mul192
    ld bc, SkyCol
    add hl, bc
    pop de
    ld e, 0
.sky:
    ld a, [hl+]
    ld [de], a
    inc d
    ld a, [hl+]
    ld [de], a
    dec d
    inc e
    ld a, e
    cp 96
    jr nz, .sky

    ; floor: off / on colour per line
    push de
    ldh a, [hFade]
    call Mul192
    ld bc, FloorCol
    add hl, bc
    pop de
    ld a, l
    ld [wBase], a
    ld a, h
    ld [wBase + 1], a
    ld bc, wFlags
.floor:
    ld a, [wBase]
    ld l, a
    ld a, [wBase + 1]
    ld h, a
    ld a, [bc]
    and a
    jr z, .off
    ld a, l
    add 96
    ld l, a
    jr nc, .off
    inc h
.off:
    ld a, e
    sub 96
    add a
    add l
    ld l, a
    jr nc, .n1
    inc h
.n1:
    ld a, [hl+]
    ld [de], a
    inc d
    ld a, [hl]
    ld [de], a
    dec d
    inc bc
    inc e
    ld a, e
    cp 144
    jr nz, .floor
    jr .scroll

.dmg:
    ld hl, DmgSky
    ld e, 0
.dsky:
    ld a, [hl+]
    ld [de], a
    inc e
    ld a, e
    cp 96
    jr nz, .dsky
    ld bc, wFlags
.dfl:
    ld a, [bc]
    and a
    ld a, $93
    jr z, .doff
    ld a, $91
.doff:
    ld [de], a
    inc bc
    inc e
    ld a, e
    cp 144
    jr nz, .dfl

.scroll:
    ; SCX: wobble on the logo lines
    inc d
    inc d
    ld e, 0
    xor a
.x0:
    ld [de], a
    inc e
    bit 3, e
    jr z, .x0                   ; lines 0..7
    push de
    ldh a, [hAmp]
    ld l, a
    ld h, 0
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    ld bc, Wobble
    add hl, bc
    ld b, h
    ld c, l
    pop de
.xw:
    ld a, e
    add a
    ld h, a
    ldh a, [hWobP]
    add h
    and 63
    add c
    ld l, a
    ld a, b
    adc 0
    ld h, a
    ld a, [hl]
    ld [de], a
    inc e
    ld a, e
    cp 56
    jr nz, .xw
    xor a
.x2:
    ld [de], a
    inc e
    ld a, e
    cp 144
    ld a, 0
    jr nz, .x2

    ; SCY: logo offset, sunrise, floor
    inc d
    ld e, 0
    xor a
.y0:
    ld [de], a
    inc e
    bit 3, e
    jr z, .y0
    ldh a, [hLogoD]
    ld b, a
.y1:
    ld a, b
    ld [de], a
    inc e
    ld a, e
    cp 56
    jr nz, .y1
    ldh a, [hSunS]              ; sun rows 12..16 (lines 96..135): SCY 40 = risen
    cpl
    add 41
    ld b, a
.y2:
    ld a, b
    ld [de], a
    inc e
    ld a, e
    cp 96
    jr nz, .y2
.y3:
    ld a, FLOOR_SCY
    ld [de], a
    inc e
    ld a, e
    cp 144
    jr nz, .y3
    ret

; ---------------------------------------------------------------- sprites
BuildSprites:
    ld hl, wOAM
    ld de, Stars
    ld c, NSTARS
.star:
    ld a, [de]                  ; x
    inc de
    ld b, a
    ldh a, [hDrift]
    add b
    ld b, a
    ld a, [de]                  ; y
    inc de
    add 16
    ld [hl+], a
    ld a, b
    ld [hl+], a
    ld a, [de]                  ; twinkle phase
    inc de
    ld b, a
    ldh a, [hFrame]
    rrca
    rrca
    rrca
    and $1F
    add b
    and 3
    inc a
    ld [hl+], a
    ld a, $80                   ; behind the logo and the sun
    ld [hl+], a
    dec c
    jr nz, .star

    ; PRESS START, blinking, after the intro
    ldh a, [hIntroDone]
    and a
    jr z, .hide
    ldh a, [hFrame]
    and $30
    cp $30
    jr z, .hide
    ld de, PressText
    ld c, 10
.txt:
    ld a, 146
    ld [hl+], a
    ld a, [de]                  ; x
    inc de
    ld [hl+], a
    ld a, [de]                  ; tile
    inc de
    ld [hl+], a
    ld a, $11
    ld [hl+], a
    dec c
    jr nz, .txt
    ret
.hide:
    ld c, 10
    xor a
.h:
    ld [hl+], a
    inc hl
    inc hl
    inc hl
    dec c
    jr nz, .h
    ret

PressText:                      ; x, tile ("PRESS START")
    db 44, 5, 52, 6, 60, 7, 68, 8, 76, 8
    db 92, 8, 100, 9, 108, 10, 116, 6, 124, 9

; ---------------------------------------------------------------- music
SoundInit:
    ld a, $80
    ldh [rNR52], a
    ld a, $77
    ldh [rNR50], a
    ld a, $FF
    ldh [rNR51], a
    xor a
    ldh [rNR30], a
    ld hl, WaveSaw
    ld de, $FF30
    ld b, 16
.w:
    ld a, [hl+]
    ld [de], a
    inc de
    dec b
    jr nz, .w
    ret

WaveSaw:
    db $01, $23, $45, $67, $89, $AB, $CD, $EF, $01, $23, $45, $67, $89, $AB, $CD, $EF

; de = period of note a
NotePeriod:
    add a
    ld e, a
    ld d, 0
    ld hl, NotePeriods
    add hl, de
    ld e, [hl]
    inc hl
    ld d, [hl]
    ret

MusicTick:
    ldh a, [hTick]
    and a
    call z, PlayRow
    ; arpeggio on channel 2: root, third, fifth on successive frames
    ldh a, [hArpBase]
    and a
    jr z, .tick
    ldh a, [hArpStep]
    ld c, 0
    and a
    jr z, .ofs
    ld c, 7
    cp 2
    jr z, .ofs
    ld c, 3
    ldh a, [hArpMaj]
    and a
    jr z, .ofs
    ld c, 4
.ofs:
    ldh a, [hArpBase]
    add c
    call NotePeriod
    ld a, e
    ldh [rNR23], a
    ld a, d
    ldh [rNR24], a
    ldh a, [hArpStep]
    inc a
    cp 3
    jr c, .st
    xor a
.st:
    ldh [hArpStep], a
.tick:
    ldh a, [hTick]
    inc a
    cp 7
    jr c, .t
    xor a
.t:
    ldh [hTick], a
    ret

PlayRow:
    ldh a, [hSong]
    ld l, a
    ldh a, [hSong + 1]
    ld h, a
    ; channel 1: lead
    ld a, [hl+]
    push hl
    and a
    jr z, .c2
    cp 1
    jr nz, .c1on
    xor a
    ldh [rNR12], a
    ld a, $80
    ldh [rNR14], a
    jr .c2
.c1on:
    call NotePeriod
    xor a
    ldh [rNR10], a
    ld a, $80
    ldh [rNR11], a
    ld a, $A6
    ldh [rNR12], a
    ld a, e
    ldh [rNR13], a
    ld a, d
    or $80
    ldh [rNR14], a
.c2:
    pop hl
    ld a, [hl+]
    push hl
    and a
    jr z, .c3
    cp 1
    jr nz, .c2on
    xor a
    ldh [hArpBase], a
    ldh [rNR22], a
    ld a, $80
    ldh [rNR24], a
    jr .c3
.c2on:
    ld b, a
    and $80
    ldh [hArpMaj], a
    ld a, b
    and $7F
    ldh [hArpBase], a
    xor a
    ldh [hArpStep], a
    ld a, b
    and $7F
    call NotePeriod
    ld a, $40
    ldh [rNR21], a
    ld a, $83
    ldh [rNR22], a
    ld a, e
    ldh [rNR23], a
    ld a, d
    or $80
    ldh [rNR24], a
.c3:
    pop hl
    ld a, [hl+]
    push hl
    and a
    jr z, .c4
    cp 1
    jr nz, .c3on
    xor a
    ldh [rNR32], a
    jr .c4
.c3on:
    call NotePeriod
    ld a, $80
    ldh [rNR30], a
    ld a, $40
    ldh [rNR32], a
    ld a, e
    ldh [rNR33], a
    ld a, d
    or $80
    ldh [rNR34], a
.c4:
    pop hl
    ld a, [hl+]
    push hl
    cp 2
    jr c, .next
    sub 2
    add a
    ld e, a
    ld d, 0
    ld hl, Drums
    add hl, de
    xor a
    ldh [rNR41], a
    ld a, [hl+]
    ldh [rNR42], a
    ld a, [hl]
    ldh [rNR43], a
    ld a, $80
    ldh [rNR44], a
.next:
    pop hl
    ; next row, loop at the end
    ldh a, [hRow]
    ld c, a
    ldh a, [hRow + 1]
    ld b, a
    inc bc
    ld a, c
    cp LOW(SONG_ROWS)
    jr nz, .store
    ld a, b
    cp HIGH(SONG_ROWS)
    jr nz, .store
    ld bc, SONG_LOOP
    ld hl, Song + 4 * SONG_LOOP
.store:
    ld a, c
    ldh [hRow], a
    ld a, b
    ldh [hRow + 1], a
    ld a, l
    ldh [hSong], a
    ld a, h
    ldh [hSong + 1], a
    ret

Drums:                          ; NR42, NR43: kick, snare, hat, crash, open hat
    db $F1, $70
    db $D2, $52
    db $61, $00
    db $F5, $23
    db $64, $01

; ---------------------------------------------------------------- data
SECTION "data", ROM0
BgPalettes:                     ; BGR555: 0 per line, logo white / cyan / magenta outline; sun
    dw $0000, $7FFF, $7F4A, $68DF
    dw $0000, $15FF, $0CDF, $40DF
BgPalettesEnd:
ObjPalettes:                    ; stars, PRESS START
    dw $0000, $5129, $7F18, $7FFF
    dw $0000, $2806, $68DF, $7FFF
ObjPalettesEnd:

BgTiles:
    INCBIN "build/gfx_bg.2bpp"
ObjTiles:
    INCBIN "build/gfx_obj.2bpp"
ObjTilesEnd:
