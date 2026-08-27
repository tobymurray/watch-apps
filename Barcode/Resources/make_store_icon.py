#!/usr/bin/env python3
"""Draw the store icon -- the one the phone app shows, not the watch's.

    python3 Barcode/Resources/make_store_icon.py Barcode/Resources

THIS IS NOT THE WATCH ICON. They are different files with different rules:

  * `icon_60x60.png` / `icon_30x30.png` are baked into the .uapp and converted
    to ABGR2222 -- two bits per channel, four shades, four alpha levels. See
    make_icon.py, where every decision is a concession to that.
  * `icon_store.png` is copied into the package as `icon.png`, and the store
    re-hosts it as an ordinary PNG for the phone to display. Full colour, full
    alpha, any size. None of the watch's constraints apply.

So this one is drawn for a phone's app list: 512x512, the size app stores
conventionally want, full-bleed and square so it looks right whether or not
the store applies its own rounded mask.

What it shows is what the app shows: a white barcode card on the watch's black
screen, with the ID underneath in the same typeface the app draws it in.
"""
from PIL import Image, ImageDraw, ImageFont
import os
import sys

SIZE = 512
SS = 4  # supersample; the store icon can afford proper antialiasing

BG_TOP = (26, 30, 34)
BG_BOTTOM = (12, 14, 16)
CARD = (255, 255, 255)
BAR = (17, 17, 17)
TEXT = (60, 64, 68)

FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    "..", "Software", "Apps", "TouchGFX-GUI",
                    "assets", "fonts", "Poppins-Medium.ttf")

# Alternating bar/gap widths, starting and ending with a bar. Wider and more
# numerous than the watch icon's: at this size a sparse pattern looks like a
# fence, not a barcode.
PATTERN = [6, 4, 2, 4, 8, 4, 4, 4, 2, 6, 6, 4, 4, 4, 2, 4, 8, 4, 4, 4, 2, 4, 6,
           4, 4, 4, 8, 4, 2, 4, 6]

CAPTION = "A1234567"


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

    # The card, proportioned like a barcode label rather than a square, and
    # sized to fill the icon: in a phone's app list the ground is just padding,
    # so the card wants most of the frame.
    card_w, card_h = int(S * 0.78), int(S * 0.60)
    card_x = (S - card_w) // 2
    card_y = (S - card_h) // 2
    radius = int(S * 0.045)

    # A soft shadow, so the card sits on the ground instead of floating flat.
    shadow = Image.new("L", (S, S), 0)
    ImageDraw.Draw(shadow).rounded_rectangle(
        [card_x, card_y + int(S * 0.012), card_x + card_w,
         card_y + card_h + int(S * 0.012)], radius=radius, fill=90)
    img = Image.composite(Image.new("RGB", (S, S), (0, 0, 0)), img, shadow)
    d = ImageDraw.Draw(img)

    d.rounded_rectangle([card_x, card_y, card_x + card_w, card_y + card_h],
                        radius=radius, fill=CARD)

    # Bars: a quiet zone either side, and room beneath for the number.
    quiet = int(card_w * 0.085)
    left = card_x + quiet
    right = card_x + card_w - quiet
    field = right - left

    top = card_y + int(card_h * 0.13)
    bottom = card_y + int(card_h * 0.63)

    unit = field / float(sum(PATTERN))
    x, is_bar = float(left), True
    for w in PATTERN:
        if is_bar:
            d.rectangle([round(x), top, round(x + w * unit), bottom], fill=BAR)
        x += w * unit
        is_bar = not is_bar

    # The number, in the app's own typeface.
    try:
        # Small enough that it stays subordinate to the bars: below about
        # 64px the number stops being readable anyway, and an oversized one
        # just makes a grey band across the icon.
        font = ImageFont.truetype(FONT, int(card_h * 0.16))
    except OSError:
        font = ImageFont.load_default()
    box = d.textbbox((0, 0), CAPTION, font=font)
    d.text((S // 2 - (box[2] - box[0]) // 2,
            bottom + int(card_h * 0.09) - box[1]),
           CAPTION, font=font, fill=TEXT)

    return img.resize((SIZE, SIZE), Image.LANCZOS)


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    path = "%s/icon_store.png" % out_dir
    draw().save(path)
    print("wrote %s (%dx%d)" % (path, SIZE, SIZE))
