#!/usr/bin/env python3
"""Draw the Spin app icon at both sizes the watch wants.

    python3 Spin/Resources/make_icon.py Spin/Resources

A spin bike in side view. A flywheel alone could be any wheel; the whole
machine says stationary bike and nothing else.

WHY THIS IS A SCRIPT AND NOT A PNG SOMEBODY DREW

The watch does not store the PNG. `app_merging.py` converts it to ABGR2222 --
two bits per channel, taken by truncating the top two bits -- so the icon has
four alpha levels and four shades and nothing else. Two consequences drive the
whole construction:

  * **A stroke thinner than a pixel downsamples to grey, and grey truncates to
    a muddy mid-tone.** This is the failure that decides the design. Ordinary
    line-art icons of a spin bike are drawn with a uniform hairline and a lot
    of separate parts -- crank arm, pedal, seat rails, the gap between the
    frame's two tubes. At 30 px every one of those is sub-pixel, and what comes
    out is a grey smudge in the shape of a bike.
  * So the two sizes are **not the same drawing scaled**. 60 px can afford the
    frame's diagonals, the crank and a suggestion of pedal. 30 px keeps only
    what survives at two pixels wide: flywheel, base, the two posts, seat and
    bars. Both are drawn at 8x and downsampled so the curves have some shape.

Everything is stroked in one weight per size and nothing is left open, because
a shape that reads at 30 px is a silhouette, not a diagram.

Needs Pillow. The repository's toolchain image has it:

    docker run --rm -v "$PWD:/apps" -w /apps <toolchain-image> \\
        python3 Spin/Resources/make_icon.py Spin/Resources
"""
from PIL import Image, ImageDraw
import sys

TEAL = (0, 128, 128, 255)
SS = 8  # supersample; the curves need it, the straight runs do not care

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

    def line(a, b, width=None):
        g.line([px(*a), px(*b)], fill=TEAL, width=width or w)

    def circle(centre, r, width=None, fill=None):
        cx, cy = px(*centre)
        rr = r * S
        box = [cx - rr, cy - rr, cx + rr, cy + rr]
        if fill:
            g.ellipse(box, fill=TEAL)
        else:
            g.ellipse(box, outline=TEAL, width=width or w)

    # WHAT MAKES A SPIN BIKE A SPIN BIKE, at a glance and at this size: one
    # big wheel where a road bike has two, and a base where it has a back
    # wheel. Those two carry the recognition, so they get the space; the frame
    # is what is left over.

    # The flywheel, front and dominant.
    circle((0.62, 0.56), 0.235)

    # The base, foot to foot.
    line((0.10, 0.91), (0.90, 0.91))

    # Seat post, leaning back to the base, with the saddle across its top.
    line((0.31, 0.91), (0.22, 0.33))
    line((0.11, 0.31), (0.33, 0.31))

    # Handlebar post. It rises out of the flywheel rather than from the base,
    # which is both where it sits on the machine and one less line crossing
    # the wheel.
    line((0.66, 0.62), (0.71, 0.19))
    line((0.58, 0.17), (0.84, 0.17))

    if d["detail"]:
        # 60 px can afford the top tube and the crank. At 30 px both land
        # between pixels and fill the middle of the bike with grey.
        line((0.245, 0.44), (0.69, 0.36), width=int(w * 0.75))
        # The hub, at the wheel's centre. It was a crank down at the frame,
        # which at this size read as a blob stuck to the rim.
        circle((0.62, 0.56), 0.045, fill=True)

    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    for size in DESIGNS:
        path = "%s/icon_%dx%d.png" % (out, size, size)
        draw(size).save(path)
        print("wrote %s" % path)
