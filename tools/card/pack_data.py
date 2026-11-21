#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""pack_data: build, check and sign the SYSTEM FILES DATA container, the OTA kind pack-N.mscf (MSCF ABI 4).

The in-game Update screen installs the "Multishine system files" (the card file MultishinePack) on a console that has only our four files and a clean retail disc: it downloads this
container, reads the four retail menu files from the player's own disc, applies the deltas in RAM and writes MultishinePack (offline/seed/seed_slp.c, byte-identical to
tools/card/build_pack.py).  The container carries ONLY what the console cannot get from the disc: Slippi's public GPL-2.0 files, unmodified (slpCSS.dat and the four *.usd.diff VCDIFF
deltas, SHA-1/size checked against offline/iso/overlay_hashes.json) and our own pack header block (head.bin: file comments + icon).  The retail menu files are never in it.

  pack_data.py build --slippi-files DIR --seq N --out OUT.gci [--mtime T]    the UNSIGNED container; sign it with mscf_sign.py sign (--key / --dev), publish with the publish tool (outside this repository) / dev_ota.py (kind pack)
  pack_data.py info FILE                                                    describe a container (.gci or served file)

Payload (the image of the signed MSCF container, big endian, plain, no segments/patches/compression):
  +0 'MSPD'  +4 u8 version 1  +5 u8 number of entries (6, or 7 with the optional id 11 = MnSlMap.usd.diff)  +6 u16 0  +8 u32 total length of the payload  +12 u32 CRC-32 of the entry table (n x 16 bytes at +16)
  +16 entries: id u8 (4..7 = MnExtAll MnMaAll SdMenu SdSlChr .usd.diff, 8 = slpCSS.dat, 9 = head.bin, 11 = MnSlMap.usd.diff (optional); each at most once), 3 bytes 0, u32 offset (32-aligned, from the payload start), u32 length, u32 CRC-32 of the data
  data at the offsets, zero padded to 32 bytes.
