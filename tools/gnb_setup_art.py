#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Game Night stage prep screen: the small pictures it is drawn with, GENERATED here (no input file) as C arrays: the turn chevrons, the small turn
triangles, the timeline arrow, the strike X, the cursor bracket and the selection frame, each a 64 x 64 IA4 canvas (power of two: the plate's texture mirrors), shapes drawn from measurements of a
reference capture.  Every other piece of the screen is Melee's own art read from the player's disc at run time or the menu font.  Generated at build time into the build's work directory
(sg_ovl_pack.py), never stored in the repository.

  gnb_setup_art.py [--png PREVIEW.png]
"""
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "offline", "iso"))
GS_SHA1 = "f611b333de3324a11c0d084910e04283c5e364a5"
CANDIDATES = (
    ".analysis-deps/work/rooms_beta2/src/x/Ishiiruka-rooms-beta-2/Data/Sys/GameFiles/GALE01/GameSetup_gui.dat",
    "vendor/ishiiruka/Data/Sys/GameFiles/GALE01/GameSetup_gui.dat",
)
SLIPPI_APP = "~/Library/Application Support/Slippi Launcher/netplay/Slippi Dolphin.app/Contents/Resources/Sys/GameFiles/GALE01/GameSetup_gui.dat"
WANT = {221: (75, 87, 2), 222: (31, 36, 2), 223: (36, 36, 2)}      # texture index in find_textures order: (w, h, GX format IA4)


def find():
    env = os.environ.get("GNB_SETUP_GUI")
    if env:
        return env if env != "none" and os.path.isfile(env) else ""
    for rel in CANDIDATES:
        p = os.path.join(REPO, rel)
        if os.path.isfile(p):
            return p
    p = os.path.expanduser(SLIPPI_APP)
    return p if os.path.isfile(p) else ""


def resample(px, w, h, nw, nh):
    """px: rows of (i, a) 0..15; box filter, intensity weighted by alpha."""
    out = []
    for y in range(nh):
        row = []
        y0, y1 = y * h / nh, (y + 1) * h / nh
        for x in range(nw):
            x0, x1 = x * w / nw, (x + 1) * w / nw
            si = sa = sw = 0.0
            yy = int(y0)
            while yy < y1 and yy < h:
                xx = int(x0)
                while xx < x1 and xx < w:
                    wy = min(yy + 1, y1) - max(yy, y0)
                    wx = min(xx + 1, x1) - max(xx, x0)
                    wt = wx * wy
                    i, a = px[yy][xx]
                    si += i * a * wt
                    sa += a * wt
                    sw += wt
                    xx += 1
                yy += 1
            a = sa / sw if sw else 0
            i = si / sa if sa else 0
            row.append((int(round(i)), int(round(a))))
        out.append(row)
    return out


def pad(px, w, h, nw, nh, ox=0, oy=0):
    """px (w x h) placed at (ox, oy) of a transparent nw x nh canvas"""
    return [[(px[y - oy][x - ox] if 0 <= y - oy < h and 0 <= x - ox < w else (0, 0)) for x in range(nw)] for y in range(nh)]


def flip(px):
    return [list(reversed(r)) for r in px]


def tiles(px, w, h):
    """IA4 8x4 tiles, high nibble alpha, low nibble intensity (gnb_art.c)."""
    out = bytearray()
    px = px[::-1]                      # stored upside down: the sort plate's quad the console draws them on maps the texture bottom to top (gnb_art.c badges do the same)
    for ty in range(0, h, 4):
        for tx in range(0, w, 8):
            for y in range(4):
                for x in range(8):
                    i, a = px[ty + y][tx + x]
                    out.append((a << 4) | i)
    return bytes(out)


def packbits(b):
    """PackBits as tools/gnb_rooms_art.py (the console side: gnb_art.c gt_unpack): a header < 128 = that many + 1 literal bytes, >= 128 = a run of (header - 126) copies of the next byte"""
    out = bytearray()
    i, n = 0, len(b)
    while i < n:
        r = 1
        while i + r < n and b[i + r] == b[i] and r < 129:
            r += 1
        if r >= 2:
            out += bytes((126 + r, b[i]))
            i += r
            continue
        j = i
        while j < n and j - i < 128 and not (j + 1 < n and b[j] == b[j + 1]):
            j += 1
        if j == i:
            j = i + 1
        out.append(j - i - 1)
        out += b[i:j]
        i = j
    return bytes(out)


def x_mark(n=64):
    """the strike X as Slippi draws it (twitch capture ): two crossing bars with round ends, each about an eighth of the X wide. (Until
    this was two chevrons point to point, which is not what Slippi shows.)"""
    import math
    def seg(x, y, x1, y1, x2, y2):
        dx, dy = x2 - x1, y2 - y1
        t = max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy)))
        return math.hypot(x - x1 - t * dx, y - y1 - t * dy)
    r = 4.3
    return shape(n, lambda x, y: seg(x, y, 10, 10, 54, 54) <= r or seg(x, y, 54, 10, 10, 54) <= r)


def check_mark(n=64):
    """the tick on a picked stage, from Slippi's Check model: a corner 1.05 units under the tile's centre, a long arm up to the right (2.4, 1.8) and a short one up to
    the left (-1.5, 0.45), round ends, the X's thickness; on the X's scale (9.17 texels a unit, centre 32, 32)"""
    import math
    def seg(x, y, x1, y1, x2, y2):
        dx, dy = x2 - x1, y2 - y1
        t = max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy)))
        return math.hypot(x - x1 - t * dx, y - y1 - t * dy)
    r = 4.3
    return shape(n, lambda x, y: seg(x, y, 32, 41.6, 54, 15.5) <= r or seg(x, y, 32, 41.6, 18.2, 27.9) <= r)


def write_png(path, px, w, h):
    sys.path.insert(0, os.path.join(REPO, "offline", "iso"))
    import hsdarc
    rgba = []
    for y in range(h):
        for x in range(w):
            i, a = px[y][x]
            v = (i * 17 * a + 48 * (15 - a)) // 15          # over a dark blue-grey, like the screen
            rgba.append((v, v, min(255, v + 24), 255))
    hsdarc.write_png(path, w, h, rgba)


def _inside(poly, x, y):
    """point in polygon (even-odd)"""
    c = False
    n = len(poly)
    for i in range(n):
        (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def _edge_dist(poly, x, y):
    import math
    best = 1e9
    n = len(poly)
    for i in range(n):
        (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % n]
        dx, dy = x2 - x1, y2 - y1
        t = max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy)))
        best = min(best, math.hypot(x - x1 - t * dx, y - y1 - t * dy))
    return best


SS = 8                                          # supersampling: 8 x 8 samples a texel (smooth edges at the small size the pictures are drawn)


def at(n, fn):
    """a coverage function written for a 64 texel canvas, sampled on an n texel one"""
    k = 64.0 / n
    return lambda x, y: fn(x * k, y * k)


def shape(n, fn):
    """n x n IA4 picture, white, alpha 0..15 from a 4 x 4 supersampled coverage function fn(x, y) -> bool"""
    px = []
    for y in range(n):
        row = []
        for x in range(n):
            c = sum(1 for sy in range(SS) for sx in range(SS) if fn(x + (sx + 0.5) / SS, y + (sy + 0.5) / SS))
            row.append((15, (c * 15 + SS * SS // 2) // (SS * SS)))
        px.append(row)
    return px


def shape2(n, outer, edge, fill_i=14, fill_a=13):
    """a two-tone picture as Slippi's chevron is: a white rim of `edge` texels along the outline, the inside a shade darker and a little see-through"""
    px = []
    for y in range(n):
        row = []
        for x in range(n):
            rim = fill = 0
            for sy in range(SS):
                for sx in range(SS):
                    fx, fy = x + (sx + 0.5) / SS, y + (sy + 0.5) / SS
                    if _inside(outer, fx, fy):
                        if _edge_dist(outer, fx, fy) <= edge:
                            rim += 1
                        else:
                            fill += 1
            tot = SS * SS
            a = (rim * 15 + fill * fill_a + tot // 2) // tot
            i = 15 if rim >= fill else fill_i
            row.append((i if a else 15, a))
        px.append(row)
    return px


def chevron(n=64):
    """the turn chevron, pointing right, in the proportions of Slippi's (75 x 87 there, 48 x 56 here at 8, 4): the two arms are cut straight down the left side, the head is one sharp
    point, a white rim (5 of 75 wide) round a slightly darker inside.  Until it was a solid shape with flat ends at the top and bottom, which read as a different arrow."""
    k, ox, oy = 48.0 / 75.0, 8.0, 4.0
    src = [(0, 0), (75, 43.5), (0, 87), (0, 62), (32, 43.5), (0, 25)]
    poly = [(ox + x * k, oy + y * 56.0 / 87.0) for x, y in src]
    return shape2(n, poly, 5.0 * k)


def outline_tri(n, x0, x1, y0, y1, stroke):
    """a right-pointing triangle drawn as an outline of `stroke` texels"""
    poly = [(x0, y0), (x0, y1), (x1, (y0 + y1) / 2.0)]
    return shape(n, lambda x, y: _inside(poly, x, y) and _edge_dist(poly, x, y) <= stroke)


def bracket(n=64):
    """the yellow cursor bracket under the selected stage, built as Slippi builds it from its two pieces (decoded a small outlined triangle and an outlined L-shaped bar with a
    darker inside, mirrored for the other end): a U-shaped bar with a stub rising at each end and a triangle outside each stub, all with a bright rim and a shaded see-through inside.
    The canvas is drawn 80 x 27 px (a texel is 1.25 x 0.42 px), so the rim is measured in screen pixels."""
    #, from the MODEL in Slippi's file (CSBoxSelector, hover joint: one strip 12 units wide and 1.44 tall under a border 7.8 x 7.06, its texture mirrored about the middle):
    # the tray is 1.54 x as wide as the tile; from each end inwards a triangle (1.27 units), a small gap, a stub 0.42 wide whose inner edge stands 0.2 OUTSIDE the tile's side, and the
    # bar (the lower half) running under the tile from stub to stub.  The version of the morning had the stubs inside the tile's width: a "TV stand".
    # Canvas: 62 of 64 texels for the 12 units (5.17 texels a unit), drawn 92 x 27 px (a texel is 1.44 x 0.42 px).
    kx, ky, rim = 1.44, 0.42, 1.25
    polys = [[(8.6, 22), (10.8, 22), (10.8, 34), (53.2, 34), (53.2, 22), (55.4, 22), (55.4, 46), (8.6, 46)], [(1, 46), (7.6, 46), (7.6, 22)], [(63, 46), (56.4, 46), (56.4, 22)]]
    spx = [[(x * kx, y * ky) for x, y in q] for q in polys]
    px = []
    for y in range(n):
        row = []
        for x in range(n):
            r = f = 0
            for sy in range(SS):
                for sx in range(SS):
                    fx, fy = (x + (sx + 0.5) / SS) * kx, (y + (sy + 0.5) / SS) * ky
                    for q in spx:
                        if _inside(q, fx, fy):
                            if _edge_dist(q, fx, fy) <= rim:
                                r += 1
                            else:
                                f += 1
                            break
            tot = SS * SS
            al = (r * 15 + f * 11 + tot // 2) // tot
            row.append(((15 if r >= f else 11) if al else 15, al))
        px.append(row)
    return px


def frame_sq(n=64):
    """the thin selection frame with rounded corners (Slippi's is a 1.5 texel line with a small corner radius on its 82 x 55 picture)"""
    def rr(x, y, m, r):
        dx = max(m + r - x, 0.0, x - (64 - m - r)); dy = max(m + r - y, 0.0, y - (64 - m - r))
        return m <= x <= 64 - m and m <= y <= 64 - m and dx * dx + dy * dy <= r * r
    return shape(n, lambda x, y: rr(x, y, 2.0, 5.0) and not rr(x, y, 3.9, 3.4))


def main(argv):
    if argv[:1] == ["--find"]:
        return 1                                    # nothing is read from a Slippi file any more: every picture is generated here
    if len(argv) < 1:
        print(__doc__)
        return 2
    out = argv[0]
    # THE CANVAS MATCHES THE SIZE ON SCREEN.  GX samples a texture with a 2 x 2 bilinear filter and no mip maps: a 64 texel canvas drawn 29 px wide (the chevron) or 18 px wide
    # (the small triangle) skips texels, and a slanted edge comes out wavy: on the cube the straight chevron read as a curved fish tail and the triangle as a bell, whatever the art was
    # (the 46 px X on its 64 canvas was fine).  So each picture is rastered here, with the supersampling as the only filter, on a canvas of about one texel a pixel: chevron and timeline
    # arrow 32, the small turn triangle 16; the X, the bracket and the frame are drawn large and stay 64.  Still powers of two (the plate's texture wrap), IA4 tiles are 8 x 4.
    # the chevron: Slippi's outline with slimmer arms (on Slippi's screen an arm is about a fifth of the chevron's height: its inside is see-through, so it reads lighter than the
    # texture's 29 %; a solid 29 % arm read as a filled arrowhead on the cube), a bright rim of about one pixel and a see-through inside (alpha 9 of 15)
    # compared with Slippi's own picture decoded next to ours ("bring up Slippi's originals"): its chevron is 75 x 87 with arms 25 tall (29 %), cut straight down the
    # left side, the notch at x 32, a white rim 5 wide and a light grey inside (intensity 14, nearly opaque).  The slim see-through version of was the wrong direction; the
    # waviness it tried to cure was the canvas size.  Drawn here from those measurements, filling the 32 canvas (x 2..30, y 0..32).
    cpoly = [(2.0 + x * 28.0 / 75.0, 1.0 + y * 30.0 / 87.0) for x, y in [(0, 0), (75, 43.5), (0, 87), (0, 62), (32, 43.5), (0, 25)]]
    chev = shape2(32, cpoly, 1.9, fill_i=14, fill_a=13)
    # the small turn triangle and the timeline arrow, icon for icon against Slippi's own pictures (31 x 36 and 36 x 36, decoded): a BOLD outlined triangle that fills its
    # picture, 0.86 as wide as tall, the stroke a sixth of its width.  Ours were a small thin triangle in the middle of a mostly empty canvas.  Both are drawn 10-14 px wide, so both
    # are rastered on a 16 texel canvas (about one texel a pixel) that the shape fills; the canvas is drawn square (gnb_prep_ev.h PR_TRI_*, PR_ARW_*).
    tpoly = [(7, 5), (7, 59), (57, 32)]            # (a texel of clear margin all round on the 16 canvas: the texture wraps)
    tri = shape(16, at(16, lambda x, y: _inside(tpoly, x, y) and _edge_dist(tpoly, x, y) <= 8.2))
    arw = shape(16, at(16, lambda x, y: _inside(tpoly, x, y) and _edge_dist(tpoly, x, y) <= 8.2))
    xm = x_mark(64)
    items = (("CHEV_R", chev, 32, 32), ("CHEV_L", flip(chev), 32, 32), ("TRI_R", tri, 16, 16), ("TRI_L", flip(tri), 16, 16), ("ARW", arw, 16, 16), ("X", xm, 64, 64), ("BRK", bracket(), 64, 64), ("FRM", frame_sq(), 64, 64), ("CHK", check_mark(64), 64, 64))
    o = ["/* Generated by tools/gnb_setup_art.py: the stage prep screen's pictures (turn chevron, small triangle, timeline arrow, strike X, cursor bracket, selection frame), drawn from measurements of a reference capture, as IA4 8x4 tiles (upside down), PackBits-packed (w * h bytes unpacked).  Do not edit; not stored in the repository. */",
         "#ifndef GNB_SETUP_ART_H", "#define GNB_SETUP_ART_H"]
    # THE PLATE HAS A STRETCH BAND.  The pictures are drawn on a copy of Melee's sort button plate (TyMnView), whose mesh is not one quad: its texture coordinate u runs
    # -1 .. 0.455 over x -5.78 .. 1.9, then 0.455 .. 0.5 over x 1.9 .. 3.24 (the band that lets the button grow: 4.5 % of the picture smeared over a fifth of its drawn width), then
    # 0.5.. 1 over x 3.24.. 5.88.  A picture that used the whole texture was bent just left of its middle: the X's arms did not line up, the chevron was lopsided (
    # after weeks of "mangled" icons).  Only u 0.5 .. 1 is drawn evenly, so every picture lives in the RIGHT HALF of a canvas twice as wide, as the rank badges always did
    # (gnb_art.h GART_RI_X: 2.64 units wide, centre at 4.56).
    for name, px, w, h in items:
        px, w = pad(px, w, h, 2 * w, h, ox=w), 2 * w
        b = packbits(tiles(px, w, h))
        o.append("#define GNB_SA_%s_W %du\n#define GNB_SA_%s_H %du" % (name, w, name, h))
        o.append("static const unsigned char GNB_SA_%s_PK[%d] = {" % (name, len(b)))
        for i in range(0, len(b), 24):
            o.append("    " + ",".join("0x%02X" % c for c in b[i:i + 24]) + ",")
        o.append("};")
    o.append("#endif")
    open(out, "w").write("\n".join(o) + "\n")
    if "--png" in argv:
        pp = argv[argv.index("--png") + 1]
        sheet = [[(0, 0)] * (66 * len(items)) for _ in range(64)]
        x0 = 0
        for name, px, w, h in items:
            for y in range(h):
                for x in range(w):
                    sheet[y][x0 + x] = px[y][x]
            x0 += 66
        write_png(pp, sheet, 66 * len(items), 64)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
