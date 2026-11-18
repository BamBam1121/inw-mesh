"""One image with everything, written at 0x0: bootloader, partition table, blank
otadata and the app, at the offsets the web installer uses (manifest-install.json).

    python tools/pager_full_bin.py site/firmware release/squatch-mesh-tlorapager-1.2.17-full.bin

For Launcher's catalog (LauncherHub asked for a pager full.bin like the T-Deck's
and the M9's, so other boards' release files don't confuse its scraper).
"""
import sys

PARTS = [("bootloader.bin", 0x0), ("partitions.bin", 0x8000), ("otadata-blank.bin", 0xE000), ("firmware.bin", 0x10000)]


def main():
    src, out = sys.argv[1], sys.argv[2]
    data = {n: open(src + "/" + n, "rb").read() for n, _ in PARTS}
    for (n, o), (_, nxt) in zip(PARTS, PARTS[1:]):
        assert o + len(data[n]) <= nxt, n + " overruns the next part"
    full = bytearray(b"\xff" * (0x10000 + len(data["firmware.bin"])))
    for n, o in PARTS:
        full[o:o + len(data[n])] = data[n]
    assert full[0] == 0xE9 and full[0x10000] == 0xE9, "not an ESP32 image"
    for n, o in PARTS:
        assert bytes(full[o:o + len(data[n])]) == data[n]
    open(out, "wb").write(full)
    print(out, len(full), "bytes")


if __name__ == "__main__":
    main()
