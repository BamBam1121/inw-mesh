"""The -full.bin image (everything at 0x0) for bmorcelli's Launcher and for esptool.
Used by package_tdeck.py and package_m9.py; the pager has the same thing in its own
tools/make_launcher_bin.py. The web installer doesn't use it (separate parts).

Why: Launcher gives every firmware whose data partition is called "spiffs" the SAME
partition. Meshtastic (LittleFS) and Squatch Mesh (SPIFFS) both call theirs "spiffs", so
each wiped the other's storage on every switch (Andrew, pager, 2026-11-21). This image
declares a data partition named "squatch" instead; Launcher creates it apart from the
shared one and keeps it on later installs. The firmware mounts "squatch" whenever it
exists (main.cpp setup). Launcher only takes the first app entry; app1 is for esptool
installs, so Wi-Fi updates still work there.

Launcher gives a data partition its declared size only when the image carries data for
it (an empty one gets its small default), so the partition starts with a 16-byte
marker. The firmware sees the marker on its first start, clears the partition and
formats it; the store comes back from the SD card and the NVS safety copy.

Layout (offsets inside the file = flash offsets, as Launcher reads them):
  0x0000    bootloader
  0x8000    partition table: nvs, otadata, app0 (ota_0), squatch (spiffs, 4 MB), app1 (ota_1)
  0x10000   app
  0x3D0000  marker (the start of "squatch"); the file ends 4 kB later
"""
import hashlib
import struct

APP_OFF, APP_SIZE = 0x10000, 0x3C0000
DATA_OFF, DATA_SIZE = 0x3D0000, 0x400000
MARK = b"SQUATCH-NEWSTORE"
assert len(MARK) == 16


def _entry(label, ptype, subtype, off, size):
    return struct.pack("<HBBII16sI", 0x50AA, ptype, subtype, off, size, label.encode().ljust(16, b"\0"), 0)


def table():
    rows = [
        _entry("nvs", 1, 0x02, 0x9000, 0x5000),
        _entry("otadata", 1, 0x00, 0xE000, 0x2000),
        _entry("app0", 0, 0x10, APP_OFF, APP_SIZE),
        _entry("squatch", 1, 0x82, DATA_OFF, DATA_SIZE),
        _entry("app1", 0, 0x11, DATA_OFF + DATA_SIZE, APP_SIZE),
    ]
    body = b"".join(rows)
    return (body + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(body).digest()).ljust(0xC00, b"\xff")


def image(boot, app):
    """bootloader.bin + firmware.bin -> the whole -full.bin."""
    assert len(boot) <= 0x8000, "bootloader overruns the partition table"
    assert len(app) <= APP_SIZE, "app is bigger than its slot"
    img = bytearray(b"\xff" * (DATA_OFF + 0x1000))
    img[0:len(boot)] = boot
    img[0x8000:0x8000 + 0xC00] = table()
    img[APP_OFF:APP_OFF + len(app)] = app
    img[DATA_OFF:DATA_OFF + len(MARK)] = MARK
    return bytes(img)
