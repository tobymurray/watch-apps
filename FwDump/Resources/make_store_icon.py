#!/usr/bin/env python3
"""Draw the store icon -- the one the phone app and Kira show, not the watch's.

    python3 FwDump/Resources/make_store_icon.py FwDump/Resources

The same chip and arrow as icon_60x60.png, drawn at 512px in a round
watch-face frame, as an ordinary full-colour PNG.
"""
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SIZE = 512
SS = 4

BG_CENTER = (10, 40, 40)
BG_EDGE = (7, 9, 11)
BEZEL = (234, 234, 231)
FACE = (10, 10, 10)
TEAL = (0, 176, 176)


def draw_glyph(pen, x0, y0, d):
    """The watch icon's chip and arrow, laid out on its own 60-unit grid."""
    u = d / 60.0

    def box(x1, y1, x2, y2, **kw):
        pen.rounded_rectangle([x0 + x1 * u, y0 + y1 * u, x0 + x2 * u, y0 + y2 * u], **kw)

    box(15, 8, 45, 30, radius=2.5 * u, outline=TEAL, width=int(3 * u))
    for y in (12.5, 19.5, 25.5):
        box(9, y, 15, y + 2.5, radius=0.5 * u, fill=TEAL)
        box(45, y, 51, y + 2.5, radius=0.5 * u, fill=TEAL)
    box(22, 18, 28, 21, radius=0.5 * u, fill=TEAL)
    box(32, 18, 38, 21, radius=0.5 * u, fill=TEAL)

    box(28.5, 36, 31.5, 45, radius=0.5 * u, fill=TEAL)
    pen.polygon([(x0 + 21 * u, y0 + 44 * u), (x0 + 39 * u, y0 + 44 * u),
                 (x0 + 30 * u, y0 + 55 * u)], fill=TEAL)


def draw():
    S = SIZE * SS
    cx = cy = S / 2.0

    yy, xx = np.mgrid[0:S, 0:S]
    t = np.clip(np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / (S * 0.62), 0.0, 1.0)[..., None]
    grad = (np.array(BG_CENTER, np.float32) * (1.0 - t)
            + np.array(BG_EDGE, np.float32) * t).astype(np.uint8)
    img = Image.fromarray(grad)

    face_d = int(S * 0.72)
    fx = fy = (S - face_d) // 2

    shadow = Image.new("L", (S, S), 0)
    spread = int(S * 0.02)
    ImageDraw.Draw(shadow).ellipse(
        [fx - spread, fy - spread + int(S * 0.028),
         fx + face_d + spread, fy + face_d + spread + int(S * 0.028)], fill=225)
    shadow = shadow.filter(ImageFilter.GaussianBlur(radius=S * 0.02))
    img = Image.composite(Image.new("RGB", (S, S), (0, 0, 0)), img, shadow)

    pen = ImageDraw.Draw(img)
    pen.ellipse([fx, fy, fx + face_d, fy + face_d], fill=FACE)

    glyph_d = face_d * 0.78
    draw_glyph(pen, cx - glyph_d / 2, cy - glyph_d / 2 - face_d * 0.01, glyph_d)

    pen.ellipse([fx, fy, fx + face_d, fy + face_d], outline=BEZEL,
                width=max(1, int(S * 0.014)))

    return img.resize((SIZE, SIZE), Image.LANCZOS)


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    path = "%s/icon_store.png" % out_dir
    draw().save(path)
    print("wrote %s (%dx%d)" % (path, SIZE, SIZE))
