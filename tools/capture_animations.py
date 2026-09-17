"""Record each theme's animated lock screen from a pager, for the website.

Needs pyserial and Pillow, and firmware 1.1.16+ (the "lockframe" USB command).
The pager renders every frame itself at an exact point in the animation, so the
result is smooth even though USB is far too slow to capture in real time, and the
saved theme on the pager is never changed.

The scenes have layers moving at unrelated speeds, so no length loops exactly.
The last few frames are cross-faded into the first ones to hide the restart.

Writes web/assets/img/anim-<theme>.webp (animated) and anim-<theme>.png (the first
frame, used when a visitor prefers reduced motion).

usage: python tools/capture_animations.py [COM5]
"""

import os
import sys
import time

import serial
from PIL import Image

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "web", "assets", "img")
THEMES = [(0, "squatch"), (1, "blocks"), (2, "hero"), (3, "aurora")]   # index in THEMES[]

FRAME_MS = 80          # 12.5 fps
FRAMES = 110           # frames captured per theme (~8 s)
FADE = 12              # frames cross-faded into the start
# The lock screen advances phase 0.32 and scroll 2.0 every 33 ms; keep that speed.
TICKS = FRAME_MS / 33.333
PHASE_STEP, SCROLL_STEP = 0.32 * TICKS, 2.0 * TICKS
START_SCROLL = 120.0


def read_frame(s):
    buf = b""
    deadline = time.time() + 10
    while b"SHOT565 " not in buf or not buf.partition(b"SHOT565 ")[2].count(b"\n"):
        if time.time() > deadline:
            raise RuntimeError("no frame header (is the firmware 1.1.16 or newer?)")
        buf += s.read(64)
    hdr, _, data = buf.partition(b"SHOT565 ")[2].partition(b"\n")
    w, h = map(int, hdr.split())
    need = w * h * 2
    while len(data) < need:
        chunk = s.read(need - len(data))
        if not chunk and time.time() > deadline + 30:
            raise RuntimeError("frame cut short")
        data += chunk
    px = bytearray(w * h * 3)
    for i in range(w * h):                  # big-endian RGB565 -> RGB888
        v = (data[2 * i] << 8) | data[2 * i + 1]
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        px[3 * i] = (r << 3) | (r >> 2)
        px[3 * i + 1] = (g << 2) | (g >> 4)
        px[3 * i + 2] = (b << 3) | (b >> 2)
    return Image.frombytes("RGB", (w, h), bytes(px))


def capture(s, idx, name):
    frames = []
    for n in range(FRAMES):
        s.reset_input_buffer()
        s.write(("lockframe %d %.3f %.3f\n" % (idx, n * PHASE_STEP, START_SCROLL + n * SCROLL_STEP)).encode())
        s.flush()
        frames.append(read_frame(s))
        print("\r  %-8s %3d/%d" % (name, n + 1, FRAMES), end="", flush=True)
    print()
    # Seamless-ish loop: play frames[FADE:], and blend the tail towards the frames
    # that were skipped at the start, so the last frame leads into frames[FADE].
    out = frames[FADE:]
    L = len(out)
    for j in range(FADE):
        a = (j + 1) / (FADE + 1)
        out[L - FADE + j] = Image.blend(frames[FRAMES - FADE + j], frames[j], a)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    s = serial.Serial(PORT, 115200, timeout=1)
    time.sleep(1)
    for idx, name in THEMES:
        out = capture(s, idx, name)
        webp = os.path.join(OUT, "anim-%s.webp" % name)
        out[0].save(webp, save_all=True, append_images=out[1:], duration=FRAME_MS, loop=0,
                    quality=88, method=6, minimize_size=True, allow_mixed=True)
        out[0].save(os.path.join(OUT, "anim-%s.png" % name), optimize=True)
        print("  saved %s (%d kB, %d frames)" % (webp, os.path.getsize(webp) // 1024, len(out)))
    s.write(b"home\n")
    s.close()


if __name__ == "__main__":
    main()
