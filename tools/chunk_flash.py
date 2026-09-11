#!/usr/bin/env python3
# Chunked no-stub flasher for the T-Pager's flaky native USB.
# Splits a binary into 32KB pieces and flashes each to base+n*0x8000 with retry.
# Small writes never die mid-transfer the way a bulk write does on this PC.
import subprocess, sys, os, tempfile

ESPTOOL = os.path.expanduser(r"~/.platformio/packages/tool-esptoolpy/esptool.py")
PORT = "COM5"
CHUNK = 0x8000  # 32KB

def flash_chunk(addr, path, first):
    # First chunk resets into bootloader; the rest keep it there with no_reset.
    before = "no_reset"
    cmd = [sys.executable, ESPTOOL, "--chip", "esp32s3", "--port", PORT,
           "--baud", "115200", "--before", before, "--after", "no_reset",
           "--no-stub", "write_flash", "--flash_mode", "keep",
           hex(addr), path]
    for attempt in range(4):
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode == 0:
            return True
        print(f"  retry {attempt+1} at {hex(addr)}: {r.stderr.strip().splitlines()[-1] if r.stderr.strip() else 'fail'}")
    return False

def main():
    src = sys.argv[1]
    base = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x0
    data = open(src, "rb").read()
    n = (len(data) + CHUNK - 1) // CHUNK
    print(f"{src}: {len(data)} bytes -> {n} chunks of {CHUNK} at base {hex(base)}")
    tmpdir = tempfile.mkdtemp()
    for i in range(n):
        piece = data[i*CHUNK:(i+1)*CHUNK]
        p = os.path.join(tmpdir, f"chunk_{i:03d}.bin")
        open(p, "wb").write(piece)
        addr = base + i*CHUNK
        print(f"chunk {i+1}/{n} -> {hex(addr)} ({len(piece)} bytes)")
        if not flash_chunk(addr, p, i == 0):
            print(f"FAILED at chunk {i+1} ({hex(addr)}). Stop.")
            sys.exit(1)
    print("All chunks written OK.")

if __name__ == "__main__":
    main()
