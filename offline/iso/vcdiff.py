#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Minimal RFC 3284 VCDIFF decoder (stdlib only).

Enough for Slippi's Sys/GameFiles/*.diff (open-vcdiff output, default code table, VCD_SOURCE
windows, no secondary compression). Supports VCD_SOURCE / VCD_TARGET windows and the VCD_ADLER32
(0x04) window extension of open-vcdiff (varint) and xdelta3 (4 raw bytes), checksum verified. Rejects secondary compressors, custom
code tables and compressed sections.

usage: vcdiff.py SOURCE DELTA OUT
"""
import sys
import zlib

NOOP, ADD, RUN, COPY = 0, 1, 2, 3
MAX_WINDOW = 1 << 27    # 128 MiB per target window: far above any Melee disc file, bounds what a hostile delta can make us allocate


def _default_code_table():
    # RFC 3284 section 5.6
    t = [((RUN, 0, 0), (NOOP, 0, 0))]
    for size in [0] + list(range(1, 18)):
        t.append(((ADD, size, 0), (NOOP, 0, 0)))
    for mode in range(9):
        t.append(((COPY, 0, mode), (NOOP, 0, 0)))
        for size in range(4, 19):
            t.append(((COPY, size, mode), (NOOP, 0, 0)))
    for mode in range(6):
        for add_size in range(1, 5):
            for copy_size in range(4, 7):
                t.append(((ADD, add_size, 0), (COPY, copy_size, mode)))
    for mode in range(6, 9):
        for add_size in range(1, 5):
            t.append(((ADD, add_size, 0), (COPY, 4, mode)))
    for mode in range(9):
        t.append(((COPY, 4, mode), (ADD, 1, 0)))
    assert len(t) == 256
    return t


CODE_TABLE = _default_code_table()


class VcdiffError(ValueError):
    pass


class _Reader:
    def __init__(self, data):
        self.b, self.p = data, 0

    def byte(self):
        if self.p >= len(self.b):
            raise VcdiffError("truncated delta")
        v = self.b[self.p]
        self.p += 1
        return v

    def varint(self):
        v = 0
        for _ in range(10):
            c = self.byte()
            v = (v << 7) | (c & 0x7F)
            if not c & 0x80:
                return v
        raise VcdiffError("varint too long")

    def take(self, n):
        if self.p + n > len(self.b):
            raise VcdiffError("truncated delta")
        v = self.b[self.p:self.p + n]
        self.p += n
        return v

    def done(self):
        return self.p >= len(self.b)


def decode(source, delta, trace=None):
    """Return the target bytes. `source` is the dictionary (the retail file).

    If `trace` is a list, (target_offset, length, kind, source_addr) tuples are appended for
    every instruction: kind is "add", "run", "copy" (from the source file) or "self" (from the
    target produced so far); source_addr is None for add/run."""
    r = _Reader(delta)
    magic = r.take(4)
    if magic[:3] != b"\xd6\xc3\xc4" or magic[3] != 0:
        raise VcdiffError("not a VCDIFF v0 delta")
    hdr = r.byte()
    if hdr & 0x01:
        raise VcdiffError("secondary compressor not supported")
    if hdr & 0x02:
        raise VcdiffError("custom code table not supported")
    if hdr & 0x04:
        r.take(r.varint())
    out = bytearray()
    while not r.done():
        win = r.byte()
        if win & 0x03 == 0x03:
            raise VcdiffError("both VCD_SOURCE and VCD_TARGET set")
        seg = b""
        if win & 0x03:
            seglen, segpos = r.varint(), r.varint()
            seg = source[segpos:segpos + seglen] if win & 0x01 else bytes(out[segpos:segpos + seglen])
            if len(seg) != seglen:
                raise VcdiffError("source segment out of range")
        delta_len = r.varint()  # length of the delta encoding (everything below)
        delta_end = r.p + delta_len
        tgtlen = r.varint()
        if tgtlen > MAX_WINDOW:
            raise VcdiffError("target window of {} bytes is larger than any Melee file".format(tgtlen))
        if r.byte():
            raise VcdiffError("compressed sections not supported")
        data_len, inst_len, addr_len = r.varint(), r.varint(), r.varint()
        checksum = None
        if win & 0x04:
            # open-vcdiff stores the Adler-32 as a varint, xdelta3 as 4 raw big-endian bytes; the
            # field length is whatever the section lengths leave over, and the value is verified
            n = delta_end - r.p - data_len - inst_len - addr_len
            if not 1 <= n <= 5:
                raise VcdiffError("bad checksum field length {}".format(n))
            raw = r.take(n)
            checksum = []
            if n == 4:
                checksum.append(int.from_bytes(raw, "big"))
            checksum.append(_Reader(raw).varint() if raw[-1] & 0x80 == 0 else -1)
        data, inst, addr = _Reader(r.take(data_len)), _Reader(r.take(inst_len)), _Reader(r.take(addr_len))
        near, near_i, same = [0] * 4, 0, [0] * (3 * 256)
        t = bytearray()
        s_len = len(seg)
        while not inst.done():
            for typ, size, mode in CODE_TABLE[inst.byte()]:
                if typ == NOOP:
                    continue
                if size == 0:
                    size = inst.varint()
                if size > tgtlen - len(t):  # never grow past the declared window (memory bomb guard; RFC 3284: a window is exactly tgtlen bytes)
                    raise VcdiffError("instruction runs past the target window")
                base = len(out) + len(t)
                if typ == ADD:
                    t += data.take(size)
                    if trace is not None:
                        trace.append((base, size, "add", None))
                elif typ == RUN:
                    t += bytes([data.byte()]) * size
                    if trace is not None:
                        trace.append((base, size, "run", None))
                else:
                    here = s_len + len(t)
                    if mode == 0:
                        a = addr.varint()
                    elif mode == 1:
                        a = here - addr.varint()
                    elif mode < 6:
                        a = near[mode - 2] + addr.varint()
                    else:
                        a = same[(mode - 6) * 256 + addr.byte()]
                    if not 0 <= a < here:  # RFC 3284 5.3: a COPY address lies before the current position
                        raise VcdiffError("copy address {} out of range".format(a))
                    near[near_i] = a
                    near_i = (near_i + 1) % 4
                    same[a % (3 * 256)] = a
                    if trace is not None:
                        if a < s_len and win & 0x01:
                            trace.append((base, size, "copy", segpos + a))
                        else:
                            trace.append((base, size, "self", a))
                    if a + size <= s_len:
                        t += seg[a:a + size]
                    else:
                        for k in range(size):  # may overlap the bytes being produced
                            x = a + k
                            t.append(seg[x] if x < s_len else t[x - s_len])
        if len(t) != tgtlen:
            raise VcdiffError("window produced {} bytes, expected {}".format(len(t), tgtlen))
        if checksum is not None and zlib.adler32(bytes(t)) & 0xFFFFFFFF not in checksum:
            raise VcdiffError("window adler32 mismatch")
        out += t
    return bytes(out)


def main():
    if len(sys.argv) != 4:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    with open(sys.argv[1], "rb") as f:
        src = f.read()
    with open(sys.argv[2], "rb") as f:
        dlt = f.read()
    res = decode(src, dlt)
    with open(sys.argv[3], "wb") as f:
        f.write(res)
    print("{} bytes".format(len(res)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
