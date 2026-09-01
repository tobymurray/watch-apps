#!/usr/bin/env python3
"""Draw the store icon -- the one the phone app shows, not the watch's.

    python3 NotifyToggle/Resources/make_store_icon.py NotifyToggle/Resources

Not the watch icon. `icon_60x60.png` / `icon_30x30.png` (make_icon.py) get
quantised to ABGR2222 and baked into the .uapp; this file is copied into the
package as `icon.png` and shown by the phone app as an ordinary full-colour
PNG. Unlike Barcode's store icon, which is built from real simulator
captures because Barcode draws several different symbologies and a single
drawing would undersell the ones it left out, this app has exactly one
screen and no format choice, so the icon is the same vector pill-and-knob
shape make_icon.py draws, scaled up, rather than a screenshot.
"""
from PIL import Image, ImageDraw, ImageFilter
import numpy as np
import sys

SIZE = 512
SS = 4

BG_CENTER = (16, 46, 34)     # a dark green glow, echoing the pill's own colour
BG_EDGE = (7, 10, 9)
BEZEL = (234, 234, 231)
CARD_BG = (10, 10, 10, 255)
PILL_ON = (0, 230, 90, 255)
KNOB = (255, 255, 255, 255)


def draw_face(size):
    """The same shape make_icon.py draws: a black rounded card, a green pill,
    a white knob on the right (ON state)."""
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pen = ImageDraw.Draw(img)

    inset = int(size * 0.05) * SS
    pen.rounded_rectangle([inset, inset, S - 1 - inset, S - 1 - inset],
                           radius=int(size * 0.2) * SS, fill=CARD_BG)

    px0 = int(size * 0.18) * SS
    py0 = int(size * 0.34) * SS
    px1 = S - 1 - px0
    py1 = S - 1 - py0
    pen.rounded_rectangle([px0, py0, px1, py1], radius=(py1 - py0) // 2, fill=PILL_ON)

    kr = (py1 - py0) // 2 - int(0.06 * S)
    kcx = px1 - kr - int(0.02 * S)
    kcy = (py0 + py1) // 2
    pen.ellipse([kcx - kr, kcy - kr, kcx + kr, kcy + kr], fill=KNOB)

    return img.resize((size, size), Image.LANCZOS)


def draw():
    S = SIZE * SS
    cx = cy = S / 2.0

    yy, xx = np.mgrid[0:S, 0:S]
    dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / (S * 0.62)
    t = np.clip(dist, 0.0, 1.0)[..., None]
    center = np.array(BG_CENTER, dtype=np.float32)
    edge = np.array(BG_EDGE, dtype=np.float32)
    grad = (center * (1.0 - t) + edge * t).astype(np.uint8)
    img = Image.fromarray(grad, mode="RGB")

    face_d = int(S * 0.72)
    face_x = (S - face_d) // 2
    face_y = (S - face_d) // 2

    shadow = Image.new("L", (S, S), 0)
    shadow_spread = int(S * 0.02)
    ImageDraw.Draw(shadow).ellipse(
        [face_x - shadow_spread, face_y - shadow_spread + int(S * 0.028),
         face_x + face_d + shadow_spread, face_y + face_d + shadow_spread + int(S * 0.028)],
        fill=225)
    shadow = shadow.filter(ImageFilter.GaussianBlur(radius=S * 0.02))
    img = Image.composite(Image.new("RGB", (S, S), (0, 0, 0)), img, shadow)

    face = draw_face(SIZE).resize((face_d, face_d), Image.LANCZOS)
    img.paste(face, (face_x, face_y), face)

    d = ImageDraw.Draw(img)
    bezel_w = max(1, int(S * 0.014))
    d.ellipse([face_x, face_y, face_x + face_d, face_y + face_d],
              outline=BEZEL, width=bezel_w)

    return img.resize((SIZE, SIZE), Image.LANCZOS)


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    path = "%s/icon_store.png" % out_dir
    draw().save(path)
    print("wrote %s (%dx%d)" % (path, SIZE, SIZE))
