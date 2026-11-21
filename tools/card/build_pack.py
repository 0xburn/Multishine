#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""build_pack: the card file "MultishinePack" (the disc-file pack, format in offline/multishine/menus/df/df.h; design ).

  build_pack.py --disc RETAIL.iso [--slippi-files <Slippi GameFiles/GALE01>] --files NAME[=mode] ... --out DIR [--sign-ovl FILE] [--mtime T]
      NAME = a disc file name of the RC1 set (MnMaAll.usd SdMenu.usd SdSlChr.usd MnExtAll.usd MnSlMap.usd slpCSS.dat ...), taken from the user's own retail disc plus the user's own
      Slippi install (offline/iso/apply_overlays.py: VCDIFF overlays decoded and hash-checked); mode = auto (default: patch for a changed file when valid and smaller, else mslz,
      else raw) | raw | mslz | patch.  Writes DIR/MultishinePack.gci and DIR/pack.json (names, modes, sizes, SHA-1 of every produced file).
  The inputs are only read.  The output holds Slippi- and Nintendo-derived bytes: built on the user's machine, never distributed (COMPLIANCE_CHARTER rule 7).
Library: make_pack(files, retail, modes) -> (gci bytes, manifest).  stdlib only (apart from the repo's gcm / apply_overlays helpers for the CLI)."""
import argparse, hashlib, json, os, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "offline", "provision"))
sys.path.insert(0, os.path.join(REPO, "offline", "cardloader"))
sys.path.insert(0, os.path.join(REPO, "tools", "payload_pack"))
sys.path.insert(0, os.path.join(REPO, "offline"))
import multishine_provision as P  # noqa: E402
import mscf_z as Z  # noqa: E402
import mslz  # noqa: E402
import mdelta  # noqa: E402
import projname  # noqa: E402
import card_text  # noqa: E402

MAGIC, VERSION = 0x4D53504B, 1
BLOCK, HDR_OFF, TOC_OFF, ENT = 0x2000, 0x1000, 0x1020, 64
RAW, MSLZ, DELTA, DELTA_IP = 0, 1, 3, 4
FILE_NAME = projname.SHORT + "Pack"
FILE_NAME_OVL = projname.SHORT + "Ovl"
OVL_ABI = 2
MAX_ENTRIES = 6
LAST_NAMES = ("MnSlMap.usd",)    # optional 6th entry of the stock pack: after the five stock entries, so a pack without it is byte for byte what it always was


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def rup32(x):
    return (x + 31) & ~31


def _ops(delta):
    """parse an mdelta stream -> [(tag, a, b, lit)]"""
    ops, i = [], 0
    def rv():
        nonlocal i
        v = s = 0
        while True:
            b = delta[i]; i += 1
            v |= (b & 0x7F) << s
            if not b & 0x80:
                return v
            s += 7
    while True:
        t = delta[i]; i += 1
        if t == 0:
            return ops
        if t == 1:
            ln = rv(); ops.append((1, ln, 0, delta[i:i + ln])); i += ln
        elif t == 2:
            z = rv(); ln = rv(); ops.append((2, z, ln, b""))
        else:
            ln = rv(); ops.append((3, ln, delta[i], b"")); i += 1


def inplace_ok(delta, old, new_size):
    """the console's in-place rules (df_core.c delta_apply with ip=1): COPY forward only and never ahead of the unread source, writes never reach it"""
    oldcap, newcap = rup32(len(old)), rup32(new_size)
    if newcap < oldcap:
        return False
    shift = newcap - oldcap
    d = prev_end = 0
    for t, a, b, lit in _ops(delta):
        if t == 2:
            src = prev_end - ((a >> 1) + 1) if a & 1 else prev_end + (a >> 1)
            if src < prev_end or d > shift + src:
                return False
            d += b; prev_end = src + b
        else:
            ln = a
            if d > shift + prev_end or ln > shift + prev_end - d:
                return False
            d += ln
    return True


