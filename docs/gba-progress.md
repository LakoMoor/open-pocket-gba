# Open Pocket GBA progress

Date: 2026-10-21. Branch: `feature/open-pocket-gba`.

Repository: [LakoMoor/open-pocket-gba](https://github.com/LakoMoor/open-pocket-gba).
The original Open Pocket GBC history is retained; GBC remains the `upstream` remote.

This is a tested, limited bring-up configuration. The full requested console is
**not finished**, and no resource-based impossibility claim is made for 20K.

| Phase | Confirmed result / remaining work |
|---|---|
| 1 audit/budget | Reviewed requested GBC modules, firmware, board, HDMI, build and flash flow; downloaded all three references; CPU selected; independent target and memory budget added. |
| 2 CPU/memory | Executable ARM/Thumb RTL, memory bridge, address decode, EWRAM/IWRAM/VRAM backing, original diagnostic vectors and ROM. Six randomized-delay/reboot CPU runs pass. Compatible BIOS, cache, full instruction-suite and waitstates remain. |
| 3 video | Existing HDMI encoder reused, 240x160 Mode 3 scanout, RGB555 and exact 3x scaling. Component and full-board pin-model tests pass. Full PPU and externally loaded homebrew remain. |
| 4 timing/peripherals | Four timers, cascade, IRQ controller, keypad and actual CPU timer IRQ/return implemented/tested. DMA, video IRQ sources and accurate bus timing remain. |
| 5 APU/cart | Not implemented. |
| 6 menu/SD/saves | Existing GBC infrastructure preserved, GBA integration not implemented. |
| 7 implementation | Synthesis/P&R results recorded separately below. Hardware demonstration not performed. |

## Simulation

`bash sim/gba/run.sh`:

* Six CPU runs (three latency seeds, two boots each), roughly 4.11 million system
  clocks per run. Checks ARM/Thumb, conditions, SWI, timer IRQ and SPSR return,
  odd-address LDRH, IWRAM and EWRAM values, exactly 38400 halfword stores and all
  VRAM pixels. Corrected an imported CPU LDRH bit-slice bug exposed by this check.
  As a negative control, the same bench was built/run against the unchanged
  upstream CPU; it fails the result check. The imported CPU with the one-line
  LDRH fix passes all six runs.
* 18 decode boundary/mirror/ROM overflow cases.
* Two component boots; 691200 output pixels per boot checked against an independent
  RGB555 oracle, continuous competing CPU reads, 33.75/27 MHz clock ratio,
  two-line CDC handoff, no scanout underrun or CPU starvation.
* Four timer prescalers, four-way cascade, counter reload and restart, byte
  enables, IE/IF/IME, W1C interrupt flags and keypad OR/AND/retrigger tests.
* Memory bus delayed acceptance/completion, byte masks, mirror translation,
  ROM write rejection, unsupported I/O/ROM-overflow faults and reset cancellation.
* Read-only diagnostic UART: independent 8N1 waveform decode, hex encoding,
  atomic snapshots while inputs change, periodic repeat and reset-to-idle.
* Real HDMI Audio Clock Regeneration (no stub): exact average 48 kHz clock,
  281/282 pixel-clock half-periods, N=6144 and CTS=27000 checked over four wraps.

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

The logic-ROM configuration passes strict place-and-route (seed 1): 9532 LUT4,
2229 DFF and 1 BSRAM. Final reported Fmax: system 85.22 MHz against 33.75 MHz;
pixel 188.22 MHz against 27 MHz. No hold errors and no `--timing-allow-fail`.
`gowin_pack` produced `build/gba_top/gba_top.fs` (7263596 bytes).

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

## Remaining concrete architecture gates

1. Fast IWRAM plus bounded ROM instruction/data cache; current uncached execution
   is too slow to claim native CPU performance.
2. Integrate the reviewed open BIOS, implement required I/O and verify ARM/Thumb
   instruction/exception suites against hardware/mGBA references.
3. Implement a bandwidth-budgeted PPU, beginning with GBA-timed modes 3/4/5, then
   tile/affine/OBJ/merge, windows, mosaic and blend behavior; retain measured fit.
4. Add DMA and WAITCNT/prefetch with timing tests before claiming audio/graphics DMA.
5. APU and sound FIFOs; real HDMI audio with CDC tests.
6. Separate GBA firmware/storage configuration, SD header validation/loading,
   save protocols and persistence, cached external cartridge storage.
7. Run integration suites, strict routed timing and physical capture/button/audio
   validation. Re-synthesize each addition instead of extrapolating this small demo.
