#!/usr/bin/env python3
"""Draw the BatteryLab app icons: a battery outline, two-thirds full, on a dark card.

    python3 BatteryLab/Resources/make_icon.py BatteryLab/Resources

Drawn supersampled and downsampled; app_merging.py then quantises it to ABGR2222.
"""
from PIL import Image, ImageDraw
import sys

SS = 16

CARD_BG = (10, 10, 10, 255)
INK = (170, 255, 170, 255)

DESIGNS = {
    60: dict(card_inset=3, card_radius=12, stroke=3),
    30: dict(card_inset=2, card_radius=6, stroke=2),
}


def draw(size):
    d = DESIGNS[size]
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pen = ImageDraw.Draw(img)

    ci = d["card_inset"] * SS
    pen.rounded_rectangle([ci, ci, S - 1 - ci, S - 1 - ci],
                          radius=d["card_radius"] * SS, fill=CARD_BG)

    w = d["stroke"] * SS
    left, right = 0.20 * S, 0.74 * S
    top, bottom = 0.33 * S, 0.67 * S
    pen.rounded_rectangle([left, top, right, bottom], radius=0.05 * S, outline=INK, width=w)
    pen.rectangle([right, 0.43 * S, right + 0.06 * S, 0.57 * S], fill=INK)
    gap = w * 1.6
    fill_right = left + gap + (right - left - 2 * gap) * 2 / 3
    pen.rectangle([left + gap, top + gap, fill_right, bottom - gap], fill=INK)

    return img.resize((size, size), Image.LANCZOS)


def main(out_dir):
    for size in DESIGNS:
        draw(size).save(f"{out_dir}/icon_{size}x{size}.png")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
