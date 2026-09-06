#!/usr/bin/env python3
"""Turns the raw frames `cargo run --example dump` writes into PNGs.

    cargo run --example dump --features std -- /tmp/frames
    python3 Tools/frames_to_png.py /tmp/frames

Each input is 240*240 bytes of ABGR2222, one pixel per byte, exactly what the
renderer hands the kernel. The two-bit channels expand to 0/85/170/255, which
is every level this panel has. Pixels outside the inscribed circle are painted
black, the way the bezel covers them on the watch.
"""
import struct
import sys
import zlib
from pathlib import Path

W = H = 240
LEVELS = 85


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


def write_png(path, raw):
    rows = bytearray()
    for y in range(H):
        rows.append(0)
        for x in range(W):
            rows.extend(decode(raw[y * W + x]) if inside_bezel(x, y) else (0, 0, 0))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    directory = Path(sys.argv[1])
    for raw_path in sorted(directory.glob("*.raw")):
        raw = raw_path.read_bytes()
        if len(raw) != W * H:
            sys.exit(f"{raw_path} is {len(raw)} bytes, expected {W * H}")
        out = raw_path.with_suffix(".png")
        write_png(out, raw)
        print(out)


if __name__ == "__main__":
    main()
