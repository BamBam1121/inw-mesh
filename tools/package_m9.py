"""Package a ThinkNode M9 build as one image for esptool or Espressif's web flasher.

    pio run -e thinknode-m9
    python tools/package_m9.py OUT_DIR

OUT_DIR gets squatch-mesh-thinknode-m9-VERSION-full.bin, written at 0x0: our
bootloader, the partition table, a blank otadata (so app0 starts) and the app - the
same layout and checks as the T-Deck's (tools/package_tdeck.py).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, ".pio", "build", "thinknode-m9")
APP_SLOT = 0x3C0000   # partitions_inw.csv: app0 and app1


def version():
    ini = open(os.path.join(ROOT, "platformio.ini"), encoding="utf-8").read()
    env = ini[ini.index("[env:thinknode-m9]"):]
    m = re.search(r'FW_VERSION=\\"([^"\\]+)\\"', env)
    if not m:
        sys.exit("no FW_VERSION in [env:thinknode-m9]")
    return m.group(1)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    ver = version()
    boot = open(os.path.join(BUILD, "bootloader.bin"), "rb").read()
    # 16 MB (header byte 3 = 0x4_), DIO (byte 2 = 2), and clear of the partition table.
    assert boot[0] == 0xE9 and boot[3] >> 4 == 4, "bootloader is not a 16MB image"
    assert boot[2] == 2, "bootloader is not DIO"
    assert len(boot) <= 0x8000, "bootloader overruns the partition table"
    parts = open(os.path.join(BUILD, "partitions.bin"), "rb").read()
    app = open(os.path.join(BUILD, "firmware.bin"), "rb").read()
    assert app[0] == 0xE9 and len(app) < APP_SLOT, "firmware.bin is not an app image that fits the slot"
    assert ver.encode() in app, "firmware.bin was not built as " + ver
    full = bytearray(b"\xff" * (0x10000 + len(app)))
    for data, off in ((boot, 0x0), (parts, 0x8000), (b"\xff" * 0x2000, 0xE000), (app, 0x10000)):
        full[off:off + len(data)] = data
    name = os.path.join(out, "squatch-mesh-thinknode-m9-%s-full.bin" % ver)
    open(name, "wb").write(full)
    print(name, len(full))


if __name__ == "__main__":
    main()
