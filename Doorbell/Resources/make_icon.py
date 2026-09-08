#!/usr/bin/env python3
"""Draw the Doorbell app icons: a push button, ring and centre.

    python3 Doorbell/Resources/make_icon.py Doorbell/Resources

Geometry only, no font. A bell drawn at 30px turns to mush: the clapper and the
flare need three tones the palette does not have to spare. A push button is two
concentric circles, which is the one shape that survives both sizes and the
quantiser, and it says press rather than alarm, which is what this app is.

Colours are pure channel extremes, so ABGR2222 quantises them exactly and the
30px version loses nothing the 60px one had except the gap width.
"""
from PIL import Image, ImageDraw
import os
import sys

SS = 16  # supersample factor

GROUND = (10, 10, 10, 255)
RING   = (255, 255, 255, 255)
CENTRE = (255, 0, 0, 255)


def draw(size):
    n = size * SS
    img = Image.new("RGBA", (n, n), GROUND)
    d = ImageDraw.Draw(img)

    # Ring stroke and gap both scale with the icon, so 30px is the 60px one
    # shrunk rather than a different drawing.
    outer = n * 0.44
    stroke = max(SS, int(n * 0.075))
    centre = n * 0.20

    mid = n / 2.0
    d.ellipse([mid - outer, mid - outer, mid + outer, mid + outer],
              outline=RING, width=stroke)
    d.ellipse([mid - centre, mid - centre, mid + centre, mid + centre], fill=CENTRE)

    return img.resize((size, size), Image.LANCZOS)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    for size, name in ((60, "icon_60x60.png"), (30, "icon_30x30.png")):
        path = os.path.join(out, name)
        draw(size).save(path)
        print("wrote", path)


if __name__ == "__main__":
    main()
