#!/usr/bin/env python3
"""Draw the store icon -- the one the phone app and Kira show, not the watch's.

    python3 Slumber/Resources/make_store_icon.py Slumber/Resources

The same crescent and stars as icon_60x60.png, drawn at 512px in a round
watch-face frame, as an ordinary full-colour PNG.
"""
import math
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from make_icon import BITE_OFFSET, BITE_RADIUS, FILL, TILT_DEG

SIZE = 512
SS = 4

BG_CENTER = (36, 12, 56)
BG_EDGE = (7, 9, 11)
BEZEL = (234, 234, 231)
FACE = (10, 10, 10)

VIOLET = np.array((85, 0, 255), np.float32)
PURPLE = np.array((170, 0, 255), np.float32)
MAGENTA = np.array((255, 0, 255), np.float32)
CYAN = (0, 255, 255)
WHITE = (255, 255, 255)

STARS = ((0.21, -0.15, 0.09, CYAN), (0.32, 0.12, 0.045, WHITE))


def rest(t):
    """The Rest ring's gradient: violet at 0, purple, magenta at 1."""
    t = np.clip(t, 0.0, 1.0)[..., None] * 2.0
    lo = np.where(t < 1.0, VIOLET, PURPLE)
    hi = np.where(t < 1.0, PURPLE, MAGENTA)
    u = np.where(t < 1.0, t, t - 1.0)
    return lo * (1.0 - u) + hi * u


def crescent(x, y, cx, cy, r):
    t = math.radians(TILT_DEG)
    u = (x - cx) * math.cos(t) + (y - cy) * math.sin(t)
    v = -(x - cx) * math.sin(t) + (y - cy) * math.cos(t)
    return (np.hypot(u, v) <= r) & (np.hypot(u - BITE_OFFSET * r, v) > BITE_RADIUS * r)


def draw_glyph(n):
    """The watch icon's crescent and stars as an RGBA layer n pixels square."""
    y, x = np.mgrid[0:n, 0:n].astype(np.float32)
    x = x + 0.5 - n / 2.0
    y = y + 0.5 - n / 2.0

    uy, ux = (np.mgrid[0:n, 0:n].astype(np.float32) + 0.5) / n * 2.0 - 1.0
    unit = crescent(ux, uy, 0, 0, 1.0)
    xs, ys = ux[unit], uy[unit]
    r = n * FILL / max(xs.max() - xs.min(), ys.max() - ys.min())
    cx = -n * 0.05 - (xs.min() + xs.max()) / 2 * r
    cy = -(ys.min() + ys.max()) / 2 * r
    moon = crescent(x, y, cx, cy, r)

    rgb = np.zeros((n, n, 3), np.float32)
    rgb[moon] = rest(0.5 - (x + y) / (n * 0.9))[moon]
    alpha = moon.astype(np.float32) * 255.0
    layer = Image.fromarray(np.dstack([rgb, alpha]).astype(np.uint8), "RGBA")

    pen = ImageDraw.Draw(layer)
    for sx, sy, arm, ink in STARS:
        w = n * arm / 3.2
        a = n * arm
        px, py = n / 2.0 + n * sx, n / 2.0 + n * sy
        pen.rectangle([px - w / 2, py - a, px + w / 2, py + a], fill=ink + (255,))
        pen.rectangle([px - a, py - w / 2, px + a, py + w / 2], fill=ink + (255,))
    return layer


def draw():
    S = SIZE * SS
    cx = cy = S / 2.0

    yy, xx = np.mgrid[0:S, 0:S]
    t = np.clip(np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / (S * 0.62), 0.0, 1.0)[..., None]
    grad = (np.array(BG_CENTER, np.float32) * (1.0 - t)
            + np.array(BG_EDGE, np.float32) * t).astype(np.uint8)
    img = Image.fromarray(grad)

    face_d = int(S * 0.72)
    fx = fy = (S - face_d) // 2

    shadow = Image.new("L", (S, S), 0)
    spread = int(S * 0.02)
    ImageDraw.Draw(shadow).ellipse(
        [fx - spread, fy - spread + int(S * 0.028),
         fx + face_d + spread, fy + face_d + spread + int(S * 0.028)], fill=225)
    shadow = shadow.filter(ImageFilter.GaussianBlur(radius=S * 0.02))
    img = Image.composite(Image.new("RGB", (S, S), (0, 0, 0)), img, shadow)

    pen = ImageDraw.Draw(img)
    pen.ellipse([fx, fy, fx + face_d, fy + face_d], fill=FACE)

    glyph_d = int(face_d * 0.78)
    glyph = draw_glyph(glyph_d)
    at = (int(cx - glyph_d / 2), int(cy - glyph_d / 2))
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    glow.paste(glyph, at)
    glow = glow.filter(ImageFilter.GaussianBlur(radius=S * 0.018))
    glow.putalpha(glow.getchannel("A").point(lambda a: a * 0.55))
    img = Image.alpha_composite(img.convert("RGBA"), glow)
    img.alpha_composite(glyph, at)

    pen = ImageDraw.Draw(img)
    pen.ellipse([fx, fy, fx + face_d, fy + face_d], outline=BEZEL,
                width=max(1, int(S * 0.014)))

    return img.convert("RGB").resize((SIZE, SIZE), Image.LANCZOS)


if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    path = "%s/icon_store.png" % out_dir
    draw().save(path)
    print("wrote %s (%dx%d)" % (path, SIZE, SIZE))
