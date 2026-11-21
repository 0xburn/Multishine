#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Exploit-save banner and icon: SHIPPED = our own art ("shell"), LOCAL = the tester's own disc's art.

FINDING: the exploit save cannot simply carry our own banner/icon. Melee's card library (HSD, header verify)
compares the file's comment, banner and icons byte for byte with the copies the game itself holds (banner = opening.bnr image, icons = LbMcGame.usd MemCardIconData) and
checks a 16-byte digest after them; a save with other art is "damaged" and Name Entry never reaches the loader (proved: same card, only the art changed: loader never ran).
So the exact Nintendo bytes are REQUIRED by the game, but they are on the tester's own disc.  Therefore:
  strip(gci)             shell: the three art regions get OUR art and the digest is zeroed (the shipped loader saves in the release bundle; no Nintendo pixels)
  localize(gci, disc)    the patcher, at patch time: copies banner/icon/palette from the tester's own disc and recomputes the digest -> byte for byte the console-proven save
  is_shell(gci) / is_localized(gci)
Regions of the GCI (file data starts at 0x40; the comment 0x40..0x7F is a 64-byte factual title/date text and stays):
  0x0080-0x187F banner 96x32 RGB5A3 = opening.bnr[0x20:0x1820]     0x1880-0x1C7F icon 32x32 CI8 + 0x1C80-0x1E7F palette = LbMcGame.usd[0x4820:0x4E20]
  0x1E80-0x1E8F digest = HSD sum hash over the file data 0x0000..0x1E3F (= gci 0x40..0x1E7F)
