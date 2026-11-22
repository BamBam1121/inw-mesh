"""Build squatch-mesh-tlorapager-<version>-launcher.bin: the image to install from
bmorcelli's Launcher (SD card or web UI) next to other firmwares.

Why it exists: Launcher gives every firmware whose data partition is called "spiffs"
the SAME partition. Meshtastic (LittleFS) and Squatch Mesh (SPIFFS) both call theirs
"spiffs", so each wiped the other's storage on every switch. This image declares a data
partition named "squatch" instead; Launcher creates it separately and keeps it (it
reuses a partition with the same label on later installs). The firmware mounts
"squatch" whenever it exists (main.cpp setup).

Launcher only gives a data partition its declared size when the image carries data for
it (an empty one gets Launcher's small default, 128-448 kB), so the partition starts
with a 16-byte marker. The firmware sees the marker on its first start, clears the
whole partition and formats it. The marker overwrites the start of an existing
partition on a reinstall, so updates should use the plain firmware.bin (app only);
reinstalling this image starts the store fresh and it comes back from the SD card.

Layout (offsets inside this file = flash offsets, as Launcher reads them):
  0x0000    bootloader
  0x8000    partition table: nvs, otadata, app0 (ota_0), squatch (spiffs, 4 MB)
  0x10000   app
  0x3D0000  marker (the start of the "squatch" partition), file ends after 4 kB

usage: python tools/make_launcher_bin.py <build dir> <out.bin>
"""
import hashlib
import struct
import sys

APP_OFF, APP_SIZE = 0x10000, 0x3C0000
DATA_OFF, DATA_SIZE = 0x3D0000, 0x400000
MARK = b"SQUATCH-NEWSTORE"
assert len(MARK) == 16


def entry(label, ptype, subtype, off, size):
    lab = label.encode().ljust(16, b"\0")
    return struct.pack("<HBBII16sI", 0x50AA, ptype, subtype, off, size, lab, 0)


def table():
    rows = [
        entry("nvs", 1, 0x02, 0x9000, 0x5000),
        entry("otadata", 1, 0x00, 0xE000, 0x2000),
        entry("app0", 0, 0x10, APP_OFF, APP_SIZE),
        entry("squatch", 1, 0x82, DATA_OFF, DATA_SIZE),
    ]
    body = b"".join(rows)
    md5 = b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(body).digest()
    return (body + md5).ljust(0xC00, b"\xff")


def main():
    build, out = sys.argv[1], sys.argv[2]
    boot = open(build + "/bootloader.bin", "rb").read()
    app = open(build + "/firmware.bin", "rb").read()
    if len(app) > APP_SIZE:
        sys.exit(f"app is {len(app):#x} bytes, more than the {APP_SIZE:#x} slot")
    img = bytearray(b"\xff" * (DATA_OFF + 0x1000))
    img[0:len(boot)] = boot
    img[0x8000:0x8000 + 0xC00] = table()
    img[APP_OFF:APP_OFF + len(app)] = app
    img[DATA_OFF:DATA_OFF + len(MARK)] = MARK
    open(out, "wb").write(img)
    print(f"{out}: {len(img)} bytes, app {len(app):#x}, squatch {DATA_SIZE // 1024} kB at {DATA_OFF:#x}")


if __name__ == "__main__":
    main()
