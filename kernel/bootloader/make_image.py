#!/usr/bin/env python3
"""Pack the BIOS stage1, fixed-size stage2, and flat kernel image."""
from __future__ import annotations
import sys
from pathlib import Path

SECTOR = 512
STAGE2_SECTORS = 4
MAX_KERNEL_SECTORS = 127
IMAGE_BYTES = 16 * 1024 * 1024

if len(sys.argv) != 5:
    raise SystemExit("usage: make_image.py STAGE1 STAGE2 KERNEL OUTPUT")
stage1, stage2, kernel = (Path(arg).read_bytes() for arg in sys.argv[1:4])
output = Path(sys.argv[4])
if len(stage1) != SECTOR or stage1[510:512] != b"\x55\xaa":
    raise SystemExit("stage1 must be one valid 512-byte BIOS boot sector")
if len(stage2) > STAGE2_SECTORS * SECTOR:
    raise SystemExit("stage2 exceeds its 4-sector reserved area")
if not kernel or (len(kernel) + SECTOR - 1) // SECTOR > MAX_KERNEL_SECTORS:
    raise SystemExit("kernel must fit in one BIOS EDD transfer (max 127 sectors)")
image = stage1 + stage2.ljust(STAGE2_SECTORS * SECTOR, b"\0")
image += kernel.ljust(((len(kernel) + SECTOR - 1) // SECTOR) * SECTOR, b"\0")
image = image.ljust(IMAGE_BYTES, b"\0")
output.write_bytes(image)
print(f"BIOS disk image: {len(image)} bytes; kernel {len(kernel)} bytes")
