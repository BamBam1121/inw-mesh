#!/usr/bin/env python3
"""Package a full T-LoRa Pager image for Custom Firmware at address 0x0.

The image includes blank NVS and OTA selection data. Back up before installing;
it is a full-install image, not a settings-preserving application update.
"""
import argparse
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build_dir", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
boot = (args.build_dir / "bootloader.bin").read_bytes()
table = (args.build_dir / "partitions.bin").read_bytes()
app = (args.build_dir / "firmware.bin").read_bytes()
if not args.output.name.endswith("-merged.bin"):
    parser.error("output must end in -merged.bin for the MeshCore web flasher")
if not (boot[0] == app[0] == 0xE9 and boot[3] >> 4 == 4):
    parser.error("expected ESP images and a 16 MB bootloader")
entries = {}
for at in range(0, len(table) - 31, 32):
    magic, kind, subtype, offset, size, label, flags = struct.unpack_from("<HBBII16sI", table, at)
    if magic != 0x50AA:
        break
    entries[label.rstrip(b"\0").decode()] = (offset, size)
expected = {"nvs": (0x9000, 0x5000), "otadata": (0xE000, 0x2000),
            "app0": (0x10000, 0x3C0000)}
if any(entries.get(key) != value for key, value in expected.items()):
    parser.error("partition layout differs from the tested Pager layout")
if len(boot) > 0x8000 or len(table) > 0x1000 or len(app) > expected["app0"][1]:
    parser.error("one of the images exceeds its allocated range")
# Blank OTA selection data makes the bootloader choose app0. A flat merged
# image also covers NVS; it cannot preserve unknown settings on the device.
image = bytearray(b"\xff" * (0x10000 + len(app)))
for offset, data in ((0, boot), (0x8000, table), (0x10000, app)):
    image[offset:offset + len(data)] = data
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(image)
print(f"Full-install image: {args.output} ({len(image)} bytes), write at 0x0")
print("Includes blank NVS/OTA data: back up settings and identity before flashing.")
