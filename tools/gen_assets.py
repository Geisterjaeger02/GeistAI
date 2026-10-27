#!/usr/bin/env python3
"""Generate the GeistAI display assets as C headers.

Produces, in firmware/src/assets/:
  logo.h   - cursive "GeistAI" wordmark (8-bit alpha) + soft glow masks
  fonts.h  - anti-aliased 4-bit UI fonts (Latin-1 + Latin Extended-A)
  icons.h  - small anti-aliased icon masks (mic, wifi, speaker, ...)

Only needs Pillow:  python3 tools/gen_assets.py
"""
from __future__ import annotations

import math
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "tools" / "assets" / "fonts"
OUT = ROOT / "firmware" / "src" / "assets"

SCRIPT_TTF = FONTS / "DancingScript.ttf"
UI_TTF = FONTS / "Inter.ttf"

# Characters the device can display: Latin (German, English, Spanish, French,
# Polish, Czech, Turkish, Romanian, Vietnamese ...), Greek and Cyrillic (Russian,
# Ukrainian, Serbian). Other scripts are transliterated by the server.
# Keep in sync with _RANGES in server/geist_server/textutil.py.
CODEPOINTS = (
    list(range(0x20, 0x7F))
    + list(range(0xA0, 0x100))
    + list(range(0x100, 0x180))
    + [0x1A0, 0x1A1, 0x1AF, 0x1B0]          # Vietnamese ơ ư
    + list(range(0x218, 0x21C))             # Romanian ș ț
    + list(range(0x384, 0x3CF))             # Greek
    + list(range(0x400, 0x460)) + [0x490, 0x491]  # Cyrillic
    + list(range(0x1EA0, 0x1EFA))           # Vietnamese
    + [0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E,
       0x2022, 0x2026, 0x20AC, 0x2039, 0x203A, 0x00B7]
)
CODEPOINTS = sorted(set(CODEPOINTS))


def _font_has(path) -> set[int]:
    """Code points the font really contains (no empty boxes for gaps in a range)."""
    try:
        from fontTools.ttLib import TTFont

        return set(TTFont(str(path)).getBestCmap())
    except Exception:  # fontTools missing: keep everything
        return set(CODEPOINTS)


_HAVE = _font_has(UI_TTF)
CODEPOINTS = [cp for cp in CODEPOINTS if cp in _HAVE or cp < 0x80]


def c_array(name: str, data: bytes, ctype: str = "uint8_t", per_line: int = 24) -> str:
    lines = []
    for i in range(0, len(data), per_line):
        lines.append("  " + ",".join(str(b) for b in data[i:i + per_line]) + ",")
    body = "\n".join(lines)
    return f"static const {ctype} {name}[{len(data)}] PROGMEM = {{\n{body}\n}};\n"


# --------------------------------------------------------------------------- fonts

