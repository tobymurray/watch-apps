#!/usr/bin/env python3
"""Draw the Squash app icon at both sizes the watch wants.

    python3 Squash/Resources/make_icon.py Squash/Resources

A squash racquet and a ball. What makes it a *squash* racquet and not a tennis
one is the head: an asymmetric teardrop, broad and round at the top, tapering
to a point where the throat begins, on a shaft nearly as long again. A tennis
head is a near-round oval on a stub. Proportions measured off a HEAD squash
racquet photographed square-on:

    head length / total length = 400/850 = 0.47
    head width  / head length  = 270/400 = 0.675
    widest point               = upper third of the head, not the middle

The icon deliberately runs longer in the head than the photo -- 0.55 of total
length against 0.47, with a correspondingly shorter handle. At this size the head
is the whole of the recognition and the grip is only a stick to hold it, so the
proportion that reads correctly is not the one that measures correctly.

That distinction survives 30 px, which was worth checking rather than assuming:
rendered side by side against an oval-headed racquet of the same weight and
tilt, the two are plainly different shapes at 30 px. An earlier version of this
file drew a symmetric ellipse and claimed the distinction was unreachable at
this size -- it was, but only because an ellipse is already the tennis shape.

WHY THIS IS A SCRIPT AND NOT A PNG SOMEBODY DREW

The watch does not store the PNG. `app_merging.py` converts it to ABGR2222 --
two bits per channel, taken by truncating the top two bits -- so the icon has
four alpha levels and four shades and nothing else. Two consequences drive the
whole construction:

  * **A stroke thinner than a pixel downsamples to grey, and grey truncates to
    a muddy mid-tone.** An ordinary line-art racquet is drawn with a hairline
    rim and a strung face. At 30 px the strings are all sub-pixel, and what
    comes out is a smear in the shape of a racquet. There are no strings here.
  * So the two sizes are **not the same drawing scaled**: they differ in stroke
    weight, and 30 px keeps only what survives at two pixels wide -- the head's
    rim, one shaft, one plain ball.

The double yellow dot marks the slowest competition ball, and is the one detail
that says squash rather than racquet sport. It is not drawn here at either
size. It needs the ball to be at least **12 px across** -- measured by sweeping
ball diameters from 6 to 20 px and looking at where two dots stop merging into
a bar -- and at BALL_DIAM the ball is 8.4 px at 60 and 4.2 px at 30. Carrying
the marking would mean a ball half again as large as the racquet wants, and a
smudged marking reads as damage, which is worse than no marking. icon_store.png
is 512 px and draws it properly.

The racquet is drawn upright and then rotated, because neither a stroked
teardrop nor a stroked ellipse can be drawn on a tilt directly -- and it is
drawn at 8x throughout so the rim keeps its shape through the rotation and the
downsample.

Needs Pillow. The repository's toolchain image has it:

    docker run --rm -v "$PWD:/apps" -w /apps <toolchain-image> \\
        python3 Squash/Resources/make_icon.py Squash/Resources
"""
from PIL import Image, ImageChops, ImageDraw
import math
import sys

TEAL = (0, 128, 128, 255)
CLEAR = (0, 0, 0, 0)
SS = 8      # supersample; the rim, the cusp and the rotation all need it
TILT = 32   # degrees clockwise, putting the head up and to the right

# Per size: stroke weight, and whether the fiddly bits are drawn at all.
DESIGNS = {
    60: dict(stroke=4, detail=True),
    30: dict(stroke=2, detail=False),
}

# Normalised geometry, from the measurements in the module docstring.
# HEAD_LEN is set from the handle, not the head: the handle should read as 45%
# of the overall length, and the eye puts its start where the head has narrowed
# to the shaft's own width -- about 0.05 above the cusp, because below that the
# two sides of the taper read as one stick. Measuring to the cusp instead gives
# 39%, which is the same racquet described against a junction nobody can see.
HEAD_TOP, HEAD_LEN, HEAD_HALFW = 0.055, 0.515, 0.132
SHAFT_END, BUTT = 0.705, 0.900

# A squash ball is 40 mm against a head some 180 mm across, so a ball drawn to
# scale would be under two pixels here and vanish. 0.14 is the compromise: the
# smallest that still resolves as round rather than as a square cluster of
# pixels at 30 px, where it is 4.2 px across.
BALL_DIAM = 0.14

# Where the ball sits, in the corner the rotated racquet leaves empty. Close
# enough to the head to read as one picture rather than two objects sharing a
# frame, far enough that they stay two shapes: measured closest approach is
# 4.1 px at 30 px and 9.0 px at 60 px, checked by flood-filling the rendered
# icon and confirming it still has exactly two components.
BALL_POS = (0.245, 0.295)

# The exponent in the head curve, which is what sets how square the shoulders
# look: the widest point of x = sin(t)*sin^m(t/2) sits at y = -m/(m+2), so
# m = 2 reaches full width only a quarter of the way down and reads flat-
# topped, while m = 1 holds it back to a third and rounds the shoulders off.
HEAD_ROUNDNESS = 1.0

