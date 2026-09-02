#!/usr/bin/env python3
"""Draw the store icon -- the one the phone app shows, not the watch's.

    python3 Spin/Resources/make_store_icon.py Spin/Resources

THIS IS NOT THE WATCH ICON. They are different files with different rules:

  * `icon_60x60.png` / `icon_30x30.png` are baked into the .uapp and converted
    to ABGR2222 -- two bits per channel, four shades, four alpha levels. See
    make_icon.py, where every decision is a concession to that.
  * `icon_store.png` is copied into the release package as `icon.png`, and the
    store re-hosts it as an ordinary PNG for the phone to display. Full colour,
    full alpha, any size. NONE of the watch's constraints apply, and this file
    is written to spend that freedom rather than imitate the watch: gradients,
    real blur, a glow, and 8-bit alpha everywhere.

So this one is drawn for a phone's app list: 512x512, the size app stores
conventionally want, full-bleed and square so it looks right whether or not the
store applies its own rounded mask.

What it shows is the app on the watch it runs on, using the app's own two
signatures: the ZONE DIAL around the rim -- the same 270-degree grey/blue/
green/amber/red ladder the riding screen draws, with the needle in zone 4 --
and the SPIN BIKE in the middle, the same geometry make_icon.py uses, so the
two icons are one drawing at two fidelities. The heart on the flywheel is the
app's only accent; nothing else on any of its screens is ever red.

No text: the two things this app draws are a clock and a heart rate, and
neither is legible at the size a phone shows an app list at.

Needs Pillow. The repository's toolchain image already has it, which is the
easiest place to run this:

    docker run --rm -v "$PWD:/apps" -w /apps <toolchain-image> \\
        python3 Spin/Resources/make_store_icon.py Spin/Resources
"""
from PIL import Image, ImageDraw, ImageFilter
import math
import sys

SIZE = 512
SS = 4  # supersample; the store icon can afford proper antialiasing

BG_TOP = (30, 35, 41)
BG_BOTTOM = (9, 11, 13)
PANEL = (5, 6, 7)
BEZEL = (58, 64, 70)
TEAL = (0, 150, 150)
TEAL_LIT = (64, 226, 220)
FRAME = (208, 214, 220)
FRAME_LO = (120, 128, 136)
HEART = (232, 58, 60)

# The riding screen's own ladder and geometry, unchanged.
ZONE_HUES = [(170, 170, 170), (0, 170, 255), (0, 255, 0), (255, 170, 0), (255, 0, 0)]
RING_START_DEG = -135.0
RING_SWEEP_DEG = 270.0
RING_GAP_DEG = 3.0
NEEDLE_ZONE = 4      # lit zone, counting from 1
SPOKES = 6

# make_icon.py's geometry, unchanged, in its own 0..1 space.
FLOOR = 0.900
AXLE_Y = 0.700
BB = (0.355, AXLE_Y)
WHEEL = (0.635, AXLE_Y)
WHEEL_R = 0.158
CRANK_R = 0.060
BAR_Y = 0.215

# Where that space lands: sized so the base's far corner clears the dial's
# inner edge. The dial's own opening is at the bottom, but the feet reach far
# enough sideways to meet the red segment before they reach the gap.
BIKE_W = 0.50
FIT = BIKE_W / 0.90
ORIGIN_X = 0.5 - BIKE_W / 2.0
ORIGIN_Y = 0.5 - (0.795 * FIT) / 2.0


def heart(d, cx, cy, w, colour):
    """A filled heart, from two discs and a triangle.

    The watch draws its heart from a hand-laid 15x13 bitmap, because at 15
    pixels a curve is a lie you place by hand. Here there is room for the real
    shape, so it is constructed rather than transcribed -- the two are the same
    heart at two resolutions, not one scaled copy of the other.
    """
    r = w / 4.0
    d.ellipse([cx - w / 2, cy - r, cx - w / 2 + 2 * r, cy + r], fill=colour)
    d.ellipse([cx + w / 2 - 2 * r, cy - r, cx + w / 2, cy + r], fill=colour)
    d.polygon([(cx - w / 2, cy), (cx + w / 2, cy), (cx, cy + w * 0.72)],
              fill=colour)


