#!/usr/bin/env python3
"""Draw the NotifyToggle app icon: the on-watch screen, reduced to its
essential shape -- a green pill with a white knob on a black rounded card.

    python3 NotifyToggle/Resources/make_icon.py NotifyToggle/Resources

Unlike Barcode's icon (Resources/make_icon.py in that app, which hand-places
1px bars because a sub-pixel bar downsamples to a muddy grey once
`app_merging.py` quantises it to ABGR2222 -- two bits per channel, four
levels), every shape here is many pixels wide even at 30px. So there is
nothing that lands on a half-pixel and needs a bespoke pattern: the whole
icon is drawn supersampled and downsampled with one function, at both sizes,
and the quantisation check at the bottom exists to prove that choice was
safe rather than to excuse a special case.
"""
from PIL import Image, ImageDraw
import sys

SS = 16  # supersample factor

CARD_BG = (10, 10, 10, 255)      # near-black, matches the app's own ground
PILL_ON = (0, 230, 90, 255)      # quantises to the same green the app draws
KNOB = (255, 255, 255, 255)

# Per size: card inset/radius, and the pill's box within the card.
DESIGNS = {
    60: dict(card_inset=3, card_radius=12,
             pill_inset_x=9, pill_inset_y=19, pill_radius=11, knob_radius=9),
    30: dict(card_inset=2, card_radius=6,
             pill_inset_x=5, pill_inset_y=10, pill_radius=5, knob_radius=4),
}


def draw(size):
    d = DESIGNS[size]
    S = size * SS

    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pen = ImageDraw.Draw(img)

    ci = d["card_inset"] * SS
    pen.rounded_rectangle([ci, ci, S - 1 - ci, S - 1 - ci],
                           radius=d["card_radius"] * SS, fill=CARD_BG)

    px0 = d["pill_inset_x"] * SS
    py0 = d["pill_inset_y"] * SS
    px1 = S - 1 - px0
    py1 = S - 1 - py0
    pen.rounded_rectangle([px0, py0, px1, py1],
                           radius=d["pill_radius"] * SS, fill=PILL_ON)

    # ON state: the knob sits on the right, same as the app screen.
    kr = d["knob_radius"] * SS
    kcx = px1 - kr - int(0.15 * SS * size / 5)  # small inset from the pill's own edge
    kcy = (py0 + py1) // 2
    pen.ellipse([kcx - kr, kcy - kr, kcx + kr, kcy + kr], fill=KNOB)

    return img.resize((size, size), Image.LANCZOS)


def quantise(img):
    """What ABGR2222 leaves of an image, expanded back to 8 bits for checking."""
    out = img.copy()
    px = out.load()
    q = lambda v: ((v >> 6) & 3) * 85
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            px[x, y] = (q(r), q(g), q(b), q(a))
    return out


def report(size, img):
    """Spot-check pixels well inside each shape, away from any rounded edge,
    against what they should quantise to. Edge pixels are expected to blend
    -- that is what the supersample-then-downsample corner treatment is for,
    the same tradeoff Barcode's icon makes on its card corners -- so this
    checks the interiors are still the intended flat colour, not that the
    whole icon has only four shades in it."""
    d = DESIGNS[size]
    q = quantise(img)

    checks = [
        ("card corner region", size // 2, d["card_inset"] + 2, CARD_BG),
        ("pill interior", size // 2, d["pill_inset_y"] + 2, PILL_ON),
        ("knob interior", size - d["pill_inset_x"] - d["knob_radius"] - 3,
         size // 2, KNOB),
    ]
    bad = []
    for label, x, y, expect in checks:
        got = q.getpixel((x, y))
        want = quantise(Image.new("RGBA", (1, 1), expect)).getpixel((0, 0))
        if got != want:
            bad.append("%s: got %s, want %s" % (label, got, want))
    print("%dx%d: %s" % (size, size, "clean" if not bad else "; ".join(bad)))


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    for size in (60, 30):
        icon = draw(size)
        icon.save("%s/icon_%dx%d.png" % (out_dir, size, size))
        report(size, icon)
