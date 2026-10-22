#!/usr/bin/env python3
"""Draw the card's two crescents from Slumber's watch icon, and pack them for the glance.

    python3 SlumberGlance/Resources/make_card_icons.py SlumberGlance/Resources
    python3 "$UNA_SDK/Utilities/Scripts/png2abgr2222/png2abgr2222.py" \
        --inputs SlumberGlance/Resources/icon_card.png SlumberGlance/Resources/icon_card_muted.png \
        -o SlumberGlance/Software/Libs/Header/Icons.h

icon_card.png is Slumber/Resources/icon_30x30.png with its black made
transparent; icon_card_muted.png is the same shape in grey, for the cards that
say nothing about a night.
"""
import sys
from pathlib import Path

from PIL import Image

SOURCE = Path(__file__).resolve().parents[2] / "Slumber" / "Resources" / "icon_30x30.png"
MUTED_LEVEL = 2


def levels(px):
    return tuple(c >> 6 for c in px[:3])


def draw():
    src = Image.open(SOURCE).convert("RGBA")
    lit, muted = Image.new("RGBA", src.size), Image.new("RGBA", src.size)
    for y in range(src.height):
        for x in range(src.width):
            px = src.getpixel((x, y))
            if max(levels(px)) == 0:
                continue
            lit.putpixel((x, y), px[:3] + (255,))
            grey = 85 * min(MUTED_LEVEL, max(levels(px)))
            muted.putpixel((x, y), (grey, grey, grey, 255))
    return lit, muted


if __name__ == "__main__":
    out_dir = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    for name, img in zip(("icon_card", "icon_card_muted"), draw()):
        img.save(out_dir / f"{name}.png")
        print(f"wrote {out_dir / name}.png ({img.width}x{img.height})")
