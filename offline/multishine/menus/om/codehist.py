# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Reference implementation of the direct-code history (codehist.h / codehist.c) for the host tests and tools.  Stdlib only.

The record layout is documented in codehist.h.  `answer_slippi_model` is a literal transcription of Slippi's handleNameEntryLoad
 used as an independent oracle for the
C `ch_answer`; `answer` is the straightforward version of the same rules.
"""
import struct
import zlib

MAX, LEN = 20, 8
MAGIC = b"SDCH"
VERSION = 1
REC_SIZE = 0xB0
PAYLOAD_OFF = 0x1000
BLOCK = 0x2000
OK, DAMAGED, NEWER = 0, 1, 2
CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#@")       # "@": the Multishine universe (TAG@n), as codehist.c CH_AT (the hub images)


def valid(code):
    if not isinstance(code, str) or not 1 <= len(code) <= LEN or any(c not in CHARS for c in code):
        return False
    marks = [i for i, c in enumerate(code) if c in "#@"]
    return len(marks) == 1 and 1 <= marks[0] <= len(code) - 2


def _char(b0, b1):
    if (b0, b1) == (0x81, 0x94):
        return "#"
    if (b0, b1) == (0x81, 0x97):
        return "@"
    if b0 == 0x82:
        if 0x4F <= b1 <= 0x58:
            return chr(ord("0") + b1 - 0x4F)
        if 0x60 <= b1 <= 0x79:
            return chr(ord("A") + b1 - 0x60)
        if 0x81 <= b1 <= 0x9A:
            return chr(ord("A") + b1 - 0x81)
    return ""


def _ascii(b):
    c = chr(b).upper() if 0x61 <= b <= 0x7A else chr(b)
    return c if c in CHARS else ""


def from_melee(b):
    """B4 code bytes -> code string, or None."""
    out, i = "", 0
    b = bytes(b)
    while i < len(b) and b[i]:
        if b[i] < 0x80:
            c, i = _ascii(b[i]), i + 1
        else:
            if i + 1 >= len(b):
                return None
            c, i = _char(b[i], b[i + 1]), i + 2
        if not c or len(out) >= LEN:
            return None
        out += c
    return out if valid(out) else None


def to_melee(code):
    out = bytearray(24)
    for i, c in enumerate(code[:LEN]):
        if c == "#":
            out[3 * i:3 * i + 2] = b"\x81\x94"
        elif c == "@":
            out[3 * i:3 * i + 2] = b"\x81\x97"
        elif c.isdigit():
            out[3 * i:3 * i + 2] = bytes((0x82, 0x4F + ord(c) - 48))
        else:
            out[3 * i:3 * i + 2] = bytes((0x82, 0x60 + ord(c) - 65))
    return bytes(out)


def add(hist, code):
    """-> (new list, changed 1/0/-1)"""
    if not valid(code):
        return list(hist), -1
    if hist and hist[0] == code:
        return list(hist), 0
    return ([code] + [c for c in hist if c != code])[:MAX], 1


def merge(dst, src):
    out = list(dst)
    for c in src:
        if len(out) < MAX and c not in out:
            out.append(c)
    return out


def encode(hist, seq=0):
    body = struct.pack(">I", seq) + b"".join(c.encode().ljust(LEN, b"\0") for c in hist) + bytes(LEN * (MAX - len(hist)))
    return MAGIC + bytes((VERSION, len(hist), 0, 0)) + struct.pack(">I", zlib.crc32(body)) + body


def check(rec):
    rec = bytes(rec)
    if rec[:4] != MAGIC or rec[4] == 0:
        return DAMAGED
    if rec[4] > VERSION:
        return NEWER
    n = rec[5]
    if n > MAX or struct.unpack(">I", rec[8:12])[0] != zlib.crc32(rec[12:REC_SIZE]):
        return DAMAGED
    seen = []
    for i in range(MAX):
        e = rec[0x10 + LEN * i:0x18 + LEN * i]
        if i >= n:
            if any(e):
                return DAMAGED
            continue
        s = e.split(b"\0")[0].decode("latin-1")
        if e[len(s):] != bytes(LEN - len(s)) or not valid(s) or s in seen:
            return DAMAGED
        seen.append(s)
    return OK


def decode(rec):
    rec = bytes(rec)
    n = rec[5]
    return [rec[0x10 + LEN * i:0x18 + LEN * i].rstrip(b"\0").decode() for i in range(n)], struct.unpack(">I", rec[12:16])[0]


def new_block(line1, line2, hist, seq=0):
    b = bytearray(BLOCK)
    b[0:len(line1[:31])] = line1[:31].encode()
    b[32:32 + len(line2[:31])] = line2[:31].encode()
    b[PAYLOAD_OFF:PAYLOAD_OFF + REC_SIZE] = encode(hist, seq)
    return bytes(b)


def _matches(inp, n, code):
    if n > len(code):
        return False
    for i in range(n):
        c = _char(inp[3 * i], inp[3 * i + 1]) or (_ascii(inp[3 * i]) if inp[3 * i] < 0x80 and not inp[3 * i + 1] else "")
        if not c or c != code[i]:
            return False
    return True


def request(text, count=None, index=0, direction=0, mode=2):
    """Build a 32-byte 0xBE request for typed text (a code prefix string); `count` = committed characters (default len(text))."""
    t = bytearray(24)
    for i, c in enumerate(text[:8]):
        t[3 * i:3 * i + 2] = to_melee(c)[0:2]
    n = len(text) if count is None else count
    return bytes([0xBE]) + bytes(t) + bytes([n]) + struct.pack(">I", index) + bytes([direction, mode])


def answer(hist, req):
    """Same rules as C ch_answer, written the direct way -> 30 bytes."""
    inp, n, initial, d, mode = req[1:25], min(req[25], LEN), struct.unpack(">I", req[26:30])[0], req[30], req[31]
    lst = hist if mode == 2 else []
    cur = initial
    if d == 1:
        cur = (cur + 1) & 0xFFFFFFFF
    elif d == 2:
        cur = cur - 1 if cur > 0 else cur
    elif d == 3:
        cur = 0
    hit = None
    while 0 <= cur < len(lst):
        if _matches(inp, n, lst[cur]):
            hit = cur
            break
        cur = cur - 1 if d == 2 else cur + 1
    if hit is None and initial < len(lst) and _matches(inp, n, lst[initial]):
        hit = initial
    if hit is None:
        return bytes([0]) + bytes(inp[:3 * n]).ljust(24, b"\0") + bytes([n]) + struct.pack(">I", initial)
    return bytes([1]) + to_melee(lst[hit]) + bytes([len(lst[hit])]) + struct.pack(">I", hit)


def answer_slippi_model(hist, req):
    """Literal model of handleNameEntryLoad with Slippi's own data model: history strings, "1" for out of range, JIS strings, u32 wrap."""
    M = 0xFFFFFFFF
    payload = req[1:]
    input_len = payload[24]
    initial = struct.unpack(">I", payload[25:29])[0]
    d = payload[29]
    mode = payload[30]
    code_history = list(hist) if mode == 2 else []

    def get(i):
        return code_history[i] if 0 <= i < len(code_history) else "1"

    def jis(tag):                     # UTF8ToSHIFTJIS of Slippi's tags: full-width characters, two bytes each ("1" stays one byte)
        return b"".join(to_melee(c)[0:2] for c in tag) if tag != "1" else b"1"

    def does_match(tag):
        jt = jis(tag)
        for i in range(min(input_len, 8)):
            if i * 2 + 1 >= len(jt):
                return False          # (Slippi reads past the string here: undefined; our rule is no match)
            if payload[i * 3] != jt[i * 2] or payload[i * 3 + 1] != jt[i * 2 + 1]:
                return False
        return True

    cur = initial
    if d == 1:
        cur = (cur + 1) & M
    elif d == 2:
        cur = cur - 1 if cur > 0 else cur
    elif d == 3:
        cur = 0
    tag = "1"
    while 0 <= cur < len(code_history):
        tag = get(cur)
        if does_match(tag):
            break
        cur = (cur - 1) & M if d == 2 else cur + 1
    tag = get(cur)
    if tag == "1":
        init_tag = get(initial)
        if init_tag != "1" and does_match(init_tag):
            tag, cur = init_tag, initial
    n = min(input_len, 8)
    if tag == "1":
        return bytes([0]) + bytes(payload[:3 * n]).ljust(24, b"\0") + bytes([n]) + struct.pack(">I", initial)
    out = bytearray([1])
    jc = jis(tag)
    for i in range(8):
        for j in range(i * 2, i * 2 + 2):
            out.append(jc[j] if j < len(jc) else 0)
        out.append(0)
    out.append(len(jc) // 2)
    out += struct.pack(">I", cur)
    return bytes(out)
