"""Flash the app over USB without touching the BOOT/RESET buttons.

Sends "dfu" to a running pager (firmware 1.1.6+), which restarts into the
chip's USB download mode, then writes the app with esptool. If the pager is
already in download mode (buttons, or a previous try), the dfu step is skipped.
Writes blank otadata too, so the pager boots the app just written. Never
touches the bootloader or erases anything.

usage: python tools/flash_usb.py [firmware.bin] [COM5]
"""

import os
import subprocess
import sys
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
FW = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", ".pio", "build", "t-lora-pager", "firmware.bin")
PORT = sys.argv[2] if len(sys.argv) > 2 else "COM5"
ESPTOOL = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")


def esptool(*args):
    cmd = [sys.executable, ESPTOOL, "--chip", "esp32s3", "--port", PORT, *args]
    return subprocess.run(cmd).returncode == 0


def in_download_mode():
    # chip_id answers only from the ROM bootloader when we don't reset first.
    return subprocess.run([sys.executable, ESPTOOL, "--chip", "esp32s3", "--port", PORT,
                           "--before", "no_reset", "--after", "no_reset", "chip_id"],
                          capture_output=True).returncode == 0


def main():
    if not in_download_mode():
        print("asking the pager to restart into flash mode...")
        try:
            with serial.Serial(PORT, 115200, timeout=1) as s:
                s.write(b"\ndfu\n")
                s.flush()
        except serial.SerialException as e:
            sys.exit("couldn't open %s: %s" % (PORT, e))
        time.sleep(3)                      # USB drops and comes back as the ROM
        for _ in range(20):
            if in_download_mode():
                break
            time.sleep(1)
        else:
            sys.exit("the pager didn't enter flash mode (older firmware? use BOOT + RESET once)")

    blank = os.path.join(HERE, "..", ".pio", "otadata-blank.bin")
    with open(blank, "wb") as f:
        f.write(b"\xff" * 8192)
    ok = esptool("--baud", "460800", "--before", "no_reset", "--after", "hard_reset",
                 "write_flash", "0xe000", blank, "0x10000", FW)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
