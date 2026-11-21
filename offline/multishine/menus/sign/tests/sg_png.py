#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Renders the text dump of tests/sg_mock.c (flow.txt) to 640x480 PNGs in the colours of the Melee menus (approximation: the real screens
draw the same rows with Melee's own text engine).  Stdlib only (zlib), 5x7 glyphs of offline/common/xfbpage.h at 2x.

  python3 sg_png.py FLOW.txt OUTDIR [--only NAME]
"""
import os, re, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
XFB = os.path.join(HERE, "..", "..", "..", "..", "common", "xfbpage.h")
W, H = 640, 480
COLS, ROWS, PITCH, CW = 52, 26, 17, 12
X0, Y0 = 8, (H - ROWS * PITCH) // 2
COL = {0: (240, 240, 250), 1: (20, 20, 40), 2: (255, 214, 64), 3: (150, 165, 215), 4: (110, 230, 120), 5: (255, 110, 96)}


def load_font():
    src = open(XFB).read()
    body = src[src.index("xp_font[95][5]"):]
    body = body[:body.index("};")]
    nums = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
    assert len(nums) == 95 * 5, len(nums)
    return [nums[i * 5:(i + 1) * 5] for i in range(95)]


FONT = load_font()


def background():
    px = bytearray(W * H * 3)
    for y in range(H):
        t = y / (H - 1)
        r, g, b = int(14 + 18 * t), int(20 + 26 * t), int(56 + 60 * t)
        row = bytes((r, g, b)) * W
        px[y * W * 3:(y + 1) * W * 3] = row
    # tiled diamond texture, as faint lines
    for y in range(0, H, 24):
        for x in range(W):
            i = (y * W + x) * 3
            px[i], px[i + 1], px[i + 2] = min(255, px[i] + 6), min(255, px[i + 1] + 8), min(255, px[i + 2] + 14)
    return px


def rect(px, x, y, w, h, c):
    for yy in range(max(0, y), min(H, y + h)):
        base = yy * W * 3
        for xx in range(max(0, x), min(W, x + w)):
            px[base + xx * 3:base + xx * 3 + 3] = bytes(c)


def blend_rect(px, x, y, w, h, c, a):
    for yy in range(max(0, y), min(H, y + h)):
        base = yy * W * 3
        for xx in range(max(0, x), min(W, x + w)):
            i = base + xx * 3
            for k in range(3):
                px[i + k] = int(px[i + k] * (1 - a) + c[k] * a)


def glyph(px, ch, cx, cy, c, shadow=True):
    g = FONT[(ord(ch) - 32) if 32 <= ord(ch) <= 126 else ord("?") - 32]
    for pass_ in ((1, (8, 8, 24)), (0, c)) if shadow else ((0, c),):
        off, col = pass_
        for fx in range(5):
            for fy in range(7):
                if (g[fx] >> fy) & 1:
                    rect(px, cx + fx * 2 + off, cy + fy * 2 + off, 2, 2, col)


def text(px, row, s, attr):
    c = COL.get(attr, COL[0])
    y = Y0 + row * PITCH + 1
    for i, ch in enumerate(s):
        if ch != " ":
            glyph(px, ch, X0 + i * CW, y, c, shadow=(attr != 1))


def qr_draw(px, n, qr_row, qr_rows, hexs):
    stride = (n + 7) // 8
    data = bytes.fromhex(hexs)
    mod = 6
    size = (n + 8) * mod
    x = (W - size) // 2
    y = Y0 + qr_row * PITCH + ((qr_rows * PITCH - size) // 2 if qr_rows * PITCH > size else 0)
    rect(px, x, y, size, size, (255, 255, 255))
    for r in range(n):
        for c in range(n):
            if (data[r * stride + (c >> 3)] >> (7 - (c & 7))) & 1:
                rect(px, x + (c + 4) * mod, y + (r + 4) * mod, mod, mod, (0, 0, 0))
    return data


def render(name, rows, qr):
    px = background()
    blend_rect(px, 4, Y0 - 4, W - 8, ROWS * PITCH + 8, (6, 8, 30), 0.72)
    rect(px, 4, Y0 - 4, W - 8, 1, (130, 150, 220)); rect(px, 4, Y0 + ROWS * PITCH + 3, W - 8, 1, (130, 150, 220))
    rect(px, 4, Y0 - 1, W - 8, PITCH, (232, 190, 60))                      # header bar, gold
    for r, (attr, s) in enumerate(rows):
        if attr == 2 and r > 0:
            blend_rect(px, 8, Y0 + r * PITCH - 1, W - 16, PITCH, (255, 214, 64), 0.18)
        if s.strip():
            text(px, r, s, attr)
    if qr:
        qr_draw(px, *qr)
    return px


def png(path, px):
    raw = b"".join(b"\x00" + bytes(px[y * W * 3:(y + 1) * W * 3]) for y in range(H))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def parse(path):
    lines = open(path).read().split("\n")
    i, out = 0, []
    while i < len(lines):
        if lines[i].startswith("SCREEN "):
            _, name, qn, qrow, qrows = lines[i].split()
            rows = []
            for k in range(ROWS):
                a, s = lines[i + 1 + k].split("|", 1)
                rows.append((int(a), s))
            i += 1 + ROWS
            qr = None
            if int(qn):
                qr = (int(qn), int(qrow), int(qrows), lines[i]); i += 1
            out.append((name, rows, qr))
        else:
            i += 1
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__); return 2
    only = argv[argv.index("--only") + 1] if "--only" in argv else None
    os.makedirs(argv[2], exist_ok=True)
    for name, rows, qr in parse(argv[1]):
        if only and only != name: continue
        png(os.path.join(argv[2], name + ".png"), render(name, rows, qr))
        print("wrote", name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
