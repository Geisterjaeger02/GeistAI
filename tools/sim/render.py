#!/usr/bin/env python3
"""Build the desktop simulator, render every screen and make a contact sheet.

    python3 tools/sim/render.py            -> tools/sim/out/screens.png
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
SRC = HERE.parent.parent / "firmware" / "src"
OUT = HERE / "out"


def main():
    OUT.mkdir(exist_ok=True)
    exe = OUT / "sim"
    cmd = ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
           f"-I{SRC}", str(HERE / "sim.cpp"), str(SRC / "gfx.cpp"), str(SRC / "ui.cpp"), str(SRC / "keyboard.cpp"), str(SRC / "qrcode.cpp"),
           "-o", str(exe)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe), str(OUT) + "/"], check=True)

    for old in OUT.glob("*.png"):
        old.unlink()
    for prefix, (sw, sh) in (("tembed_", (320, 170)), ("m5_", (240, 135))):
        make_sheet(sorted(OUT.glob(prefix + "*.ppm")), sw, sh,
                   OUT / ("screens.png" if prefix == "tembed_" else "screens_m5.png"))
    print(OUT / "screens.png")


def make_sheet(shots, sw, sh, target):
    scale, cols, gap = 2, 3, 12
    w, h = sw * scale, sh * scale
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + gap) + gap, rows * (h + gap) + gap), (24, 24, 28))
    for i, p in enumerate(shots):
        im = Image.open(p).resize((w, h), Image.NEAREST)
        im.save(p.with_suffix(".png"))
        sheet.paste(im, (gap + (i % cols) * (w + gap), gap + (i // cols) * (h + gap)))
        p.unlink()
    sheet.save(target)


if __name__ == "__main__":
    sys.exit(main())
