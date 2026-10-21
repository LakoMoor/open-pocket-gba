; Open Pocket home screen: the ROM the core runs while no game is loaded.
;
; Scenes drawn with per-scanline tricks. A STAT (HBlank) interrupt on every line loads the
; next line's background colour (palette 0 and 1 colour 0) and SCX / SCY from four 256-byte
; tables. The main loop fills the back set of tables each frame and the VBlank handler swaps
; them. The firmware picks the scene by writing its number to $0150 before the start.
;   0 Synthwave: sky gradient, rising sun, moving floor grid
;   1 Space: nebula gradient, scrolling star field, a ringed planet in the window, meteors
;   2 City: two skylines scrolling at different speeds, moon, rain, lightning with thunder
; All scenes share the logo drop with a bounce and wobble, twinkling stars, PRESS START,
; and a music player on all four APU channels. CGB in double speed; on a DMG the same tables
; drive BGP instead of colour palettes.

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
DEF rWY   EQU $FF4A
DEF rWX   EQU $FF4B
DEF rKEY1 EQU $FF4D
DEF rVBK  EQU $FF4F
DEF rBCPS EQU $FF68
DEF rBCPD EQU $FF69
DEF rOCPS EQU $FF6A
DEF rOCPD EQU $FF6B
DEF rIE   EQU $FFFF

DEF FLOOR_SCY EQU 48            ; synthwave floor rows 18..23 (lines 144..191) shown from line 96

; ---------------------------------------------------------------- RAM
SECTION "tables", WRAM0[$C000]
wTabA:   ds $400                ; colour lo, colour hi, SCX, SCY: one page each
wTabB:   ds $400
wOAM:    ds $A0
wFlags:  ds 48
wBase:   ds 2
wRain:   ds 2 * 22

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
hScroll1:  ds 1                 ; space: star field / city: far skyline
hScroll2:  ds 1                 ; city: near skyline
hFlash:    ds 1                 ; city: lightning frames left
hThunder:  ds 1                 ; city: frames until the thunder
hRand:     ds 2
hShootT:   ds 1                 ; space: meteor timer / position
hShootX:   ds 1
hShootY:   ds 1
; music
hTick:     ds 1
hRow:      ds 2
hSong:     ds 2
hArpBase:  ds 1
hArpMaj:   ds 1
hArpStep:  ds 1
hLast:     ds 1                 ; end of the cleared variables
; scene parameters (from SceneTable)
hScene:    ds 1
hColBase:  ds 2
hDmgBase:  ds 2
hSongBase: ds 2
hSongRows: ds 2
hSongLoop: ds 2
hTempo:    ds 1
hArp7:     ds 1

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

SECTION "scene_sel", ROM0[$0150]
SceneSel::
    db 0                        ; written by the firmware

SECTION "gendata", ROM0
INCLUDE "build/data.inc"

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

    ; variables
    xor a
    ld hl, hCGB + 1
    ld b, hLast - hCGB
.zv:
    ld [hl+], a
    dec b
    jr nz, .zv
    ld a, $A5
    ldh [hRand], a
    ldh [hRand + 1], a
    ld a, 200
    ldh [hShootT], a
    ld a, 255
    ldh [hFlash + 1], a         ; hThunder idle

    ; scene descriptor: de -> SceneTable + scene * SCENE_SIZE
    ld a, [SceneSel]
.mod:
    cp NSCENES
    jr c, .modok
    sub NSCENES
    jr .mod
.modok:
    ldh [hScene], a
    ld l, a
    ld h, 0
    ld d, h
    ld e, l
    add hl, hl
    add hl, de                  ; *3
    add hl, hl
    add hl, hl
    add hl, hl                  ; *24
    ld de, SceneTable
    add hl, de
    ld d, h
    ld e, l

    ; tiles: BG index 0..127 at $9000, 128..255 at $8800
    call LoadWord               ; hl = tiles
    push hl
    call LoadWord               ; hl = length
    ld b, h
    ld c, l
    pop hl
    push de
    ld de, $9000
    call CopyTiles
    pop de
    ; map list
    call LoadWord
    push de
    call LoadMaps
    pop de
    ; attribute list
    call LoadWord
    push de
    ldh a, [hCGB]
    and a
    call nz, LoadAttrs
    pop de
    ; BG palettes 0 and 1
    call LoadWord
    ldh a, [hCGB]
    and a
    jr z, .nopal
    ld a, $80
    ldh [rBCPS], a
    ld b, 16
