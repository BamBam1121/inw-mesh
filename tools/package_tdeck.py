"""Package a T-Deck build for the web installer (squatchmesh.com/tdeck-beta).

    pio run -e t-deck
    python tools/package_tdeck.py OUT_DIR

OUT_DIR gets what the site serves under /tdeck/:
    manifest-install.json, manifest-update.json   (version stamped from platformio.ini)
    firmware/bootloader.bin      our bootloader, padded to the full 32 KB region
    firmware/partitions.bin
    firmware/otadata-blank.bin   0xFF: the bootloader starts app0 after a USB install
    firmware/firmware.bin
    squatch-mesh-tdeck-VERSION-full.bin   all of the above in one image, written at 0x0
                                          (for esptool, or Espressif's web tool)

Same layout and the same checks as the pager's (.github/workflows), so a T-Deck
install leaves nothing behind from the firmware that was there before.
"""
import json
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, ".pio", "build", "t-deck")

OFFSETS = [("bootloader.bin", 0x0), ("partitions.bin", 0x8000), ("otadata-blank.bin", 0xE000), ("firmware.bin", 0x10000)]
APP_SLOT = 0x3C0000   # partitions_inw.csv: app0 and app1


def version():
    ini = open(os.path.join(ROOT, "platformio.ini"), encoding="utf-8").read()
    env = ini[ini.index("[env:t-deck]"):]
    env = env[:env.find("\n[", 1)] if "\n[" in env[1:] else env
    m = re.search(r'FW_VERSION=\\"([^"\\]+)\\"', env)
    if not m:
        sys.exit("no FW_VERSION in [env:t-deck]")
    return m.group(1)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    fw = os.path.join(out, "firmware")
    os.makedirs(fw, exist_ok=True)
    ver = version()

    boot = open(os.path.join(BUILD, "bootloader.bin"), "rb").read()
    # 16 MB (header byte 3 = 0x4_), DIO (byte 2 = 2): an 8 MB bootloader rejects our
    # partition table and resets forever; DIO reads on any flash chip.
    assert boot[0] == 0xE9 and boot[3] >> 4 == 4, "bootloader is not a 16MB image"
    assert boot[2] == 2, "bootloader is not DIO"
    assert len(boot) <= 0x8000, "bootloader overruns the partition table"
    open(os.path.join(fw, "bootloader.bin"), "wb").write(boot + b"\xff" * (0x8000 - len(boot)))
    shutil.copy(os.path.join(BUILD, "partitions.bin"), os.path.join(fw, "partitions.bin"))
    open(os.path.join(fw, "otadata-blank.bin"), "wb").write(b"\xff" * 0x2000)
    app = open(os.path.join(BUILD, "firmware.bin"), "rb").read()
    assert app[0] == 0xE9 and len(app) < APP_SLOT, "firmware.bin is not an app image that fits the slot"
    assert ver.encode() in app, "firmware.bin was not built as " + ver
    open(os.path.join(fw, "firmware.bin"), "wb").write(app)

    full = bytearray(b"\xff" * (0x10000 + len(app)))
    for name, off in OFFSETS:
        data = open(os.path.join(fw, name), "rb").read()
        full[off:off + len(data)] = data
    full_name = "squatch-mesh-tdeck-%s-full.bin" % ver
    open(os.path.join(out, full_name), "wb").write(full)

    # The installer's copies also go in a folder named for the version, and the
    # manifests point there: Cloudflare keeps .bin files for hours, so the same address
    # served the previous build after an update (the installer then flashed the old
    # firmware and said it came back on the old version). firmware/*.bin stays, for
    # the release assets CI takes from it.
    vdir = os.path.join(fw, ver)
    os.makedirs(vdir, exist_ok=True)
    for name, _ in OFFSETS:
        shutil.copy(os.path.join(fw, name), os.path.join(vdir, name))

    for kind, parts in (("install", OFFSETS), ("update", OFFSETS[2:])):
        manifest = {
            "name": "Squatch Mesh for T-Deck (%s)" % ("first install" if kind == "install" else "update"),
            "version": ver,
            "new_install_prompt_erase": False,
            "builds": [{"chipFamily": "ESP32-S3",
                        "parts": [{"path": "firmware/%s/%s" % (ver, n), "offset": o} for n, o in parts]}],
        }
        with open(os.path.join(out, "manifest-%s.json" % kind), "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2)
            f.write("\n")
    print("packaged T-Deck", ver, "->", out, "(" + full_name + ")")


main()