The ids are those of struct slp_io (offline/seed/seed_slp.h).  stdlib only."""
import argparse, json, os, struct, sys, zlib, hashlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "offline", "cardloader"))
sys.path.insert(0, os.path.join(REPO, "offline", "provision"))

PACK_ABI = 4
BLOCK, HDR_OFF = 0x2000, 0x1000
GCI_HDR = 0x40
MAX_FILE = 128 * BLOCK            # SGU_MAX_FILE of the Update screen (sgu_core.h): 1 MiB
NAMES = {4: "MnExtAll.usd.diff", 5: "MnMaAll.usd.diff", 6: "SdMenu.usd.diff", 7: "SdSlChr.usd.diff", 8: "slpCSS.dat", 9: "head.bin", 11: "MnSlMap.usd.diff"}
OPTIONAL = (11,)                  # MnSlMap.usd.diff: the stage select screen of Slippi; a container without it (6 entries) stays valid and builds the five-entry pack
REQUIRED = tuple(i for i in sorted(NAMES) if i not in OPTIONAL)
MAGIC = b"MSPD"
FILE_NAME = "MultishinePackData"


class PackDataError(Exception):
    pass


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def build_payload(files):
    """files: {id: bytes} for the six required ids (+ the optional 11) -> the payload bytes"""
    if sorted(i for i in files if i not in OPTIONAL) != list(REQUIRED) or any(i not in NAMES for i in files):
        raise PackDataError("need exactly the ids %s (+ optional %s)" % (list(REQUIRED), list(OPTIONAL)))
    n = len(files)
    off = (16 + 16 * n + 31) & ~31
    ents, data = [], bytearray()
    for i in sorted(files):
        d = files[i]
        if not d:
            raise PackDataError("empty %s" % NAMES[i])
        ents.append((i, off + len(data), len(d), crc32(d)))
        data += d + bytes(-len(d) % 32)
    tab = b"".join(struct.pack(">B3xIII", *e) for e in ents)
    total = off + len(data)
    head = MAGIC + struct.pack(">BBHII", 1, n, 0, total, crc32(tab))
    return (head + tab).ljust(off, b"\0") + bytes(data)


def parse_payload(p):
    """-> {id: bytes}; raises PackDataError.  Same rules as the console (sgp_core.c)."""
    if len(p) < 32 or p[:4] != MAGIC or p[4] != 1 or p[5] not in (len(REQUIRED), len(REQUIRED) + len(OPTIONAL)) or p[6:8] != b"\0\0":
        raise PackDataError("header")
    total, tcrc = struct.unpack_from(">II", p, 8)
    if total != len(p) or total % 32:
        raise PackDataError("length")
    n = p[5]
    tab = p[16:16 + 16 * n]
    if crc32(tab) != tcrc:
        raise PackDataError("table CRC")
    out = {}
    for k in range(n):
        i, off, ln, crc = struct.unpack_from(">B3xIII", tab, 16 * k)
        if i not in NAMES or i in out or off % 32 or off < 16 + 16 * n or ln == 0 or off + ln > total:
            raise PackDataError("entry %d" % k)
        d = p[off:off + ln]
        if crc32(d) != crc:
            raise PackDataError("entry %d CRC" % k)
        out[i] = d
    if any(i not in out for i in REQUIRED):
        raise PackDataError("missing entry")
    return out


def head_bin(mtime=1760000000.0):
    sys.path.insert(0, os.path.join(REPO, "offline", "seed"))
    import make_slp_head
    return make_slp_head.head(mtime, 29)


def slippi_inputs(slp_dir, slmap=False):
    """the five Slippi files of Sys/GameFiles/GALE01 (+ MnSlMap.usd.diff with slmap), SHA-1 and size checked against overlay_hashes.json (the same check as apply_overlays / build_seed_disc)"""
    hashes = json.load(open(os.path.join(REPO, "offline", "iso", "overlay_hashes.json")))
    out = {}
    for i in (4, 5, 6, 7, 8) + ((11,) if slmap else ()):
        name = NAMES[i]
        d = open(os.path.join(slp_dir, name), "rb").read()
        want = hashes["added"][name] if i == 8 else hashes["patched"][name[:-5] if name.endswith(".diff") else name]["diff"]
        if len(d) != want["size"] or hashlib.sha1(d).hexdigest() != want["sha1"]:
            raise PackDataError("%s is not the Slippi file this build was made for (size/sha1)" % name)
        out[i] = d
    return out


def build_gci(payload, seq):
    """-> the UNSIGNED .gci: 0x40 directory entry + header block + payload blocks"""
    import multishine_provision as P
    import mscf as M
    if not 1 <= seq <= 0xFFFFFFFF:
        raise PackDataError("seq out of range")
    pay = payload + bytes(-len(payload) % BLOCK)
    h = bytearray(0x80)
    struct.pack_into(">IHHIIIIIIIII", h, 0, M.MAGIC, M.VERSION, PACK_ABI, seq, 0, 0, BLOCK, len(payload), crc32(payload), 0, 0, 0)
    struct.pack_into(">I", h, 0x2C, crc32(bytes(h[:0x2C])))
    blk = bytearray(BLOCK)
    blk[0:32] = b"Multishine".ljust(32, b"\0")
    blk[32:64] = b"System files".ljust(32, b"\0")
    P.put_icon(blk, FILE_NAME)
    blk[HDR_OFF:HDR_OFF + 0x80] = h
    data = bytes(blk) + pay
    if len(data) > MAX_FILE:
        raise PackDataError("container %d bytes is more than the Update screen can stage (%d)" % (len(data), MAX_FILE))
    return P.make_dir_entry(FILE_NAME, 0, len(data) // BLOCK, P.PERM_PUBLIC, None) + data


def guard(src):
    """src: a .gci (with the 0x40 entry) or a served file.  -> (ok, message).  A plain ABI 4 container, no segments / patches / flags, payload parses and every CRC holds, nothing after it.
    Run by dev_ota.py / the publish tool (outside this repository) publish (kind pack) and by the update catalog."""
    import mscf as M
    b = bytes(src)
    if len(b) % BLOCK == GCI_HDR:
        b = b[GCI_HDR:]
    if len(b) < 2 * BLOCK or len(b) % BLOCK or len(b) > MAX_FILE:
        return False, "REFUSED: a system-files container is 2..%d blocks (got %d bytes)" % (MAX_FILE // BLOCK, len(b))
    h = b[HDR_OFF:HDR_OFF + 0x80]
    magic, ver, abi, seq, flags, base, size, ilen, icrc, nseg, npatch, entry = struct.unpack_from(">IHHIIIIIIIII", h, 0)
    if magic != M.MAGIC or ver != M.VERSION or abi != PACK_ABI or flags or nseg or npatch:
        return False, "REFUSED: not a plain ABI 4 container (version %d abi %d flags 0x%X segments %d patches %d)" % (ver, abi, flags, nseg, npatch)
    if not 0 < ilen <= len(b) - BLOCK or crc32(b[BLOCK:BLOCK + ilen]) != icrc:
        return False, "REFUSED: payload length or CRC"
    try:
        f = parse_payload(b[BLOCK:BLOCK + ilen])
    except PackDataError as e:
        return False, "REFUSED: %s" % e
    if any(b[BLOCK + ilen:]):
        return False, "REFUSED: bytes after the payload"
    return True, "ok: system files data %d: %s" % (seq, ", ".join("%s %d B" % (NAMES[i], len(f[i])) for i in sorted(f)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--slippi-files", required=True)
    b.add_argument("--with-slmap", action="store_true", help="also MnSlMap.usd.diff (id 11): the console then builds a six-entry pack (34 blocks instead of 30) whose MnSlMap.usd carries Slippi's frozen-Stadium stage select art")
    b.add_argument("--seq", type=int, required=True)
    b.add_argument("--out", required=True)
    b.add_argument("--mtime", type=float, default=1760000000.0)
    i = sub.add_parser("info")
    i.add_argument("file")
    a = ap.parse_args(argv)
    if a.cmd == "build":
        files = slippi_inputs(a.slippi_files, a.with_slmap)
        files[9] = head_bin(a.mtime)
        gci = build_gci(build_payload(files), a.seq)
        ok, msg = guard(gci)
        if not ok:
            raise SystemExit(msg)
        open(a.out, "wb").write(gci)
        print(msg)
        return 0
    ok, msg = guard(open(a.file, "rb").read())
    print(msg)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
