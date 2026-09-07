#!/usr/bin/env python3
"""Draw the UnitToggle app icons: the two units named, one above the other.

    python3 UnitToggle/Resources/make_icon.py UnitToggle/Resources

Pictures of measuring do not survive this scale. A dual-scale ruler -- ticks on
both edges at two spacings -- reads as a fence; a bar with notches cut into it
reads as piano keys; and a single baseline-and-ticks ruler, which does read at
60px, collapses into a letter W at 30px. Two bars divided four ways and three
ways read as a progress bar. All four were rendered at true size and quantised
before being discarded.

Naming the units works where drawing them does not, because two short words in
a bold face are big shapes. At 30px the caps stand 7-8 px with strokes 1-3 px
solid, and the row-by-row ink census in `report` is what checks none of that
is lost to ABGR2222 -- two bits a channel, four levels, which `app_merging.py`
reduces this to.

Both words are bright. An earlier version set MI in mid grey to mark metric as
the chosen one; at 30px that grey is what made it mushy, and an icon has no
business claiming which unit is set anyway -- the setting changes, the icon
does not. The 60px size adds a divider rule between them, which is the one
piece of detail that size can carry and this one cannot.

Poppins SemiBold is the app's own face, from TextKit/Fonts.
"""
from PIL import Image, ImageDraw, ImageFont
import os
import sys

SS = 16  # supersample factor

CARD_BG = (10, 10, 10, 255)    # near-black, matches the app's own ground
TOP = (0, 170, 255, 255)       # quantises exactly, and is nobody else's colour here
BOTTOM = (255, 255, 255, 255)
RULE = (85, 85, 85, 255)

FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    "..", "..", "TextKit", "Fonts", "Poppins-SemiBold.ttf")

TOP_WORD = "KM"
BOTTOM_WORD = "MI"

# Per size: the card, how large the words are, and whether that size can carry
# the divider between them.
DESIGNS = {
    60: dict(card_inset=3, card_radius=12, scale=0.30, top_cy=0.31, bottom_cy=0.70,
             with_rule=True),
    30: dict(card_inset=2, card_radius=6, scale=0.34, top_cy=0.31, bottom_cy=0.70,
             with_rule=False),
}


def centred(pen, text, face, cx, cy, colour):
    left, top, right, bottom = pen.textbbox((0, 0), text, font=face)
    pen.text((cx - (right - left) / 2 - left, cy - (bottom - top) / 2 - top),
             text, font=face, fill=colour)


def draw(size):
    d = DESIGNS[size]
    S = size * SS

    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pen = ImageDraw.Draw(img)

    ci = d["card_inset"] * SS
    pen.rounded_rectangle([ci, ci, S - 1 - ci, S - 1 - ci],
                          radius=d["card_radius"] * SS, fill=CARD_BG)

    face = ImageFont.truetype(FONT, int(d["scale"] * S))
    centred(pen, TOP_WORD, face, S / 2, d["top_cy"] * S, TOP)
    centred(pen, BOTTOM_WORD, face, S / 2, d["bottom_cy"] * S, BOTTOM)

    if d["with_rule"]:
        pen.rectangle([0.24 * S, 0.495 * S, 0.76 * S, 0.515 * S], fill=RULE)

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
    """Counts the ink each word keeps after quantisation, and prints the rows so
    a shape that has gone mushy is visible rather than merely counted. Glyphs
    are strokes, not fills, so the spot-checks a solid shape gets would prove
    nothing here; what matters is that both words survive as several rows of
    connected pixels."""
    q = quantise(img)
    px = q.load()

    rows = []
    for y in range(size):
        ink = "".join("#" if sum(px[x, y][:3]) > 200 else "." for x in range(size))
        if "#" in ink:
            rows.append((y, ink))

    if not rows:
        print("%dx%d: NOTHING SURVIVED QUANTISATION" % (size, size))
        return

    # Each word is a run of inked rows; the divider, where a size draws one, is
    # a band of its own between them.
    gaps = [i for i in range(1, len(rows)) if rows[i][0] != rows[i - 1][0] + 1]
    bounds = [0] + gaps + [len(rows)]
    bands = [rows[bounds[i]:bounds[i + 1]] for i in range(len(bounds) - 1)]
    expected = 3 if DESIGNS[size]["with_rule"] else 2
    word_bands = sorted((len(b) for b in bands), reverse=True)[:2]

    ok = len(bands) == expected and min(word_bands) >= 5
    print("%dx%d: %d inked bands (expected %d), words %d and %d rows tall -- %s"
          % (size, size, len(bands), expected, word_bands[0], word_bands[1],
             "clean" if ok else "CHECK THIS BY EYE"))
    for y, ink in rows:
        print("        y=%2d  %s" % (y, ink))


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    for size in (60, 30):
        icon = draw(size)
        icon.save("%s/icon_%dx%d.png" % (out_dir, size, size))
        report(size, icon)