# Normalising by the curve's own maximum makes HEAD_HALFW the true half-width
# rather than a scale factor. Computed rather than written down, so it cannot
# drift out of step with HEAD_ROUNDNESS above.
TEARDROP_XMAX = max(
    math.sin(2.0 * math.pi * i / 4000) *
    (math.sin(math.pi * i / 4000) ** HEAD_ROUNDNESS)
    for i in range(1, 4000))


def ring(pts, width):
    """Outer and inner edges of a closed path, offset along its own normals.

    Stroking the outline with ImageDraw.line puts a rounded joint at every
    vertex, which at a few hundred vertices beads into a serrated rim. Filling
    the band between two offset copies has no joints to show.
    """
    n = len(pts)
    outer, inner = [], []
    for i in range(n):
        ax, ay = pts[(i - 1) % n]
        bx, by = pts[(i + 1) % n]
        tx, ty = bx - ax, by - ay
        length = math.hypot(tx, ty) or 1.0
        nx, ny = -ty / length, tx / length
        x, y = pts[i]
        outer.append((x + nx * width / 2.0, y + ny * width / 2.0))
        inner.append((x - nx * width / 2.0, y - ny * width / 2.0))
    return outer, inner


def fill_ring(img, pts, width, colour):
    """Paint the band between the two offsets of `pts`; return the outer edge."""
    outer, inner = ring(pts, width)
    band = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(band).polygon(outer, fill=colour)
    hole = Image.new("L", img.size, 0)
    ImageDraw.Draw(hole).polygon(inner, fill=255)
    band.paste((0, 0, 0, 0), (0, 0), hole)
    img.alpha_composite(band)
    return outer


def plug_cusp(img, silhouette, centre, radius, colour):
    """Fill the thin wedge at the cusp without adding to the outline.

    At the cusp the head's two normals point at each other, so the ring's inner
    edge crosses itself and leaves a sliver of thin coverage. A disc laid over
    the junction fills it, but a bare disc also bulges past the outline -- so it
    is masked by the silhouette of what has already been drawn, and can only
    paint where the racquet already is.
    """
    disc = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(disc).ellipse(
        [centre[0] - radius, centre[1] - radius,
         centre[0] + radius, centre[1] + radius], fill=colour)
    disc.putalpha(ImageChops.multiply(disc.getchannel("A"), silhouette))
    img.alpha_composite(disc)


def teardrop(n=400):
    """The head outline: rounded cap at t=pi, point at t=0."""
    pts = []
    for i in range(n + 1):
        t = 2.0 * math.pi * i / n
        x = (math.sin(t) * (math.sin(t / 2.0) ** HEAD_ROUNDNESS)
             / TEARDROP_XMAX)
        y = math.cos(t)
        pts.append((0.5 + x * HEAD_HALFW,
                    HEAD_TOP + (y + 1.0) / 2.0 * HEAD_LEN))
    return pts


def draw(size):
    d = DESIGNS[size]
    S = size * SS
    w = d["stroke"] * SS

    # The racquet is drawn upright on its own layer so it can be rotated whole.
    layer = Image.new("RGBA", (S, S), CLEAR)
    g = ImageDraw.Draw(layer)

    def line(a, b, width=None):
        g.line([(a[0] * S, a[1] * S), (b[0] * S, b[1] * S)],
               fill=TEAL, width=width or w)

    pts = [(x * S, y * S) for x, y in teardrop()[:-1]]
    outer = fill_ring(layer, pts, w, TEAL)

    # No separate throat: the teardrop has already tapered to a point, so the
    # shaft simply continues from the cusp. Drawing a V there as well puts two
    # sets of lines through the same few pixels and reads as a snarl.
    head_bottom = HEAD_TOP + HEAD_LEN
    shaft = ((0.50, head_bottom - 0.015), (0.50, SHAFT_END))
    # The grip, only just heavier than the shaft -- enough to read as a grip,
    # not so much that it reads as a magnifying glass handle.
    grip = ((0.50, SHAFT_END - 0.010), (0.50, BUTT))
    grip_w = int(w * 1.15)
    line(*shaft)
    line(*grip, width=grip_w)

    silhouette = Image.new("L", layer.size, 0)
    sd = ImageDraw.Draw(silhouette)
    sd.polygon(outer, fill=255)
    for (a, b), lw in ((shaft, w), (grip, grip_w)):
        sd.line([(a[0] * S, a[1] * S), (b[0] * S, b[1] * S)], fill=255, width=lw)
    plug_cusp(layer, silhouette, (0.50 * S, head_bottom * S), w * 1.2, TEAL)

    layer = layer.rotate(-TILT, resample=Image.BICUBIC, center=(S / 2.0, S / 2.0))
    img = Image.new("RGBA", (S, S), CLEAR)
    img.alpha_composite(layer)

    # The ball, in the corner the rotated racquet leaves empty. Filled, because
    # a small outlined circle is the first thing to disappear.
    g = ImageDraw.Draw(img)
    cx, cy = BALL_POS[0] * S, BALL_POS[1] * S
    r = BALL_DIAM / 2.0 * S
    g.ellipse([cx - r, cy - r, cx + r, cy + r], fill=TEAL)

    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    for size in DESIGNS:
        path = "%s/icon_%dx%d.png" % (out, size, size)
        draw(size).save(path)
        print("wrote %s" % path)
