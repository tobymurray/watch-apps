#!/usr/bin/env python3
"""Draw the store icon -- the one the phone app shows, not the watch's.

    python3 Squash/Resources/make_store_icon.py Squash/Resources

THIS IS NOT THE WATCH ICON. They are different files with different rules:

  * `icon_60x60.png` / `icon_30x30.png` are baked into the .uapp and converted
    to ABGR2222 -- two bits per channel, four shades, four alpha levels. See
    make_icon.py, where every decision is a concession to that.
  * `icon_store.png` is copied into the release package as `icon.png`, and the
    store re-hosts it as an ordinary PNG for the phone to display. Full colour,
    full alpha, any size. None of the watch's constraints apply.

So this one is drawn for a phone's app list: 512x512, the size app stores
conventionally want, full-bleed and square so it looks right whether or not the
store applies its own rounded mask.

What it shows is the app on the watch it runs on: the round black panel, the
racquet that is the watch icon, and the ball. Here the ball is the squash ball
it actually is -- black, and carrying the two yellow dots that mark the slowest
competition speed. That marking is the one detail that says squash rather than
racquet sport, and 512 px is the first size with room for it. No text: the two
things this app draws are a clock and a heart rate, and neither is legible at
the size a phone shows an app list at.

Needs Pillow. The repository's toolchain image already has it, which is the
easiest place to run this:

    docker run --rm -v "$PWD:/apps" -w /apps <toolchain-image> \\
        python3 Squash/Resources/make_store_icon.py Squash/Resources
"""
from PIL import Image, ImageChops, ImageDraw
import math
import sys

SIZE = 512
SS = 4  # supersample; the store icon can afford proper antialiasing

BG_TOP = (26, 30, 34)
BG_BOTTOM = (12, 14, 16)
PANEL = (0, 0, 0)
BEZEL = (44, 48, 52)
TEAL = (0, 128, 128)
BALL = (38, 38, 40)      # a squash ball is black; pure black vanishes on the panel
BALL_EDGE = (96, 100, 104)
DOT = (240, 200, 40)

TILT = 32  # degrees clockwise, matching the watch icon


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



def draw():
    S = SIZE * SS
    img = Image.new("RGB", (S, S), BG_BOTTOM)
    d = ImageDraw.Draw(img)

    # A quiet vertical gradient so the ground is not a flat slab.
    for y in range(S):
        t = y / float(S - 1)
        d.line([(0, y), (S, y)],
               fill=tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t)
                          for i in range(3)))

    # The panel: round, because the watch's is, and black because that is what
    # a memory-in-pixel LCD shows where nothing is drawn.
    c = S / 2.0
    panel_r = S * 0.40
    d.ellipse([c - panel_r, c - panel_r, c + panel_r, c + panel_r],
              fill=PANEL, outline=BEZEL, width=int(S * 0.012))

    # The racquet, same construction as the watch icon: drawn upright on its
    # own layer, then rotated, because a stroked ellipse cannot be tilted in
    # place. Kept smaller than the watch icon's so it sits inside the panel
    # rather than running off the edge of it.
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    g = ImageDraw.Draw(layer)
    w = int(S * 0.028)

    def line(a, b, width=None):
        g.line([(a[0] * S, a[1] * S), (b[0] * S, b[1] * S)],
               fill=TEAL + (255,), width=width or w)

    # The same teardrop as the watch icon, from the same measurements -- broad
    # and round at the top, cusped where the throat begins. See make_icon.py.
    top, hlen, halfw, m = 0.175, 0.410, 0.105, 1.0
    xmax = max(math.sin(2.0 * math.pi * i / 4000) *
               (math.sin(math.pi * i / 4000) ** m) for i in range(1, 4000))
    pts = []
    for i in range(400):
        t = 2.0 * math.pi * i / 400
        x = math.sin(t) * (math.sin(t / 2.0) ** m) / xmax
        y = math.cos(t)
        pts.append(((0.5 + x * halfw) * S, (top + (y + 1.0) / 2.0 * hlen) * S))
    outer = fill_ring(layer, pts, w, TEAL + (255,))

    # No separate throat: the teardrop has already tapered to a point, so the
    # shaft simply continues from it.
    shaft = ((0.50, top + hlen - 0.008), (0.50, 0.690))
    grip = ((0.50, 0.680), (0.50, 0.845))
    grip_w = int(w * 1.15)
    line(*shaft)
    line(*grip, width=grip_w)

    # The ring's inner edge crosses itself at the cusp and leaves a wedge of
    # thin coverage there. The disc that fills it is masked by the silhouette of
    # what is already drawn, so it cannot bulge past the outline.
    silhouette = Image.new("L", layer.size, 0)
    sd = ImageDraw.Draw(silhouette)
    sd.polygon(outer, fill=255)
    for (a, b), lw in ((shaft, w), (grip, grip_w)):
        sd.line([(a[0] * S, a[1] * S), (b[0] * S, b[1] * S)], fill=255, width=lw)
    plug_cusp(layer, silhouette, (0.50 * S, (top + hlen) * S), w * 1.2,
              TEAL + (255,))

    layer = layer.rotate(-TILT, resample=Image.BICUBIC, center=(c, c))
    img.paste(Image.alpha_composite(
        img.convert("RGBA"), layer).convert("RGB"), (0, 0))

    d = ImageDraw.Draw(img)

    # The ball, in the corner the rotated racquet leaves empty. Outlined,
    # because a black disc on a black panel is only a hole.
    # Same ball-to-head proportion as the watch icon, so the two read as one
    # design: BALL_DIAM 0.14 against a head 0.264 wide there, and 0.111 against
    # a head 0.21 wide here.
    bx, by = 0.360 * S, 0.378 * S
    br = S * 0.0555
    d.ellipse([bx - br, by - br, bx + br, by + br],
              fill=BALL, outline=BALL_EDGE, width=int(S * 0.006))

    # Two yellow dots, side by side: the double-yellow marking of the slowest
    # ball, which is the one squash is actually played with.
    dr = br * 0.24
    for sx in (-0.38, 0.38):
        dx = bx + br * sx
        d.ellipse([dx - dr, by - dr, dx + dr, by + dr], fill=DOT)

    return img.resize((SIZE, SIZE), Image.LANCZOS)


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    path = "%s/icon_store.png" % out_dir
    draw().save(path)
    print("wrote %s (%dx%d)" % (path, SIZE, SIZE))
