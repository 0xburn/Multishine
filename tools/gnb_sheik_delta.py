#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Game Night pick grid: Sheik's own icon as a DELTA against the retail Zelda icon ("We only ship the delta").  Sheik's picture is the character icon of
Slippi's GameSetup_gui.dat (project-slippi/Ishiiruka, GPL-2.0; the Rooms character grid uses it): the 20th of its 26 60x51 CI8 images with the 28th of its 256-entry
palettes.  It is resized to the retail icon's 64x56, quantized to 256 colours and written in the retail block layout (CI8 8x4 tiles + RGB5A3 palette at +0xE00, 4 KB);
the header holds ONLY that block XOR the retail Zelda block (MnSlChr.usd, FNV 0xF4CF48D1), PackBits-packed, plus the FNV of the result.  The console applies it to its
copy of Zelda's block read from the player's disc (gnb_art.c); any other base gives today's Zelda-with-SHEIK-caption.  Generated at build time into the build's work
directory (sg_ovl_pack.py), never stored in the repository.

  gnb_sheik_delta.py GAMESETUP_GUI.dat MNSLCHR.usd  [--png PREVIEW.png]   exit 0 written, 2 an input is not the expected file
  gnb_sheik_delta.py --find                                                    prints the GameSetup_gui.dat and MnSlChr.usd paths (one a line), exit 1 if either is missing
