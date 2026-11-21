#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""mdelta: the delta format of the disc-file pack mode DELTA (menus/df/df.h): our own tiny op stream, produced here from the user's retail file and the RC1 result,
decoded on the console by df_core.c against the retail file the disc already holds.  A delta carries only the changed bytes (ADD literals) and references into the retail file
(COPY): no Nintendo bytes beyond what the differences themselves are.  Stdlib only.

Stream: ops, each starts with a tag byte
  0x00 END
  0x01 ADD varint(len) len literal bytes
  0x02 COPY varint(zigzag(src - prev_src_end)) varint(len)      src = offset in the OLD file; prev_src_end = src + len of the previous COPY (0 at the start)
  0x03 RUN varint(len) byte
varint = LEB128 (7 bits per byte, low first).  The decoder writes the NEW file from the start; every COPY reads the old file (random access)."""
import struct, sys

W = 12        # window of the match index
STEP = 4      # old positions indexed
MINCOPY = 16  # shortest COPY worth emitting


def _varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def _zz(n):
    return (n << 1) ^ (n >> 63) if n >= 0 else ((-n) << 1) - 1


# (window, old-position step, shortest COPY) sets tried by build_pack for a DELTA entry: a larger shortest COPY leaves fewer, longer literal runs, which the MSLZ stream then
# packs better even though the op stream itself grows (MnMaAll.usd: 115,009 B with the first set, 112,058 B with the second).  The decoder is the same.
PARAM_SETS = ((12, 4, 16), (16, 4, 32), (16, 4, 24))


def encode(old, new, params=None):
    W, STEP, MINCOPY = params if params else (globals()["W"], globals()["STEP"], globals()["MINCOPY"])
    old = bytes(old); new = bytes(new)
    idx = {}
    for i in range(0, len(old) - W + 1, STEP):
        idx.setdefault(old[i:i + W], []).append(i)
    out = bytearray()
    n = len(new)
    i = 0                 # scan position in new
    pend = 0              # start of the pending literal run
    prev_end = 0
    exp_new_to_old = 0    # predicted old position for new position i (keeps sequential copies sequential)

    while i <= n - W:
        key = new[i:i + W]
        cands = idx.get(key)
        best = None
        if cands:
            # prefer the candidate that continues the previous copy
            pref = exp_new_to_old
            order = sorted(cands, key=lambda c: abs(c - pref))[:6]
            for c in order:
                l = 0
                m = min(n - i, len(old) - c)
                while l < m and old[c + l] == new[i + l]:
                    l += 1
                if best is None or l > best[1]:
                    best = (c, l)
        if best and best[1] >= MINCOPY:
            c, l = best
            # extend backwards into the pending literals
            back = 0
            while i - back > pend and c - back > 0 and old[c - back - 1] == new[i - back - 1]:
                back += 1
            c -= back; i -= back; l += back
            if i > pend:
                lit = new[pend:i]
                out.append(1); out += _varint(len(lit)); out += lit
            out.append(2); out += _varint(_zz(c - prev_end)); out += _varint(l)
            prev_end = c + l
            i += l
            pend = i
            exp_new_to_old = c + l
        else:
            i += 1
            exp_new_to_old += 1
    if pend < n:
        lit = new[pend:]
        out.append(1); out += _varint(len(lit)); out += lit
    out.append(0)
    return bytes(out)


def apply(old, delta, new_size):
    out = bytearray()
    i = 0
    prev_end = 0

    def rv():
        nonlocal i
        v = 0; s = 0
        while True:
            b = delta[i]; i += 1
            v |= (b & 0x7F) << s
            if not b & 0x80:
                return v
            s += 7
    while True:
        t = delta[i]; i += 1
        if t == 0:
            break
        if t == 1:
            ln = rv(); out += delta[i:i + ln]; i += ln
        elif t == 2:
            z = rv(); ln = rv()
            d = (z >> 1) if not z & 1 else -((z + 1) >> 1)
            src = prev_end + d
            out += old[src:src + ln]
            prev_end = src + ln
        elif t == 3:
            ln = rv(); out += bytes([delta[i]]) * ln; i += 1
        else:
            raise ValueError("bad op")
    if len(out) != new_size:
        raise ValueError("size %d != %d" % (len(out), new_size))
    return bytes(out)


if __name__ == "__main__":
    o, nw = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
    d = encode(o, nw)
    assert apply(o, d, len(nw)) == nw
    print("delta %d bytes for %d -> %d" % (len(d), len(o), len(nw)))
