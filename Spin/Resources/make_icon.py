#!/usr/bin/env python3
"""Draw the Spin app icon at both sizes the watch wants.

    python3 Spin/Resources/make_spin_icon.py Spin/Resources

A flywheel: the one part of a stationary bike that is unmistakably a stationary
bike at 30 pixels. Teal on transparent, matching the SDK's own activity icons.

WHY THIS IS A SCRIPT AND NOT A PNG SOMEBODY DREW

The watch does not store the PNG. `app_merging.py` converts it to ABGR2222 --
two bits per channel, taken by truncating the top two bits -- so the icon has
four alpha levels and four shades and nothing else. A ring and six spokes have
no fine detail to lose to that, but they do need the curve smoothed, so the
whole thing is drawn at 8x and downsampled: four alpha levels is not much, and
it is the difference between a ring that reads as round and one that reads as
a polygon.

Each size gets its own stroke weights rather than one scaled design. At 30px a
scaled 60px spoke lands sub-pixel and comes back as a grey smear.

Needs ImageMagick 7 (`magick`) on PATH; no Python imaging dependency.
"""
import math
import subprocess
import sys

TEAL = "rgb(0,128,128)"
SS = 8          # supersample factor, for the ring's curve
SPOKES = 6

#            outer  ring  hub  spoke
DESIGNS = {
    60: dict(outer=26, ring=5, hub=5, spoke=4),
    30: dict(outer=13, ring=3, hub=2, spoke=2),
}


def draw(size, out_dir):
    d = DESIGNS[size]
    s = size * SS
    c = s / 2.0
    r = d["outer"] * SS
    ring = d["ring"] * SS
    hub = d["hub"] * SS
    spoke = d["spoke"] * SS

    args = ["magick", "-size", f"{s}x{s}", "xc:none", "-stroke", TEAL, "-fill", "none"]

    # The rim, stroked on its centre line so `outer` is the mid-radius.
    args += ["-strokewidth", str(ring),
             "-draw", f"circle {c},{c} {c},{c - r}"]

    # Spokes, from the hub to the inside of the rim. Endpoints are computed
    # here rather than with -draw's `rotate`, which rotates the whole canvas
    # about its origin and throws every spoke but the first off the image.
    args += ["-strokewidth", str(spoke)]
    inner = r - ring / 2.0
    for i in range(SPOKES):
        a = 2.0 * math.pi * i / SPOKES
        x0, y0 = c + hub * math.sin(a), c - hub * math.cos(a)
        x1, y1 = c + inner * math.sin(a), c - inner * math.cos(a)
        args += ["-draw", f"line {x0:.2f},{y0:.2f} {x1:.2f},{y1:.2f}"]

    # The hub, filled and unstroked so it does not grow by half a stroke.
    args += ["-stroke", "none", "-fill", TEAL,
             "-draw", f"circle {c},{c} {c},{c - hub}"]

    path = f"{out_dir}/icon_{size}x{size}.png"
    # PNG32: forces straight RGBA out. Without it ImageMagick notices the image
    # has few colours and writes a palette PNG, which is a different file for
    # the packer to read than every other icon in this repository.
    args += ["-resize", f"{size}x{size}", f"PNG32:{path}"]
    subprocess.run(args, check=True)
    print(f"wrote {path}")


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    for size in DESIGNS:
        draw(size, out)