.bp:
    ld a, [hl+]
    ldh [rBCPD], a
    dec b
    jr nz, .bp
.nopal:
    call LoadWord               ; colour table
    ld a, l
    ldh [hColBase], a
    ld a, h
    ldh [hColBase + 1], a
    call LoadWord               ; DMG table
    ld a, l
    ldh [hDmgBase], a
    ld a, h
    ldh [hDmgBase + 1], a
    call LoadWord               ; song
    ld a, l
    ldh [hSongBase], a
    ldh [hSong], a
    ld a, h
    ldh [hSongBase + 1], a
    ldh [hSong + 1], a
    call LoadWord
    ld a, l
    ldh [hSongRows], a
    ld a, h
    ldh [hSongRows + 1], a
    call LoadWord
    ld a, l
    ldh [hSongLoop], a
    ld a, h
    ldh [hSongLoop + 1], a
    ld a, [de]
    inc de
    ldh [hTempo], a
    ld a, [de]
    inc de
    ldh [hArp7], a
    ld a, [de]
    inc de
    ldh [rWX], a
    ld a, [de]
    ldh [rWY], a
    ld b, %10000011             ; LCD on, BG + OBJ, map $9800, tiles $8800 mode, BG priority
    and a
    jr z, .nowin
    ld b, %11100011             ; + window from map $9C00
.nowin:
    ld a, b
    ldh [hTmp], a

    ; sprites
    ld hl, ObjTiles
    ld de, $8000
    ld bc, ObjTilesEnd - ObjTiles
    call Copy
    ld hl, RainInit
    ld de, wRain
    ld bc, 2 * NRAIN
    call Copy

    ldh a, [hCGB]
    and a
    jr z, .dmg_pal
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

    ld hl, DMARoutine
    ld de, hOAMDMA
    ld b, DMARoutine.end - DMARoutine
.dma:
    ld a, [hl+]
    ld [de], a
    inc de
    dec b
    jr nz, .dma

    ld a, HIGH(wTabA)
    ldh [hCur], a
    call SoundInit

    ldh a, [hTmp]
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
LoadWord:                       ; hl = [de], de += 2
    ld a, [de]
    inc de
    ld l, a
    ld a, [de]
    inc de
    ld h, a
    ret

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

; bc bytes of BG tiles from hl: the first $800 to $9000, the rest to $8800
CopyTiles:
    ld a, b
    cp $08
    jr c, Copy                  ; less than $800
    push bc
    ld bc, $800
    call Copy
    pop bc
    ld a, b
    sub $08
    ld b, a
    or c
    ret z
    ld de, $8800
    jr Copy

; list at hl: dw dest, db rows, db width, dw src ... dw 0
LoadMaps:
    ld a, [hl+]
    ld e, a
    ld a, [hl+]
    ld d, a
    or e
    ret z
    ld a, [hl+]
    ld b, a                     ; rows
    ld a, [hl+]
    ldh [hTmp], a               ; width
    ld a, [hl+]
    ld c, a
    ld a, [hl+]
    push hl
    ld h, a
    ld l, c                     ; hl = src
.row:
    push de
    ldh a, [hTmp]
    ld c, a
.t:
    ld a, [hl+]
    ld [de], a
    inc de
    dec c
    jr nz, .t
    pop de
    ld a, e
    add 32
    ld e, a
    jr nc, .n
    inc d
.n:
    dec b
    jr nz, .row
    pop hl
    jr LoadMaps

; list at hl: dw dest, db rows, db width, db palette ... dw 0 (VRAM bank 1)
LoadAttrs:
    ld a, 1
    ldh [rVBK], a
