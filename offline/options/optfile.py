#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Options card file: format, generator and checker (host side of the persistence design, README section 4).

  python3 offline/options/optfile.py build --options [--chat ] --gci out.gci
  python3 offline/options/optfile.py show FILE.gci

The console side (reading the file at boot, writing it from the page) is NOT implemented yet; this fixes the format so a PC
tool and the console agree, and so the format can be unit tested now.

Card file: its OWN file "<projname.SHORT>Options" (GALE/01, 1 block), never Melee's save (SuperSmashBros0110290334) and
never the identity file.  Data block layout like the identity file (offline/provision): comment 0x000, icon 0x040,
payload at 0x1000:
  +0x000 'SDOF'  +0x004 u8 version 1  +0x005 u8 count 16  +0x006 u16 flags (bit0: chat table present)
  +0x008 u32 CRC-32 (zlib) of payload[0x00C:0x240]
  +0x00C u8 value[16]: option id order of optengine.h; 0xFF = "use the patched default" (so a missing or reset entry is the
         build default, never a guess)
  +0x01C 4 reserved bytes (0)
  +0x020 SDQC chat table (gen_chat.py, 524 bytes) or zeros when flag bit0 is clear
  payload size 0x240.
: {"format": "multishine-options", "version": 1, "options": {"widescreen": 1, "delay": 3, ...}} with the keys of
build_options.OPTIONS; values must be inside each option's range.  Only option values and chat texts: anything that looks
like account data is refused (gen_chat's credential scan runs on both files).
"""
import argparse
import json
import pathlib
import struct
import sys
import zlib

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "offline/provision"))
sys.path.insert(0, str(REPO / "offline"))
import gen_chat  # noqa: E402
import projname  # noqa: E402
import card_text  # noqa: E402

MAGIC = b"SDOF"
VERSION = 1
NVAL = 16
SIZE = 0x240
CHAT_OFF = 0x20
UNSET = 0xFF
FILE_NAME = projname.SHORT + "Options"
# id, key, (min, max) -- must match build_options.OPTIONS (unit test)
RANGES = {"widescreen": (0, 0, 1), "no-screen-shake": (1, 0, 1), "fd-freeze": (2, 0, 1), "center-hud": (3, 0, 1),
          "flash-red-lcancel": (4, 0, 2), "friendly-indicators": (5, 0, 1), "ucf": (6, 0, 1), "delay": (7, 1, 15),
          "offline-delay": (8, 0, 1), "quick-chat": (9, 0, 2),
          "hitbox-view": (10, 0, 1), "tech-card": (11, 0, 1), "quick-reset": (12, 0, 1)}


class OptFileError(Exception):
    pass


def values_from_json(d):
    try:
        gen_chat._scan_credentials(d)
    except gen_chat.ChatError as e:
        raise OptFileError(str(e))
    if not isinstance(d, dict) or d.get("format") != "multishine-options" or d.get("version") != 1:
        raise OptFileError('need "format": "multishine-options", "version": 1')
    extra = set(d) - {"format", "version", "options", "_comment"}
    if extra:
        raise OptFileError("unknown keys: " + ", ".join(sorted(extra)))
    vals = [UNSET] * NVAL
    for k, v in (d.get("options") or {}).items():
        if k not in RANGES:
            raise OptFileError("unknown option " + k)
        i, lo, hi = RANGES[k]
        if not isinstance(v, int) or isinstance(v, bool) or not lo <= v <= hi:
            raise OptFileError("%s: %r outside %d..%d" % (k, v, lo, hi))
        vals[i] = v
    return vals


def pack(vals, chat=None):
    p = bytearray(SIZE)
    p[0:4] = MAGIC
    p[4] = VERSION
    p[5] = NVAL
    struct.pack_into(">H", p, 6, 1 if chat else 0)
    p[0x0C:0x0C + NVAL] = bytes(vals)
    if chat:
        if len(chat) != gen_chat.SIZE:
            raise OptFileError("chat table size")
        p[CHAT_OFF:CHAT_OFF + len(chat)] = chat
    struct.pack_into(">I", p, 8, zlib.crc32(bytes(p[0x0C:SIZE])) & 0xFFFFFFFF)
    return bytes(p)


def unpack(p):
    if len(p) < SIZE or p[0:4] != MAGIC:
        raise OptFileError("not an options payload")
    if p[4] != VERSION or p[5] != NVAL:
        raise OptFileError("version/count")
    if struct.unpack(">I", p[8:12])[0] != zlib.crc32(bytes(p[0x0C:SIZE])) & 0xFFFFFFFF:
        raise OptFileError("CRC mismatch")
    flags = struct.unpack(">H", p[6:8])[0]
    vals = list(p[0x0C:0x0C + NVAL])
    out = {}
    for k, (i, lo, hi) in RANGES.items():
        v = vals[i]
        out[k] = None if v == UNSET or not lo <= v <= hi else v   # out of range = patched default, like opt_sanitize
    chat = None
    if flags & 1:
        try:
            chat = gen_chat.unpack(bytes(p[CHAT_OFF:CHAT_OFF + gen_chat.SIZE]))
        except gen_chat.ChatError as e:
            raise OptFileError("chat table: " + str(e))
    return {"options": out, "chat": chat}


def build_gci(payload, mtime=None):
    import multishine_provision as P
    data = bytearray(P.BLOCK)
    card_text.put(data, "Options")
    P.put_icon(data, FILE_NAME)
    data[P.PAYLOAD_OFFSET:P.PAYLOAD_OFFSET + SIZE] = payload
    return P.make_dir_entry(FILE_NAME, 0, 1, P.PERM_PUBLIC, mtime) + bytes(data)


def read_gci(buf):
    import multishine_provision as P
    if len(buf) != P.GCI_ENTRY + P.BLOCK or buf[0:4] != P.GAME_CODE or buf[4:6] != P.MAKER:
        raise OptFileError("not a 1-block GALE/01 GCI")
    name = buf[8:40].split(b"\0", 1)[0].decode("ascii")
    if name != FILE_NAME:
        raise OptFileError("file name %r, expected %r" % (name, FILE_NAME))
    o = P.GCI_ENTRY + P.PAYLOAD_OFFSET
    return unpack(buf[o:o + SIZE])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("--options", required=True)
    b.add_argument("--chat")
    b.add_argument("--gci", required=True)
    s = sp.add_parser("show")
    s.add_argument("path")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "build":
            vals = values_from_json(json.loads(pathlib.Path(a.options).read_text(encoding="utf-8")))
            chat = None
            if a.chat:
                chat = gen_chat.pack(gen_chat.texts_from_json(json.loads(pathlib.Path(a.chat).read_text(encoding="utf-8"))),
                                     custom=True)
            pathlib.Path(a.gci).write_bytes(build_gci(pack(vals, chat)))
            print("%s: %s, chat %s" % (a.gci, FILE_NAME, "custom" if chat else "none"))
        else:
            print(json.dumps(read_gci(pathlib.Path(a.path).read_bytes()), indent=1))
    except (OptFileError, gen_chat.ChatError) as e:
        print("optfile: " + str(e), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
