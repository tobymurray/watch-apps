#!/usr/bin/env python3
"""Draw the watch icon at 30 and 60 px.

    python3 HapticLab/Resources/make_icon.py HapticLab/Resources

PANEL: the watch stores these as ABGR2222, two bits a channel, so four
shades and four alpha levels. Everything below is quantised here rather than
left to the converter, because an antialiased edge on a four-level alpha
channel comes out as a dashed line. Falsified by a different panel format;
re-measure by counting distinct values in the PNG.

WHAT MAKES IT HAPTIC LAB: three arcs spreading from a point at the lower left,
which is the wrist, plus a filled dot where the actuator is. Arcs rather than
a bell or a phone, because what this app varies is the *pattern* and a
spreading wavefront is the one drawing that says so at 30 px. The arcs thin
and shorten with distance, which is the only cue left once colour is down to
four levels.

COLOUR: white for the actuator and the near arc, mid grey (170) for the two
further ones. 85 is deliberately unused: it is the first level to wash out in
daylight on this reflective panel.
"""
from PIL import Image, ImageDraw
import sys

WHITE = (255, 255, 255, 255)
GREY = (170, 170, 170, 255)
SS = 8  # supersample; the arcs need it, and the result is re-quantised after

# Normalised geometry, 0..1 across the icon, y downward. The source sits low
# and left so the arcs have room to spread into the opposite corner.
SOURCE = (0.24, 0.76)
DOT_R = 0.085
# Radius, stroke weight and sweep for each arc, near to far.
ARCS = [
    (0.30, 0.085, GREY),
    (0.47, 0.070, GREY),
    (0.64, 0.055, GREY),
]
# The near arc is white so the drawing has one bright edge besides the dot.
NEAR_ARC_COLOUR = WHITE

# Degrees, measured the way PIL does: 0 is three o'clock, growing clockwise.
# This span points the fan at the top right corner.
ARC_START = 235
ARC_END = 335

# At 30 px the innermost arc lands nine pixels from a two-pixel dot and the
# two merged into one blob after the resample, so the small design drops that
# arc rather than the outermost one; with the arcs further out the dot keeps
# its full size and stays solid.
DESIGNS = {
    60: dict(first=0, last=3, dot=DOT_R),
    30: dict(first=1, last=3, dot=DOT_R),
}


def draw(size):
    design = DESIGNS[size]
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    g = ImageDraw.Draw(img)

    cx, cy = SOURCE[0] * S, SOURCE[1] * S

    arcs = ARCS[design["first"] : design["last"]]
    for index, (radius, weight, colour) in enumerate(arcs):
        r = radius * S
        w = max(int(weight * S), SS)
        ink = NEAR_ARC_COLOUR if index == 0 else colour
        g.arc([cx - r, cy - r, cx + r, cy + r], ARC_START, ARC_END, fill=ink, width=w)

    r = design["dot"] * S
    g.ellipse([cx - r, cy - r, cx + r, cy + r], fill=WHITE)

    small = img.resize((size, size), Image.LANCZOS)
    return quantise(small)


def quantise(img):
    """Snap every channel to 0/85/170/255, which is all the panel has."""
    out = img.copy()
    pixels = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = pixels[x, y]
            pixels[x, y] = tuple(round(c / 85) * 85 for c in (r, g, b, a))
    return out


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    for size in sorted(DESIGNS):
        img = draw(size)
        path = f"{out_dir}/icon_{size}x{size}.png"
        img.save(path)
        distinct = len(set(img.getdata()))
        print(f"wrote {path} ({distinct} distinct pixel values)")


if __name__ == "__main__":
    main()