def radial(size, centre, r0, r1, inner, outer):
    """A radial gradient disc, for the glow behind the flywheel."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    steps = 48
    for i in range(steps, 0, -1):
        t = i / float(steps)
        r = r0 + (r1 - r0) * t
        col = tuple(int(inner[c] + (outer[c] - inner[c]) * t) for c in range(4))
        d.ellipse([centre[0] - r, centre[1] - r, centre[0] + r, centre[1] + r], fill=col)
    return img


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

    c = S / 2.0
    panel_r = S * 0.455   # fills the square, the way an app icon should

    # The panel, with a soft rim light so it reads as glass rather than a hole.
    img = img.convert("RGBA")
    rim = radial(S, (c, c), panel_r * 0.97, panel_r * 1.10,
                 (120, 200, 210, 0), (90, 170, 190, 70))
    img = Image.alpha_composite(img, rim)
    d = ImageDraw.Draw(img)
    d.ellipse([c - panel_r, c - panel_r, c + panel_r, c + panel_r],
              fill=PANEL, outline=BEZEL, width=int(S * 0.011))

    # -- The zone dial, on its own layer so it can be blurred into a glow ----
    ring = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    rd = ImageDraw.Draw(ring)
    r_out = panel_r * 0.93
    r_in = panel_r * 0.76
    seg = (RING_SWEEP_DEG - RING_GAP_DEG * (len(ZONE_HUES) - 1)) / len(ZONE_HUES)
    box = [c - r_out, c - r_out, c + r_out, c + r_out]
    for i, hue in enumerate(ZONE_HUES):
        a0 = RING_START_DEG + (seg + RING_GAP_DEG) * i
        lit = (i + 1) == NEEDLE_ZONE
        # The lit zone is full strength and thicker, exactly as the dial draws
        # it: two signals rather than one, brightness and weight.
        col = hue if lit else tuple(int(v * 0.50) for v in hue)
        w = int((r_out - r_in) * (1.0 if lit else 0.62))
        rd.arc([c - r_out, c - r_out, c + r_out, c + r_out],
               a0 - 90, a0 + seg - 90, fill=col + (255,), width=w)

    glow = ring.filter(ImageFilter.GaussianBlur(S * 0.020))
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, glow)   # twice: the lit zone should bloom
    img = Image.alpha_composite(img, ring)
    d = ImageDraw.Draw(img)

    # The needle, where the riding screen puts it: across the lit zone.
    ndeg = RING_START_DEG + (seg + RING_GAP_DEG) * (NEEDLE_ZONE - 1) + seg * 0.55
    na = math.radians(ndeg)
    n0, n1 = r_in * 0.99, r_out * 1.02
    d.line([(c + n0 * math.sin(na), c - n0 * math.cos(na)),
            (c + n1 * math.sin(na), c - n1 * math.cos(na))],
           fill=(255, 255, 255, 255), width=int(S * 0.013))

    def px(p):
        """Bike space to supersampled pixels."""
        x, y = p
        return ((ORIGIN_X + (x - 0.05) * FIT) * S,
                (ORIGIN_Y + (y - 0.105) * FIT) * S)

    stroke = int(S * 0.017)
    wcx, wcy = px(WHEEL)
    wr = WHEEL_R * FIT * S

    # A bloom behind the flywheel: the one part of the machine that is moving.
    img = Image.alpha_composite(
        img, radial(S, (wcx, wcy), wr * 0.7, wr * 2.3, (40, 200, 195, 130), (0, 120, 120, 0)))
    d = ImageDraw.Draw(img)

    def line(a, b, colour, width=None):
        w = width or stroke
        d.line([px(a), px(b)], fill=colour, width=w)
        for p in (a, b):     # round the ends so the joints meet cleanly
            cx, cy = px(p)
            r = w / 2.0
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=colour)

    # The frame, drawn twice: a darker pass offset down for depth, then the
    # light pass on top. A flat silhouette looks printed; this looks lit.
    for dy, colour in ((stroke * 0.30, FRAME_LO + (255,)), (0, FRAME + (255,))):
        def q(p, dy=dy):
            x, y = px(p)
            return (x, y + dy)
        for a, b in (((0.05, FLOOR), (0.40, FLOOR)),
                     ((0.60, FLOOR), (0.95, FLOOR)),
                     (BB, (0.665, 0.265)),
                     ((0.67, 0.265), (0.815, FLOOR)),
                     (BB, (0.225, FLOOR)),
                     ((0.35, AXLE_Y), (0.245, 0.395)),
                     ((0.505, BAR_Y), (0.815, BAR_Y)),
                     ((0.80, BAR_Y), (0.845, 0.105))):
            d.line([q(a), q(b)], fill=colour, width=stroke)
            for p in (a, b):
                cx, cy = q(p)
                r = stroke / 2.0
                d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=colour)
        # The saddle: a wedge, nose forward, rather than the watch's plain bar.
        d.polygon([q((0.115, 0.383)), q((0.355, 0.362)),
                   q((0.355, 0.392)), q((0.135, 0.408))], fill=colour)

    # The belt, then the flywheel over it.
    d.line([px(BB), px(WHEEL)], fill=TEAL + (255,), width=int(stroke * 0.5))

    # The flywheel: a lit rim, spokes, and a hub. The watch draws a solid disc
    # because at 30 px a spoke is sub-pixel; here there is room for the real one.
    rim_w = int(S * 0.015)
    d.ellipse([wcx - wr, wcy - wr, wcx + wr, wcy + wr],
              outline=TEAL_LIT + (255,), width=rim_w)
    inner = wr - rim_w / 2.0
    hub = S * 0.020
    for i in range(SPOKES):
        a = 2.0 * math.pi * i / SPOKES
        d.line([(wcx + hub * math.sin(a), wcy - hub * math.cos(a)),
                (wcx + inner * math.sin(a), wcy - inner * math.cos(a))],
               fill=TEAL + (255,), width=int(S * 0.010))
    d.ellipse([wcx - hub, wcy - hub, wcx + hub, wcy + hub], fill=TEAL_LIT + (255,))

    ccx, ccy = px(BB)
    cr = CRANK_R * FIT * S
    d.ellipse([ccx - cr, ccy - cr, ccx + cr, ccy + cr], fill=TEAL_LIT + (255,))

    # The heart on the hub, with its own small bloom.
    img = Image.alpha_composite(
        img, radial(S, (wcx, wcy), S * 0.02, S * 0.075, (255, 90, 90, 120), (200, 40, 40, 0)))
    d = ImageDraw.Draw(img)
    heart(d, wcx, wcy - S * 0.005, S * 0.062, HEART + (255,))

    return img.convert("RGB").resize((SIZE, SIZE), Image.LANCZOS)


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    path = "%s/icon_store.png" % out_dir
    draw().save(path)
    print("wrote %s (%dx%d)" % (path, SIZE, SIZE))

