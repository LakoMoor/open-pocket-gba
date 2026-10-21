#!/usr/bin/env bash
# Builds the Tang Nano 20K simulator (build/tang_sim/tang_sim.exe) with Verilator: the real
# gbc_top RTL, the GHDL netlists of the CPU and APU, models of the Gowin parts and the SDRAM.
set -euo pipefail
cd "$(dirname "$0")/../.."
export VERILATOR_ROOT=${VERILATOR_ROOT:-/e/tools/oss-cad-suite/share/verilator}
export PATH=/e/tools/oss-cad-suite/bin:/e/tools/oss-cad-suite/lib:/c/Users/lakom/mingw64/bin:$PATH
OUT=build/tang_sim
SRCS=$(sed 's/#.*//' rtl/files.f | tr ' ' '\n' | grep -v -E 'gowin_prims|pll_gb|pll_hdmi|ghdl_stubs|audio_clock_regeneration|^$')
mkdir -p "$OUT"
verilator_bin --cc --exe --timing -O3 --x-assign fast --x-initial fast \
    -Wno-fatal -Wno-lint -Wno-style -Wno-MULTIDRIVEN -Wno-UNOPTFLAT -Wno-TIMESCALEMOD -Wno-INITIALDLY \
    +1364-2005ext+v --top-module sim_top -Irtl/gb -Irtl/sys -Irtl/gb_port \
    -Mdir "$OUT" -o tang_sim \
    sim/tang/sim_prims.v sim/tang/sdram_model.v sim/tang/hdmi_audio_stub.sv rtl/gen/GBse.v rtl/gen/gbc_snd.v $SRCS sim/tang/sim_top.sv sim/tang/main.cpp \
    -CFLAGS "-O2 -std=c++17" -LDFLAGS "-static" 2>&1 | grep -E "%Error|error:" | head -30 || true
mingw32-make -j8 -C "$OUT" -f Vsim_top.mk > "$OUT/make.log" 2>&1 || { grep -E "error" "$OUT/make.log" | head -20; exit 1; }
ls -la "$OUT/tang_sim.exe"
