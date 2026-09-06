#!/usr/bin/env python3
"""Draw the store icon -- the one the phone app shows, not the watch's.

    python3 UnitToggle/Resources/make_store_icon.py UnitToggle/Resources

Not the watch icon. `icon_60x60.png` / `icon_30x30.png` (make_icon.py) get
quantised to ABGR2222 and baked into the .uapp; this file is copied into the
package as `icon.png` and shown by the phone app as an ordinary full-colour
PNG. This app has one screen and no format choice, so the icon is the same
vector ruler-and-bar shape make_icon.py draws at 60px, scaled up, rather than
a screenshot.
"""
from PIL import Image, ImageDraw, ImageFilter
import numpy as np
import sys

SIZE = 512
SS = 4

BG_CENTER = (14, 36, 52)     # a dark blue glow, echoing the ruler's own colour
BG_EDGE = (7, 9, 11)
BEZEL = (234, 234, 231)
CARD_BG = (10, 10, 10, 255)
RULE = (0, 170, 255, 255)
UNCHOSEN = (85, 85, 85, 255)

RULE_X0 = 0.15
RULE_X1 = 0.85
TICKS = 5


def draw_face(size):
    """What make_icon.py draws at 60px: a black rounded card, the ruler, and
    the two-position bar with its left half chosen."""
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pen = ImageDraw.Draw(img)

    inset = int(size * 0.05) * SS
    pen.rounded_rectangle([inset, inset, S - 1 - inset, S - 1 - inset],
                          radius=int(size * 0.2) * SS, fill=CARD_BG)

    x0, x1 = RULE_X0 * S, RULE_X1 * S
    base_y = 0.56 * S
    pen.rectangle([x0, base_y, x1, base_y + 0.065 * S], fill=RULE)

    span = x1 - x0
    tick_w = span / (TICKS * 2 - 1)
    for i in range(TICKS):
        x = x0 + i * tick_w * 2
        h = (0.26 if i % 2 == 0 else 0.15) * S
        pen.rectangle([x, base_y - h, x + tick_w, base_y], fill=RULE)

    by0, by1 = 0.70 * S, 0.84 * S
    r = (by1 - by0) / 2
    mid = (x0 + x1) / 2
    pen.rounded_rectangle([x0, by0, x1, by1], radius=r, fill=UNCHOSEN)
    pen.rounded_rectangle([x0, by0, mid + r, by1], radius=r, fill=RULE)

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
    img = Image.fromarray(grad)

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
