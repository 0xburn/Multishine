#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh a Multishine card WITHOUT touching its loader: new code/overlay builds + the save-file icons.

  card_refresh.py --base OLD.raw --out NEW.raw --pub DEVPUBHEX \
      --code 392.mscf --code-prev 391.mscf --ovl ovl-392.mscf --ovl-prev ovl-391.mscf [--manifest M.json] [--mtime T]
      [--records [target,hrc,tenman,events]] [--records-source resolved|tasvideos] [--records-basis rta|tas] [--records-force]
  (the records option works alone too: without --code/--ovl only MultishineMelee changes)

What it does, on a copy in memory (the base file is only read):
  * MultishineCode / MultishineCodeB / MultishineOvl / MultishineOvlB: replaced by the given served OTA containers (<seq>.mscf / ovl-<seq>.mscf = the file
    data without its 0x40 directory entry).  Each must verify under --pub exactly like the loader / the installer decide (mscf_sign.check: code ABI 1,
    overlay ABI 2).  Block 0 gets the current icon (tools/brand/make_save_art.py) and comment lines; both are outside the signed bytes (mscf_sign.py doc).
    A code file keeps the dev watermark comment when it has one (it is dev-signed).
  * every other Multishine* file (Pack, Options, Online, Codes, State, Bridge, Identity): ICON REFRESH ONLY.  The directory entry gets icon format 1 at
    0x40 (speed 1, comment at 0) and block 0 bytes 0x40..0x840 get the CI8 icon (0x600 B) + zeros; refused unless those bytes were the old RGB5A3 placeholder
    or all zero.  Comment lines change only for Identity (old builder default -> new default).  Everything from 0x840 on (the payload at 0x1000) is
    compared byte for byte afterwards: identical, or nothing is written.
  * --records [SETS]: the Melee single-player records (Target Test, Home-Run, 10-Man, Event Matches; tools/card/melee_records.py) are
    rewritten in the card's existing MultishineMelee: only the record fields of the two game-data sectors and those sectors' own checksums change (a player's better
    record is kept unless --records-force); the directory entry and every other block of the file are checked byte-identical.  A card without MultishineMelee is refused.
  * the exploit save SuperSmashBros0110290334 (the loader), Melee's own save MultishineMelee (unless --records) and any other file: not touched; checked byte-identical
    (directory entry and data) at the end.
