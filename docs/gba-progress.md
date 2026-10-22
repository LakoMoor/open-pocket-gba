# Open Pocket GBA progress

Started: 2026-10-21. Updated: 2026-10-22. Branch: `feature/open-pocket-gba`.

Repository: [LakoMoor/open-pocket-gba](https://github.com/LakoMoor/open-pocket-gba).
The original Open Pocket GBC history is retained; GBC remains the `upstream` remote.

This is a tested, limited bring-up configuration. The full requested console is
**not finished**, and no resource-based impossibility claim is made for 20K.

| Phase | Confirmed result / remaining work |
|---|---|
| 1 audit/budget | Reviewed requested GBC modules, firmware, board, HDMI, build and flash flow; downloaded all three references; CPU selected; independent target and memory budget added. |
| 2 CPU/memory | Executable ARM/Thumb RTL, memory bridge, address decode, EWRAM/IWRAM/VRAM backing, original diagnostic vectors and ROM. Six randomized-delay/reboot CPU runs pass. Compatible BIOS, cache, full instruction-suite and waitstates remain. |
| 3 video | Existing HDMI encoder reused, 240x160 Mode 3 scanout, RGB555 and exact 3x scaling. Component and full-board pin-model tests pass. Full PPU and externally loaded homebrew remain. |
| 4 timing/peripherals | Four timers, cascade, IRQ controller, keypad, LCD status and ordinary DMA0..3 implemented/tested. CPU timer/VBlank IRQ in ARM and DMA IRQ/return in Thumb pass. FIFO/capture DMA and accurate bus timing remain. |
| 5 APU/cart | Not implemented. |
| 6 menu/SD/saves | Existing GBC infrastructure preserved, GBA integration not implemented. |
| 7 implementation | Synthesis/P&R results recorded separately below. Hardware demonstration not performed. |

## Simulation

`bash sim/gba/run.sh`:

* Six CPU runs (three latency seeds, two boots each), roughly 4.51 million system
  clocks per run. Checks ARM/Thumb, conditions, SWI, timer/VBlank/DMA IRQ and SPSR return,
  odd-address LDRH, IWRAM and EWRAM values, exactly 38400 halfword stores and all
  VRAM pixels. Corrected an imported CPU LDRH bit-slice bug exposed by this check.
  An earlier version of the diagnostic was also run against the unchanged upstream
  CPU as a negative control and failed its result check. That negative control has
  not been repeated for the newly added VBlank/DMA portions. The DMA3 copy and
  Thumb IRQ return are checked before completion, including exactly four DMA
  destination writes, copied values and the DMA3 IF flag. A simulation-only
  wrapper confirms CPU enable is low and fetch PC remains stable during DMA.
* 18 decode boundary/mirror/ROM overflow cases.
* Two component boots; 691200 output pixels per boot checked against an independent
  RGB555 oracle, continuous competing CPU reads, 33.75/27 MHz clock ratio,
  two-line CDC handoff, no scanout underrun or CPU starvation.
* Four timer prescalers, four-way cascade, counter reload and restart, byte
  enables, IE/IF/IME, W1C interrupt flags and keypad OR/AND/retrigger tests;
  video flags latch with IRQs masked and win over simultaneous W1C.
* Memory bus delayed acceptance/completion, byte masks, mirror translation,
  ROM write rejection, unsupported I/O/ROM-overflow faults and reset cancellation.
  LCD status and IF continue across 410000 clocks of an unaccepted SDRAM read.
* Native video timing: three frames/842688 GBA ticks, every DISPSTAT/VCOUNT
  boundary checked, including line 227, 684 HBlank events, three VBlank and three
  VCount events; byte writes, comparison writes, enable gating and reset.
* DMA unit bench: 24 width/address-mode combinations, random bus stalls, halfword
  lanes, repeat/reload, four-channel priority/preemption, two-tick startup,
  16384/65536 unit counts, byte writes, masks, completion IRQ, unsupported modes,
  abort and reset. The integration bench checks CPU response ownership/blocking,
  a 32-word copy, IE/IF/IME, 160 visible HBlank transfers and suppression during
  VBlank, VBlank-triggered DMA, IO writes and observable access/configuration faults.
* Read-only diagnostic UART: independent 8N1 waveform decode, hex encoding,
  atomic snapshots while inputs change, periodic repeat and reset-to-idle.
* Real HDMI Audio Clock Regeneration (no stub): exact average 48 kHz clock,
  281/282 pixel-clock half-periods, N=6144 and CTS=27000 checked over four wraps.
* Diagnostic ROM writes DISPCNT=0x0403 (Mode 3 and BG2 enable), and the scanout
  enable respects the mode/BG2/blank controls. The board-level pixel oracle runs
  through this CPU-written control path.

`bash sim/gba/board.sh`: full `gba_top` with existing behavioral board primitives
and SDRAM pin model. Loader -> SDRAM -> CPU -> VRAM -> HDMI RGB verified for a
691200 pixels across two board boots, including a button reset and repeat ROM
load, approximately 266.96 ms simulated. The real ACR module is included in the
current test. Serializer electrical behavior is not covered by that model.

CPU-generated VRAM captured from the simulated HDMI RGB input (not a physical
capture and not a replacement pattern generator):

![Original diagnostic RGB555 ramp, simulated 720x480 HDMI](img/gba-diagnostic-simulation.png)

Raw results: `build/gba/*-results.txt`; compile/lint output stays in the matching
logs. Nothing in the test runner suppresses tool failure with `|| true`.

GBC regression checks: the original `gbc_top` and generated CPU/APU netlists
pass Verilator elaboration/lint with the existing board simulation models;
`fw/build.sh` completes (159668-byte firmware). These checks do not establish
physical GBC compatibility or replace a full GBC gameplay regression run.
After the equivalent ACR constant refactor, GBC elaboration/lint was repeated
successfully with the real ACR module instead of the legacy simulation stub.

## Synthesis / place-and-route / physical testing

Final build measurements are recorded in `gba-measurements.txt`.
`./build.sh gba_top` produces `gba_top.fs`, `yosys.log`, `nextpnr.log` and
`stat.txt` under `build/gba_top/`. GBA uses `gba_clocks.sdc` and does not pass
`--timing-allow-fail`. GBC retains the original build defaults.

The earlier logic-ROM revision (`4c6df94`, before telemetry/audio fixes) passed
strict place-and-route (seed 1): 9532 LUT4,
2229 DFF and 1 BSRAM. Final reported Fmax: system 85.22 MHz against 33.75 MHz;
pixel 188.22 MHz against 27 MHz. No hold errors and no `--timing-allow-fail`.
`gowin_pack` produced `build/gba_top/gba_top.fs` (7263596 bytes).
That archived measurement is retained in `gba-measurements-logic-rom.txt`.
Current revision results are generated by `python tools/gba_report.py` into
`gba-measurements.txt`; pending routing is explicitly marked unconfirmed.

Earlier code revision `abc780e` (UART, working HDMI audio clock, DISPCNT=0x0403)
also passes strict routing/packing with seed 1: **10554 LUT4, 2908 DFF, 1 BSRAM**.
Post-route Fmax is **72.44 MHz system** (33.75 MHz required) and **110.25 MHz pixel**
(27 MHz required), with zero timing errors. The one router warning is a GPIO
input dedicated-route fallback; routing completed using ordinary fabric.
For that revision, all required artifacts were generated under `build/gba_top/`. Its 7263596-byte
bitstream SHA256 is `d686cb32a53771895af411f3d78bfe46ba446270beaa34409b70c609a9ef0673`.

Physical test on 2026-10-22: the GW2AR-18 board was detected over JTAG and the
bitstream loaded into volatile SRAM successfully. USB3.0 UHD HDMI capture is
currently black; the CPU/SDRAM/video path is **not hardware-validated**. Diagnosis
continues. No persistent flash changes were made. Successful fit does not establish
full GBA compatibility.

Read-only UART telemetry, first-fault address capture and visible HDMI bring-up
states have been added to investigate the black capture. Their RTL tests and
two-boot board simulation pass. This newer code has **not** been loaded onto the
physical board. Per the user's instruction, any further board programming must
wait for an explicit approval after code checks are complete.
Code review also found that the first GBA top tied the HDMI audio clock low.
That defect is corrected with a clocked-silence transport and verified against
the real ACR module. Physical behavior after that correction is not yet tested.

The first BSRAM-ROM implementation failed 12 hold checks on the loader counter
to diagnostic-ROM address paths, despite passing setup timing. The sparse 1 KiB
ROM was changed to logic ROM (verified by synthesis: only the scanout uses BSRAM).
Both simulated board boots still pass. Strict routing of that change is recorded
in the final measurements; no timing checks were waived.

## Native LCD timing continuation

Continuation on 2026-10-22 adds native LCD status timing: DISPSTAT/VCOUNT,
VBlank/HBlank/VCount IRQ sources and blank-start strobes. The independent timing
bench examines every tick in three frames (842688 GBA ticks), with irregular
system-clock gaps and byte/register/reset checks. The memory integration bench
holds an SDRAM request unaccepted for 410000 system clocks while video IF events
continue to accumulate. The original ARM program now requires both timer and
VBlank exception/return paths before entering Thumb and filling VRAM. These are
status/timing additions, not a complete PPU; HDMI scanout remains independent.
Tests force C++ object rebuilds to avoid stale test drivers when Windows file
timestamps coincide. New measurements supersede the `abc780e` figures above;
consult `gba-measurements.txt` for the current artifact and source revision.
This version has not been programmed onto the board.

Code revision `8fcda42` passes the full simulation set and strict seed-1 routing
and packing: **10702 LUT4, 2944 DFF, 1 BSRAM**, with post-route Fmax
**69.97 MHz system** (33.75 MHz required) and **109.04 MHz pixel** (27 MHz
required). The router reports one GPIO dedicated-route fallback warning and zero
errors. The fresh 7263596-byte bitstream and its SHA256/source fingerprint are
recorded in `gba-measurements.txt`. No timing failures were waived.

## Ordinary DMA continuation

DMA0..3 now perform ordinary immediate/VBlank/visible-HBlank transfers with
16/32-bit units, address modes, repeat/reload, priority, CPU hold and IF bits 8..11.
The updated ROM completes a four-word DMA3 transfer and returns from its IRQ in
Thumb. Across the randomized-latency CPU runs, PC/enable are held for 106–122
system clocks during the transfer. The unit bench includes changed CNT_L/DAD
between repeated blocks, preservation of the running source despite SAD writes,
and enable-time latches for the first block. The memory integration test covers
exactly 160 HBlank transfers, VBlank suppression, response ownership and faults.
It also forces VBlank to arrive during a 500000-clock CPU read: that in-flight
response reaches the CPU, then queued DMA runs before the next CPU request.

FIFO, video capture, cartridge DRQ, native bus timing and open-bus behavior remain
unsupported. Access/configuration faults are explicit rather than silently
performing an incorrect transfer. Current fit/timing measurements are recorded
in `gba-measurements.txt`. This DMA version has not been loaded onto the board.

## Remaining concrete architecture gates

1. Fast IWRAM plus bounded ROM instruction/data cache; current uncached execution
   is too slow to claim native CPU performance.
2. Integrate the reviewed open BIOS, implement required I/O and verify ARM/Thumb
   instruction/exception suites against hardware/mGBA references.
3. Implement a bandwidth-budgeted PPU, beginning with GBA-timed modes 3/4/5, then
   tile/affine/OBJ/merge, windows, mosaic and blend behavior; retain measured fit.
4. Add DMA FIFO/video-capture/DRQ, native bus timing and WAITCNT/prefetch before
   claiming accurate audio/graphics DMA.
5. APU and sound FIFOs; real HDMI audio with CDC tests.
6. Separate GBA firmware/storage configuration, SD header validation/loading,
   save protocols and persistence, cached external cartridge storage.
7. Run integration suites, strict routed timing and physical capture/button/audio
   validation. Re-synthesize each addition instead of extrapolating this small demo.
