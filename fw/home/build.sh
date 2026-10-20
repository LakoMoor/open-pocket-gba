#!/usr/bin/env bash
# Builds the home screen ROM: fw/home/home.gb (embedded in the firmware by fw/build.sh).
set -euo pipefail
cd "$(dirname "$0")"
RGBDS=${RGBDS:-/e/tools/rgbds}
python gen.py
"$RGBDS/rgbasm.exe" -o build/home.o home.asm
"$RGBDS/rgblink.exe" -o home.gb -m build/home.map -n build/home.sym build/home.o
"$RGBDS/rgbfix.exe" -v -c -p 0xFF -t "OPEN POCKET" -m 0 -r 0 home.gb
echo "home.gb $(wc -c < home.gb) bytes"