def build_font(cname: str, size: int, weight: str, opsz: int | None = None) -> tuple[str, dict]:
    font = ImageFont.truetype(str(UI_TTF), size)
    # Inter axes: [opsz, wght]
    wmap = {"Regular": 400, "Medium": 500, "SemiBold": 600, "Bold": 700, "ExtraBold": 800}
    try:
        font.set_variation_by_axes([opsz or max(14, min(32, size)), wmap[weight]])
    except Exception:
        font.set_variation_by_name(weight)

    ascent, descent = font.getmetrics()
    glyphs = []
    bitmap = bytearray()
    pad = size * 2
    for cp in CODEPOINTS:
        ch = chr(cp)
        try:
            bbox = font.getbbox(ch, anchor="ls")
        except Exception:
            continue
        adv = int(round(font.getlength(ch)))
        x0, y0, x1, y1 = bbox
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        if cp == 0x20 or cp == 0xA0:
            w = h = 0
        offset = len(bitmap)
        if w > 0 and h > 0:
            img = Image.new("L", (w + pad * 2, h + pad * 2), 0)
            ImageDraw.Draw(img).text((pad - x0, pad - y0), ch, font=font, fill=255, anchor="ls")
            crop = img.crop((pad, pad, pad + w, pad + h))
            px = list(crop.tobytes())
            # 4 bpp, two pixels per byte, high nibble first
            nib = [(v + 8) // 17 for v in px]
            nib = [min(15, n) for n in nib]
            if len(nib) % 2:
                nib.append(0)
            for i in range(0, len(nib), 2):
                bitmap.append((nib[i] << 4) | nib[i + 1])
        else:
            w = h = 0
        glyphs.append((cp, w, h, x0, y0, adv, offset))

    out = []
    out.append(c_array(f"{cname}_bitmap", bytes(bitmap)))
    out.append(f"static const geist::Glyph {cname}_glyphs[{len(glyphs)}] PROGMEM = {{")
    for cp, w, h, xo, yo, adv, off in glyphs:
        out.append(f"  {{0x{cp:04X},{w},{h},{xo},{yo},{adv},{off}}},")
    out.append("};")
    line_h = ascent + descent
    out.append(
        f"static const geist::Font {cname} = {{ {cname}_glyphs, {len(glyphs)}, {cname}_bitmap, "
        f"{line_h}, {ascent} }};\n"
    )
    return "\n".join(out), {"size": size, "glyphs": len(glyphs), "bytes": len(bitmap)}


# --------------------------------------------------------------------------- logo

def render_wordmark(size: int, weight: int = 700, text: str = "GeistAI"):
    font = ImageFont.truetype(str(SCRIPT_TTF), size)
    font.set_variation_by_axes([weight])
    pad = size
    bbox = font.getbbox(text)
    w = bbox[2] - bbox[0] + pad * 2
    h = bbox[3] - bbox[1] + pad * 2
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((pad - bbox[0], pad - bbox[1]), text, font=font, fill=255)
    return img


def crop_to_content(img: Image.Image, margin: int = 0) -> Image.Image:
    bb = img.getbbox()
    x0, y0, x1, y1 = bb
    return img.crop((max(0, x0 - margin), max(0, y0 - margin),
                     min(img.width, x1 + margin), min(img.height, y1 + margin)))


def build_logo() -> str:
    parts = []
    for cname, size, glow_r in (("logo_big", 70, 7), ("logo_mid", 44, 5), ("logo_small", 26, 0)):
        img = render_wordmark(size)
        margin = glow_r * 3 if glow_r else 0
        img = crop_to_content(img, margin)
        parts.append(f"// {cname}: {img.width}x{img.height}")
        parts.append(f"static const uint16_t {cname}_w = {img.width};")
        parts.append(f"static const uint16_t {cname}_h = {img.height};")
        parts.append(c_array(f"{cname}_mask", img.tobytes()))
        if glow_r:
            glow = img.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(glow_r))
            # boost so the halo is visible on a bright background
            glow = glow.point(lambda v: min(255, int(v * 1.8)))
            parts.append(c_array(f"{cname}_glow", glow.tobytes()))
    return "\n".join(parts)


# --------------------------------------------------------------------------- icons

def _icon_canvas(size: int, ss: int = 4):
    img = Image.new("L", (size * ss, size * ss), 0)
    return img, ImageDraw.Draw(img), ss


def _finish(img: Image.Image, size: int) -> Image.Image:
    return img.resize((size, size), Image.LANCZOS)


def icon_mic(size: int) -> Image.Image:
    img, d, s = _icon_canvas(size)
    S = size * s
    lw = max(s, int(S * 0.085))
    cw, ch = S * 0.34, S * 0.52
    cx = S / 2
    top = S * 0.08
    d.rounded_rectangle([cx - cw / 2, top, cx + cw / 2, top + ch], radius=cw / 2, fill=255)
    # cradle arc
    r = S * 0.30
    cy = top + ch * 0.55
    d.arc([cx - r, cy - r, cx + r, cy + r], start=0, end=180, fill=255, width=lw)
    # stem + base
    d.rectangle([cx - lw / 2, cy + r - lw / 2, cx + lw / 2, S * 0.90], fill=255)
    d.rounded_rectangle([cx - S * 0.2, S * 0.88, cx + S * 0.2, S * 0.88 + lw], radius=lw / 2, fill=255)
    return _finish(img, size)


