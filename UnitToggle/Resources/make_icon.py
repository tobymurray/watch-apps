#!/usr/bin/env python3
"""Draw the UnitToggle app icons: a ruler -- the one symbol that says measuring
-- above the two-position bar this app actually draws on screen.

    python3 UnitToggle/Resources/make_icon.py UnitToggle/Resources

A dual-scale ruler does not survive these sizes: rendered at true size, ticks on
both edges at two different spacings read as a fence, and a bar with notches cut
into it reads as piano keys. One scale drawn the ordinary way does read -- a
baseline with ticks rising from it, alternating long and short -- so that is
what this draws, and the choosing is carried by the two-position bar under it.

The two sizes carry different amounts of that idea, on purpose. At 60px the
ruler and the bar both read. At 30px the bar collapses into a smear beside the
ticks, so the small icon is the ruler alone with three ticks instead of five.
Compare at true size before changing either; magnified previews flatter a 30px
icon into looking like it works.

Every shape here is at least two pixels wide even at 30px, so the whole icon is
drawn supersampled and downsampled with one function. The quantisation check at
the bottom is what proves that choice was safe -- `app_merging.py` reduces this
to ABGR2222, two bits per channel, four levels.
"""
from PIL import Image, ImageDraw
import sys

SS = 16  # supersample factor

CARD_BG = (10, 10, 10, 255)   # near-black, matches the app's own ground
RULE = (0, 170, 255, 255)     # quantises exactly, and is nobody else's colour here
UNCHOSEN = (85, 85, 85, 255)  # the dark grey the app fills the unchosen half with

# Per size: the card, how many ticks, and whether that size can carry the bar.
DESIGNS = {
    60: dict(card_inset=3, card_radius=12, ticks=5, with_choice=True,
             base_y=0.56, long_h=0.26, short_h=0.15, bar_h=0.065),
    30: dict(card_inset=2, card_radius=6, ticks=3, with_choice=False,
             base_y=0.62, long_h=0.30, short_h=0.17, bar_h=0.085),
}

RULE_X0 = 0.15
RULE_X1 = 0.85


def draw_ruler(pen, S, d):
    """A baseline with ticks up from it, every other one long -- the shape a
    ruler has to have to read as one rather than as a comb."""
    x0, x1 = RULE_X0 * S, RULE_X1 * S
    base_y = d["base_y"] * S
    pen.rectangle([x0, base_y, x1, base_y + d["bar_h"] * S], fill=RULE)

    span = x1 - x0
    tick_w = span / (d["ticks"] * 2 - 1)
    for i in range(d["ticks"]):
        x = x0 + i * tick_w * 2
        h = (d["long_h"] if i % 2 == 0 else d["short_h"]) * S
        pen.rectangle([x, base_y - h, x + tick_w, base_y], fill=RULE)


def draw_choice(pen, S):
    """The app's own control, reduced to its one readable fact: two halves,
    with the left one chosen."""
    x0, x1 = RULE_X0 * S, RULE_X1 * S
    y0, y1 = 0.70 * S, 0.84 * S
    r = (y1 - y0) / 2
    mid = (x0 + x1) / 2
    pen.rounded_rectangle([x0, y0, x1, y1], radius=r, fill=UNCHOSEN)
    pen.rounded_rectangle([x0, y0, mid + r, y1], radius=r, fill=RULE)


def draw(size):
    d = DESIGNS[size]
    S = size * SS

    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pen = ImageDraw.Draw(img)

    ci = d["card_inset"] * SS
    pen.rounded_rectangle([ci, ci, S - 1 - ci, S - 1 - ci],
                          radius=d["card_radius"] * SS, fill=CARD_BG)

    draw_ruler(pen, S, d)
    if d["with_choice"]:
        draw_choice(pen, S)

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
    """Spot-check pixels well inside each shape, away from any edge, against
    what they should quantise to. Edge pixels are expected to blend -- that is
    what the supersample-then-downsample is for -- so this checks the interiors
    are still the intended flat colour, not that the whole icon has only four
    shades in it."""
    d = DESIGNS[size]
    q = quantise(img)

    base_mid = int((d["base_y"] + d["bar_h"] / 2) * size)
    checks = [("card", size // 2, d["card_inset"] + 2, CARD_BG),
              ("baseline", int(0.5 * size), base_mid, RULE),
              ("first tick", int(RULE_X0 * size) + 1,
               int((d["base_y"] - d["long_h"] / 2) * size), RULE)]
    if d["with_choice"]:
        checks.append(("chosen half", int((RULE_X0 + 0.06) * size), int(0.77 * size), RULE))
        checks.append(("unchosen half", int((RULE_X1 - 0.06) * size), int(0.77 * size), UNCHOSEN))

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
