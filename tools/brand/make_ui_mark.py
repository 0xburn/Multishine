#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""The small Multishine mark for the Settings screens: our own five open hexagons ( _logo_svg, the same geometry as
the save icons of make_save_art.py) rendered white on black as ONE I4 texture, 64 x 32 texels, 8 x 8 tiles, two texels per byte (high nibble first) = 1,024 bytes.
Writes offline/multishine/menus/ml/ml_mark_tex.h (a committed generated file; checks it equals this output). Stdlib only,
deterministic, no Slippi or Nintendo art.

  make_ui_mark.py [--out PATH]      write the header
  make_ui_mark.py --check           exit 1 when the header on disk differs
  make_ui_mark.py --preview OUT.pgm 4x enlarged grey preview (P5)
"""
import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_save_art as A   # noqa: E402  (HEXES, _seg_dist, _hex_pts: the one set of hexagon constants)

REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
DEFAULT_OUT = os.path.join(REPO, "offline", "multishine", "menus", "ml", "ml_mark_tex.h")
W, H = 64, 32
HEAVY = 1.45
GAIN = 1.0                #: the 1.3 gain clipped the middle hexagons to full white, so the mark read wider and flatter than the title logo ("stretched"); the same intensities as the title now


def title_logo_box():
    """-> (w, h, stroke) of the logo as the title screen composes it (docs/data/title_text.json layout.logo): the ONE size of the mark's texels"""
    import json
    with open(os.path.join(REPO, "docs", "data", "title_text.json")) as f:
        lg = json.load(f)["layout"]["logo"]
    return lg["w"], lg["h"], lg.get("stroke_scale", 1.0)


# the optional FADE-OFF variant of the server-contact (cyan) state (compile-time MARK_BLUE_FADEOFF in ml_mark_level.h): the same artwork with the opacity of the pairs of hexagons
# scaled like the brand mark's grading, centre full, next pair 70 percent, outer pair 40 percent; the default state is flat (every texel the same cyan)
FADEOFF = (1.0, 0.7, 0.4)       # by |slot|


def coverage(ss=6, fade=None):
    """-> list H x W of intensity 0..1: the title banner's logo texels EXACTLY (one look on the title, the menus and the Settings pages): fit(64, 26, 1.45), the same call as
    tools/title_art.py makes, centred in the 64 x 32 texture (rows 3..28).  The sprite is drawn with the same bilinear filter as the title's quad (ml_glue.c spr_emit mode 1).  The ink box on the
    screen stays where our confirmed it: the quad is 64 / 62 of the old width because the ink is 62 texels wide, 26 rows high."""
    lw, lh, stroke = title_logo_box()
    core = fit(lw, lh, stroke, ss=4, fade=fade)
    ox, oy = (W - lw) // 2, (H - lh) // 2
    img = [[0.0] * W for _ in range(H)]
    for y in range(lh):
        for x in range(lw):
            img[y + oy][x + ox] = min(1.0, core[y][x] * GAIN)
    return img


def fit(w, h, heavy=HEAVY, ss=4, fade=None):
    """the SAME mark (the same hexagons, strokes and opacities as coverage()), cropped to its own box and fitted into w x h texels: -> list h x w of intensity 0..1.  The title banner
    (tools/title_art.py) takes its logo from here, so the title, the menus and the overlay pages show one artwork."""
    k = 1.0
    polys = []
    for slot, r, sw, op in A.HEXES:
        pts = A._hex_pts(100 + slot * 42, r)
        polys.append((pts, sw * k * heavy / 2, op))
    ops = [op * (fade[abs(slot)] if fade else 1.0) for slot, _, _, op in A.HEXES]      # the box and the strokes stay the artwork's; only the opacity of the pairs changes
    hw = max(p[1] for p in polys)
    x0 = min(x for pts, _, _ in polys for x, _ in pts) - hw
    x1 = max(x for pts, _, _ in polys for x, _ in pts) + hw
    y0 = min(y for pts, _, _ in polys for _, y in pts) - hw
    y1 = max(y for pts, _, _ in polys for _, y in pts) + hw
    sc = min(w / (x1 - x0), h / (y1 - y0))
    ox, oy = (w - (x1 - x0) * sc) / 2.0, (h - (y1 - y0) * sc) / 2.0
    img = [[0.0] * w for _ in range(h)]
    for ty in range(h):
        for tx in range(w):
            tot = 0.0
            for sy in range(ss):
                for sx in range(ss):
                    px = x0 + ((tx + (sx + 0.5) / ss) - ox) / sc
                    py = y0 + ((ty + (sy + 0.5) / ss) - oy) / sc
                    a_ = 0.0
                    for (pts, r_, _), op in zip(polys, ops):
                        d = min(A._seg_dist(px, py, pts[i][0], pts[i][1], pts[(i + 1) % 6][0], pts[(i + 1) % 6][1]) for i in range(6))
                        if d <= r_:
                            a_ = a_ + (1.0 - a_) * op
                    tot += a_
            img[ty][tx] = tot / (ss * ss)
    return img


def i4_bytes(img):
    out = bytearray()
    for ty in range(H // 8):
        for tx in range(W // 8):
            for y in range(8):
                for x in range(0, 8, 2):
                    a = int(round(min(1.0, img[ty * 8 + y][tx * 8 + x]) * 15))
                    b = int(round(min(1.0, img[ty * 8 + y][tx * 8 + x + 1]) * 15))
                    out.append((a << 4) | b)
    return bytes(out)


def array_lines(name, data, note):
    lines = ["#if %s" % note] if note else []
    lines.append("static const unsigned char %s[ML_MARK_BYTES] __attribute__((aligned(32))) = {" % name)
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",")
    lines.append("};")
    if note:
        lines.append("#endif")
    return lines


def header_text():
    data = i4_bytes(coverage())
    fo = i4_bytes(coverage(fade=FADEOFF))
    lines = ["/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */",
             "/* GENERATED by tools/brand/make_ui_mark.py: the small Multishine mark of the Settings screens, I4 %d x %d, 8 x 8 tiles, %d bytes.  Do not edit. */" % (W, H, len(data)),
             "#ifndef ML_MARK_TEX_H", "#define ML_MARK_TEX_H", "#define ML_MARK_W %d" % W, "#define ML_MARK_H %d" % H, "#define ML_MARK_BYTES %d" % len(data),
             "#ifndef MARK_BLUE_FADEOFF", "#define MARK_BLUE_FADEOFF 0", "#endif"]
    lines += array_lines("ml_mark_tex", data, None)
    lines += ["/* the same artwork with the pairs of hexagons at 100 / 70 / 40 percent opacity: drawn in the cyan state when MARK_BLUE_FADEOFF is 1 (ml_mark_level.h) */"]
    lines += array_lines("ml_mark_tex_fo", fo, "MARK_BLUE_FADEOFF")
    lines += ["#endif", ""]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--preview")
    a = ap.parse_args()
    if a.preview:
        img = coverage()
        with open(a.preview, "wb") as f:
            f.write(b"P5\n%d %d\n255\n" % (W * 4, H * 4))
            for y in range(H * 4):
                f.write(bytes(int(img[y // 4][x // 4] * 255) for x in range(W * 4)))
        return 0
    text = header_text()
    if a.check:
        cur = open(a.out).read() if os.path.exists(a.out) else ""
        if cur != text:
            print("ml_mark_tex.h differs from make_ui_mark.py output", file=sys.stderr)
            return 1
        return 0
    with open(a.out, "w") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