def icon_wifi(size: int, on: bool = True) -> Image.Image:
    img, d, s = _icon_canvas(size)
    S = size * s
    lw = max(s, int(S * 0.11))
    cx, cy = S / 2, S * 0.86
    for k, rr in enumerate((0.78, 0.53, 0.28)):
        r = S * rr
        d.arc([cx - r, cy - r, cx + r, cy + r], start=225, end=315, fill=255, width=lw)
    d.ellipse([cx - lw * 0.9, cy - lw * 0.9, cx + lw * 0.9, cy + lw * 0.9], fill=255)
    if not on:
        d.line([S * 0.12, S * 0.12, S * 0.88, S * 0.88], fill=0, width=lw * 3)
        d.line([S * 0.12, S * 0.12, S * 0.88, S * 0.88], fill=255, width=lw)
    return _finish(img, size)


def icon_speaker(size: int) -> Image.Image:
    img, d, s = _icon_canvas(size)
    S = size * s
    lw = max(s, int(S * 0.09))
    d.polygon([(S * 0.10, S * 0.36), (S * 0.30, S * 0.36), (S * 0.52, S * 0.14),
               (S * 0.52, S * 0.86), (S * 0.30, S * 0.64), (S * 0.10, S * 0.64)], fill=255)
    for rr in (0.22, 0.38):
        r = S * rr
        cx, cy = S * 0.50, S * 0.5
        d.arc([cx - r, cy - r, cx + r, cy + r], start=-50, end=50, fill=255, width=lw)
    return _finish(img, size)


def icon_globe(size: int) -> Image.Image:
    img, d, s = _icon_canvas(size)
    S = size * s
    lw = max(s, int(S * 0.08))
    m = S * 0.08
    d.ellipse([m, m, S - m, S - m], outline=255, width=lw)
    d.ellipse([S * 0.32, m, S * 0.68, S - m], outline=255, width=lw)
    d.line([m, S / 2, S - m, S / 2], fill=255, width=lw)
    return _finish(img, size)


def icon_gear(size: int) -> Image.Image:
    img, d, s = _icon_canvas(size)
    S = size * s
    cx = cy = S / 2
    ro, ri = S * 0.46, S * 0.34
    pts = []
    teeth = 8
    for i in range(teeth * 4):
        a = i / (teeth * 4) * 2 * math.pi
        r = ro if (i % 4) in (0, 1) else ri
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    d.polygon(pts, fill=255)
    rh = S * 0.15
    d.ellipse([cx - rh, cy - rh, cx + rh, cy + rh], fill=0)
    return _finish(img, size)


def icon_shift(size: int) -> Image.Image:
    img, d, s = _icon_canvas(size)
    S = size * s
    lw = max(s, int(S * 0.1))
    pts = [(S * 0.5, S * 0.08), (S * 0.92, S * 0.52), (S * 0.68, S * 0.52), (S * 0.68, S * 0.9),
           (S * 0.32, S * 0.9), (S * 0.32, S * 0.52), (S * 0.08, S * 0.52)]
    d.polygon(pts, outline=255, width=lw)
    return _finish(img, size)


def icon_backspace(size: int) -> Image.Image:
    img, d, s = _icon_canvas(size)
    S = size * s
    lw = max(s, int(S * 0.09))
    pts = [(S * 0.04, S * 0.5), (S * 0.32, S * 0.18), (S * 0.96, S * 0.18), (S * 0.96, S * 0.82),
           (S * 0.32, S * 0.82)]
    d.polygon(pts, outline=255, width=lw)
    d.line([S * 0.46, S * 0.35, S * 0.76, S * 0.65], fill=255, width=lw)
    d.line([S * 0.46, S * 0.65, S * 0.76, S * 0.35], fill=255, width=lw)
    return _finish(img, size)


def icon_send(size: int) -> Image.Image:
    img, d, s = _icon_canvas(size)
    S = size * s
    d.polygon([(S * 0.06, S * 0.1), (S * 0.96, S * 0.5), (S * 0.06, S * 0.9), (S * 0.22, S * 0.5)],
              fill=255)
    return _finish(img, size)


def icon_close(size: int) -> Image.Image:
    img, d, s = _icon_canvas(size)
    S = size * s
    lw = max(s, int(S * 0.13))
    d.line([S * 0.15, S * 0.15, S * 0.85, S * 0.85], fill=255, width=lw)
    d.line([S * 0.15, S * 0.85, S * 0.85, S * 0.15], fill=255, width=lw)
    return _finish(img, size)