OVL_SLACK = 32      # MultishineOvl entries are multiples of 32 bytes (sgo_check), and a stream with a match can never decode in place in exactly n bytes (gap + z >= n + 1): the
                    # overlay loaders therefore hand df_produce a buffer of rup32(n) + DF_OVL_SLACK bytes (df.h); an entry that needs the slack is only chosen when it saves a card block


def best_delta(old, new):
    """-> (delta ops, stream, gap): the op stream of mdelta.PARAM_SETS whose MSLZ stream is the smallest"""
    best = None
    for prm in mdelta.PARAM_SETS:
        d = mdelta.encode(old, new, prm)
        z, gap = stream_z(d)
        if best is None or len(z) < len(best[1]):
            best = (d, z, gap)
    return best


def stream_z(data):
    """smallest MSLZ stream of data (optimal parse when a C compiler is there, else the reference compressor) and its in-place gap"""
    z = mslz.compress_best(data)
    assert mslz.decompress(z, len(data)) == bytes(data)
    return z, Z.inplace_gap(z, len(data))


def plan_mslz(new, slack=0):
    """-> (stream, gap) that decodes in place within rup32(len(new)) + slack bytes (df_produce: gap + stream <= cap), or None"""
    cap = rup32(len(new)) + slack
    z = mslz.compress_best(new, cap=cap)
    if z is None:
        return None
    assert mslz.decompress(z, len(new)) == bytes(new)
    gap = Z.inplace_gap(z, len(new))
    if gap + len(z) > cap:
        return None
    return z, gap


