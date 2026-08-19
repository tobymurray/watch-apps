#!/usr/bin/env python3
"""Draw the two glance icons, so they can be reviewed as code rather than as blobs.

A binary PNG in a pull request is a wall nobody can see over: "make the arrow a
bit bigger" is a comment on a file that cannot be diffed. The shapes are
therefore described here, in numbers somebody can argue with, and the PNGs are
regenerated from them.

    python3 -m venv .venv && .venv/bin/pip install pillow
    .venv/bin/python Tools/draw_icons.py                 # writes Resources/*.png
    python3 "$UNA_SDK/Utilities/Scripts/png2abgr2222/png2abgr2222.py" \
        --inputs Resources/icon_sunrise.png Resources/icon_sunset.png \
        -o Software/Libs/Header/Icons.h

The second step is the SDK's own converter, not one written here: the packing it
produces -- one byte per pixel, `(A<<6)|(B<<4)|(G<<2)|R`, two bits a channel --
is what the kernel's glance renderer reads, and reimplementing it would be
reimplementing the one part of this that has to match exactly.

Two bits a channel is why the palette below is so blunt. Every colour is
quantised to the top two bits, so 0xFF and 0xC0 are the same white and there are
four alpha levels in total; anything subtle -- a soft edge, a gradient, a thin
antialiased ray -- either disappears or turns into a hard step. The shapes are
drawn to survive that: solid fills, no antialiasing, nothing thinner than two
pixels.
"""

from pathlib import Path

from PIL import Image, ImageDraw

# The canvas. Small: it sits next to a time in a 30-pixel font, and an icon
# taller than the digits would read as the subject rather than the label. Not
# square, because the composition is not -- a square canvas left three empty
# rows above the sun, which is three rows of nothing shipped in every build.
WIDTH = 24
HEIGHT = 21

# Quantised deliberately -- these are the values that survive the top-two-bits
# packing, so what is drawn here is exactly what the watch will show rather than
# something that rounds to it.
AMBER = (255, 170, 0, 255)   # -> R 3, G 2, B 0   the sun
WHITE = (255, 255, 255, 255) # -> R 3, G 3, B 3   the arrow, which carries the meaning
GREY  = (170, 170, 170, 255) # -> R 2, G 2, B 2   the horizon, which is only context
CLEAR = (0, 0, 0, 0)

SUN_CENTRE = (12, 6)
SUN_RADIUS = 6
HORIZON_Y = 13
HORIZON_THICKNESS = 2
ARROW_HALF_WIDTH = 5
ARROW_TOP = 16
ARROW_BOTTOM = 20


def sun(draw: ImageDraw.ImageDraw) -> None:
    """A plain disc, resting on the horizon.

    No rays. They were drawn first and thrown away: at 24 pixels with no
    antialiasing to lean on, three two-pixel rays came out as lopsided blobs
    that merged with the disc, and the icon read as a smudge rather than as a
    sun. A disc sitting on a horizon line, with an arrow under it, is
    unambiguous without them -- and this icon is never seen alone, it is seen
    next to a time.
    """
    cx, cy = SUN_CENTRE
    draw.ellipse(
        [cx - SUN_RADIUS, cy - SUN_RADIUS, cx + SUN_RADIUS, cy + SUN_RADIUS],
        fill=AMBER,
    )


def horizon(draw: ImageDraw.ImageDraw) -> None:
    """The line the sun is crossing. Full width: it is the ground, not a dash."""
    # Grey rather than white: a full-width white bar out-shouts both the sun and
    # the arrow, and the horizon is the one element on here carrying no
    # information -- it is what makes the other two read as a sunrise.
    draw.rectangle(
        [0, HORIZON_Y, WIDTH - 1, HORIZON_Y + HORIZON_THICKNESS - 1],
        fill=GREY,
    )


def arrow(draw: ImageDraw.ImageDraw, up: bool) -> None:
    """The only difference between the two icons, and so the loudest thing on them.

    Below the horizon rather than over the sun: the sun is the subject, the
    direction is the verb, and a chevron drawn across the disc turns both into
    a scribble at this size.
    """
    cx = WIDTH // 2
    if up:
        points = [
            (cx, ARROW_TOP),
            (cx - ARROW_HALF_WIDTH, ARROW_BOTTOM),
            (cx + ARROW_HALF_WIDTH, ARROW_BOTTOM),
        ]
    else:
        points = [
            (cx, ARROW_BOTTOM),
            (cx - ARROW_HALF_WIDTH, ARROW_TOP),
            (cx + ARROW_HALF_WIDTH, ARROW_TOP),
        ]
    draw.polygon(points, fill=WHITE)


def icon(up: bool) -> Image.Image:
    image = Image.new("RGBA", (WIDTH, HEIGHT), CLEAR)
    draw = ImageDraw.Draw(image)
    sun(draw)
    horizon(draw)
    arrow(draw, up)
    return image


def preview(image: Image.Image) -> str:
    """An ASCII look at what was drawn, quantised the way the watch will see it."""
    rows = []
    for y in range(image.height):
        row = ""
        for x in range(image.width):
            r, g, b, a = image.getpixel((x, y))
            if (a >> 6) == 0:
                row += "."
            elif (b >> 6) == 0:
                row += "O"   # amber: no blue
            else:
                row += "#"   # white
        rows.append(row)
    return "\n".join(rows)


if __name__ == "__main__":
    here = Path(__file__).resolve().parent.parent
    for name, up in (("icon_sunrise", True), ("icon_sunset", False)):
        image = icon(up)
        path = here / "Resources" / f"{name}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        image.save(path)
        print(f"{path.relative_to(here)}  {image.width}x{image.height}")
        print(preview(image))
        print()
