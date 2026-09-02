#!/usr/bin/env python3
"""Draw the watch icon at 30 and 60 px.

    python3 Spin/Resources/make_icon.py Spin/Resources

PANEL: the watch stores these as ABGR2222 -- two bits a channel, so four
shades and four alpha levels. Every decision below is a concession to that, and
the output is quantised here rather than left for the converter, because an
antialiased edge on a four-level alpha channel becomes a dashed line. The
previous icon carried 335 distinct pixel values and speckled on the glass; this
one carries nine. Falsified by a different panel format; re-measure by counting
distinct values in the PNG.

COLOUR follows the SDK's own Cycling icon: teal for the subject, light grey for
the apparatus. Both survive truncation exactly -- 128 >> 6 is 2, so teal lands
on (0,170,170), and 192 >> 6 is 3, so grey lands on white. Nothing dithers.

The two tones are also what makes the drawing legible at 30 px: the frame
crosses in front of the flywheel, and separating them by colour costs no pixels
where separating them by geometry would cost several.

WHAT MAKES IT A SPIN BIKE, since an earlier version lost it: the crank is its
own circle at the bottom bracket, level with the flywheel and driven by a belt.
Pedals at a big front wheel's hub is a penny-farthing, which is what this drew
for several releases. The flywheel is low and forward, the frame reaches the
floor at both ends, and the bars turn up at the front.
"""
from PIL import Image, ImageDraw
import sys

TEAL = (0, 128, 128, 255)
GREY = (192, 192, 192, 255)
SS = 8  # supersample; the curves need it, the straight runs do not care

# Normalised geometry, 0..1 across the icon, y downward.
FLOOR = 0.900
AXLE_Y = 0.700   # the crank and the flywheel share it, as on a real bike
BB = (0.355, AXLE_Y)
WHEEL = (0.635, AXLE_Y)
WHEEL_R = 0.158
CRANK_R = 0.060
BAR_Y = 0.215

# Per size: stroke weight, and whether the fiddly bits are drawn at all.
DESIGNS = {
    60: dict(stroke=4, detail=True),
    30: dict(stroke=2, detail=False),
}


def draw(size):
    d = DESIGNS[size]
    S = size * SS
    w = d["stroke"] * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    g = ImageDraw.Draw(img)

    def px(x, y):
        """Normalised (0..1) to supersampled pixels."""
        return (x * S, y * S)

    def line(a, b, colour, width=None):
        ww = width or w
        g.line([px(*a), px(*b)], fill=colour, width=ww)
        for p in (a, b):   # round the ends, so diagonals meet cleanly
            r = ww / 2.0
            cx, cy = px(*p)
            g.ellipse([cx - r, cy - r, cx + r, cy + r], fill=colour)

    def disc(centre, r, colour):
        cx, cy = px(*centre)
        rr = r * S
        g.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=colour)

    # The belt, drawn first so the crank and flywheel sit on top of it.
    if d["detail"]:
        line(BB, WHEEL, TEAL, width=int(w * 0.45))

    disc(WHEEL, WHEEL_R, TEAL)

    # Two feet with floor between them: one continuous bar reads as the ground
    # rather than as something the machine stands on.
    line((0.05, FLOOR), (0.40, FLOOR), GREY)
    line((0.60, FLOOR), (0.95, FLOOR), GREY)

    line(BB, (0.665, 0.265), GREY)              # top tube, up to the stem
    line((0.67, 0.265), (0.815, FLOOR), GREY)   # front leg, down to its foot
    line(BB, (0.225, FLOOR), GREY)              # rear leg, down to its foot
    line((0.35, AXLE_Y), (0.245, 0.395), GREY)  # seat tube
    line((0.115, 0.375), (0.355, 0.375), GREY)  # saddle
    line((0.505, BAR_Y), (0.815, BAR_Y), GREY)  # bars
    line((0.80, BAR_Y), (0.845, 0.105), GREY)   # the forward grip, turning up

    disc(BB, CRANK_R, TEAL)

    return quantise(img.resize((size, size), Image.LANCZOS))


def quantise(im):
    """Snap to the two colours and the panel's four alpha levels.

    Doing it here rather than letting the ABGR2222 converter do it is what
    keeps the edges solid: the converter has to guess at an antialiased pixel,
    and half its guesses land on a level that reads as a hole.
    """
    out = []
    for r, g_, b, a in im.getdata():
        if a < 32:
            out.append((0, 0, 0, 0))
            continue
        a = min(range(4), key=lambda i: abs(a - i * 85)) * 85
        colour = TEAL if (g_ - r) > 40 and b > 60 and r < 120 else GREY
        out.append((colour[0], colour[1], colour[2], a))
    im.putdata(out)
    return im


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    for size in DESIGNS:
        path = "%s/icon_%dx%d.png" % (out, size, size)
        icon = draw(size)
        icon.save(path)
        print("wrote %s (%d distinct pixel values)" % (path, len(set(icon.getdata()))))
