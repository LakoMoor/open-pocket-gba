#!/usr/bin/env python3
"""SPDX-License-Identifier: GPL-3.0-or-later
Collect measured GBA results. Never builds, uploads, or infers game compatibility.
"""
from pathlib import Path
import argparse
import hashlib
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="docs/gba-measurements.txt")
    args = ap.parse_args()
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    names = [line.split("#", 1)[0].strip() for line in (ROOT/"rtl/gba/files.f").read_text().splitlines()]
    names = sorted(set(filter(None, names)) | {
        "rtl/gba/files.f", "fw/gba/diagnostic.hex", "rtl/board/tangnano20k.cst",
        "rtl/board/gba_clocks.sdc", "rtl/board/place.py", "build.sh",
    })
    digest = hashlib.sha256()
    for name in names:
        digest.update(name.encode()+b"\0")
        digest.update((ROOT/name).read_bytes().replace(b"\r\n", b"\n"))
    result = ["Open Pocket GBA measured diagnostic results", f"Code revision: {revision}",
              f"Build source fingerprint (LF normalized): {digest.hexdigest()}",
              "Full GBA compatibility: NOT implemented", ""]
    versions = ROOT/"build/gba/tool-versions.txt"
    if versions.exists():
        result += [versions.read_text().strip(), ""]
    tests_ok = True
    for name in ("cpu", "components", "peripherals", "memory", "video-timing", "uart", "audio-clock", "board"):
        path = ROOT/f"build/gba/{name}-results.txt"
        text = path.read_text().strip() if path.exists() else "NOT RUN"
        tests_ok &= text.startswith("PASS ") and "FAIL" not in text
        result += [f"[{name}]", text]
    result += ["", f"Listed RTL tests: {'PASS' if tests_ok else 'INCOMPLETE/FAILED'}", ""]
    candidates = list((ROOT/"build/gba_top").glob("nextpnr*.log"))
    log_path = max(candidates, key=lambda p:p.stat().st_mtime) if candidates else None
    log = log_path.read_text() if log_path else ""
    fs = ROOT/"build/gba_top/gba_top.fs"
    netlist = ROOT/"build/gba_top/synth.json"
    source_fresh = netlist.exists() and all((ROOT/name).stat().st_mtime <= netlist.stat().st_mtime for name in names)
    clean_log = bool(re.search(r"\b0 errors\b", log)) and "ERROR:" not in log and "Program finished normally" in log
    packed = source_fresh and clean_log and fs.exists() and fs.stat().st_size > 0 and fs.stat().st_mtime >= max(log_path.stat().st_mtime,netlist.stat().st_mtime)
    result += [f"Strict routed/packed artifact: {'PASS' if packed else 'NOT CONFIRMED'}",
               f"Timing log: {log_path.relative_to(ROOT) if log_path else 'NOT FOUND'}",
               f"Netlist newer than build sources: {source_fresh}",
               "Command: SEEDS=1 bash build.sh gba_top; default GBA flow rejects timing failures."]
    result += [line for line in log.splitlines() if re.search(r"LUT4:|DFF:|BSRAM:|Max frequency|warning,|Program finished", line)]
    if packed:
        result += [f"Bitstream: {fs.stat().st_size} bytes", f"SHA256: {hashlib.sha256(fs.read_bytes()).hexdigest()}"]
    result += ["", "Physical status: the earlier logic-ROM diagnostic loaded into SRAM but HDMI capture was black.",
               "The current LCD-timing/UART/audio-clock revision has NOT been loaded. Explicit user approval is required.",
               "No persistent flash writes. No commercial-game, external-suite or mGBA comparison results.",
               "GBC: existing firmware build passed; elaboration/lint with real shared ACR passed."]
    out = ROOT/args.out
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(result)+"\n")
    print(f"Wrote {out}; tests={'PASS' if tests_ok else 'INCOMPLETE'}, routed/packed={'PASS' if packed else 'NOT CONFIRMED'}")


if __name__ == "__main__":
    main()
