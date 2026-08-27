#!/usr/bin/env python3
"""Draw the Barcode app icon at both sizes the watch wants.

    python3 Barcode/Resources/make_icon.py Barcode/Resources

WHY THIS IS A SCRIPT AND NOT A PNG SOMEBODY DREW

The watch does not store the PNG. `app_merging.py` converts it to **ABGR2222**
-- one byte per pixel, two bits per channel, taken by truncating the top two
bits -- so the icon has four alpha levels and four shades, and nothing else.
Two consequences drive the whole construction:

  * A bar narrower than a whole pixel downsamples to grey, and grey truncates
    to a muddy mid-tone instead of black. So the bars are drawn at 1x on
    integer pixel boundaries and never resampled. The first version of this
    icon was scaled from one design and the 30px bars came out grey.
  * The rounded corner is the one place antialiasing is wanted, so the card
    alone is drawn at 16x and downsampled. Four alpha levels is not much, but
    it is enough to take the hard corner off.

Each size therefore gets its own bar pattern rather than one scaled design: at
30px a scaled pattern is sub-pixel everywhere.

The old icon was 46px of bars in a 60px square with a 4px gap on the left and
10px on the right, which read as lop-sided because it was.
"""
from PIL import Image, ImageDraw
import sys

SS = 16  # supersample factor for the card's corners

# Per size: the margins, the bevel, and alternating bar/gap widths starting and
# ending with a bar. Each pattern sums exactly to the bar field its margins
# leave, which the assert below enforces -- the icon is symmetric by
# construction rather than by eye.
DESIGNS = {
    60: dict(inset=2, radius=5, quiet=7, vmargin=7,
             pattern=[3, 2, 1, 2, 4, 2, 2, 2, 1, 3, 3, 2, 2, 2, 3, 2, 1, 2, 3]),
    30: dict(inset=1, radius=3, quiet=4, vmargin=4,
             pattern=[2, 1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2]),
}


def draw(size):
    d = DESIGNS[size]

    # The card, supersampled so the bevel has some shape to it.
    S = size * SS
    card = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(card).rounded_rectangle(
        [d["inset"] * SS, d["inset"] * SS,
         S - 1 - d["inset"] * SS, S - 1 - d["inset"] * SS],
        radius=d["radius"] * SS, fill=(255, 255, 255, 255))
    img = card.resize((size, size), Image.LANCZOS)

    # The bars, at 1x on whole pixels so every one survives quantisation solid.
    left = d["inset"] + d["quiet"]
    right = size - d["inset"] - d["quiet"]
    assert sum(d["pattern"]) == right - left, (
        "%dpx: pattern sums to %d, the bar field is %d"
        % (size, sum(d["pattern"]), right - left))

    top = d["inset"] + d["vmargin"]
    bottom = size - d["inset"] - d["vmargin"]

    pen = ImageDraw.Draw(img)
    x, is_bar = left, True
    for w in d["pattern"]:
        if is_bar:
            pen.rectangle([x, top, x + w - 1, bottom - 1], fill=(0, 0, 0, 255))
        x += w
        is_bar = not is_bar

    return img


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
    """Confirm the bars survive quantisation black, and the icon is centred."""
    q = quantise(img)
    px = q.load()
    greys = set()
    for y in range(size):
        for x in range(size):
            r, g, b, a = px[x, y]
            if a == 255 and r not in (0, 255):
                greys.add(r)

    opaque_cols = [x for x in range(size)
                   if any(px[x, y][3] == 255 and px[x, y][0] == 0
                          for y in range(size))]
    left_gap = opaque_cols[0]
    right_gap = size - 1 - opaque_cols[-1]

    print("%dx%d: bar margins %d left / %d right%s; muddy shades: %s"
          % (size, size, left_gap, right_gap,
             "" if left_gap == right_gap else "  <-- NOT CENTRED",
             sorted(greys) or "none"))


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    for size in (60, 30):
        icon = draw(size)
        icon.save("%s/icon_%dx%d.png" % (out_dir, size, size))
        report(size, icon)
