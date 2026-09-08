#!/usr/bin/env python3
"""Tile the preview PNGs into the sheets the README shows.

    Tools/docker-build.sh preview          # writes Docs/screens/
    python3 HapticLab/Tools/contact_sheet.py HapticLab/Docs

The individual PNGs are one command away and are not committed; the sheets are,
because a sheet is what a person reviews. Every frame in them is already masked
to the round bezel by the `preview` binary, which the device simulator does not
do. An unmasked screenshot is not evidence about this panel.
"""
import os
import sys

from PIL import Image, ImageDraw

W = H = 240
GAP = 8
LABEL = 16
COLS = 4
GROUND = (24, 24, 24)

SHEETS = {
    "screens.png": [
        "menu_wrist_unmasked",
        "menu_table_unmasked",
        "calibration_none",
        "calibration_all",
        "learn_first",
        "learn_longest_name",
        "wait",
        "playing",
        "answer_real",
        "answer_longest",
        "feedback_correct",
        "feedback_wrong",
        "rest_first_test",
        "rest_widest",
        "done",
        "aborted",
    ],
    "screens-probe.png": [
        "probe_00_click_123",
        "probe_03_effect_127",
        "probe_06_pause_1270",
        "probe_08_queue",
        "probe_10_count_12",
        "probe_sent",
        "sweep_start",
        "storage_full",
    ],
}


def sheet(src_dir, names):
    rows = (len(names) + COLS - 1) // COLS
    width = COLS * W + (COLS + 1) * GAP
    height = rows * (H + LABEL) + (rows + 1) * GAP
    out = Image.new("RGB", (width, height), GROUND)
    draw = ImageDraw.Draw(out)

    for i, name in enumerate(names):
        path = os.path.join(src_dir, f"{name}.png")
        if not os.path.exists(path):
            raise SystemExit(f"{path} is missing; run Tools/docker-build.sh preview first")
        col, row = i % COLS, i // COLS
        x = GAP + col * (W + GAP)
        y = GAP + row * (H + LABEL + GAP)
        out.paste(Image.open(path), (x, y))
        draw.text((x + 2, y + H + 2), name, fill=(170, 170, 170))
    return out


def main():
    docs = sys.argv[1] if len(sys.argv) > 1 else "."
    src = os.path.join(docs, "screens")
    for leaf, names in SHEETS.items():
        image = sheet(src, names)
        path = os.path.join(docs, leaf)
        image.save(path)
        print(f"wrote {path} ({len(names)} screens)")


if __name__ == "__main__":
    main()
