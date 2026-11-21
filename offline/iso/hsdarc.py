#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Small HSD archive (.dat/.usd) parser + structural differ (stdlib only).

HSD archive layout (big endian):
  0x00 file_size, 0x04 data_size, 0x08 reloc_count, 0x0C root_count, 0x10 ref_count, 0x14..0x1F pad
  0x20 data block (data_size bytes; all offsets below are relative to 0x20)
  reloc table: reloc_count u32 = data offsets of pointer slots
  root table: root_count x (data offset, string-table offset)
  ref table: ref_count x (data offset, string-table offset)
  string table (NUL-terminated symbol names)

"Objects" are approximated as the ranges between consecutive pointer targets / root offsets.
Two archives are compared symbol by symbol: each root's reachable object graph is serialised in
DFS order with pointers replaced by (DFS index, offset-in-object), so layout moves do not count as
changes but any content/shape change does.

usage: hsdarc.py info FILE | hsdarc.py diff OLD NEW [--strings]
"""
import bisect
import hashlib
import struct
import sys


class HsdError(ValueError):
    pass


class Archive:
    def __init__(self, data, name="?"):
        self.name = name
        if len(data) < 0x20:
            raise HsdError("too small")
        (self.file_size, self.data_size, self.reloc_count, self.root_count,
         self.ref_count) = struct.unpack_from(">5I", data, 0)
        if self.file_size != len(data):
            raise HsdError("{}: header file_size {} != {}".format(name, self.file_size, len(data)))
        self.data = data[0x20:0x20 + self.data_size]
        p = 0x20 + self.data_size
        self.relocs = list(struct.unpack_from(">{}I".format(self.reloc_count), data, p))
        p += 4 * self.reloc_count
        roots = [struct.unpack_from(">II", data, p + 8 * i) for i in range(self.root_count)]
        p += 8 * self.root_count
        refs = [struct.unpack_from(">II", data, p + 8 * i) for i in range(self.ref_count)]
        p += 8 * self.ref_count
        self.strtab = data[p:]

        def sym(o):
            e = self.strtab.find(b"\0", o)
            return self.strtab[o:e].decode("latin-1")

        self.roots = [(sym(s), off) for off, s in roots]
        self.refs = [(sym(s), off) for off, s in refs]
        self.ptr = {}
        for r in self.relocs:
            if r + 4 > len(self.data):
                raise HsdError("reloc outside data")
            self.ptr[r] = struct.unpack_from(">I", self.data, r)[0]
        starts = set(self.ptr.values()) | {off for _, off in self.roots} | {0}
        self.starts = sorted(s for s in starts if s < len(self.data))
        self.slots = sorted(self.ptr)

    def obj_range(self, off):
        i = bisect.bisect_right(self.starts, off) - 1
        s = self.starts[i]
        e = self.starts[i + 1] if i + 1 < len(self.starts) else len(self.data)
        return s, e

    def graph(self, root_off):
        """DFS from a root; returns list of (start, end, [(slot_rel, target_idx, target_rel)])."""
        order, index = [], {}
        stack = [root_off]
        while stack:
            off = stack.pop()
            s, e = self.obj_range(off)
            if s in index:
                continue
            index[s] = len(order)
            order.append((s, e))
            lo = bisect.bisect_left(self.slots, s)
            hi = bisect.bisect_left(self.slots, e)
            for slot in reversed(self.slots[lo:hi]):
                stack.append(self.ptr[slot])
        out = []
        for s, e in order:
            lo = bisect.bisect_left(self.slots, s)
            hi = bisect.bisect_left(self.slots, e)
            links = []
            for slot in self.slots[lo:hi]:
                t = self.ptr[slot]
                ts, _ = self.obj_range(t)
                links.append((slot - s, index[ts], t - ts))
            out.append((s, e, links))
        return out

    def canon(self, root_off):
        """Canonical bytes per object (pointer slots zeroed, links kept separately)."""
        res = []
        for s, e, links in self.graph(root_off):
            b = bytearray(self.data[s:e])
            for rel, _, _ in links:
                b[rel:rel + 4] = b"\0\0\0\0"
            res.append((s, e, bytes(b), tuple(links)))
        return res


def symbol_diff(old, new):
    """Per-symbol comparison. Returns list of dicts."""
    oroots, nroots = dict(old.roots), dict(new.roots)
    rows = []
    for name in sorted(set(oroots) | set(nroots), key=lambda n: (n not in oroots, n)):
        row = {"symbol": name}
        if name not in oroots:
            g = new.canon(nroots[name])
            row.update(status="added", new_objects=len(g), new_bytes=sum(e - s for s, e, _, _ in g))
        elif name not in nroots:
            row.update(status="removed")
        else:
            go, gn = old.canon(oroots[name]), new.canon(nroots[name])
            ho = hashlib.sha1(repr([(b, l) for _, _, b, l in go]).encode("latin-1")).hexdigest()
            hn = hashlib.sha1(repr([(b, l) for _, _, b, l in gn]).encode("latin-1")).hexdigest()
            row.update(old_objects=len(go), new_objects=len(gn),
                       old_bytes=sum(e - s for s, e, _, _ in go), new_bytes=sum(e - s for s, e, _, _ in gn))
            if ho == hn:
                row["status"] = "same"
            else:
                row["status"] = "changed"
                # parallel DFS walk: count objects whose content or links differ
                diff_objs, diff_bytes, ranges = 0, 0, []
                for i in range(max(len(go), len(gn))):
                    a = go[i] if i < len(go) else None
                    b = gn[i] if i < len(gn) else None
                    if a is None or b is None or a[2] != b[2] or a[3] != b[3]:
                        diff_objs += 1
                        if a and b and len(a[2]) == len(b[2]):
                            diff_bytes += sum(1 for x, y in zip(a[2], b[2]) if x != y)
                        else:
                            diff_bytes += len((b or a)[2])
                        if b is not None and len(ranges) < 8:
                            ranges.append("new+0x{:X}..0x{:X}".format(b[0] + 0x20, b[1] + 0x20))
                row.update(diff_objects=diff_objs, diff_bytes=diff_bytes, first_new_ranges=ranges)
        rows.append(row)
    return rows


# ---------------------------------------------------------------------------
# SIS text (Sd*.usd/.dat): byte-code strings; 0x2X YY = common glyph, 0x4X YY = file glyph
# ---------------------------------------------------------------------------
SIS_PARAMS = {0x05: 2, 0x06: 4, 0x07: 4, 0x0A: 4, 0x0C: 3, 0x0E: 4}
SIS_PUNCT = {0xE7: ".", 0xEB: "?", 0xF3: "'"}


def _common_glyph(i):
    if i < 10:
        return chr(ord("0") + i)
    if i < 36:
        return chr(ord("A") + i - 10)
    if i < 62:
        return chr(ord("a") + i - 36)
    return SIS_PUNCT.get(i, "{%X}" % i)


def sis_decode(data, off):
    """Decode one SIS string at data offset `off` -> (text, raw_bytes)."""
    out, i = [], off
    while i < len(data):
        c = data[i]
        if c == 0x00:
            i += 1
            break
        if 0x20 <= c < 0x30:
            out.append(_common_glyph(((c & 0xF) << 8) | data[i + 1]))
            i += 2
        elif 0x40 <= c < 0x50:
            out.append("{S%X}" % (((c & 0xF) << 8) | data[i + 1]))
            i += 2
        elif c == 0x1A:
            out.append(" ")
            i += 1
        elif c == 0x03:
            out.append("|")
            i += 1
        else:
            i += 1 + SIS_PARAMS.get(c, 0)
    return "".join(out), bytes(data[off:i])


def sis_table(arc):
    """List of (index, text, raw) for the pointer table of the first root symbol."""
    root = arc.roots[0][1]
    s, e = arc.obj_range(root)
    rows = []
    for idx, slot in enumerate(range(s, e, 4)):
        if slot not in arc.ptr:
            rows.append((idx, None, None))
            continue
        text, raw = sis_decode(arc.data, arc.ptr[slot])
        rows.append((idx, text, raw))
    return rows


# ---------------------------------------------------------------------------
# textures (HSD ImageDesc: image_ptr, u16 w, u16 h, u32 GX format, ...)
# ---------------------------------------------------------------------------
GX_FMT = {0: ("I4", 4, 8, 8), 1: ("I8", 8, 8, 4), 2: ("IA4", 8, 8, 4), 3: ("IA8", 16, 4, 4),
          4: ("RGB565", 16, 4, 4), 5: ("RGB5A3", 16, 4, 4), 6: ("RGBA8", 32, 4, 4),
          8: ("C4", 4, 8, 8), 9: ("C8", 8, 8, 4), 10: ("C14X2", 16, 4, 4), 14: ("CMPR", 4, 8, 8)}


def tex_size(w, h, fmt):
    _, bpp, bw, bh = GX_FMT[fmt]
    return ((w + bw - 1) // bw * bw) * ((h + bh - 1) // bh * bh) * bpp // 8


def find_textures(arc):
    """[(desc_off, data_off, w, h, fmt, size)] for every plausible ImageDesc in the archive."""
    out = []
    for slot in arc.slots:
        if arc.obj_range(slot)[0] != slot or slot + 12 > len(arc.data):
            continue
        w, h = struct.unpack_from(">HH", arc.data, slot + 4)
        fmt = struct.unpack_from(">I", arc.data, slot + 8)[0]
        if fmt in GX_FMT and 1 <= w <= 1024 and 1 <= h <= 1024:
            d = arc.ptr[slot]
            n = tex_size(w, h, fmt)
            if d + n <= len(arc.data):
                out.append((slot, d, w, h, fmt, n))
    return out


def _rgb565(v):
    return ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255)


def _rgb5a3(v):
    if v & 0x8000:
        return (((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
    return (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 7) * 255 // 7)


def decode_texture(buf, w, h, fmt):
    """-> list of RGBA tuples (row major). Palette formats are shown as grey indices."""
    name, bpp, bw, bh = GX_FMT[fmt]
    pw, ph = (w + bw - 1) // bw * bw, (h + bh - 1) // bh * bh
    px = [(0, 0, 0, 0)] * (pw * ph)
    pos = 0
    for by in range(0, ph, bh):
        for bx in range(0, pw, bw):
            if name == "CMPR":
                for sub in range(4):
                    sx, sy = bx + (sub & 1) * 4, by + (sub >> 1) * 4
                    c0, c1 = struct.unpack_from(">HH", buf, pos)
                    bits = struct.unpack_from(">I", buf, pos + 4)[0]
                    pos += 8
                    a, b = _rgb565(c0), _rgb565(c1)
                    if c0 > c1:
                        pal = [a, b, tuple((2 * x + y) // 3 for x, y in zip(a, b)),
                               tuple((x + 2 * y) // 3 for x, y in zip(a, b))]
                    else:
                        pal = [a, b, tuple((x + y) // 2 for x, y in zip(a, b)), (0, 0, 0, 0)]
                    for k in range(16):
                        px[(sy + k // 4) * pw + sx + k % 4] = pal[(bits >> (30 - 2 * k)) & 3]
                continue
            if name == "RGBA8":
                for k in range(16):
                    ar, gb = buf[pos + 2 * k:pos + 2 * k + 2], buf[pos + 32 + 2 * k:pos + 34 + 2 * k]
                    px[(by + k // 4) * pw + bx + k % 4] = (ar[1], gb[0], gb[1], ar[0])
                pos += 64
                continue
            for k in range(bw * bh):
                x, y = bx + k % bw, by + k // bw
                if bpp == 4:
                    v = (buf[pos + k // 2] >> (4 if k % 2 == 0 else 0)) & 15
                    c = (v * 17, v * 17, v * 17, 255)
                elif bpp == 8:
                    v = buf[pos + k]
                    if name == "IA4":
                        c = ((v & 15) * 17,) * 3 + ((v >> 4) * 17,)
                    else:
                        c = (v, v, v, 255)
                else:
                    v = struct.unpack_from(">H", buf, pos + 2 * k)[0]
                    if name == "IA8":
                        c = (v & 255,) * 3 + (v >> 8,)
                    elif name == "RGB565":
                        c = _rgb565(v)
                    elif name == "RGB5A3":
                        c = _rgb5a3(v)
                    else:
                        c = ((v & 0x3FFF) >> 6,) * 3 + (255,)
                px[y * pw + x] = c
            pos += bw * bh * bpp // 8
    return [px[y * pw + x] for y in range(h) for x in range(w)]


def write_png(path, w, h, rgba):
    import zlib
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for r, g, b, a in rgba[y * w:(y + 1) * w]:
            raw += bytes((r, g, b, a))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def ascii_strings(data, minlen=5):
    out, cur = [], bytearray()
    for b in data:
        if 32 <= b < 127:
            cur.append(b)
        else:
            if len(cur) >= minlen:
                out.append(cur.decode())
            cur = bytearray()
    if len(cur) >= minlen:
        out.append(cur.decode())
    return out


def main(argv):
    if len(argv) >= 2 and argv[0] == "info":
        a = Archive(open(argv[1], "rb").read(), argv[1])
        print("file_size={} data={} relocs={} roots={} refs={}".format(
            a.file_size, a.data_size, a.reloc_count, a.root_count, a.ref_count))
        for n, o in a.roots:
            g = a.graph(o)
            print("  root {:<40} @0x{:06X} objects={} bytes={}".format(n, o + 0x20, len(g),
                                                                      sum(e - s for s, e, _ in g)))
        for n, o in a.refs:
            print("  ref  {:<40} @0x{:06X}".format(n, o + 0x20))
        return 0
    if len(argv) >= 3 and argv[0] == "diff":
        old = Archive(open(argv[1], "rb").read(), argv[1])
        new = Archive(open(argv[2], "rb").read(), argv[2])
        for r in symbol_diff(old, new):
            print(r)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
