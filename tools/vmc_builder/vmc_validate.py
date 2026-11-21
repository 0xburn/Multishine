# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Dolphin-free strict validator for raw card images (our own code, stdlib only).

Goes further than Card.consistency: both table copies, counters, directory entry fields, free-block bookkeeping, unused map entries,
header fields.  Returns a list of (severity, text); severity "error" means a console or Swiss may refuse the card, "warn" is odd but legal.
Structure only: it cannot tell whether a loader file works.
"""
import struct

import vmc_card as C


def validate(raw):
    f = []
    err = lambda t: f.append(("error", t))
    warn = lambda t: f.append(("warn", t))
    try:
        card = C.Card(raw)
    except C.CardError as x:
        return [("error", str(x))]
    img = card.img
    for n in card.notes:
        warn(n)
    d1, d2 = bytes(img[C.BLOCK:2 * C.BLOCK]), bytes(img[2 * C.BLOCK:3 * C.BLOCK])
    b1, b2 = bytes(img[3 * C.BLOCK:4 * C.BLOCK]), bytes(img[4 * C.BLOCK:5 * C.BLOCK])
    if d1 != d2:
        warn("directory copies differ (legal while a write is in flight; Swiss/console pick the newer counter)")
    if b1 != b2:
        warn("block map copies differ")
    hdr = bytes(img[:C.BLOCK])
    if struct.unpack_from(">H", hdr, 0x24)[0] not in (0, 1):
        err("header encoding is neither ANSI (0) nor SJIS (1)")
    if card.total - C.SYS_BLOCKS != len(range(C.SYS_BLOCKS, card.total)):
        err("block count arithmetic")
    for p in card.consistency():
        err(p)
    bat = card.batblk()
    last = struct.unpack_from(">H", bat, 8)[0]
    if not (C.SYS_BLOCKS - 1 <= last < card.total):
        err("last-allocated block %d out of range" % last)
    seen = set()
    for e in card.entries():
        r = e["raw"]
        key = (e["gamecode"], e["maker"], e["name"])
        if key in seen:
            err("duplicate file %r" % e["name"])
        seen.add(key)
        if struct.unpack_from(">H", r, 0x3A)[0] != 0xFFFF:
            err("%r: directory entry padding is not 0xFFFF" % e["name"])
        if r[6] != 0xFF:
            warn("%r: directory byte 6 is not 0xFF" % e["name"])
        if not e["blocks"]:
            err("%r: zero blocks" % e["name"])
        if e["perm"] & ~0x1C:
            warn("%r: unusual permission bits 0x%02X" % (e["name"], e["perm"]))
        nm = r[8:40]
        if b"\0" in nm and nm[nm.index(b"\0"):].strip(b"\0"):
            err("%r: bytes after the name terminator" % e["name"])
        img_off = struct.unpack_from(">I", r, 0x2C)[0]
        if img_off != 0xFFFFFFFF and e["blocks"] * C.BLOCK <= img_off:
            err("%r: comment/image offset 0x%X beyond the file" % (e["name"], img_off))
    ents = card.entries()
    if len(ents) > C.DIR_ENTRIES:
        err("too many directory entries")
    # free map entries beyond the card must be zero
    for b in range(card.total, C.BLOCK // 2 - 5 + 1):
        if 10 + 2 * (b - C.SYS_BLOCKS) + 2 <= C.BLOCK and struct.unpack_from(">H", bat, 10 + 2 * (b - C.SYS_BLOCKS))[0] not in (0,):
            warn("map entry past the end of the card is non-zero")
            break
    return f


def errors(findings):
    return [t for s, t in findings if s == "error"]


if __name__ == "__main__":
    import sys
    if len(sys.argv) != 2:
        sys.exit("usage: vmc_validate.py CARD.raw   (prints the findings; exit 1 on any error)")
    with open(sys.argv[1], "rb") as fh:
        findings = validate(fh.read())
    for sev, text in findings:
        print("%-5s %s" % (sev, text))
    print("errors: %d, warnings: %d" % (sum(1 for s, _ in findings if s == "error"), sum(1 for s, _ in findings if s == "warn")))
    sys.exit(1 if any(s == "error" for s, _ in findings) else 0)
