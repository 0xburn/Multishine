#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""ovl_guard: refuse an overlay-only OTA file (MultishineOvl container) that LACKS an entry the target card's container has.

Incident 20:30: ovl-331.mscf held only ms/sign.ovl; installed over OTA on a card whose MultishineOvl holds ms/sign.ovl AND ms/hub.ovl it REPLACED the whole
container, so ms/hub.ovl was gone ("Online files missing on the memory card", Update says Offline).  A container update is a whole-file replacement: it must carry every entry.

  ovl_guard.py check NEW [--like REF ...] [--require NAME ...] [--allow-sign-only]
      NEW / REF: an OTA file (ovl-N.mscf), a MultishineOvl .gci, or a card image (.raw: its MultishineOvl is read; a card without one has no entries).
      Reference list = the entries of every --like plus every --require; with none of them the default reference is DEFAULT_REF (sign + hub, the hub-overlay lineage).
      --allow-sign-only: the default reference is just ms/sign.ovl (cards of the pre-hub lineage, e.g. disc_free_1; say so on purpose).
      Exit 0 = NEW has every reference entry; 1 = refused (names the missing entries); 2 = unreadable.
Library: entries(path_or_bytes) -> [names], missing(new, refs) -> [names], check(new, like=, require=, sign_only=False) -> (ok, message).  Stdlib only."""
import argparse, os, struct, subprocess, sys, tempfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
MAGIC_PACK, MAGIC_MSCF = 0x4D53504B, 0x4D534346
BLOCK, HDR_OFF, ENT = 0x2000, 0x1000, 64
DIR_ENTRY = 0x40
DEFAULT_REF = ("ms/hub.ovl", "ms/sign.ovl")
SIGN_ONLY_REF = ("ms/sign.ovl",)
CARD_FILE = "MultishineOvl"


class GuardError(Exception):
    pass


def _entries_of_body(b):
    """b = the file WITHOUT the 0x40 directory entry (an .mscf): block 0 holds the MSCF header at 0x1000, block 1 the entry table."""
    if len(b) < 2 * BLOCK or struct.unpack_from(">I", b, HDR_OFF)[0] != MAGIC_MSCF:
        raise GuardError("not a MultishineOvl container (no MSCF header)")
    magic, ver, n, total, tcrc = struct.unpack_from(">IIIII", b, BLOCK)
    if magic != MAGIC_PACK or ver != 1 or not 0 < n <= 6:
        raise GuardError("not a MultishineOvl container (bad entry table: magic %08x version %d entries %d)" % (magic, ver, n))
    toc = b[BLOCK + 0x20:BLOCK + 0x20 + n * ENT]
    if len(toc) != n * ENT or zlib.crc32(toc) & 0xFFFFFFFF != tcrc:
        raise GuardError("entry table CRC mismatch")
    return [toc[i * ENT:i * ENT + 24].split(b"\0")[0].decode("ascii") for i in range(n)]


def _card_container(path):
    with tempfile.TemporaryDirectory() as d:
        r = subprocess.run([sys.executable, os.path.join(REPO, "tools", "vmc_builder", "vmc_builder.py"), "export-gci", "--card", path, "--name", CARD_FILE, "--gci-out", d],
                           capture_output=True, text=True)
        gcis = [f for f in os.listdir(d) if f.endswith(".gci")]
        if r.returncode or not gcis:
            return None                                    # no MultishineOvl on that card
        return open(os.path.join(d, gcis[0]), "rb").read()


def entries(src):
    """entry names of an OTA file, a .gci or a card image; [] for a card without a MultishineOvl"""
    if isinstance(src, (bytes, bytearray)):
        b = bytes(src)
    else:
        if str(src).endswith(".raw"):
            g = _card_container(src)
            if g is None:
                return []
            b = g
        else:
            b = open(src, "rb").read()
    if len(b) >= DIR_ENTRY + 2 * BLOCK and struct.unpack_from(">I", b, DIR_ENTRY + HDR_OFF)[0] == MAGIC_MSCF:
        b = b[DIR_ENTRY:]
    return _entries_of_body(b)


def missing(new, refs):
    have = set(entries(new))
    return [n for n in refs if n not in have]


def check(new, like=(), require=(), sign_only=False):
    refs = []
    for l in like:
        for n in entries(l):
            if n not in refs:
                refs.append(n)
    for n in require:
        if n not in refs:
            refs.append(n)
    if not like and not require:
        refs = list(SIGN_ONLY_REF if sign_only else DEFAULT_REF)
    have = entries(new)
    miss = [n for n in refs if n not in have]
    if miss:
        return False, ("REFUSED: this overlay container has %s but the target lineage has %s: it lacks %s.  Installing it would REPLACE the card's whole MultishineOvl and remove %s "
                       "(incident 2026-10-13 ovl-331: 'Online files missing on the memory card').  Build the container with every entry (offline/multishine/menus/hub/hub_pack.py)."
                       % (have, refs, miss, ", ".join(miss)))
    return True, "ok: container entries %s cover the reference %s" % (have, refs)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    c = sp.add_parser("check"); c.add_argument("new"); c.add_argument("--like", action="append", default=[]); c.add_argument("--require", action="append", default=[])
    c.add_argument("--allow-sign-only", action="store_true")
    a = ap.parse_args(argv)
    try:
        ok, msg = check(a.new, a.like, a.require, a.allow_sign_only)
    except (GuardError, OSError) as e:
        print("ovl_guard: %s: %s" % (a.new, e), file=sys.stderr)
        return 2
    print(msg, file=sys.stdout if ok else sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
