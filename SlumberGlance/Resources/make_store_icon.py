#!/usr/bin/env python3
"""Draw the store icon -- the one the phone app and Kira show.

    python3 SlumberGlance/Resources/make_store_icon.py SlumberGlance/Resources

Slumber's frame and crescent (Slumber/Resources/make_store_icon.py), on a
glance card between the edges of its neighbours, so the two apps read as a
pair in the store and apart at a glance.
"""
import sys
from pathlib import Path

from PIL import ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Slumber" / "Resources"))
import make_store_icon as slumber  # noqa: E402

CARD = (34, 28, 52, 255)
CARD_EDGE = (86, 70, 124, 255)
NEIGHBOUR = (30, 26, 44, 255)
LINE = (245, 245, 245, 255)
LINE_ACCENT = (0, 220, 220, 255)


def draw():
    img, fx, face_d = slumber.face()
    S = slumber.SIZE * slumber.SS
    cy = S / 2.0
    w, h = face_d * 0.74, face_d * 0.36
    x0, y0 = S / 2.0 - w / 2, cy - h / 2
    peek = face_d * 0.05

    pen = ImageDraw.Draw(img)
    for dy in (-h / 2 - peek * 2.2, h / 2 + peek * 2.2):
        pen.rounded_rectangle([x0 + w * 0.08, cy + dy - peek, x0 + w * 0.92, cy + dy + peek],
                              radius=peek, fill=NEIGHBOUR)
    pen.rounded_rectangle([x0, y0, x0 + w, y0 + h], radius=h * 0.22, fill=CARD,
                          outline=CARD_EDGE, width=int(S * 0.006))

    glyph_d = int(h * 0.82)
    img = slumber.glowing(img, slumber.draw_glyph(glyph_d), (int(x0 + h * 0.10), int(cy - glyph_d / 2)))

    pen = ImageDraw.Draw(img)
    left = x0 + h * 0.10 + glyph_d * 0.95
    bar = h * 0.13
    pen.rounded_rectangle([left, cy - bar * 1.6, x0 + w - h * 0.22, cy - bar * 0.6], radius=bar / 2, fill=LINE)
    pen.rounded_rectangle([left, cy + bar * 0.6, x0 + w * 0.78, cy + bar * 1.6], radius=bar / 2, fill=LINE_ACCENT)
    return slumber.finish(img, fx, face_d)


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    path = "%s/icon_store.png" % out_dir
    draw().save(path)
    print("wrote %s (%dx%d)" % (path, slumber.SIZE, slumber.SIZE))
