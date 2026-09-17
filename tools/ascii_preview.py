#!/usr/bin/env python3
"""ascii_preview.py — print a PNG as coarse ASCII hue/luma art.

Used to eyeball a rendered frame (opensw --shot) from a terminal: no image
viewer, no GUI. Not part of any build target.

  python3 tools/ascii_preview.py /tmp/shot.png [cols] [rows]
"""
import struct
import sys
import zlib


def load_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    i = 8
    idat = b''
    w = h = ct = 0
    while i < len(data):
        ln = struct.unpack('>I', data[i:i + 4])[0]
        typ = data[i + 4:i + 8]
        chunk = data[i + 8:i + 8 + ln]
        i += 12 + ln
        if typ == b'IHDR':
            w, h, _bd, ct = struct.unpack('>IIBB', chunk[:10])
        elif typ == b'IDAT':
            idat += chunk
        elif typ == b'IEND':
            break
    raw = zlib.decompress(idat)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    stride = w * ch
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for _y in range(h):
        f = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if f:
            for x in range(stride):
                a = line[x - ch] if x >= ch else 0
                b = prev[x]
                c = prev[x - ch] if x >= ch else 0
                if f == 1:
                    line[x] = (line[x] + a) & 255
                elif f == 2:
                    line[x] = (line[x] + b) & 255
                elif f == 3:
                    line[x] = (line[x] + (a + b) // 2) & 255
                elif f == 4:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                    line[x] = (line[x] + pr) & 255
        out += line
        prev = line
    return w, h, ch, bytes(out)


def classify(r, g, b):
    lum = (r + g + b) // 3
    if lum < 6:
        return '.'
    mx = max(r, g, b)
    if mx - lum < 10:
        return '#' if lum > 80 else '+'
    if r == mx and r > g + 20 and r > b + 20:
        return 'R'
    if g == mx and g > r + 20 and g > b + 20:
        return 'G'
    if b == mx and b > r + 20 and b > g + 20:
        return 'B'
    return 'o' if r >= g >= b else 'x'


def main():
    path = sys.argv[1]
    cols = int(sys.argv[2]) if len(sys.argv) > 2 else 72
    rows = int(sys.argv[3]) if len(sys.argv) > 3 else 34
    w, h, ch, px = load_png(path)
    print(f'# {path} {w}x{h} ch={ch}')
    for ry in range(rows):
        line = ''
        for rx in range(cols):
            x0, x1 = rx * w // cols, max(rx * w // cols + 1, (rx + 1) * w // cols)
            y0, y1 = ry * h // rows, max(ry * h // rows + 1, (ry + 1) * h // rows)
            r = g = b = n = 0
            for y in range(y0, y1, 7):
                for x in range(x0, x1, 7):
                    o = (y * w + x) * ch
                    r += px[o]
                    g += px[o + 1]
                    b += px[o + 2]
                    n += 1
            line += classify(r // n, g // n, b // n)
        print(line)


if __name__ == '__main__':
    main()
