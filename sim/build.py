"""Build the Squatch Mesh screen simulator for Windows (MinGW from PlatformIO).

The firmware's own screen code (src/) is compiled as-is. Only what talks to
hardware or the radio is replaced: sim/overlay (Arduino, file systems), sim/board
(the board headers, T-Deck by default), sim/replace (the mesh node, with sample
data). src/ is copied first so a replaced header wins over the original next to it.

usage: python sim/build.py [--pager]      -> .pio/sim/squatch_sim.exe
"""
import glob, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, ".pio", "sim")
SRC = os.path.join(OUT, "src")
OBJ = os.path.join(OUT, "obj" + ("-pager" if "--pager" in sys.argv else ""))
GXX = os.path.expanduser(r"~\.platformio\packages\toolchain-gccmingw32\bin\g++.exe")
LGFX = os.path.join(ROOT, ".pio", "libdeps", "t-lora-pager", "LovyanGFX", "src")

# The firmware files the simulator runs. Everything else is stubbed in sim/.
FIRMWARE = ["ui.cpp", "home.cpp", "quips.cpp", "regional.cpp", "settings.cpp", "history.cpp",
            "chats.cpp", "notify.cpp", "fx.cpp", "fx_squatch.cpp", "fx_blocks.cpp", "fx_hero.cpp",
            "fx_aurora.cpp"]
SIM = ["sim_stubs.cpp", "sim_main.cpp"]

FLAGS = ["-std=gnu++14", "-O1", "-w", "-DARDUINO=10819", "-DLGFX_USE_V1", "-DINW_SIM=1",
         '-DFW_VERSION="1.2.1"', "-DMAX_CONTACTS=2000", "-DMAX_GROUP_CHANNELS=40"]
if "--pager" in sys.argv:
    FLAGS.append("-DSIM_PAGER=1")
INC = ["-I" + p for p in (SRC, os.path.join(ROOT, "sim", "board"), os.path.join(ROOT, "sim", "overlay"), LGFX)]
env = dict(os.environ, PATH=os.path.dirname(GXX) + os.pathsep + os.environ["PATH"])


def run(cmd):
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if r.returncode:
        errs = [l for l in (r.stdout + r.stderr).splitlines() if "error" in l or "undefined reference" in l]
        print("\n".join(errs[:40]) or (r.stdout + r.stderr)[-3000:])
        sys.exit(1)


def main():
    os.makedirs(SRC, exist_ok=True); os.makedirs(OBJ, exist_ok=True)
    def sync(f):
        dst = os.path.join(SRC, os.path.basename(f))
        if not os.path.exists(dst) or open(f, "rb").read() != open(dst, "rb").read():
            shutil.copy2(f, dst)
    for f in glob.glob(os.path.join(ROOT, "src", "*.cpp")) + glob.glob(os.path.join(ROOT, "src", "*.h")):
        if os.path.basename(f) not in os.listdir(os.path.join(ROOT, "sim", "replace")):
            sync(f)
    for f in glob.glob(os.path.join(ROOT, "sim", "replace", "*")):
        sync(f)
    objs = []
    lg = os.path.join(OBJ, "lgfx_v1.o")
    if not os.path.exists(lg):
        run([GXX] + FLAGS + INC + ["-c", os.path.join(LGFX, "lgfx", "v1", "lgfx_v1.cpp"), "-o", lg])
    objs.append(lg)
    gcc = GXX.replace("g++.exe", "gcc.exe")
    for c in glob.glob(os.path.join(LGFX, "lgfx", "**", "*.c"), recursive=True):   # fonts, PNG/JPEG/QR codecs
        o = os.path.join(OBJ, "lgfx_" + os.path.basename(c).replace(".c", ".o"))
        if not os.path.exists(o):
            print("cc", os.path.basename(c)); run([gcc, "-O1", "-w", "-I" + LGFX, "-c", c, "-o", o])
        objs.append(o)
    # Headers can change anything; if one did, rebuild everything, else just what changed.
    heads = glob.glob(os.path.join(SRC, "*.h")) + glob.glob(os.path.join(ROOT, "sim", "**", "*.h"), recursive=True)
    newest_h = max(os.path.getmtime(h) for h in heads)
    def stale(src, o):
        return not os.path.exists(o) or os.path.getmtime(o) < max(os.path.getmtime(src), newest_h)
    for src in [os.path.join(SRC, f) for f in FIRMWARE] + [os.path.join(ROOT, "sim", f) for f in SIM]:
        o = os.path.join(OBJ, os.path.basename(src).replace(".cpp", ".o"))
        if stale(src, o):
            print("cc", os.path.basename(src)); run([GXX] + FLAGS + INC + ["-c", src, "-o", o])
        objs.append(o)
    exe = os.path.join(OUT, "squatch_sim" + ("_pager" if "--pager" in sys.argv else "") + ".exe")
    run([GXX] + objs + ["-o", exe, "-static"])
    print("built", exe)


main()
