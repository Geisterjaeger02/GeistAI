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
           f"-I{SRC}", str(HERE / "sim.cpp"), str(SRC / "gfx.cpp"), str(SRC / "ui.cpp"),
           "-o", str(exe)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe), str(OUT) + "/"], check=True)

    shots = sorted(OUT.glob("*.ppm"))
    scale, cols, gap = 2, 3, 12
    w, h = 320 * scale, 170 * scale
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (w + gap) + gap, rows * (h + gap) + gap), (24, 24, 28))
    for i, p in enumerate(shots):
        im = Image.open(p).resize((w, h), Image.NEAREST)
        im.save(p.with_suffix(".png"))
        sheet.paste(im, (gap + (i % cols) * (w + gap), gap + (i // cols) * (h + gap)))
        p.unlink()
    sheet.save(OUT / "screens.png")
    print(OUT / "screens.png")


if __name__ == "__main__":
    sys.exit(main())
