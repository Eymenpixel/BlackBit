#!/usr/bin/env python3
"""Wrap an ARM32 flat kernel in a U-Boot legacy uImage header."""
import argparse
import struct
import zlib
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("input", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--load", type=lambda s: int(s, 0), default=0x10000)
args = parser.parse_args()
payload = args.input.read_bytes()
if not payload:
    raise SystemExit("kernel image is empty")
magic = 0x27051956
name = b"Blackbit ARM32 bootstrap".ljust(32, b"\0")
header = struct.pack(">7I4B32s", magic, 0, 0, len(payload), args.load,
                     args.load, zlib.crc32(payload), 5, 2, 2, 0, name)
header_crc = zlib.crc32(header)
header = struct.pack(">7I4B32s", magic, header_crc, 0, len(payload),
                     args.load, args.load, zlib.crc32(payload), 5, 2, 2, 0, name)
args.output.write_bytes(header + payload)
print(f"U-Boot ARM32 uImage: {args.output} ({len(header) + len(payload)} bytes)")