def icon_keyboard(size: int) -> Image.Image:
    img, d, s = _icon_canvas(size)
    S = size * s
    lw = max(s, int(S * 0.08))
    d.rounded_rectangle([S * 0.04, S * 0.2, S * 0.96, S * 0.8], radius=S * 0.1, outline=255, width=lw)
    k = S * 0.09
    for row, y in enumerate((0.36, 0.5)):
        for i in range(5):
            x = S * (0.17 + i * 0.165)
            d.rectangle([x - k / 2, S * y - k / 2, x + k / 2, S * y + k / 2], fill=255)
    d.rectangle([S * 0.3, S * 0.62, S * 0.7, S * 0.62 + k], fill=255)
    return _finish(img, size)


def build_icons() -> str:
    icons = [
        ("icon_mic_big", icon_mic(46)),
        ("icon_mic", icon_mic(14)),
        ("icon_wifi", icon_wifi(14, True)),
        ("icon_wifi_off", icon_wifi(14, False)),
        ("icon_speaker", icon_speaker(14)),
        ("icon_globe", icon_globe(13)),
        ("icon_gear", icon_gear(14)),
        ("icon_shift", icon_shift(14)),
        ("icon_backspace", icon_backspace(16)),
        ("icon_send", icon_send(14)),
        ("icon_close", icon_close(12)),
        ("icon_keyboard", icon_keyboard(16)),
    ]
    parts = []
    for name, img in icons:
        parts.append(f"static const uint16_t {name}_w = {img.width};")
        parts.append(f"static const uint16_t {name}_h = {img.height};")
        parts.append(c_array(f"{name}_mask", img.tobytes()))
    return "\n".join(parts)


CHESS_TTF = FONTS / "DejaVuChess.ttf"


def build_chess() -> str:
    """Chess pieces K Q R B N P: a filled body mask and an outline mask per size."""
    parts = []
    for size in (18, 14):
        font = ImageFont.truetype(str(CHESS_TTF), int(size * 1.08))
        for i, letter in enumerate("KQRBNP"):
            for kind, base in (("fill", 0x265A), ("line", 0x2654)):
                ss = 4
                big = Image.new("L", (size * ss, size * ss), 0)
                f = ImageFont.truetype(str(CHESS_TTF), int(size * 1.08 * ss))
                d = ImageDraw.Draw(big)
                d.text((size * ss / 2, size * ss / 2 + ss), chr(base + i), font=f, fill=255,
                       anchor="mm")
                img = big.resize((size, size), Image.LANCZOS)
                name = f"chess{size}_{letter}_{kind}"
                parts.append(c_array(name, img.tobytes()))
        parts.append(f"static const uint16_t chess{size}_size = {size};")
    return "\n".join(parts)


HEADER = """// AUTO-GENERATED by tools/gen_assets.py - do not edit by hand.
// Fonts: Dancing Script and Inter, both SIL Open Font License 1.1 (see tools/assets/fonts).
#pragma once
#include <stdint.h>
#ifndef PROGMEM
#define PROGMEM
#endif
"""


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    (OUT / "logo.h").write_text(HEADER + "\n" + build_logo())
    print("logo.h written")

    font_src = [HEADER, '#include "../gfx.h"\n']
    for cname, size, weight in (
        ("font_body", 14, "Medium"),
        ("font_small", 11, "SemiBold"),
        ("font_title", 18, "Bold"),
    ):
        src, info = build_font(cname, size, weight)
        font_src.append(src)
        print(cname, info)
    (OUT / "fonts.h").write_text("\n".join(font_src))

    (OUT / "icons.h").write_text(HEADER + "\n" + build_icons())
    print("icons.h written")

    (OUT / "chess.h").write_text(HEADER.replace("Fonts: Dancing Script and Inter, both SIL Open "
                                                "Font License 1.1", "Chess pieces from DejaVu Sans "
                                                "(see tools/assets/fonts/LICENSE-DejaVu.txt)")
                                 + "\n" + build_chess())
    print("chess.h written")


if __name__ == "__main__":
    main()