"""
import hashlib
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GS_REL = ".analysis-deps/work/rooms_beta2/src/x/Ishiiruka-rooms-beta-2/Data/Sys/GameFiles/GALE01/GameSetup_gui.dat"
CSS_RELS = (".analysis-deps/work/probe_check/ovl/files/MnSlChr.usd",)
GS_SHA1 = "f611b333de3324a11c0d084910e04283c5e364a5"
CSS_SIZE, ZELDA_OFF, ZELDA_FNV = 3811401, 0x2DC80, 0xF4CF48D1
SHEIK_IMG, SHEIK_TLUT = 19, 27


def fnv(b):
    h = 0x811C9DC5
    for c in b:
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


def c565(v):
    return ((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, 255)


def c5a3(v):
    if v & 0x8000:
        return ((v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
    return ((v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17, (v >> 12 & 7) * 255 // 7)


def to5a3(r, g, b, a):
    if a >= 224:
        return 0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3)
    return (a >> 5) << 12 | (r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)


def sheik_rgba(gs):
    d = gs[0x20:]
    imgs = [o for o in range(0, len(d) - 16, 4) if struct.unpack(">IHHI", d[o:o + 12])[1:] == (60, 51, 9) and struct.unpack(">I", d[o:o + 4])[0]]
    tls = []
    for o in range(0, len(d) - 16, 4):
        p, fmt, _, n = struct.unpack(">IIIH", d[o:o + 14])
        if p and p < len(d) and fmt in (0, 1, 2) and n == 256:
            tls.append(o)
    if len(imgs) != 26 or len(tls) != 38:
        return None
    p = struct.unpack(">I", d[imgs[SHEIK_IMG]:imgs[SHEIK_IMG] + 4])[0]
    tp, fmt = struct.unpack(">II", d[tls[SHEIK_TLUT]:tls[SHEIK_TLUT] + 8])
    conv = {1: c565, 2: c5a3}.get(fmt)
    if conv is None:
        return None
    pal = [conv(struct.unpack(">H", d[tp + 2 * i:tp + 2 * i + 2])[0]) for i in range(256)]
    px, i = [[None] * 60 for _ in range(51)], 0
    for by in range(0, 52, 4):
        for bx in range(0, 64, 8):
            for y in range(4):
                for x in range(8):
                    v = d[p + i]
                    i += 1
                    if bx + x < 60 and by + y < 51:
                        px[by + y][bx + x] = pal[v]
    return px


FRAME_W = 2                                                    # the retail frame is 2 px wide on the 64 x 56 icon (light 197 outer, grey 148 inner line)
FRAME_GREYS = (187, 197, 148, 136)                             # its exact grey levels; a portrait pixel is never one of these at the edge by accident (checked on every retail icon)


def decode_block(b):
    """RGBA of the 64 x 56 retail icon block (CI8 8x4 tiles, RGB5A3 palette at +0xE00) as {(x, y): (r, g, b, a)}"""
    pal = [c5a3(struct.unpack(">H", b[0xE00 + 2 * k:0xE02 + 2 * k])[0]) for k in range(256)]
    return {(x, y): pal[b[((y >> 2) * 8 + (x >> 3)) * 32 + (y & 3) * 8 + (x & 7)]] for y in range(56) for x in range(64)}


def frame_pixels(block):
    """the frame of a retail icon block: {(x, y): rgba} for the grey, within 3 px of the edge (the corners' translucent pixels included, the portrait's transparent
    background is not)"""
    px = decode_block(block)
    return {(x, y): c for (x, y), c in px.items() if min(x, y, 63 - x, 55 - y) <= 3 and c[0] == c[1] == c[2] and c[0] in FRAME_GREYS}


def build(gs_path, css_path):
    from PIL import Image
    gs = open(gs_path, "rb").read()
    if hashlib.sha1(gs).hexdigest() != GS_SHA1:
        return None, "GameSetup_gui.dat is not the rooms-beta-2 / PR 474 file"
    css = open(css_path, "rb").read()
    if len(css) != CSS_SIZE:
        return None, "MnSlChr.usd is not the NTSC 1.02 file"
    zelda = css[0x20 + ZELDA_OFF:0x20 + ZELDA_OFF + 0x1000]
    if fnv(zelda) != ZELDA_FNV:
        return None, "the Zelda icon block does not match"
    src = sheik_rgba(gs)
    if src is None:
        return None, "Sheik's icon not found in GameSetup_gui.dat"
    im = Image.new("RGBA", (60, 51))
    for y in range(51):
        for x in range(60):
            im.putpixel((x, y), src[y][x])
    # the portrait goes INSIDE the retail frame: the frame pixels (the light rounded border, the grey inner line, the translucent corners) are read from the Zelda
    # block itself, so the tile has the same frame as every other character (Sheik showed no frame)
    fr = frame_pixels(zelda)
    inner = im.resize((64 - 2 * FRAME_W, 56 - 2 * FRAME_W), Image.LANCZOS).convert("RGB")
    ext = sorted(set(fr.values()))
    if len(ext) > 32:
        return None, "the Zelda frame has an unexpected number of colours"
    img = Image.new("RGB", (64, 56), (0, 0, 0))
    img.paste(inner, (FRAME_W, FRAME_W))
    q = img.quantize(colors=256 - len(ext), method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:768] + [0] * max(0, 768 - len(q.getpalette()))
    nq = 256 - len(ext)
    alpha = [255] * 256
    for k, c in enumerate(ext):
        pal[3 * (nq + k):3 * (nq + k) + 3] = list(c[:3])
        alpha[nq + k] = c[3]
    q_idx = {c: nq + k for k, c in enumerate(ext)}
    block = bytearray(0x1000)
    for y in range(56):
        for x in range(64):
            block[((y >> 2) * 8 + (x >> 3)) * 32 + (y & 3) * 8 + (x & 7)] = q_idx[fr[(x, y)]] if (x, y) in fr else q.getpixel((x, y))
    for k in range(256):
        struct.pack_into(">H", block, 0xE00 + 2 * k, to5a3(pal[3 * k], pal[3 * k + 1], pal[3 * k + 2], alpha[k]))
    return bytes(block), zelda


def packbits(b):
    out, i, n = bytearray(), 0, len(b)
    while i < n:
        j = i
        while j + 1 < n and b[j + 1] == b[i] and j - i < 127:
            j += 1
        if j > i:
            out += bytes([126 + (j - i + 1), b[i]])                    # run of 2..128: header 128..254 (gnb_art.c: h - 126 copies)
            i = j + 1
            continue
        j = i
        while j < n and j - i < 128 and not (j + 1 < n and b[j + 1] == b[j]):
            j += 1
        out += bytes([j - i - 1]) + b[i:j]
        i = j
    return bytes(out)


def unpack(pk, n):
    out, i = bytearray(), 0
    while len(out) < n:
        h = pk[i]
        i += 1
        if h < 128:
            out += pk[i:i + h + 1]
            i += h + 1
        else:
            out += bytes([pk[i]]) * (h - 126)
            i += 1
    return bytes(out[:n])


def find():
    gs = os.environ.get("GNB_SHEIK_GUI") or os.path.join(REPO, GS_REL)
    css = os.environ.get("GNB_SHEIK_CSS") or next((os.path.join(REPO, r) for r in CSS_RELS if os.path.exists(os.path.join(REPO, r))), "")
    return (gs, css) if os.path.isfile(gs) and css and os.path.isfile(css) else None


def main(argv):
    if argv[1:] == ["--find"]:
        f = find()
        if not f:
            return 1
        print("%s\n%s" % f)
        return 0
    if len(argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    sheik, zelda = build(argv[1], argv[2])
    if sheik is None:
        sys.stderr.write("gnb_sheik_delta: %s\n" % zelda)
        return 2
    x = bytes(a ^ b for a, b in zip(sheik, zelda))
    pk = packbits(x)
    assert unpack(pk, 0x1000) == x
    with open(argv[3], "w") as f:
        f.write("/* generated by tools/gnb_sheik_delta.py at build time: Sheik's icon as XOR against the retail Zelda icon block, PackBits (never stored in the repository).\n"
                "   Derived from Slippi's GameSetup_gui.dat (project-slippi/Ishiiruka, GPL-2.0) against the retail Zelda icon. */\n")
        f.write("#define GNB_SD_N %du\n#define GNB_SD_SUM 0x%08Xu\nstatic const unsigned char GNB_SD[GNB_SD_N] = {\n" % (len(pk), fnv(sheik)))
        for i in range(0, len(pk), 24):
            f.write("    " + ",".join("0x%02X" % c for c in pk[i:i + 24]) + ",\n")
        f.write("};\n")
    if "--png" in argv:
        from PIL import Image
        im = Image.new("RGBA", (64, 56))
        for y in range(56):
            for x in range(64):
                v = sheik[((y >> 2) * 8 + (x >> 3)) * 32 + (y & 3) * 8 + (x & 7)]
                im.putpixel((x, y), c5a3(struct.unpack(">H", sheik[0xE00 + 2 * v:0xE02 + 2 * v])[0]))
        im.resize((256, 224), Image.NEAREST).save(argv[argv.index("--png") + 1])
    print("gnb_sheik_delta: %d bytes (block 4096)" % len(pk))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