def make_pack(files, retail, modes=None, mtime=None, container_seq=None, fallback=()):
    """files: {name: bytes} (the produced disc files); retail: {name: bytes} for the names that exist on the retail disc; modes: {name: 'auto'|'raw'|'mslz'|'patch'}.
    fallback: names that ALSO get a FULL entry (MSLZ or raw, no base) appended after all primary entries (base-aware decoder,: used when the disc's file is neither the stock base nor the
    intended result).  Costs the whole file's blocks and one of the MAX_ENTRIES slots; the delta entry stays first so older engines and df_find see it."""
    modes = modes or {}
    fallback = list(fallback)
    if len(files) + len(fallback) > MAX_ENTRIES or any(n not in files for n in fallback):
        raise ValueError("too many entries (or a fallback name that is not a file)")
    cont = container_seq is not None            # MultishineOvl: MSCF-style signed container (ABI 2, flag 8), table in the first payload block, data after it
    ents, data, off, man = [], bytearray(), (2 * BLOCK if cont else BLOCK), []
    for name, is_alt in [(n, False) for n in sorted(files, key=lambda n: (n in LAST_NAMES, n))] + [(n, True) for n in fallback]:
        new = bytes(files[name])
        old = None if is_alt else retail.get(name)
        want = "auto" if is_alt else modes.get(name, "auto")
        e = dict(name=name.encode("ascii"), mode=RAW, new_size=len(new), old_size=0, stored=new, gap=0, crc_new=crc32(new), crc_old=0, p_pos=0, p_old=0)  # p_old = CRC-32 of the stored bytes (set below)
        if len(name) >= 24:
            raise ValueError("name too long: " + name)
        chosen = None
        if want in ("auto", "inplace") and old is not None and old != new and len(new) >= len(old):
            d = mdelta.encode(old, new)         # in place: tiny ops only (< 4096 B), the first parameter set
            if inplace_ok(d, old, len(new)) and len(d) < 4096:
                z, gap = stream_z(d)
                chosen = ("inplace", len(z))
                e.update(mode=DELTA_IP, old_size=len(old), stored=z, gap=gap, crc_old=crc32(old), p_pos=len(d))
            elif want == "inplace":
                raise ValueError("%s: in-place delta not possible" % name)
        if chosen is None and want in ("auto", "delta") and old is not None and old != new:
            d, z, gap = best_delta(old, new)
            whole = plan_mslz(new)
            blocks_ = lambda x: (x + BLOCK - 1) // BLOCK
            # delta when it is less than half of the whole-file stream (the earlier rule) or when it saves at least one card block (SdMenu.usd: 22,015 B in 3 blocks against 35,870 B in 5)
            if want == "delta" or whole is None or len(z) * 2 < len(whole[0]) or blocks_(len(z)) < blocks_(len(whole[0])):
                chosen = ("delta", len(z))
                e.update(mode=DELTA, old_size=len(old), stored=z, gap=gap, crc_old=crc32(old), p_pos=len(d))
        if chosen is None and want in ("auto", "mslz"):
            zp = plan_mslz(new, OVL_SLACK if cont else 0)
            blocks = lambda x: (x + BLOCK - 1) // BLOCK
            if zp and (want == "mslz" or (len(zp[0]) < len(new) and (not cont or blocks(len(zp[0])) < blocks(len(new))))):
                e.update(mode=MSLZ, stored=zp[0], gap=zp[1])
                chosen = ("mslz", len(zp[0]))
            elif want == "mslz":
                raise ValueError("%s: mslz in-place does not fit" % name)
        if chosen is None:
            chosen = ("raw", len(new))
        e["stored_len"] = len(e["stored"])
        e["p_old"] = crc32(e["stored"])
        e["data_off"] = off
        data += e["stored"] + b"\0" * (-len(e["stored"]) % BLOCK)
        off += (len(e["stored"]) + BLOCK - 1) // BLOCK * BLOCK
        ents.append(e)
        man.append(dict(name=name, mode=chosen[0], full_fallback=is_alt, new_size=len(new), old_size=len(old) if old is not None else 0, stored_len=e["stored_len"],
                        blocks=(e["stored_len"] + BLOCK - 1) // BLOCK, sha1=hashlib.sha1(new).hexdigest(), crc_new="%08x" % e["crc_new"]))
    toc = b""
    for e in ents:
        toc += e["name"].ljust(24, b"\0") + struct.pack(">IIIIIIIIII", e["mode"], e["new_size"], e["old_size"], e["stored_len"], e["data_off"], e["gap"],
                                                        e["crc_new"], e["crc_old"], e["p_pos"], e["p_old"])
    assert len(toc) == ENT * len(ents)
    blk = bytearray(BLOCK)
    card_text.put(blk, "Ovl" if cont else "Pack")
    P.put_icon(blk, FILE_NAME_OVL if cont else FILE_NAME)
    if cont:
        tb = bytearray(BLOCK)
        total = 2 * BLOCK + len(data)
        struct.pack_into(">IIIII", tb, 0, MAGIC, VERSION, len(ents), total, crc32(toc))
        tb[0x20:0x20 + len(toc)] = toc
        payload = bytes(tb) + bytes(data)
        payload_len = len(payload)
        hd = bytearray(0x80)
        struct.pack_into(">IHHIIIIII", hd, 0, 0x4D534346, 1, OVL_ABI, container_seq, 8, 0x81700000, (payload_len + BLOCK - 1) // BLOCK * BLOCK, payload_len, crc32(payload))
        struct.pack_into(">III", hd, 0x20, 0, 0, 0)               # nseg, npatch, entry
        struct.pack_into(">I", hd, 0x2C, crc32(bytes(hd[:0x2C])))   # header CRC (no tables)
        blk[HDR_OFF:HDR_OFF + 0x80] = hd
        body = bytes(blk) + payload
        name = FILE_NAME_OVL
    else:
        total = BLOCK + len(data)
        struct.pack_into(">IIIII", blk, HDR_OFF, MAGIC, VERSION, len(ents), total, crc32(toc))
        blk[TOC_OFF:TOC_OFF + len(toc)] = toc
        body = bytes(blk) + bytes(data)
        name = FILE_NAME
    gci = P.make_dir_entry(name, 0, len(body) // BLOCK, P.PERM_PUBLIC, mtime) + body
    return gci, dict(file=name, blocks=len(body) // BLOCK, bytes=len(body), entries=man)


def load_files(disc, slippi_files, names, sign_ovl=None):
    """-> (files, retail) from the user's own disc and Slippi install.  Added files are not on the retail disc."""
    import tempfile, shutil
    sys.path.insert(0, os.path.join(REPO, "offline", "iso"))
    import apply_overlays
    tmp = tempfile.mkdtemp(prefix="pack-", dir=os.environ.get("TMPDIR") or None)
    try:
        ovl = os.path.join(tmp, "ovl")
        errs, done = apply_overlays.apply_all(disc, slippi_files, ovl, use_xdelta=False, only=set(n for n in names if n != "ms/sign.ovl"), log=lambda *_: None)
        if errs:
            sys.exit("overlay decode/verify failed: " + "; ".join(errs))
        files, retail = {}, {}
        img = apply_overlays.gcm.Image(disc)
        for n in names:
            if n == "ms/sign.ovl":
                files[n] = open(sign_ovl, "rb").read()
                continue
            files[n] = open(os.path.join(ovl, n), "rb").read()
            node, _, _ = img.lookup(n)
            if node is not None and not node.is_dir:
                retail[n] = node.src.read(img.stream)
        return files, retail
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--disc", default=os.environ.get("MULTISHINE_RETAIL_ISO"))
    sys.path.insert(0, os.path.join(REPO, "offline", "iso"))
    import apply_overlays
    ap.add_argument("--slippi-files", default=str(apply_overlays.DEFAULT_OVERLAYS))
    ap.add_argument("--files", nargs="*", default=[])
    ap.add_argument("--sign-ovl", help="ms/sign.ovl: goes into the separate MultishineOvl container (ours, OTA-able), not into the pack")
    ap.add_argument("--ovl-seq", type=int, default=1, help="build_seq of the MultishineOvl container (monotonic; sign it with tools/card/mscf_sign.py)")
    ap.add_argument("--fallback", nargs="*", default=[], help="names that also get a FULL entry (used when the disc's file is neither stock nor the intended result; costs the whole file in blocks)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--mtime", type=float, default=1760000000.0)
    a = ap.parse_args(argv)
    names, modes = [], {}
    for f in a.files:
        n, _, m = f.partition("=")
        names.append(n)
        if m:
            modes[n] = m
    os.makedirs(a.out, exist_ok=True)
    if names:
        files, retail = load_files(a.disc, a.slippi_files, names, None)
        gci, man = make_pack(files, retail, modes, a.mtime, fallback=a.fallback)
        open(os.path.join(a.out, FILE_NAME + ".gci"), "wb").write(gci)
        man["gci_sha1"] = hashlib.sha1(gci).hexdigest()
        json.dump(man, open(os.path.join(a.out, "pack.json"), "w"), indent=1)
        print("pack %s: %d blocks (%d entries) sha1 %s" % (FILE_NAME, man["blocks"], len(man["entries"]), man["gci_sha1"]))
        for e in man["entries"]:
            print("  %-14s %-5s new %8d old %8d stored %8d (%d blocks)" % (e["name"], e["mode"], e["new_size"], e["old_size"], e["stored_len"], e["blocks"]))
    if a.sign_ovl:
        ogci, oman = make_pack({"ms/sign.ovl": open(a.sign_ovl, "rb").read()}, {}, {"ms/sign.ovl": "auto"}, a.mtime, container_seq=a.ovl_seq)
        open(os.path.join(a.out, FILE_NAME_OVL + ".gci"), "wb").write(ogci)
        oman["gci_sha1"] = hashlib.sha1(ogci).hexdigest()
        json.dump(oman, open(os.path.join(a.out, "ovl.json"), "w"), indent=1)
        print("ovl %s: %d blocks (unsigned container, seq %d) sha1 %s" % (FILE_NAME_OVL, oman["blocks"], a.ovl_seq, oman["gci_sha1"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
