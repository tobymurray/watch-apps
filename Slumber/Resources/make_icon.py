#!/usr/bin/env python3
"""Draw the Slumber app icons: a crescent in the Rest ring's violet-to-magenta,
with a cyan star and a white one, on black.

    python3 Slumber/Resources/make_icon.py Slumber/Resources

app_merging.py keeps only the top two bits of each channel and does not dither,
so the icon is drawn supersampled, then dithered here to levels 0/85/170/255 and
saved already as the watch will show it.
"""
import math
import sys

import numpy as np
from PIL import Image

SS = 8
BAYER = (np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) + 0.5) / 16


def level(r, g, b):
    return np.array([r, g, b], float) / 3


VIOLET, PURPLE, MAGENTA = level(1, 0, 3), level(2, 0, 3), level(3, 0, 3)
CYAN, WHITE = level(0, 3, 3), level(3, 3, 3)

BITE_OFFSET = 0.62  # bite centre along the moon's axis, in moon radii
BITE_RADIUS = 0.90  # in moon radii
TILT_DEG = -20
FILL = 0.86  # the crescent's larger extent, as a fraction of the icon


def rest(t):
    """The Rest ring's gradient: violet at 0, purple, magenta at 1."""
    t = np.clip(t, 0, 1)[..., None] * 2
    lo = np.where(t < 1, VIOLET, PURPLE)
    hi = np.where(t < 1, PURPLE, MAGENTA)
    u = np.where(t < 1, t, t - 1)
    return lo * (1 - u) + hi * u


def draw(n):
    S = n * SS
    y, x = np.mgrid[0:S, 0:S]
    x = (x + 0.5) / SS - n / 2
    y = (y + 0.5) / SS - n / 2
    img = np.zeros((S, S, 3))

    def crescent(cx, cy, r):
        t = math.radians(TILT_DEG)
        u = (x - cx) * math.cos(t) + (y - cy) * math.sin(t)
        v = -(x - cx) * math.sin(t) + (y - cy) * math.cos(t)
        return (np.hypot(u, v) <= r) & (np.hypot(u - BITE_OFFSET * r, v) > BITE_RADIUS * r)

    unit = crescent(0, 0, 1.0)
    xs, ys = x[unit], y[unit]
    r = n * FILL / max(xs.max() - xs.min(), ys.max() - ys.min())
    cx = -n * 0.05 - (xs.min() + xs.max()) / 2 * r
    cy = -(ys.min() + ys.max()) / 2 * r
    moon = crescent(cx, cy, r)
    img[moon] = rest(0.5 - (x + y) / (n * 0.9))[moon]

    for sx, sy, arm, ink in ((0.21, -0.15, 0.09, CYAN), (0.32, 0.12, 0.045, WHITE)):
        w = max(0.5, n * arm / 3.2)
        a = n * arm
        star = ((np.abs(x - n * sx) <= w / 2) & (np.abs(y - n * sy) <= a)) | \
               ((np.abs(y - n * sy) <= w / 2) & (np.abs(x - n * sx) <= a))
        img[star] = ink

    small = img.reshape(n, SS, n, SS, 3).mean(axis=(1, 3))
    threshold = np.tile(BAYER, (n // 4 + 1, n // 4 + 1))[:n, :n, None]
    levels = np.clip(np.floor(small * 3 + threshold), 0, 3).astype(np.uint8)
    rgb = levels * 85
    alpha = np.full((n, n, 1), 255, np.uint8)
    return Image.fromarray(np.concatenate([rgb, alpha], axis=2))


def main(out_dir):
    for n in (60, 30):
        draw(n).save(f"{out_dir}/icon_{n}x{n}.png")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