Never prints the contents of MultishineIdentity or MultishineOnline (only sizes and sha1 of their payload area).  Stdlib only."""
import argparse, hashlib, json, os, struct, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
for p in ("tools/vmc_builder", "tools/card", "offline/provision", "offline/cardloader"):
    sys.path.insert(0, os.path.join(REPO, p))
sys.path.insert(0, os.path.join(REPO, "offline"))
import card_text as CT      # noqa: E402
import vmc_card as C        # noqa: E402
import vmc_gci as G         # noqa: E402
import vmc_validate as VV   # noqa: E402
import mscf_sign as S       # noqa: E402
import multishine_provision as P  # noqa: E402
import melee_records as MR  # noqa: E402
import melee_coins as MC    # noqa: E402

BLOCK, ICON_OFF, OLD_ICON_END, NEW_ICON_END = 0x2000, 0x40, 0x840, 0x40 + P.ICON_LEN
LOADER_SAVE = "SuperSmashBros0110290334"
MELEE = MC.MELEE_NAME
CONTAINERS = {"code": "MultishineCode", "code_prev": "MultishineCodeB", "ovl": "MultishineOvl", "ovl_prev": "MultishineOvlB"}
ICON_ONLY = ("MultishinePack", "MultishineOptions", "MultishineOnline", "MultishineCodes", "MultishineState", "MultishineBridge", "MultishineIdentity")
PRIVATE = ("MultishineIdentity", "MultishineOnline")
DEV_MARK = b"DEV-SIGNED CODE FILE"


class Refuse(Exception):
    pass


def placeholder():
    """The 0x800-byte RGB5A3 placeholder the builders wrote before 62329ea (multishine_provision._icon_placeholder, removed there)."""
    out = bytearray()
    for ty in range(8):
        for tx in range(8):
            for y in range(4):
                for x in range(4):
                    edge = (tx == 0 and x == 0) or (tx == 7 and x == 3) or (ty == 0 and y == 0) or (ty == 7 and y == 3)
                    out += struct.pack(">H", 0x8E7F if edge else 0x8008)
    return bytes(out)


def sha1(b):
    return hashlib.sha1(b).hexdigest()


def comment(blk):
    return [blk[i:i + 32].split(b"\0")[0].decode("ascii", "replace") for i in (0, 32)]


def file_rows(card):
    rows = {}
    for e in card.entries():
        d = card.read(e)
        r = e["raw"]
        row = dict(name=e["name"], blocks=e["blocks"], sha1_file=sha1(d), sha1_payload=sha1(d[OLD_ICON_END:]), sha1_entry=sha1(r),
                   icon_format=struct.unpack_from(">H", r, 0x30)[0], icon_addr="0x%X" % struct.unpack_from(">I", r, 0x2C)[0])
        if e["name"] not in PRIVATE and e["name"] != LOADER_SAVE:
            row["comment"] = comment(d)
        if e["name"] in CONTAINERS.values():
            try:
                p = S.parse(r + d)
                row["build_seq"], row["abi"] = p["seq"], p["abi"]
            except S.Bad:
                row["build_seq"] = None
        rows[e["name"]] = row
    return rows


def container_gci(path, name, pub, abi, mtime):
    data = open(path, "rb").read()
    if not data or len(data) % BLOCK:
        raise Refuse("%s: not whole 8 KiB blocks" % path)
    blk = bytearray(data[:BLOCK])
    if name.startswith("MultishineCode"):
        if not blk[0:32].startswith(DEV_MARK):
            CT.put(blk, CT.suffix_of(name) or "Code")           # Code / CodeB / CodeN
    else:
        CT.put(blk, CT.suffix_of(name) or "Ovl")
    blk[ICON_OFF:OLD_ICON_END] = bytes(OLD_ICON_END - ICON_OFF)
    P.put_icon(blk, name)
    new = bytes(blk) + data[BLOCK:]
    assert new[OLD_ICON_END:] == data[OLD_ICON_END:]
    gci = P.make_dir_entry(name, 0, len(new) // BLOCK, P.PERM_PUBLIC, mtime) + new
    r = S.check(gci, [bytes.fromhex(pub)], min_seq=1, abi=abi)
    if r != "OK":
        raise Refuse("%s (as %s) does not verify under the given key: %s" % (path, name, r))
    src_sig = S.check(P.make_dir_entry(name, 0, len(data) // BLOCK, P.PERM_PUBLIC, mtime) + data, [bytes.fromhex(pub)], min_seq=1, abi=abi)
    assert src_sig == "OK"
    return gci, dict(source=os.path.basename(path), source_sha1=sha1(data), source_sha1_from_0x840=sha1(data[OLD_ICON_END:]), build_seq=S.parse(gci)["seq"])


def _known_old_to_new(blk, name):
    """the readable comment lines (offline/card_text.py) when the file holds exactly a known older pair; anything else stays"""
    suf = CT.suffix_of(name)
    if suf and tuple(comment(blk)) in CT.KNOWN_OLD[suf]:
        CT.put(blk, suf)
        return True
    return False


def text_refresh(card, ent):
    """comment lines only (block 0 bytes 0..0x3F), for a file whose icon is already current"""
    first = card.chain(ent)[0]
    off = first * BLOCK
    blk = bytearray(card.img[off:off + BLOCK])
    if not _known_old_to_new(blk, ent["name"]):
        return ""
    card.img[off:off + BLOCK] = blk
    return ", comment lines made readable"


def icon_refresh(card, ent, ph):
    """Rewrite block 0 icon area + directory icon fields of one existing file; payload untouched."""
    first = card.chain(ent)[0]
    off = first * BLOCK
    blk = bytearray(card.img[off:off + BLOCK])
    old_area = bytes(blk[ICON_OFF:OLD_ICON_END])
    if old_area != ph and old_area != bytes(len(old_area)):
        if old_area[:P.ICON_LEN] == P.icon_for(ent["name"]) and old_area[P.ICON_LEN:] == bytes(OLD_ICON_END - NEW_ICON_END):
            return "already current" + text_refresh(card, ent)
        raise Refuse("%s: bytes 0x40..0x840 are neither the old placeholder icon nor empty; not touched" % ent["name"])
    before = bytes(blk)
    blk[ICON_OFF:OLD_ICON_END] = bytes(OLD_ICON_END - ICON_OFF)
    P.put_icon(blk, ent["name"])
    _known_old_to_new(blk, ent["name"])
    assert bytes(blk[OLD_ICON_END:]) == before[OLD_ICON_END:]
    card.img[off:off + BLOCK] = blk
    d = card.dirblk()
    e = bytearray(d[ent["slot"] * C.ENTRY:(ent["slot"] + 1) * C.ENTRY])
    struct.pack_into(">I", e, 0x2C, ICON_OFF)
    struct.pack_into(">H", e, 0x30, P.ICON_FORMAT)
    struct.pack_into(">H", e, 0x32, 0x0001)
    e[7] = 0x00                                     # no banner
    struct.pack_into(">I", e, 0x3C, 0)              # comment at file offset 0 (as before for every Multishine file)
    d[ent["slot"] * C.ENTRY:(ent["slot"] + 1) * C.ENTRY] = e
    card._store(d, card.batblk())
    return "placeholder replaced" if old_area == ph else "icon added (was none)"


def refresh_records(card, a):
    """Rewrite the records in the card's MultishineMelee (same blocks, same length); only the record fields of the game-data sectors and their checksums may change."""
    ent = MC.melee_entry(card, MELEE)
    if ent is None:
        raise Refuse("the card has no %s: use tools/card/melee_records.py apply with a template, --records only refreshes an existing save" % MELEE)
    ds = MR.load_dataset(None, False, False, a.records_source, a.records_basis)
    sets = MR.parse_sets(a.records)
    old = card.read(ent)
    new, log = MR.set_records(old, ds, sets, a.records_force)
    if len(new) != len(old):
        raise Refuse("%s changed length" % MELEE)
    game_blocks = set(MC.file1_sectors(old))
    for b in range(len(old) // BLOCK):
        if b not in game_blocks and old[b * BLOCK:(b + 1) * BLOCK] != new[b * BLOCK:(b + 1) * BLOCK]:
            raise Refuse("%s: block %d outside the game-data sectors would change" % (MELEE, b))
    for i, b in enumerate(card.chain(ent)):
        card.img[b * BLOCK:(b + 1) * BLOCK] = new[i * BLOCK:(i + 1) * BLOCK]
    return dict(action="records %s rewritten (%d changes, dataset %s)" % ("+".join(sets), len(log), ds["version"]), changes=log, dataset_sha1=ds["sha1"],
                sha1_payload_before=sha1(old[OLD_ICON_END:]), sha1_payload_after=sha1(new[OLD_ICON_END:]))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--pub", required=True, help="64 hex: the public key the card's loader pins (mscf_sign.py pubkey --key ~/.multishine/dev-signing.key)")
    ap.add_argument("--code")
    ap.add_argument("--code-prev")
    ap.add_argument("--ovl")
    ap.add_argument("--ovl-prev")
    ap.add_argument("--manifest")
    ap.add_argument("--mtime", type=int, help="seconds since 1970 for the new containers' directory entries (default: now)")
    ap.add_argument("--records", nargs="?", const=",".join(MR.DEFAULT_SETS), help="rewrite the records of the card's MultishineMelee (sets: target,hrc,tenman,events; default all four)")
    ap.add_argument("--records-source", choices=("resolved", "tasvideos"), default="resolved")
    ap.add_argument("--records-basis", choices=("rta", "tas"), default="rta")
    ap.add_argument("--records-force", action="store_true", help="also overwrite records the player made better than the dataset")
    a = ap.parse_args(argv)
    if not (a.code and a.ovl) and not a.records:
        raise SystemExit("--code and --ovl are required unless only --records is given")
    if os.path.abspath(a.out) == os.path.abspath(a.base) or os.path.exists(a.out):
        raise SystemExit("refusing: --out must be a new file (the base is never written)")
    raw = open(a.base, "rb").read()
    base = C.Card(raw)
    if base.consistency():
        raise SystemExit("base card is not consistent: %s" % base.consistency())
    if base.find(LOADER_SAVE) is None:
        raise SystemExit("base card has no %s (the loader): not a Multishine card" % LOADER_SAVE)
    card = C.Card(raw)
    mtime = a.mtime if a.mtime is not None else int(time.time())
    actions = {}
    try:
        for key, name in CONTAINERS.items():
            src = getattr(a, key) if (a.code and a.ovl) else None
            if not src:
                continue
            gci, info = container_gci(src, name, a.pub, 2 if key.startswith("ovl") else 1, mtime)
            G.import_gci(card, gci, replace=True)
            info["action"] = "replaced by the served container (icon + comment refreshed, signed bytes identical)"
            actions[name] = info
        ph = placeholder()
        for name in (ICON_ONLY if (a.code and a.ovl) else ()):
            ent = card.find(name)
            if ent is None:
                continue
            actions[name] = dict(action="header refresh: " + icon_refresh(card, ent, ph))
        if a.records:
            actions[MELEE] = refresh_records(card, a)
    except (Refuse, G.GciError, C.CardError, MR.RecError, MC.CoinError) as x:
        raise SystemExit("refused, nothing written: %s" % x)
    # checks: every untouched file identical; every icon-only file identical from 0x840 on; consistency; no growth beyond the new containers
    new = C.Card(bytes(card.img))
    bad = new.consistency() + VV.errors(VV.validate(bytes(new.img)))
    for e in base.entries():
        n = new.find(e["name"], e["gamecode"], e["maker"])
        if e["name"] in CONTAINERS.values() and e["name"] in actions:
            continue
        if e["name"] == MELEE and MELEE in actions:        # records: checked inside refresh_records (same length, entry identical, only the two game-data sectors differ)
            if n is None or n["raw"] != e["raw"]:
                bad.append("%s directory entry changed" % MELEE)
            continue
        if n is None:
            bad.append("%s disappeared" % e["name"])
            continue
        od, nd = base.read(e), new.read(n)
        if e["name"] in ICON_ONLY:
            if od[OLD_ICON_END:] != nd[OLD_ICON_END:] or n["blocks"] != e["blocks"]:
                bad.append("%s payload changed" % e["name"])
        elif od != nd or n["raw"] != e["raw"]:
            bad.append("%s is not byte-identical" % e["name"])
    if bad:
        raise SystemExit("refused, nothing written: %s" % "; ".join(bad))
    fd = os.open(a.out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(bytes(new.img))
    before, after = file_rows(base), file_rows(new)
    man = dict(base=os.path.abspath(a.base), base_sha1=sha1(raw), out=os.path.abspath(a.out), out_sha1=sha1(bytes(new.img)),
               free_blocks_before=base.free_blocks(), free_blocks_after=new.free_blocks(), used_before=sum(r["blocks"] for r in before.values()),
               used_after=sum(r["blocks"] for r in after.values()), files_before=before, files_after=after, actions=actions,
               untouched=[n for n in before if n not in actions], note="sha1_payload = sha1 of the file from byte 0x840 on (everything after the icon area)")
    if a.manifest:
        with open(a.manifest, "w") as f:
            json.dump(man, f, indent=1, sort_keys=True)
            f.write("\n")
    print("wrote %s  sha1 %s  used blocks %d -> %d  free %d -> %d" % (a.out, man["out_sha1"], man["used_before"], man["used_after"], man["free_blocks_before"], man["free_blocks_after"]))
    for n in before:
        b, r = before[n], after.get(n, {})
        print("  %-26s %3d -> %3s blk  %-11s %s" % (n, b["blocks"], r.get("blocks", "-"), ("seq %s->%s" % (b.get("build_seq"), r.get("build_seq"))) if "build_seq" in b else "",
                                                   actions.get(n, {}).get("action", "untouched, byte-identical")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
