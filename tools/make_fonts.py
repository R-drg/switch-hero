#!/usr/bin/env python3
"""Rasterise the bundled TTF fonts into the .font atlases the game loads.

SDL2's Switch port has no SDL_ttf, so glyphs are baked ahead of time. Run from
the repository root after changing a font or size (requires Pillow):

    python3 tools/make_fonts.py

Format (little endian): "FNT1", u16 atlas width, u16 atlas height, u16 pixel
size, u16 line height, u16 ascent, u16 glow padding, u16 glyph count, then per glyph (Latin-1
code 32 upward) i16 x, y, w, h, xoff, yoff and u16 advance in 1/16 px, then the
alpha atlas and a blurred glow atlas, one byte per pixel each. The C1 control
codes (127-159) are empty slots, so a glyph's index is always its code - 32.
"""
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent / "assets" / "fonts"
FONTS = [
    ("BlackOpsOne-Regular.ttf", "stencil.font", 72),
    ("PermanentMarker-Regular.ttf", "marker.font", 56),
    ("RussoOne-Regular.ttf", "body.font", 40),
]
FIRST, LAST = 32, 255  # printable ASCII and Latin-1, for Portuguese accents
ATLAS_W = 1024


def build(ttf, out, size):
    font = ImageFont.truetype(str(ROOT / ttf), size)
    ascent, descent = font.getmetrics()
    pad = max(4, size // 7)  # room for the glow blur
    glyphs, x, y, row = [], pad, pad, 0
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        if 127 <= code < 160:
            glyphs.append((ch, 0, 0, 0, 0, 0, 0, 0))
            continue
        left, top, right, bottom = font.getbbox(ch)
        w, h = max(0, right - left), max(0, bottom - top)
        if x + w + pad > ATLAS_W:
            x, y, row = pad, y + row + pad * 2, 0
        glyphs.append((ch, x, y, w, h, left, top, font.getlength(ch)))
        x += w + pad * 2
        row = max(row, h)
    height = (y + row + pad + 3) // 4 * 4
    atlas = Image.new("L", (ATLAS_W, height), 0)
    draw = ImageDraw.Draw(atlas)
    for ch, gx, gy, w, h, left, top, _ in glyphs:
        if w and h:
            draw.text((gx - left, gy - top), ch, font=font, fill=255)
    glow = atlas.filter(ImageFilter.GaussianBlur(pad / 2))
    peak = max(glow.getextrema()[1], 1)
    glow = glow.point(lambda v: min(255, v * 255 // peak))
    with open(ROOT / out, "wb") as f:
        f.write(b"FNT1")
        f.write(struct.pack("<7H", ATLAS_W, height, size, ascent + descent, ascent, pad, len(glyphs)))
        for _, gx, gy, w, h, left, top, advance in glyphs:
            f.write(struct.pack("<6hH", gx, gy, w, h, left, top, round(advance * 16)))
        f.write(atlas.tobytes())
        f.write(glow.tobytes())
    print(f"{out}: {ATLAS_W}x{height}")


for spec in FONTS:
    build(*spec)
