"""Capture documentation screenshots from a pager over USB.

Needs pyserial and Pillow. Uses the pager's USB commands (see usbCommands() in
src/main.cpp): switches through the themes and saves the lock screen and the
home screen of each to docs/img/. The home screen's footer carries the node's
own name, so it's cropped off.

usage: python tools/capture_screens.py [COM5]
"""

import os
import sys
import time

import serial
from PIL import Image

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
OUT = os.path.join(os.path.dirname(__file__), "..", "docs", "img")
THEMES = ["inw", "blocks", "hero", "aurora"]


def cmd(s, line, wait=0.4):
    s.write((line + "\n").encode())
    s.flush()
    time.sleep(wait)


def shot(s, path, crop_bottom=0):
    s.reset_input_buffer()
    s.write(b"shot\n")
    buf = b""
    deadline = time.time() + 10
    while b"SHOT565 " not in buf or not buf.partition(b"SHOT565 ")[2].count(b"\n"):
        if time.time() > deadline:
            raise RuntimeError("no screenshot header")
        buf += s.read(64)
    rest = buf.partition(b"SHOT565 ")[2]
    hdr, _, data = rest.partition(b"\n")
    w, h = map(int, hdr.split())
    need = w * h * 2
    while len(data) < need and time.time() < deadline + 30:
        data += s.read(need - len(data))
    px = bytearray(w * h * 3)
    for i in range(w * h):                  # big-endian RGB565 -> RGB888
        v = (data[2 * i] << 8) | data[2 * i + 1]
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        px[3 * i] = (r << 3) | (r >> 2)
        px[3 * i + 1] = (g << 2) | (g >> 4)
        px[3 * i + 2] = (b << 3) | (b >> 2)
    img = Image.frombytes("RGB", (w, h), bytes(px))
    if crop_bottom:
        img = img.crop((0, 0, w, h - crop_bottom))
    img.save(path)
    print("saved", path)


def main():
    os.makedirs(OUT, exist_ok=True)
    s = serial.Serial(PORT, 115200, timeout=1)
    time.sleep(1)
    for i, name in enumerate(THEMES):
        cmd(s, "theme %d" % i, 0.8)
        cmd(s, "home", 0.6)
        shot(s, os.path.join(OUT, "home-%s.png" % name), crop_bottom=32)
        cmd(s, "lock", 1.5)                 # let the scene animate a moment
        shot(s, os.path.join(OUT, "lock-%s.png" % name))
        cmd(s, "key x", 0.6)                # any key leaves the lock screen
    s.close()


if __name__ == "__main__":
    main()
