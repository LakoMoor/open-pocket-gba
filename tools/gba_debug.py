#!/usr/bin/env python3
"""SPDX-License-Identifier: GPL-3.0-or-later
Decode read-only GBA bring-up UART snapshots (115200 8N1).
Use --line HEX for offline decoding; --port COM6 reads without sending commands.
"""
import argparse
import json
import re


def decode(line):
    if not re.fullmatch(r"[0-9a-fA-F]{32}", line):
        raise ValueError("expected exactly 32 hexadecimal digits")
    status, pc, instruction, fault = (int(line[n:n+8], 16) for n in range(0, 32, 8))
    if status >> 16 != 0x4742:
        raise ValueError("invalid GBA telemetry signature")
    return {
        "status": f"{status:08x}",
        **{name: bool(status & (1 << bit)) for bit, name in enumerate(
            ("reset", "thumb", "fault", "done", "running", "sdram_ready", "system_pll", "hdmi_pll", "underrun"))},
        "pc": f"{pc:08x}", "fetched_word": f"{instruction:08x}", "first_fault_address": f"{fault:08x}",
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--line")
    group.add_argument("--port")
    args = ap.parse_args()
    if args.line:
        print(json.dumps(decode(args.line.strip()), indent=2))
        return
    import serial
    with serial.Serial(args.port, 115200, timeout=1) as port:
        pending = bytearray()
        while True:
            pending.extend(port.read(256))
            while b"\n" in pending:
                raw, _, pending = pending.partition(b"\n")
                try:
                    print(json.dumps(decode(raw.decode("ascii").strip())), flush=True)
                except (ValueError, UnicodeDecodeError):
                    pass  # Ignore a partial first packet after attaching.
            if len(pending) > 256:
                pending.clear()


if __name__ == "__main__":
    main()
