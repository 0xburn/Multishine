# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Single-save-file support for the VMC builder: export one file from a raw card image as a standard .gci, import a .gci into a card
image, name the Multishine files, check a Melee save (our own code, stdlib only).

A .gci is the 0x40-byte directory entry exactly as the card stores it, followed by the file's data blocks (0x2000 bytes each); this is
the layout Dolphin's card manager writes (and Swiss reads).  The "first block" field inside the entry belongs to the card it came from,
so an export writes 0 there and an import lets the card model assign the real chain (readers ignore the field).  Other container formats
(.gcs, .sav) have different headers and are refused with a hint, never guessed.  Region and encoding: a game code whose region letter is
J/K needs a Shift-JIS card (header encoding 1), every other letter an ANSI card (encoding 0, what Melee USA/PAL need); a mismatch is
refused unless the caller says otherwise.  Nothing here touches the original card or file; callers write results with their own
atomic/backup rules (vmc_builder.py).
"""
import os
import re
import struct
import sys

import vmc_card as C
import vmc_files as F

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..")))
import melee_save_check as MSC  # noqa: E402  (the HSD block checksum, tools/melee_save_check.py)

GCI_ENTRY = 0x40
MELEE_SAVE = "SuperSmashBros0110290334"
MELEE_BLOCKS = 11
BRIDGE_NAME = F.OPTIONS_NAME[:-len("Options")] + "Bridge"      # the console's cache of the signed status document (offline/bridge/sbr_card.h)
SECRET_NAMES = (F.IDENTITY_NAME, F.ONLINE_NAME)                 # hold a playKey / device secret: private files
ALIASES = {"options": F.OPTIONS_NAME, "identity": F.IDENTITY_NAME, "online": F.ONLINE_NAME, "codes": F.CODES_NAME,
           "bridge": BRIDGE_NAME, "melee": MELEE_SAVE}


class GciError(Exception):
    pass


def resolve_names(spec):
    """'options,identity,melee' (or a list) -> [file names]; a name that is not an alias is taken literally."""
    items = spec.split(",") if isinstance(spec, str) else list(spec)
    out = []
    for s in (x.strip() for x in items):
        if not s:
            continue
        n = ALIASES.get(s.lower(), s)
        if n not in out:
            out.append(n)
    return out


def gci_filename(entry):
    """Dolphin-style '<maker>-<game>-<name>.gci' with every character that is unsafe in a file name replaced."""
    e = bytes(entry)
    name = e[8:40].split(b"\0")[0].decode("ascii", "replace")
    safe = lambda s: re.sub(r"[^A-Za-z0-9._ +-]", "_", s)
    return "%s-%s-%s.gci" % (safe(e[4:6].decode("ascii", "replace")), safe(e[0:4].decode("ascii", "replace")), safe(name))


def is_secret(name):
    return name in SECRET_NAMES


def expected_encoding(gamecode):
    """0 ANSI / 1 SJIS from the region letter of a game code, None when it is not a letter."""
    r = chr(gamecode[3]) if len(gamecode) == 4 else ""
    if not r.isalpha() or not r.isupper():
        return None
    return 1 if r in "JK" else 0


def parse_gci(buf):
    """-> (entry bytes, data bytes, info) or GciError.  Structure checks only."""
    buf = bytes(buf)
    if buf[:6] == b"GCSAVE" or buf[:12] == b"DATELGC_SAVE":
        raise GciError("this is a .gcs/.sav container, not a standard .gci; convert it to .gci with Dolphin's memory card manager first")
    if len(buf) < GCI_ENTRY + C.BLOCK or (len(buf) - GCI_ENTRY) % C.BLOCK:
        raise GciError("not a standard .gci: size must be 0x40 plus a whole number of 0x2000-byte blocks (got %d bytes)" % len(buf))
    e, data = buf[:GCI_ENTRY], buf[GCI_ENTRY:]
    n = len(data) // C.BLOCK
    if e[:4] == b"\xff\xff\xff\xff":
        raise GciError("the directory entry is empty (free slot marker)")
    if e[6] != 0xFF:
        raise GciError("directory entry byte 6 must be 0xFF")
    if struct.unpack_from(">H", e, 0x3A)[0] != 0xFFFF:
        raise GciError("directory entry padding is not 0xFFFF")
    raw_name = e[8:40]
    name = raw_name.split(b"\0")[0]
    if not name or any(c < 0x20 or c > 0x7E for c in name):
        raise GciError("file name is empty or not printable ASCII")
    if raw_name[len(name):].strip(b"\0"):
        raise GciError("file name has bytes after its terminator")
    if struct.unpack_from(">H", e, 0x38)[0] != n:
        raise GciError("the entry says %d blocks but the data holds %d" % (struct.unpack_from(">H", e, 0x38)[0], n))
    if n > 2043:
        raise GciError("a file cannot have more than 2043 blocks")
    off = struct.unpack_from(">I", e, 0x2C)[0]
    if off != 0xFFFFFFFF and off >= len(data):
        raise GciError("comment/image offset points outside the data")
    coff = struct.unpack_from(">I", e, 0x3C)[0]
    if coff >= len(data) and coff != 0xFFFFFFFF:
        raise GciError("comment offset points outside the data")
    return e, data, {"name": name.decode("ascii"), "gamecode": bytes(e[:4]), "maker": bytes(e[4:6]), "blocks": n}


def load_gci(path):
    try:
        with open(path, "rb") as f:
            buf = f.read(C.MAX_BYTES + 1)
    except OSError as x:
        raise GciError("cannot read %s (%s)" % (path, x.strerror))
    try:
        return parse_gci(buf)
    except GciError as x:
        raise GciError("%s: %s" % (path, x))


def export_gci(card, ent):
    """-> .gci bytes of one file of a parsed Card (entry with first block 0, then the data blocks)."""
    e = bytearray(ent["raw"])
    struct.pack_into(">H", e, 0x36, 0)
    return bytes(e) + card.read(ent)


def melee_problems(data):
    """HSD checksum check of a Melee save's data (block 0 is comment/banner/icons, blocks 1.. carry checksums).  [] = fine."""
    bad = []
    if len(data) != MELEE_BLOCKS * C.BLOCK:
        bad.append("a Melee save is %d blocks, this one is %d" % (MELEE_BLOCKS, len(data) // C.BLOCK))
        return bad
    for i in range(1, len(data) // C.BLOCK):
        ok, _ = MSC.verify(data[i * C.BLOCK:(i + 1) * C.BLOCK])
        if not ok:
            bad.append("Melee save data block %d fails its checksum" % i)
    return bad


def check_region(card, gamecode, ignore=False):
    want = expected_encoding(gamecode)
    if ignore or want is None or want == card.encoding:
        return
    name = lambda v: {0: "ANSI", 1: "Shift-JIS"}.get(v, str(v))
    raise GciError("game code %s needs a %s card but this card is %s (a USA/EUR card is ANSI, a JPN card Shift-JIS); "
                   "use a card of the right region or --ignore-region" % (gamecode.decode("ascii", "replace"), name(want), name(card.encoding)))


def _key(e):
    return (bytes(e[0:4]), bytes(e[4:6]), bytes(e[8:40]))


def import_gci(card, gci_bytes, replace=False, ignore_region=False):
    """Add one parsed .gci to a Card (in memory).  Refuses a duplicate unless replace; a replaced file's old data is dropped.
    Every other file must come out byte-identical.  -> info dict (name, blocks, replaced)."""
    entry, data, info = parse_gci(gci_bytes)
    check_region(card, info["gamecode"], ignore_region)
    if info["name"] == MELEE_SAVE and info["gamecode"] == C.GAME_CODE:
        bad = melee_problems(data)
        if bad:
            raise GciError("this looks like a Melee save but is damaged: " + "; ".join(bad))
    before = C.Card(bytes(card.img))
    old = card.find(info["name"], info["gamecode"], info["maker"])
    if old is not None:
        if not replace:
            raise GciError("%s is already on the card; refusing to overwrite it (use --replace; a verified backup is written first)" % info["name"])
        card.remove(old)
    entry = bytearray(entry)
    struct.pack_into(">H", entry, 0x36, 0)
    try:
        card.add(bytes(entry), data)
    except C.CardError as x:
        raise GciError(str(x))
    key = _key(entry)
    for e in before.entries():
        if _key(e["raw"]) == key:
            continue
        n = card.find(e["name"], e["gamecode"], e["maker"])
        if n is None or card.read(n) != before.read(e) or n["raw"][:0x36] != e["raw"][:0x36]:
            raise GciError("internal check failed: file %r would change; nothing written" % e["name"])
    info["replaced"] = old is not None
    return info
