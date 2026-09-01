#!/usr/bin/env python3
"""Draw the store icon -- the one the phone app shows, not the watch's.

    python3 Spin/Resources/make_store_icon.py Spin/Resources

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
flywheel that is the watch icon, and the one red heart that is the app's only
accent. No text -- the two things this app draws are a clock and a heart rate,
and neither is legible at the size a phone shows an app list at.

Needs Pillow. The repository's toolchain image already has it, which is the
easiest place to run this:

    docker run --rm -v "$PWD:/apps" -w /apps <toolchain-image> \\
        python3 Spin/Resources/make_store_icon.py Spin/Resources
"""
from PIL import Image, ImageDraw
import math
import sys

SIZE = 512
SS = 4  # supersample; the store icon can afford proper antialiasing

BG_TOP = (26, 30, 34)
BG_BOTTOM = (12, 14, 16)
PANEL = (0, 0, 0)
BEZEL = (44, 48, 52)
TEAL = (0, 128, 128)
HEART = (214, 48, 49)

SPOKES = 6


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

    # The panel: round like the watch's, and black because that is what a
    # memory-in-pixel LCD shows where nothing is drawn.
    c = S / 2.0
    panel_r = S * 0.40
    d.ellipse([c - panel_r, c - panel_r, c + panel_r, c + panel_r],
              fill=PANEL, outline=BEZEL, width=int(S * 0.012))

    # The flywheel: a rim on its centre line, spokes from the hub to the inside
    # of the rim, and a filled hub.
    r = S * 0.255
    rim = S * 0.030
    hub = S * 0.045
    spoke = S * 0.024

    d.ellipse([c - r, c - r, c + r, c + r], outline=TEAL, width=int(rim))
    inner = r - rim / 2.0
    for i in range(SPOKES):
        a = 2.0 * math.pi * i / SPOKES
        d.line([(c + hub * math.sin(a), c - hub * math.cos(a)),
                (c + inner * math.sin(a), c - inner * math.cos(a))],
               fill=TEAL, width=int(spoke))
    d.ellipse([c - hub, c - hub, c + hub, c + hub], fill=TEAL)

    # The heart on the hub: the app's one accent, and the only thing on any of
    # its screens that is ever red.
    heart(d, c, c - S * 0.018, S * 0.105, HEART)

    return img.resize((SIZE, SIZE), Image.LANCZOS)


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    path = "%s/icon_store.png" % out_dir
    draw().save(path)
    print("wrote %s (%dx%d)" % (path, SIZE, SIZE))
