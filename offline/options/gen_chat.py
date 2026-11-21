#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Quick-chat string table generator (host side).

  python3 offline/options/gen_chat.py build [--json ] --out table.bin [--custom]
  python3 offline/options/gen_chat.py show table.bin
  python3 offline/options/gen_chat.py ids

 (UTF-8 JSON, names only): {"format": "multishine-chat", "version": 1,
  "messages": {GROUP: {SLOT: TEXT}}}   GROUP, SLOT in up/left/right/down (GROUP = first D-pad press, SLOT = second).
Every one of the 16 slots must be present, 1-31 characters QC_CHARSET (what Melee's SisLib ASCII encoder maps:
letters, digits, space. , ' - :).  Any other key is refused; keys that look like account data (uid, playKey, connectCode,
displayName, ...) are refused with a pointed message: a chat file carries message texts only, never credentials
( rule 5).

Table "SDQC" v1, 524 bytes: magic, version 1, count 16, u16 flags (bit0 custom), u32 CRC-32 (zlib) of the 512 text bytes,
16 x 32-byte NUL-padded strings in table order (group Up, Left, Right, Down; slot Up, Left, Right, Down).
Wire id of table index i: (PAD[group] << 4) | PAD[slot] with PAD = {left 1, right 2, down 4, up 8} (Slippi's ids).
"""
import argparse
import json
import pathlib
import struct
import sys
import zlib

HERE = pathlib.Path(__file__).resolve().parent
MAGIC = b"SDQC"
VERSION = 1
N, LEN = 16, 32
SIZE = 12 + N * LEN
ORDER = ("up", "left", "right", "down")
PAD = {"left": 1, "right": 2, "down": 4, "up": 8}
CHARSET = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,'-:")
CREDENTIAL_KEYS = {"uid", "playkey", "play_key", "connectcode", "connect_code", "displayname", "display_name",
                   "latestversion", "token", "password", "secret", "user", "userjson"}
DEFAULTS = HERE / "chat_defaults.json"


class ChatError(Exception):
    pass


def wire_id(index):
    return (PAD[ORDER[index // 4]] << 4) | PAD[ORDER[index % 4]]


def index_of(wid):
    for i in range(N):
        if wire_id(i) == wid:
            return i
    return -1


def _scan_credentials(obj, path="$"):
    if isinstance(obj, dict):
        for k, v in obj.items():
            if str(k).replace("-", "_").lower() in CREDENTIAL_KEYS:
                raise ChatError("%s.%s: chat.json carries message texts only, never account data or credentials" % (path, k))
            _scan_credentials(v, path + "." + str(k))
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            _scan_credentials(v, "%s[%d]" % (path, i))


def texts_from_json(d):
    _scan_credentials(d)
    if not isinstance(d, dict):
        raise ChatError("top level must be an object")
    extra = set(d) - {"format", "version", "messages", "_comment"}
    if extra:
        raise ChatError("unknown keys: %s" % ", ".join(sorted(extra)))
    if d.get("format") != "multishine-chat" or d.get("version") != 1:
        raise ChatError('need "format": "multishine-chat", "version": 1')
    m = d.get("messages")
    if not isinstance(m, dict) or set(m) != set(ORDER):
        raise ChatError("messages must have exactly the groups up, left, right, down")
    out = []
    for g in ORDER:
        if not isinstance(m[g], dict) or set(m[g]) != set(ORDER):
            raise ChatError("messages.%s must have exactly the slots up, left, right, down" % g)
        for s in ORDER:
            t = m[g][s]
            if not isinstance(t, str) or not 1 <= len(t) <= LEN - 1:
                raise ChatError("messages.%s.%s: 1-%d characters" % (g, s, LEN - 1))
            bad = sorted(set(t) - CHARSET)
            if bad:
                raise ChatError("messages.%s.%s: characters Melee's menu font cannot draw: %r" % (g, s, "".join(bad)))
            out.append(t)
    return out


def pack(texts, custom=False):
    if len(texts) != N:
        raise ChatError("need 16 texts")
    body = b"".join(t.encode("ascii").ljust(LEN, b"\0") for t in texts)
    hdr = MAGIC + struct.pack(">BBH", VERSION, N, 1 if custom else 0) + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    out = hdr + body
    assert len(out) == SIZE
    return out


def unpack(b):
    if len(b) < SIZE or b[:4] != MAGIC:
        raise ChatError("not an SDQC table")
    ver, cnt, flags = struct.unpack(">BBH", b[4:8])
    crc = struct.unpack(">I", b[8:12])[0]
    body = b[12:SIZE]
    if ver != VERSION or cnt != N:
        raise ChatError("version/count")
    if zlib.crc32(body) & 0xFFFFFFFF != crc:
        raise ChatError("CRC mismatch")
    texts = []
    for i in range(N):
        raw = body[i * LEN:(i + 1) * LEN]
        s = raw.split(b"\0", 1)[0]
        if not s or len(s) >= LEN or raw[len(s):] != bytes(LEN - len(s)):
            raise ChatError("slot %d: bad padding/length" % i)
        t = s.decode("ascii")
        if set(t) - CHARSET:
            raise ChatError("slot %d: charset" % i)
        texts.append(t)
    return {"custom": bool(flags & 1), "texts": texts}


def default_table():
    return pack(texts_from_json(json.loads(DEFAULTS.read_text())), custom=False)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("--json", default=str(DEFAULTS))
    b.add_argument("--out", required=True)
    b.add_argument("--custom", action="store_true")
    s = sp.add_parser("show")
    s.add_argument("path")
    sp.add_parser("ids")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "build":
            t = pack(texts_from_json(json.loads(pathlib.Path(a.json).read_text(encoding="utf-8"))), a.custom)
            pathlib.Path(a.out).write_bytes(t)
            print("%s: %d bytes, CRC %s" % (a.out, len(t), t[8:12].hex()))
        elif a.cmd == "show":
            d = unpack(pathlib.Path(a.path).read_bytes())
            for i, t in enumerate(d["texts"]):
                print("0x%02X %-5s %-5s %s" % (wire_id(i), ORDER[i // 4], ORDER[i % 4], t))
        else:
            for i in range(N):
                print("%2d 0x%02X %s/%s" % (i, wire_id(i), ORDER[i // 4], ORDER[i % 4]))
    except ChatError as e:
        print("gen_chat: " + str(e), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
