#!/usr/bin/env python3
"""Write a binary to the T-Pager, with fallbacks for its flaky native USB.

Tries one stub write first. The stub is fast and, more importantly, it is the
only mode that reliably writes past 0x200000 on this chip: the ROM loader
(--no-stub) fails at that address every time. That used to stop a flash two
thirds of the way through and leave a truncated app on an otherwise healthy
pager, which looks exactly like a brick. No-stub is now a last resort.

Whatever path it takes, the result is verified against the source file before
this reports success. If a write fails part way it says so plainly, because a
partly written app will not boot and has to be flashed again.

usage: python tools/chunk_flash.py <file.bin> [base addr] [port]
"""

import os
import subprocess
import sys
import tempfile

ESPTOOL = os.path.expanduser(r"~/.platformio/packages/tool-esptoolpy/esptool.py")
CHUNK = 0x8000  # 32KB
BAUD = "921600"


def esptool(*args):
    cmd = [sys.executable, ESPTOOL, "--chip", "esp32s3", "--port", PORT, *args]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode == 0, ((r.stdout or "") + (r.stderr or "")).strip()


def last_line(out):
    lines = [l for l in out.splitlines() if l.strip()]
    return lines[-1] if lines else "no output"


def enter_download():
    # Reset into the ROM loader once, then every later call keeps it there.
    ok, out = esptool("--before", "default_reset", "--after", "no_reset", "flash_id")
    if not ok:
        print("could not reach the pager: " + last_line(out))
        print("plug it in, or hold BOOT and tap RESET, then try again.")
    return ok


def write(addr, path, stub=True):
    args = ["--baud", BAUD, "--before", "no_reset", "--after", "no_reset"]
    if not stub:
        args.append("--no-stub")
    args += ["write_flash", "--flash_mode", "keep", hex(addr), path]
    return esptool(*args)


def write_chunked(base, data, stub):
    tmpdir = tempfile.mkdtemp()
    n = (len(data) + CHUNK - 1) // CHUNK
    for i in range(n):
        piece = data[i * CHUNK:(i + 1) * CHUNK]
        p = os.path.join(tmpdir, "chunk_%03d.bin" % i)
        with open(p, "wb") as f:
            f.write(piece)
        addr = base + i * CHUNK
        print("  chunk %d/%d -> %s (%d bytes)" % (i + 1, n, hex(addr), len(piece)))
        for attempt in range(3):
            ok, out = write(addr, p, stub=stub)
            if ok:
                break
            print("    retry %d: %s" % (attempt + 1, last_line(out)))
            # A failed write often drops the chip out of the loader, and every
            # later no_reset call then fails the same way. Get it back first.
            enter_download()
        else:
            return False, addr
    return True, None


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    base = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x0
    global PORT
    PORT = sys.argv[3] if len(sys.argv) > 3 else os.environ.get("PAGER_PORT", "COM5")

    with open(src, "rb") as f:
        data = f.read()
    print("%s: %d bytes -> %s on %s" % (src, len(data), hex(base), PORT))

    if not enter_download():
        sys.exit(1)

    print("writing (stub)...")
    ok, out = write(base, src, stub=True)

    if not ok:
        print("  bulk write failed: " + last_line(out))
        print("falling back to 32KB chunks (stub)...")
        enter_download()
        ok, failed_at = write_chunked(base, data, stub=True)
        if not ok:
            print("  chunked stub write failed at " + hex(failed_at))
            print("last resort: chunks without the stub (slow, and known to fail past 0x200000)...")
            enter_download()
            ok, failed_at = write_chunked(base, data, stub=False)
            if not ok:
                print("")
                print("FAILED at " + hex(failed_at) + ".")
                print("The pager now holds a PARTLY WRITTEN image and will not boot.")
                print("This is recoverable: run this command again, or use the web")
                print("installer, which writes the bootloader and partition table too.")
                sys.exit(1)

    print("verifying against " + os.path.basename(src) + "...")
    vok, vout = esptool("--before", "no_reset", "--after", "no_reset",
                        "verify_flash", hex(base), src)
    if not vok:
        print("VERIFY FAILED: " + last_line(vout))
        print("The image on the pager does not match the file. Flash it again")
        print("before rebooting; it will not boot as it stands.")
        esptool("--before", "no_reset", "--after", "no_reset", "flash_id")
        sys.exit(1)

    print("verified. rebooting.")
    esptool("--before", "no_reset", "--after", "hard_reset", "flash_id")
    print("done.")


if __name__ == "__main__":
    main()
