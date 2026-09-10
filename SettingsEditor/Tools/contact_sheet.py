#!/usr/bin/env python3
"""Lays the raw frames `cargo run --example dump` writes out as one PNG.

    cd Software/Apps/CustomGUI/rust
    cargo run --example dump --features std -- /tmp/frames
    python3 ../../../../Tools/contact_sheet.py /tmp/frames ../../../../Docs/screens.png

Each input is 240*240 bytes of ABGR2222, one pixel per byte, exactly what the
renderer hands the kernel. The two-bit channels expand to 0/85/170/255, which is
every level this panel has. Pixels outside the inscribed circle are painted the
sheet's own background rather than black, so on a sheet of many frames the round
mask the watch applies reads as the edge of the glass instead of blending into
an unlit screen.
"""
import struct
import sys
import zlib
from pathlib import Path

W = H = 240
LEVELS = 85
COLUMNS = 4
BEZEL = (24, 24, 28)


def decode(byte):
    return (
        (byte & 0b11) * LEVELS,
        ((byte >> 2) & 0b11) * LEVELS,
        ((byte >> 4) & 0b11) * LEVELS,
    )


def inside_bezel(x, y):
    dx = 2 * x - (W - 1)
    dy = 2 * y - (H - 1)
    return dx * dx + dy * dy <= W * W


def chunk(tag, data):
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def main():
    frames = sorted(Path(sys.argv[1]).glob("*.raw"))
    if not frames:
        sys.exit(f"no .raw frames in {sys.argv[1]}")
    rows = (len(frames) + COLUMNS - 1) // COLUMNS
    width, height = W * COLUMNS, H * rows

    sheet = [[BEZEL] * width for _ in range(height)]
    for i, path in enumerate(frames):
        raw = path.read_bytes()
        if len(raw) != W * H:
            sys.exit(f"{path} is {len(raw)} bytes, expected {W * H}")
        ox, oy = (i % COLUMNS) * W, (i // COLUMNS) * H
        for y in range(H):
            for x in range(W):
                if inside_bezel(x, y):
                    sheet[oy + y][ox + x] = decode(raw[y * W + x])

    scanlines = bytearray()
    for row in sheet:
        scanlines.append(0)
        for pixel in row:
            scanlines.extend(pixel)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(scanlines), 9))
    png += chunk(b"IEND", b"")

    out = Path(sys.argv[2])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(png)
    print(f"{out} ({len(frames)} frames, {width}x{height})")


if __name__ == "__main__":
    main()