.next:
    ld a, [hl+]
    ld e, a
    ld a, [hl+]
    ld d, a
    or e
    jr z, .done
    ld a, [hl+]
    ld b, a
    ld a, [hl+]
    ldh [hTmp], a
    ld a, [hl+]
    push hl
    ld h, a                     ; palette
.row:
    push de
    ldh a, [hTmp]
    ld c, a
.t:
    ld a, h
    ld [de], a
    inc de
    dec c
    jr nz, .t
    pop de
    ld a, e
    add 32
    ld e, a
    jr nc, .n
    inc d
.n:
    dec b
    jr nz, .row
    pop hl
    jr .next
.done:
    xor a
    ldh [rVBK], a
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
    add hl, hl
    ld d, h
    ld e, l
    add hl, hl
    add hl, de
    ret

; hl = a * 288
Mul288:
    ld l, a
    ld h, 0
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl                  ; *32
    ld d, h
    ld e, l
    ld h, a
    ld l, 0                     ; *256
    add hl, de
    ret

; 16-bit Galois LFSR, a = new low byte
Rand:
    ldh a, [hRand + 1]
    srl a
    ldh [hRand + 1], a
    ldh a, [hRand]
    rra
    ldh [hRand], a
    ret nc
    ldh a, [hRand + 1]
    xor $B4
    ldh [hRand + 1], a
    ldh a, [hRand]
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
    ldh a, [hFrame]             ; a short wobble every 256 frames
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
    ; scene parts
    ldh a, [hScene]
    cp 1
    jr z, AnimSpace
    cp 2
    jp z, AnimCity
    ret

AnimSpace:
    ldh a, [hFrame]             ; star field drifts left
    and 1
    jr nz, .m
    ldh a, [hScroll1]
    inc a
    ldh [hScroll1], a
.m:
    ; meteor: hShootT counts down to a flight of 48 frames
    ldh a, [hShootT]
    dec a
    ldh [hShootT], a
    cp 48
    jr z, .start
    ret nc
    ldh a, [hShootX]
    sub 3
    ldh [hShootX], a
    ldh a, [hShootY]
    inc a
    ldh [hShootY], a
    ldh a, [hShootT]
    and a
    ret nz
    call Rand
    or $C0                      ; next one in 192..255 frames, plus a flight
    ldh [hShootT], a
    ret
.start:
    call Rand
    and 63
    add 120
    ldh [hShootX], a
    call Rand
    and 31
    add 20
    ldh [hShootY], a
    ret

AnimCity:
    ldh a, [hFrame]             ; far skyline 1 px / 4 frames, near 1 px / 2 frames
    ld b, a
    and 3
    jr nz, .f
    ldh a, [hScroll1]
    inc a
    ldh [hScroll1], a
.f:
    ld a, b
    and 1
    jr nz, .r
    ldh a, [hScroll2]
    inc a
    ldh [hScroll2], a
.r:
    ; rain: down 4, left 1, wrap at the bottom
    ld hl, wRain
    ld c, NRAIN
.drop:
    ld a, [hl]
    dec a
    ld [hl+], a
    ld a, [hl]
    add 4
    cp 176
    jr c, .ok
    call Rand
    dec hl
    ld [hl+], a
    xor a
.ok:
    ld [hl+], a
    dec c
    jr nz, .drop
    ; lightning
    ldh a, [hFlash]
    and a
    jr z, .maybe
    dec a
    ldh [hFlash], a
    jr .thunder
.maybe:
    ldh a, [hIntroDone]
    and a
    jr z, .thunder
    call Rand
    and a
    jr nz, .thunder
    ldh a, [hRand + 1]
    cp $30                      ; ~1 in 1300 frames
    jr nc, .thunder
    ld a, 7
    ldh [hFlash], a
    ld a, 25
    ldh [hThunder], a
.thunder:
    ldh a, [hThunder]
    cp 255
    ret z
    dec a
    ldh [hThunder], a
    ret nz
    ld a, 255
    ldh [hThunder], a
    xor a                       ; a long low rumble on the noise channel
    ldh [rNR41], a
    ld a, $F7
    ldh [rNR42], a
    ld a, $73
    ldh [rNR43], a
    ld a, $80
    ldh [rNR44], a
    ret

