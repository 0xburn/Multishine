#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Helper of tools/dev_ota.sh (the developer OTA loop).  Stdlib only; never reads or prints the signing key.

  dev_ota.py next-seq  [--update-dir D] [--floor N]    print the next dev build_seq: max(state file, files in D, floor) + 1 (1..999; release builds start at 1000)
  dev_ota.py commit SEQ                                record SEQ as used (state file, only after the build and publish succeeded)
  dev_ota.py publish --gci CODE.gci --seq N --update-dir D --pub HEX [--keep K]
                                                       strip the 0x40-byte directory entry, check the file with the PUBLIC key exactly as the server's catalog will,
                                                       write D/N.mscf atomically (temporary name the server does not list, then rename) and keep the newest K files;
                                                       an existing D/N.mscf with other bytes is REFUSED (--replace to allow), an identical one is left as is
  dev_ota.py publish --kind ovl --gci MultishineOvl.gci ... [--like CARD.raw|FILE ...] [--allow-sign-only]
                                                       an own-files container: written as D/ovl-N.mscf (never pruned), and REFUSED when it lacks an entry of the reference (every --like, else
                                                       ms/sign.ovl + ms/hub.ovl; --allow-sign-only = the pre-hub lineage): a container update replaces the whole card file (ovl_guard.py, incident)
  dev_ota.py publish --kind net --gci net.gci ...      a network patch (tools/card/net_patch.py build): written as D/net-N.mscf (never pruned), REFUSED unless it carries only allow-listed fields (net_patch.guard)
State: $MS_DEV_HOME/dev_ota.seq (default ~/.multishine/dev_ota.seq), outside git."""
import argparse, hashlib, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "card"))
import mscf_sign as S  # noqa: E402
import ovl_guard as OG  # noqa: E402
import net_patch as NP  # noqa: E402

GCI_HDR = 0x40
FLOOR = int(os.environ.get("MS_DEV_OTA_FLOOR", "102"))      # the console card of runs build 101/102: the first loop build is 103 (MS_DEV_OTA_FLOOR for lab proofs)


def state_path():
    return os.path.join(os.environ.get("MS_DEV_HOME") or os.path.expanduser("~/.multishine"), "dev_ota.seq")


def read_state():
    try:
        return int(open(state_path()).read().strip())
    except (OSError, ValueError):
        return 0


def served(d):
    try:
        return sorted(int(m.group(1)) for n in os.listdir(d) if (m := re.fullmatch(r"(\d{1,10})\.mscf", n)))
    except OSError:
        return []


def next_seq(update_dir=None, floor=FLOOR):
    seqs = [read_state(), floor] + (served(update_dir) if update_dir else [])
    n = max(seqs) + 1
    if n > 999:
        raise SystemExit("dev build_seq would pass 999 (release builds start at 1000): reset %s by hand" % state_path())
    return n


def commit(seq):
    p = state_path()
    os.makedirs(os.path.dirname(p), mode=0o700, exist_ok=True)
    tmp = p + ".tmp"
    with open(tmp, "w") as f:
        f.write("%d\n" % seq)
    os.replace(tmp, p)


def publish(gci_path, seq, update_dir, pub_hex, keep=3, kind="code", like=(), sign_only=False, replace=False):
    gci = open(gci_path, "rb").read()
    pub = bytes.fromhex(pub_hex)
    if len(pub) != 32:
        raise SystemExit("--pub must be 64 hex digits")
    r = S.check(gci, [pub], min_seq=1, abi={"ovl": 2, "net": 3, "pack": 4}[kind]) if kind in ("ovl", "net", "pack") else S.check(gci, [pub], min_seq=1)      # own-files containers are ABI 2, network patches ABI 3
    if r != "OK":
        raise SystemExit("the code file does not verify under the dev public key: %s" % r)
    parsed = S.parse(gci)
    got = parsed["seq"]
    if got != seq:
        raise SystemExit("file build_seq %d != requested %d" % (got, seq))
    data = gci[GCI_HDR:]
    if kind == "ovl":
        ok, msg = OG.check(gci, like=like, sign_only=sign_only)
        if not ok:
            raise SystemExit(msg)
    elif kind == "net":                                     # a network patch: only allow-listed fields (net_patch.py guard); never pruned
        ok, msg = NP.guard(gci)
        if not ok:
            raise SystemExit(msg)
    elif kind == "pack":                                    # the system-files data container (pack_data.py guard); never pruned
        import pack_data as PD
        ok, msg = PD.guard(gci)
        if not ok:
            raise SystemExit(msg)
    elif kind != "code":
        raise SystemExit("--kind must be code, ovl, net or pack")
    os.makedirs(update_dir, exist_ok=True)
    stem = ("ovl-%d" if kind == "ovl" else "net-%d" if kind == "net" else "pack-%d" if kind == "pack" else "%d") % seq
    final = os.path.join(update_dir, stem + ".mscf")
    if os.path.exists(final):                               # never replace a published build_seq silently (incident: a 371 pair was overwritten)
        with open(final, "rb") as f:
            same = f.read() == data
        if same:
            return {"path": final, "seq": seq, "size": len(data), "blocks": len(data) // 0x2000, "sha256": hashlib.sha256(data).hexdigest(),
                    "compressed": parsed["comp"], "same": True}
        if not replace:
            raise SystemExit("%s already exists with other bytes: a console that saw it would never take the new one (same build_seq). Use the next seq, "
                             "or --replace if no console can have it" % final)
    tmp = os.path.join(update_dir, ".%s.part" % stem)       # not "N.mscf": the server's listing ignores it until the rename
    with open(tmp, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, final)
    old = [s for s in served(update_dir) if s != seq] if kind == "code" else []
    for s in sorted(old, reverse=True)[max(keep - 1, 0):]:
        if s < seq:
            os.unlink(os.path.join(update_dir, "%d.mscf" % s))
    return {"path": final, "seq": seq, "size": len(data), "blocks": len(data) // 0x2000, "sha256": hashlib.sha256(data).hexdigest(),
            "compressed": parsed["comp"]}


def main(argv=None):
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    n = sp.add_parser("next-seq"); n.add_argument("--update-dir"); n.add_argument("--floor", type=int, default=FLOOR)
    c = sp.add_parser("commit"); c.add_argument("seq", type=int)
    p = sp.add_parser("publish"); p.add_argument("--gci", required=True); p.add_argument("--seq", type=int, required=True)
    p.add_argument("--update-dir", required=True); p.add_argument("--pub", required=True); p.add_argument("--keep", type=int, default=3)
    p.add_argument("--kind", choices=("code", "ovl", "net", "pack"), default="code"); p.add_argument("--like", action="append", default=[]); p.add_argument("--allow-sign-only", action="store_true")
    p.add_argument("--replace", action="store_true", help="allow replacing an existing file of the same build_seq with other bytes (default: refuse)")
    a = ap.parse_args(argv)
    if a.cmd == "next-seq":
        print(next_seq(a.update_dir, a.floor))
    elif a.cmd == "commit":
        commit(a.seq)
    else:
        r = publish(a.gci, a.seq, a.update_dir, a.pub, a.keep, a.kind, a.like, a.allow_sign_only, a.replace)
        print("%s  %d bytes = %d blocks  sha256 %s%s" % (r["path"], r["size"], r["blocks"], r["sha256"], "  (already there, identical)" if r.get("same") else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