CLI: save_art.py strip IN OUT | localize IN OUT DISC | check IN | png OUTDIR (previews of our art, needs Pillow)
Python standard library (Pillow only for previews)."""
import hashlib, math, os, struct, sys

BANNER_OFF, BANNER_LEN = 0x80, 96 * 32 * 2
ICON_OFF, ICON_LEN = 0x1880, 32 * 32
PAL_OFF, PAL_LEN = 0x1C80, 512


# The real logo: five interlocking open hexagons, the centre one heavier and brighter.
# (points, stroke width, stroke opacity) in drawing order, cyan on navy; transcribed from the SVG so this tool stays stdlib only.
LOGO_NAVY = (10, 24, 48)
LOGO_CYAN = (55, 214, 255)
LOGO = [
    ([(28.9, 57.2), (16.5, 64.4), (4.0, 57.2), (4.0, 42.8), (16.5, 35.6), (28.9, 42.8)], 1.08, 0.50),
    ([(96.0, 57.2), (83.5, 64.4), (71.1, 57.2), (71.1, 42.8), (83.5, 35.6), (96.0, 42.8)], 1.08, 0.50),
    ([(47.1, 58.0), (33.2, 66.0), (19.4, 58.0), (19.4, 42.0), (33.2, 34.0), (47.1, 42.0)], 1.73, 0.75),
    ([(80.6, 58.0), (66.8, 66.0), (52.9, 58.0), (52.9, 42.0), (66.8, 34.0), (80.6, 42.0)], 1.73, 0.75),
    ([(65.2, 58.8), (50.0, 67.6), (34.8, 58.8), (34.8, 41.2), (50.0, 32.4), (65.2, 41.2)], 2.48, 1.00),
]
FONT = {  # 5x7 pixel capitals for the wordmark
    "M": "10001 11011 10101 10101 10001 10001 10001", "U": "10001 10001 10001 10001 10001 10001 01110",
    "L": "10000 10000 10000 10000 10000 10000 11111", "T": "11111 00100 00100 00100 00100 00100 00100",
    "I": "01110 00100 00100 00100 00100 00100 01110", "S": "01111 10000 10000 01110 00001 00001 11110",
    "H": "10001 10001 10001 11111 10001 10001 10001", "N": "10001 11001 10101 10101 10011 10001 10001",
    "E": "11111 10000 10000 11110 10000 10000 11111"}


def _mark_cov(w, h, cx, cy, scale, wmul=1.0, ss=8, which=range(5)):
    """Logo mark coverage 0..1 per pixel: strokes (round joins) composited in order, supersampled ss x ss; centred at (cx, cy) px, scale = px per logo unit."""
    segs = []
    for pts, sw, op in [LOGO[i] for i in which]:
        sg = []
        for i in range(6):
            (ax, ay), (bx, by) = pts[i], pts[(i + 1) % 6]
            sg.append(((cx + (ax - 50) * scale, cy + (ay - 50) * scale), (cx + (bx - 50) * scale, cy + (by - 50) * scale)))
        segs.append((sg, (sw * scale * wmul / 2.0) ** 2, op))
    x0, x1 = int(cx - 50 * scale) - 2, int(cx + 50 * scale) + 2
    y0, y1 = int(cy - 20 * scale) - 2, int(cy + 20 * scale) + 2
    cov = [[0.0] * w for _ in range(h)]
    for y in range(max(0, y0), min(h, y1)):
        for x in range(max(0, x0), min(w, x1)):
            tot = 0.0
            for sy in range(ss):
                for sx in range(ss):
                    px, py = x + (sx + .5) / ss, y + (sy + .5) / ss
                    a = 0.0
                    for sg, hw2, op in segs:
                        for (ax, ay), (bx, by) in sg:
                            dx, dy = bx - ax, by - ay
                            t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
                            if (px - ax - t * dx) ** 2 + (py - ay - t * dy) ** 2 <= hw2:
                                a = a + (1 - a) * op
                                break
                    tot += a
            cov[y][x] = tot / (ss * ss)
    return cov


def _mix(a):
    return tuple(int(round(LOGO_NAVY[i] + (LOGO_CYAN[i] - LOGO_NAVY[i]) * a)) for i in range(3))


def _text(cov, s, x, y, a=0.9):
    for ch in s:
        for r, row in enumerate(FONT[ch].split()):
            for c, v in enumerate(row):
                if v == "1":
                    cov[y + r][x + c] = a
        x += 6


BANNER_LAYOUT = "A"


def banner_cov(layout=None):
    """96x32 banner coverage.  A: mark 57 px wide over the 59 px wordmark (chosen); B: mark left + MULTI/SHINE right; C: big mark, no wordmark."""
    L = layout or BANNER_LAYOUT
    if L == "A":
        cov = _mark_cov(96, 32, 48, 11.6, 0.62, 1.25)
        _text(cov, "MULTISHINE", 18, 24)
    elif L == "B":
        cov = _mark_cov(96, 32, 31, 16, 0.63, 1.25)
        _text(cov, "MULTI", 62, 8)
        _text(cov, "SHINE", 62, 18)
    else:
        cov = _mark_cov(96, 32, 48, 16, 0.85, 1.15)
    return cov


def banner_pixels(layout=None):
    cov = banner_cov(layout)
    return [[_mix(cov[y][x]) for x in range(96)] for y in range(32)]


ICON_STEPS = 64


def icon_pixels():
    # the mark only, reduced to its three heavier hexagons (the thin outer pair would be sub-pixel at 32 px), strokes 1.5x; 64 blend steps navy -> cyan (indices 0..63)
    cov = _mark_cov(32, 32, 16, 16, 0.49, 1.5, which=(2, 3, 4))
    pal = [_mix(i / float(ICON_STEPS - 1)) for i in range(ICON_STEPS)]
    idx = [[min(ICON_STEPS - 1, int(round(cov[y][x] * (ICON_STEPS - 1)))) for x in range(32)] for y in range(32)]
    return idx, pal


def _rgb5a3(c):
    return 0x8000 | ((c[0] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[2] >> 3)



def make_regions():
    px = banner_pixels()
    b = bytearray()
    for ty in range(0, 32, 4):               # RGB5A3: 4x4-pixel tiles, row-major inside the tile
        for tx in range(0, 96, 4):
            for y in range(4):
                for x in range(4):
                    b += struct.pack(">H", _rgb5a3(px[ty + y][tx + x]))
    idx, pal = icon_pixels()
    ic = bytearray()
    for ty in range(0, 32, 4):               # CI8: 8x4-pixel tiles
        for tx in range(0, 32, 8):
            for y in range(4):
                for x in range(8):
                    ic.append(idx[ty + y][tx + x])
    pb = b"".join(struct.pack(">H", _rgb5a3(c)) for c in pal)
    pb += struct.pack(">H", _rgb5a3(LOGO_NAVY)) * (256 - len(pal))
    assert len(b) == BANNER_LEN and len(ic) == ICON_LEN and len(pb) == PAL_LEN
    return bytes(b), bytes(ic), pb


OURS = None
# SHA-256 of the two disc-side regions (hashes only: lets the patcher refuse a disc that does not hold the expected bytes)
DISC_BANNER_SHA256 = "72b9f15e0885896ea78fc42655e2ca626dd8509453b6d7c54f89a2e5041a00cb"
DISC_ICON_SHA256 = "27cc1e567a22392a7005d078f6943599d5bc06094ab91908dcb81d1607db89f2"
DIGEST_OFF, DIGEST_LEN = 0x1E80, 16


def _ours():
    global OURS
    if OURS is None:
        OURS = make_regions()
    return OURS


def digest(gci):
    """HSD hsd_803B2B20 over the file data 0x0000..0x1E3F (gci 0x40..0x1E7F): 16 running byte sums seeded with the MD5 init words, then neighbours made different."""
    sp = bytearray.fromhex("0123456789abcdeffedcba9876543210")
    for i, b in enumerate(gci[0x40:0x1E80]):
        sp[i % 16] = (sp[i % 16] + b) & 255
    for i in range(1, 16):
        if sp[i - 1] == sp[i]:
            sp[i] ^= 0xFF
    return bytes(sp)


def _layout_ok(g):
    # Melee's exploit save layout: GALE01 / banner format 2 (RGB5A3) / image offset 0x40 / icon format 1 (CI8, shared palette) / 11 blocks
    return (len(g) >= 0x2040 and g[0:6] == b"GALE01" and g[7] == 2 and struct.unpack_from(">I", g, 0x2C)[0] == 0x40
            and struct.unpack_from(">H", g, 0x30)[0] == 1 and struct.unpack_from(">H", g, 0x38)[0] == 11)


def _put(g, banner, icon, pal, dig):
    if not _layout_ok(g):
        raise SystemExit("save_art: not the expected exploit-save layout (GALE01, banner RGB5A3, icon CI8, image offset 0x40, 11 blocks); nothing changed")
    out = bytearray(g)
    out[BANNER_OFF:BANNER_OFF + BANNER_LEN] = banner
    out[ICON_OFF:ICON_OFF + ICON_LEN] = icon
    out[PAL_OFF:PAL_OFF + PAL_LEN] = pal
    out[DIGEST_OFF:DIGEST_OFF + DIGEST_LEN] = dig if dig is not None else digest(bytes(out))
    return bytes(out)


def strip(g):
    """The shippable shell: our art, digest ZERO (a card manager shows our hexagons; Melee refuses it until it is localized)."""
    b, ic, pb = _ours()
    return _put(g, b, ic, pb, bytes(DIGEST_LEN))


def ours_full(g):
    """Our art AND the correct digest: the card download for a disc that carries our art; no Nintendo bytes anywhere."""
    b, ic, pb = _ours()
    return _put(g, b, ic, pb, None)


def is_shell(g):
    b, ic, pb = _ours()
    return (g[BANNER_OFF:BANNER_OFF + BANNER_LEN] == b and g[ICON_OFF:ICON_OFF + ICON_LEN] == ic and g[PAL_OFF:PAL_OFF + PAL_LEN] == pb
            and g[DIGEST_OFF:DIGEST_OFF + DIGEST_LEN] == bytes(DIGEST_LEN))


def disc_art(disc_path):
    """-> (banner 6144 B, icon 1024 B, palette 512 B) read from the tester's own disc image (read only), hash-checked."""
    for d in ("offline/iso", "tools/baker/vendor"):
        p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", d)
        if os.path.isdir(p) and p not in sys.path:
            sys.path.insert(0, p)
    import gcm
    img = gcm.Image(disc_path)
    try:
        def rd(name):
            n = img.lookup(name)[0]
            if n is None or n.is_dir:
                raise SystemExit("save_art: %s not found on the disc" % name)
            return n.src.read(img.stream)
        bn, lb = rd("opening.bnr"), rd("LbMcGame.usd")
    finally:
        img.close()
    banner, icpal = bn[0x20:0x20 + BANNER_LEN], lb[0x4820:0x4820 + ICON_LEN + PAL_LEN]
    if hashlib.sha256(banner).hexdigest() != DISC_BANNER_SHA256 or hashlib.sha256(icpal).hexdigest() != DISC_ICON_SHA256:
        raise SystemExit("save_art: the banner/icon data on this disc is not the one of Melee NTSC-U 1.02 (wrong disc image?)")
    return banner, icpal[:ICON_LEN], icpal[ICON_LEN:]


