"""Flash the app over USB without touching the BOOT/RESET buttons.

Sends "dfu" to a running pager (firmware 1.1.6+), which restarts into the
chip's USB download mode, then writes the app with esptool. If the pager is
already in download mode (buttons, or a previous try), the dfu step is skipped.

It also repairs the bootloader and partition table when they are missing. That
is deliberate: a pager whose flash was erased has nothing at 0x0, and a flasher
that writes only the app leaves it unbootable with no way back. Writing the same
known-good bootloader again is harmless; refusing to write it is not.

Nothing here erases, and nothing touches the filesystem at 0x790000, so
contacts, channels and messages are left alone.

usage: python tools/flash_usb.py [firmware.bin] [COM5]
"""

import os
import subprocess
import sys
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
# ESP32-S3 RTC_CNTL_OPTION1_REG: holds the "boot into download mode" flag.
RTC_CNTL_OPTION1_REG = 0x6000812C
BUILD = os.path.join(HERE, "..", ".pio", "build", "t-lora-pager")
FW = sys.argv[1] if len(sys.argv) > 1 else os.path.join(BUILD, "firmware.bin")
PORT = sys.argv[2] if len(sys.argv) > 2 else "COM5"
ESPTOOL = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
# The IDF bootloader that actually runs on this board; ours crash-loops.
BOOTLOADER = os.path.join(HERE, "..", "site", "firmware", "bootloader.bin")
PARTITIONS = os.path.join(BUILD, "partitions.bin")


def esptool(*args):
    cmd = [sys.executable, ESPTOOL, "--chip", "esp32s3", "--port", PORT, *args]
    return subprocess.run(cmd).returncode == 0


def esptool_quiet(*args):
    cmd = [sys.executable, ESPTOOL, "--chip", "esp32s3", "--port", PORT, *args]
    return subprocess.run(cmd, capture_output=True).returncode == 0


def in_download_mode():
    # chip_id answers only from the ROM bootloader when we don't reset first.
    return esptool_quiet("--before", "no_reset", "--after", "no_reset", "chip_id")


def is_app_image(path):
    # Every ESP32 image starts 0xE9. Catches a truncated or wrong file before
    # it reaches the flash, where it would look like a dead pager.
    try:
        with open(path, "rb") as f:
            return f.read(1) == b"\xe9"
    except OSError:
        return False


def blank_at(addr, size):
    """True if that region reads back as erased flash."""
    import tempfile
    out = os.path.join(tempfile.mkdtemp(), "peek.bin")
    if not esptool_quiet("--before", "no_reset", "--after", "no_reset",
                         "read_flash", hex(addr), hex(size), out):
        return False
    with open(out, "rb") as f:
        return all(b == 0xFF for b in f.read())


def main():
    if not os.path.exists(FW):
        sys.exit("no firmware at %s (build it first)" % FW)
    if not is_app_image(FW):
        sys.exit("%s is not an ESP32 app image (should start 0xE9). Refusing to "
                 "flash it; writing this would leave the pager unbootable." % FW)

    if not in_download_mode():
        print("asking the pager to restart into flash mode...")
        try:
            with serial.Serial(PORT, 115200, timeout=1) as s:
                s.write(b"\ndfu\n")
                s.flush()
        except serial.SerialException as e:
            sys.exit("couldn't open %s: %s" % (PORT, e))
        # USB drops and comes back as the ROM. The port can be missing, or
        # briefly refuse to open, during that gap, and a probe landing in it
        # looks like failure, so keep retrying well past the re-enumeration.
        time.sleep(4)
        for _ in range(30):
            if in_download_mode():
                break
            time.sleep(2)
        else:
            sys.exit("the pager didn't enter flash mode (older firmware? use BOOT + RESET once)")

    parts = []

    # Repair the boot chain if it is missing. An erased pager has 0xFF here and
    # cannot start at all; writing the app alone would not help it.
    if blank_at(0x0, 0x20) and os.path.exists(BOOTLOADER):
        print("bootloader is missing - restoring it")
        parts += ["0x0", BOOTLOADER]
    if blank_at(0x8000, 0x20) and os.path.exists(PARTITIONS):
        print("partition table is missing - restoring it")
        parts += ["0x8000", PARTITIONS]

    blank = os.path.join(HERE, "..", ".pio", "otadata-blank.bin")
    os.makedirs(os.path.dirname(blank), exist_ok=True)
    with open(blank, "wb") as f:
        f.write(b"\xff" * 8192)
    parts += ["0xe000", blank, "0x10000", FW]

    ok = esptool("--baud", "460800", "--before", "no_reset", "--after", "no_reset",
                 "write_flash", *parts)

    if ok:
        print("verifying...")
        ok = esptool("--before", "no_reset", "--after", "no_reset",
                     "verify_flash", "0x10000", FW)
        if not ok:
            print("VERIFY FAILED - the app on the pager does not match the file.")

    if not ok:
        # Say what state it is in rather than rebooting into a half-written app
        # and letting it look like dead hardware.
        print("")
        print("The write did not complete. The pager holds an incomplete app and")
        print("will not boot until it is flashed again. Re-run this command, or")
        print("use the web installer. Your contacts and channels are untouched.")
        sys.exit(1)

    # The force-download flag lives in the RTC domain and survives a reset, and
    # the chip never reaches our firmware to clear it, so clear it from here.
    esptool("--before", "no_reset", "--after", "hard_reset",
            "write_mem", hex(RTC_CNTL_OPTION1_REG), "0", "0xFFFFFFFF")
    print("done. if the screen stays dark, power-cycle the pager.")


if __name__ == "__main__":
    main()
