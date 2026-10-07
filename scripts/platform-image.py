#!/usr/bin/env python3
"""Convert a PNG into an Analogue Pocket platform image (Platforms/_images).

    scripts/platform-image.py IN.png OUT.bin [--scale S]

The Pocket's platform banner is 521x165, stored rotated 90 degrees
counter-clockwise as 165 columns by 521 rows of 16-bit pixels: the first
byte is the inverted brightness (255 is the dark background), the second
is zero.  This matches the reference image shipped with the openfpgaOS SDK.

The PNG is converted to grey by luminance times alpha, scaled by S with
area averaging (default: the largest scale that fits with a small margin)
and centred.  Only the standard library is used.
"""
import argparse
import struct
import sys
import zlib

WIDTH, HEIGHT = 521, 165


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, plte, trns = 8, b"", None, None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(
                ">IIBBBBB", chunk)
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"PLTE":
            plte = chunk
        elif kind == b"tRNS":
            trns = chunk
    if depth != 8 or interlace:
        sys.exit(f"{path}: only 8-bit, non-interlaced PNGs are supported")
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw, stride = zlib.decompress(idat), width * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(height):
        filt, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if filt == 1:
                line[x] = (line[x] + a) & 255
            elif filt == 2:
                line[x] = (line[x] + b) & 255
            elif filt == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[x] = (line[x] + pred) & 255
        rows.append(line)
        prev = line

    def grey(x, y):
        row = rows[y]
        if ctype == 6:
            r, g, b, alpha = row[x * 4:x * 4 + 4]
        elif ctype == 2:
            (r, g, b), alpha = row[x * 3:x * 3 + 3], 255
        elif ctype == 0:
            r = g = b = row[x]
            alpha = 255
        elif ctype == 4:
            r = g = b = row[x * 2]
            alpha = row[x * 2 + 1]
        else:
            k = row[x]
            r, g, b = plte[k * 3:k * 3 + 3]
            alpha = trns[k] if trns and k < len(trns) else 255
        return (0.299 * r + 0.587 * g + 0.114 * b) * alpha / 255

    return width, height, [[grey(x, y) for x in range(width)]
                           for y in range(height)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("png")
    parser.add_argument("out")
    parser.add_argument("--scale", type=float)
    args = parser.parse_args()

    src_w, src_h, src = read_png(args.png)
    scale = args.scale or min((WIDTH - 8) / src_w, (HEIGHT - 4) / src_h)
    out_w, out_h = round(src_w * scale), round(src_h * scale)
    if out_w > WIDTH or out_h > HEIGHT:
        sys.exit(f"scaled image {out_w}x{out_h} exceeds {WIDTH}x{HEIGHT}")
    left, top = (WIDTH - out_w) // 2, (HEIGHT - out_h) // 2

    # Area-average each destination pixel over the source pixels it covers.
    banner = [[0.0] * WIDTH for _ in range(HEIGHT)]
    for oy in range(out_h):
        y0, y1 = oy / scale, (oy + 1) / scale
        for ox in range(out_w):
            x0, x1 = ox / scale, (ox + 1) / scale
            total = area = 0.0
            for sy in range(int(y0), min(src_h, int(y1 - 1e-9) + 1)):
                wy = min(y1, sy + 1) - max(y0, sy)
                for sx in range(int(x0), min(src_w, int(x1 - 1e-9) + 1)):
                    wx = min(x1, sx + 1) - max(x0, sx)
                    total += src[sy][sx] * wx * wy
                    area += wx * wy
            banner[top + oy][left + ox] = total / area if area else 0.0

    # Rotate 90 degrees counter-clockwise: stored row r is banner column
    # WIDTH-1-r, read top to bottom.
    out = bytearray()
    for r in range(WIDTH):
        x = WIDTH - 1 - r
        for y in range(HEIGHT):
            value = max(0, min(255, round(banner[y][x])))
            out += bytes((255 - value, 0))
    open(args.out, "wb").write(out)
    print(f"{args.out}: {out_w}x{out_h} at {left},{top} (scale {scale:.3f})")


if __name__ == "__main__":
    main()
