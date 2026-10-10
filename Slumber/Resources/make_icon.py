#!/usr/bin/env python3
"""Draw the Slumber app icons: a crescent moon on the app's dark ground.

    python3 Slumber/Resources/make_icon.py Slumber/Resources

One shape, many pixels wide at both sizes, so it is drawn supersampled and
downsampled; app_merging.py then quantises it to ABGR2222.
"""
from PIL import Image, ImageDraw
import sys

SS = 16

CARD_BG = (10, 10, 10, 255)
MOON = (170, 170, 255, 255)

DESIGNS = {
    60: dict(card_inset=3, card_radius=12),
    30: dict(card_inset=2, card_radius=6),
}


def draw(size):
    d = DESIGNS[size]
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pen = ImageDraw.Draw(img)

    ci = d["card_inset"] * SS
    pen.rounded_rectangle([ci, ci, S - 1 - ci, S - 1 - ci],
                          radius=d["card_radius"] * SS, fill=CARD_BG)

    r = 0.30 * S
    cx, cy = 0.50 * S, 0.50 * S
    pen.ellipse([cx - r, cy - r, cx + r, cy + r], fill=MOON)
    bite = 0.26 * S
    bx, by = cx + 0.16 * S, cy - 0.10 * S
    pen.ellipse([bx - bite, by - bite, bx + bite, by + bite], fill=CARD_BG)

    return img.resize((size, size), Image.LANCZOS)


def main(out_dir):
    for size in DESIGNS:
        draw(size).save(f"{out_dir}/icon_{size}x{size}.png")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