; ---------------------------------------------------------------- raster tables
BuildTables:
    ; colours: 144 lines of the fade level (city lightning: level 8)
    ldh a, [hFade]
    ld b, a
    ldh a, [hFlash]
    and a
    jr z, .lvl
    cp 3                        ; flicker: bright, dim, bright
    jr z, .lvl
    ld b, 8
.lvl:
    ld a, b
    call Mul288
    ldh a, [hColBase]
    ld e, a
    ldh a, [hColBase + 1]
    ld d, a
    add hl, de
    ldh a, [hCur]
    xor $04
    ld d, a                     ; d = colour lo page of the back set
    ldh a, [hCGB]
    and a
    jr z, .dmgcol
    ld e, 0
.col:
    ld a, [hl+]
    ld [de], a
    inc d
    ld a, [hl+]
    ld [de], a
    dec d
    inc e
    ld a, e
    cp 144
    jr nz, .col
    jr .colend
.dmgcol:
    ldh a, [hDmgBase]
    ld l, a
    ldh a, [hDmgBase + 1]
    ld h, a
    ld e, 0
.dc:
    ld a, [hl+]
    ld [de], a
    inc e
    ld a, e
    cp 144
    jr nz, .dc
.colend:
    ldh a, [hScene]
    and a
    call z, FloorGrid

    ; SCX: 0, wobble on the logo lines, then per scene
    inc d
    inc d
    ld e, 0
    xor a
.x0:
    ld [de], a
    inc e
    bit 3, e
    jr z, .x0
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
    ; lines 56..143: b = SCX up to line c, then the next
    ldh a, [hScene]
    ld b, 0
    ld c, 144
    cp 1
    jr nz, .xc
    ldh a, [hScroll1]
    ld b, a
    jr .x2
.xc:
    cp 2
    jr nz, .x2
    ld c, 64                    ; city: sky to 63, far skyline to 103, near to 143
    call .fill
    ldh a, [hScroll1]
    ld b, a
    ld c, 104
    call .fill
    ldh a, [hScroll2]
    ld b, a
    ld c, 144
.x2:
    call .fill

    ; SCY: logo offset, then per scene
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
    ld c, 56
    call .fill
    ldh a, [hScene]
    and a
    jr nz, .yflat
    ldh a, [hSunS]              ; sun rows 12..16 (lines 96..135): SCY 40 = risen
    cpl
    add 41
    ld b, a
    ld c, 96
    call .fill
    ld b, FLOOR_SCY
    ld c, 144
    jr .fill
.yflat:
    ld b, 0
    ld c, 144
    ; fall through
; [de] = b for lines e .. c-1
.fill:
    ld a, e
    cp c
    ret nc
    ld a, b
    ld [de], a
    inc e
    jr .fill

; synthwave floor (lines 96..143): grid lines wherever the depth crosses a multiple of 32
FloorGrid:
    push de
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
    pop de
    ld e, 96
    ldh a, [hCGB]
    and a
    jr z, .dmg
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
    ret
.dmg:
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
    ret

; ---------------------------------------------------------------- sprites
BuildSprites:
    ld hl, wOAM
    call DrawText               ; first in OAM: it wins the 10-sprites-per-line limit
    ldh a, [hScene]
    cp 2
    jr z, .city
    ; stars (space: every other one drifts twice as fast)
    ld de, Stars
    ld c, NSTARS
.star:
    ldh a, [hDrift]
    ld b, a
    ldh a, [hScene]
    and a
    jr z, .one
    bit 0, c
    jr z, .one
    sla b                       ; fast layer in space
.one:
    ld a, [de]                  ; x
    inc de
    add b
    ld b, a
.sx:
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
    ld a, $80                   ; behind the logo, sun and planet
    ld [hl+], a
    dec c
    jr nz, .star
    ldh a, [hScene]
    cp 1
    jr nz, .rest
    ; meteor: head and two tail pieces (only while flying)
    ldh a, [hShootT]
    cp 48
    jr c, .fly
    ld c, 3
    xor a
.mh:
    ld [hl+], a
    inc hl
    inc hl
    inc hl
    dec c
    jr nz, .mh
    jr .rest