def localize(g, disc_path):
    """Shell -> the real save (banner, icon, palette from the disc, digest recomputed).  Refuses anything that is not our shell."""
    if not is_shell(g):
        raise SystemExit("save_art: this loader save is not a shell (it already carries other art); not modified")
    b, ic, pb = disc_art(disc_path)
    return _put(g, b, ic, pb, None)


def is_localized(g, disc_path):
    b, ic, pb = disc_art(disc_path)
    return g[BANNER_OFF:BANNER_OFF + BANNER_LEN] == b and g[ICON_OFF:ICON_OFF + ICON_LEN] == ic and g[PAL_OFF:PAL_OFF + PAL_LEN] == pb and g[DIGEST_OFF:DIGEST_OFF + DIGEST_LEN] == digest(g)


def main(argv):
    if len(argv) == 3 and argv[0] == "strip":
        open(argv[2], "wb").write(strip(open(argv[1], "rb").read())); return 0
    if len(argv) == 4 and argv[0] == "localize":
        open(argv[2], "wb").write(localize(open(argv[1], "rb").read(), argv[3])); return 0
    if len(argv) == 2 and argv[0] == "check":
        g = open(argv[1], "rb").read(); print("shell (our art, no Nintendo pixels)" if is_shell(g) else "NOT a shell"); return 0 if is_shell(g) else 1
    if len(argv) == 2 and argv[0] == "png":
        from PIL import Image
        os.makedirs(argv[1], exist_ok=True)
        im = Image.new("RGB", (96, 32))
        im.putdata([c for row in banner_pixels() for c in row]); im.resize((384, 128), Image.NEAREST).save(os.path.join(argv[1], "banner.png"))
        idx, pal = icon_pixels()
        im = Image.new("RGB", (32, 32)); im.putdata([pal[i] for row in idx for i in row]); im.resize((128, 128), Image.NEAREST).save(os.path.join(argv[1], "icon.png"))
        return 0
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
