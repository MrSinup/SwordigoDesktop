#!/usr/bin/env python3
"""shot_stats.py — summarise a captured preview frame.

Prints the colour spread of a .png (or .ppm) so a frame can be judged without a
viewer: a real rendered scene has hundreds of distinct colours and a broad
luminance range, a black/garbled frame does not.

Usage: python3 tools/shot_stats.py FRAME.png [--grid COLSxROWS]
"""
import struct
import sys
import zlib
from collections import Counter


def load_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a png")
    i = 8
    width = height = None
    idat = b""
    while i < len(data):
        length = struct.unpack(">I", data[i:i + 4])[0]
        kind = data[i + 4:i + 8]
        chunk = data[i + 8:i + 8 + length]
        if kind == b"IHDR":
            width, height, _depth, colour = struct.unpack(">IIBB", chunk[:10])
            if colour not in (2, 6):
                raise ValueError(f"unsupported png colour type {colour}")
            bpp = 3 if colour == 2 else 4
        elif kind == b"IDAT":
            idat += chunk
        i += 12 + length
    raw = zlib.decompress(idat)
    return width, height, raw, bpp


def load_ppm(path):
    data = open(path, "rb").read()
    # P6\n<w> <h>\n255\n<binary>
    parts = data.split(b"\n", 3)
    width, height = (int(v) for v in parts[1].split())
    return width, height, parts[3]


def rows(width, height, raw, ppm, bpp=3):
    if ppm:
        stride = width * 3
        return [raw[y * stride:(y + 1) * stride] for y in range(height)], 3
    # PNG: one filter byte per scanline; filters are ignored (stats only).
    stride = width * bpp + 1
    return [raw[y * stride + 1:(y + 1) * stride] for y in range(height)], bpp


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    grid = 64
    if len(sys.argv) > 3 and sys.argv[2] == "--grid":
        grid = int(sys.argv[3].split("x")[0])

    ppm = path.lower().endswith(".ppm")
    if ppm:
        width, height, raw = load_ppm(path)
        bpp = 3
    else:
        width, height, raw, bpp = load_png(path)
    lines, _bpp = rows(width, height, raw, ppm, bpp)

    def pixel(x, y):
        line = lines[y]
        return line[x * bpp], line[x * bpp + 1], line[x * bpp + 2]

    colours = Counter()
    luma_sum = 0.0
    luma_min, luma_max = 255.0, 0.0
    dark = 0
    samples = 0
    step_x = max(1, width // grid)
    step_y = max(1, height // grid)
    for y in range(0, height, step_y):
        for x in range(0, width, step_x):
            r, g, b = pixel(x, y)
            colours[(r, g, b)] += 1
            luma = 0.299 * r + 0.587 * g + 0.114 * b
            luma_sum += luma
            luma_min = min(luma_min, luma)
            luma_max = max(luma_max, luma)
            if luma < 6.0:
                dark += 1
            samples += 1

    print(f"{path}: {width}x{height}")
    print(f"  distinct colours : {len(colours)}")
    print(f"  mean luma        : {luma_sum / max(1, samples):.1f}  (min {luma_min:.0f} max {luma_max:.0f})")
    print(f"  fraction black   : {dark / max(1, samples):.2%}")
    print("  top colours      : " + ", ".join(
        f"{c}x{n}" for c, n in colours.most_common(6)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
