#!/usr/bin/env python3
"""Draw the NotifyToggle app icons: a bell -- the one symbol that says
notifications -- above the green pill this app actually draws on screen.

    python3 NotifyToggle/Resources/make_icon.py NotifyToggle/Resources

The two sizes carry different amounts of that idea, on purpose. At 60px the
bell and the pill both read, so the icon says "notification switch". At 30px
the pill's knob collapses to a blob and the pair reads as clutter, so the
small icon is the bell alone: same subject, same colour, one shape instead of
two. Rendered at true size and compared before choosing -- magnified previews
flatter a 30px icon into looking like it works.

Unlike Barcode's icon (Resources/make_icon.py in that app, which hand-places
1px bars because a sub-pixel bar downsamples to a muddy grey once
`app_merging.py` quantises it to ABGR2222 -- two bits per channel, four
levels), every shape here is many pixels wide even at 30px, so the whole icon
is drawn supersampled and downsampled with one function. The quantisation
check at the bottom is what proves that choice was safe.
"""
from PIL import Image, ImageDraw
import sys

SS = 16  # supersample factor

CARD_BG = (10, 10, 10, 255)      # near-black, matches the app's own ground
PILL_ON = (0, 230, 90, 255)      # quantises to the same green the app draws
BELL = (255, 255, 255, 255)      # white against the green, so neither shape hides the other

# Per size: the card, and which of the two shapes that size can carry.
DESIGNS = {
    60: dict(card_inset=3, card_radius=12, with_pill=True,
             bell_cx=0.50, bell_cy=0.36, bell_w=0.42, bell_h=0.46,
             pill_x0=0.18, pill_x1=0.82, pill_y0=0.66, pill_y1=0.88),
    30: dict(card_inset=2, card_radius=6, with_pill=False,
             bell_cx=0.50, bell_cy=0.50, bell_w=0.60, bell_h=0.64),
}


def draw_bell(pen, cx, cy, w, h, colour):
    """A bell in a w-by-h box: crown, dome, flared skirt, lip, clapper."""
    left, top = cx - w / 2, cy - h / 2
    pen.ellipse([left + 0.44 * w, top, left + 0.56 * w, top + 0.12 * h], fill=colour)
    pen.ellipse([left + 0.20 * w, top + 0.06 * h,
                 left + 0.80 * w, top + 0.78 * h], fill=colour)
    pen.polygon([(left + 0.20 * w, top + 0.42 * h), (left + 0.08 * w, top + 0.74 * h),
                 (left + 0.92 * w, top + 0.74 * h), (left + 0.80 * w, top + 0.42 * h)],
                fill=colour)
    pen.rounded_rectangle([left + 0.04 * w, top + 0.70 * h,
                           left + 0.96 * w, top + 0.82 * h],
                          radius=0.05 * h, fill=colour)
    pen.ellipse([left + 0.40 * w, top + 0.84 * h,
                 left + 0.60 * w, top + 1.00 * h], fill=colour)


def draw(size):
    d = DESIGNS[size]
    S = size * SS

    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pen = ImageDraw.Draw(img)

    ci = d["card_inset"] * SS
    pen.rounded_rectangle([ci, ci, S - 1 - ci, S - 1 - ci],
                           radius=d["card_radius"] * SS, fill=CARD_BG)

    bell_colour = BELL if d["with_pill"] else PILL_ON
    draw_bell(pen, d["bell_cx"] * S, d["bell_cy"] * S,
              d["bell_w"] * S, d["bell_h"] * S, bell_colour)

    if d["with_pill"]:
        x0, x1 = d["pill_x0"] * S, d["pill_x1"] * S
        y0, y1 = d["pill_y0"] * S, d["pill_y1"] * S
        r = (y1 - y0) / 2
        pen.rounded_rectangle([x0, y0, x1, y1], radius=r, fill=PILL_ON)
        # ON state: the knob sits on the right, same as the app screen.
        kr = r * 0.72
        pen.ellipse([x1 - r - kr, (y0 + y1) / 2 - kr,
                     x1 - r + kr, (y0 + y1) / 2 + kr], fill=BELL)

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
    against what they should quantise to. Edge pixels are expected to blend --
    that is what the supersample-then-downsample corner treatment is for -- so
    this checks the interiors are still the intended flat colour, not that the
    whole icon has only four shades in it."""
    d = DESIGNS[size]
    q = quantise(img)

    checks = [("card", size // 2, d["card_inset"] + 2, CARD_BG),
              ("bell dome", int(d["bell_cx"] * size),
               int((d["bell_cy"] - d["bell_h"] * 0.10) * size),
               BELL if d["with_pill"] else PILL_ON)]
    if d["with_pill"]:
        mid_y = int((d["pill_y0"] + d["pill_y1"]) / 2 * size)
        checks.append(("pill", int((d["pill_x0"] + 0.06) * size), mid_y, PILL_ON))
        checks.append(("knob", int((d["pill_x1"] - 0.055) * size), mid_y, BELL))

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