.fly:
    ldh a, [hShootY]
    ld b, a
    ldh a, [hShootX]
    ld c, a
    ld a, b
    add 16
    ld [hl+], a
    ld a, c
    ld [hl+], a
    ld a, 16
    ld [hl+], a
    ld a, 4
    ld [hl+], a
    ld a, b
    add 15
    ld [hl+], a
    ld a, c
    add 8
    ld [hl+], a
    ld a, 17
    ld [hl+], a
    ld a, 4
    ld [hl+], a
    ld a, b
    add 14
    ld [hl+], a
    ld a, c
    add 16
    ld [hl+], a
    ld a, 17
    ld [hl+], a
    ld a, 4
    ld [hl+], a
    jr .rest

.city:
    ; moon: 2 x 2 sprites at (136, 6)
    ld de, MoonOAM
    ld c, 16
.moon:
    ld a, [de]
    inc de
    ld [hl+], a
    dec c
    jr nz, .moon
    ; rain behind the buildings
    ld de, wRain
    ld c, NRAIN
.rain:
    ld a, [de]                  ; x
    inc de
    ld b, a
    ld a, [de]                  ; y
    inc de
    ld [hl+], a
    ld a, b
    ld [hl+], a
    ld a, 15
    ld [hl+], a
    ld a, $83
    ld [hl+], a
    dec c
    jr nz, .rain
.rest:                          ; clear the remaining OAM entries
    ld a, l
    cp $A0
    ret nc
    xor a
    ld [hl+], a
    inc hl
    inc hl
    inc hl
    jr .rest

; PRESS START, blinking, after the intro (10 entries at hl)
DrawText:
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
    ld a, [de]
    inc de
    ld [hl+], a
    ld a, [de]
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

MoonOAM:                        ; y, x, tile, attr (palette 2)
    db 22, 136, 11, 2, 22, 144, 12, 2, 30, 136, 13, 2, 30, 144, 14, 2

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

ArpMinor: db 0, 3, 7, 10
ArpMajor: db 0, 4, 7, 11

MusicTick:
    ldh a, [hTick]
    and a
    call z, PlayRow
    ; arpeggio on channel 2: root, third, fifth (and seventh in arp7 scenes) on successive frames
    ldh a, [hArpBase]
    and a
    jr z, .tick
    ldh a, [hArpStep]
    ld c, a
    ld b, 0
    ld hl, ArpMinor
    ldh a, [hArpMaj]
    and a
    jr z, .mn
    ld hl, ArpMajor
.mn:
    add hl, bc
    ldh a, [hArpBase]
    add [hl]
    call NotePeriod
    ld a, e
    ldh [rNR23], a
    ld a, d
    ldh [rNR24], a
    ldh a, [hArp7]
    add 3                       ; 3 or 4 steps
    ld b, a
    ldh a, [hArpStep]
    inc a
    cp b
    jr c, .st
    xor a
.st:
    ldh [hArpStep], a
.tick:
    ldh a, [hTempo]
    ld b, a
    ldh a, [hTick]
    inc a
    cp b
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
    ldh a, [hSongRows]
    cp c
    jr nz, .store
    ldh a, [hSongRows + 1]
    cp b
    jr nz, .store
    ldh a, [hSongLoop]          ; bc = loop row, hl = song + 4 * loop
    ld c, a
    ldh a, [hSongLoop + 1]
    ld b, a
    ld h, b
    ld l, c
    add hl, hl
    add hl, hl
    ldh a, [hSongBase]
    add l
    ld l, a
    ldh a, [hSongBase + 1]
    adc h
    ld h, a
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
ObjPalettes:                    ; stars, PRESS START, moon, rain, meteor
    dw $0000, $5129, $7F18, $7FFF
    dw $0000, $2806, $68DF, $7FFF
    dw $0000, $3D8F, $5ADA, $77BD
    dw $0000, $5A8A, $6F51, $7FB8
    dw $0000, $1E7F, $3B5F, $7FFF
ObjPalettesEnd:

ObjTiles:
    INCBIN "build/gfx_obj.2bpp"
ObjTilesEnd:
