#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Decode Slippi Dolphin's Sys/GameFiles/GALE01 overlays against the retail files and verify them.

For every `X.diff` in the overlay dir the retail file X (extracted from the retail ISO with gcm.py,
read-only) is the VCDIFF dictionary; the result is checked against overlay_hashes.json (size and
SHA-1 of diff, retail input and result) and, with --xdelta, against `xdelta3 -d`.  Plain files in
the overlay dir (GameSetup.dat, MxDb.dat, ...) are 'added' files and are only hash-checked.

usage:
  apply_overlays.py --iso RETAIL.iso --overlays DIR --out OUTDIR [--xdelta] [--only NAME ...]

OUTDIR gets one file per overlay under its disc name (MnSlMap.dat, GameSetup.dat, ...), ready for
`gcm.py build --overlay-dir OUTDIR` or --overlays OUTDIR.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gcm  # noqa: E402
import vcdiff  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent
DEFAULT_OVERLAYS = pathlib.Path.home().joinpath("Library/Application Support/Slippi Launcher/netplay/"
                                "Slippi Dolphin.app/Contents/Resources/Sys/GameFiles/GALE01")


def sha1(data):
    return hashlib.sha1(data).hexdigest()


def load_hashes():
    return json.loads((HERE / "overlay_hashes.json").read_text())


def retail_file(img, name):
    node, _, _ = img.lookup(name)
    if node is None or node.is_dir:
        raise gcm.GcmError("retail ISO has no file {}".format(name))
    return node.src.read(img.stream)


def check(label, data, exp):
    errs = []
    if len(data) != exp["size"]:
        errs.append("{}: size {} != expected {}".format(label, len(data), exp["size"]))
    if sha1(data) != exp["sha1"]:
        errs.append("{}: sha1 {} != expected {}".format(label, sha1(data), exp["sha1"]))
    return errs


def xdelta_decode(src, delta):
    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        (td / "s").write_bytes(src)
        (td / "d").write_bytes(delta)
        r = subprocess.run(["xdelta3", "-d", "-f", "-s", str(td / "s"), str(td / "d"), str(td / "o")],
                           capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError("xdelta3 failed: " + r.stderr.strip())
        return (td / "o").read_bytes()


def _sis_string_starts(d, nstr):
    import struct
    _fs, ds, nr, _nroot, _nref = struct.unpack(">5I", d[:20])
    root = struct.unpack(">I", d[0x20 + ds + nr * 4:0x20 + ds + nr * 4 + 4])[0]
    return [0x20 + struct.unpack(">I", d[0x20 + root + 4 * i:0x20 + root + 4 * i + 4])[0] for i in range(nstr)]


def fix_item_names(res, src):
    """Couch feedback item 16.  Slippi's SdMenu overlay differs from retail in exactly ONE byte: the first byte of SIS string 2
    (the left-hand item name list of Custom Rules > Item Switch) is 0x15 instead of 0x14, and with 0x15 the names lose their right
    alignment and run under the item icons.  Returns (bytes, changed).  Only applied when the two files differ in that single byte
    (every other string equal), otherwise the overlay is returned untouched."""
    try:
        ra, rb = _sis_string_starts(src, 1604), _sis_string_starts(res, 1604)
        import struct
        ends = sorted(set(ra) | {0x20 + struct.unpack(">I", src[4:8])[0]})
        nxt = dict(zip(ends, ends[1:]))
        for i, (a, b) in enumerate(zip(ra, rb)):
            n = nxt[a] - a
            if i == 2:
                if src[a] != 0x14 or res[b] != 0x15 or src[a + 1:a + n] != res[b + 1:b + n]:
                    return res, False
            elif src[a:a + n] != res[b:b + n]:
                return res, False
        out = bytearray(res)
        out[rb[2]] = src[ra[2]]
        return bytes(out), True
    except Exception:
        return res, False


def apply_all(iso, overlay_dir, out, use_xdelta=False, only=None, log=print):
    """Returns (list of error strings, {disc name: result bytes length})."""
    hashes = load_hashes()
    img = gcm.Image(iso)
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    errs, done = [], {}
    names = sorted(p.name for p in pathlib.Path(overlay_dir).iterdir() if not p.name.startswith("."))
    seen = set()
    for fname in names:
        raw = (pathlib.Path(overlay_dir) / fname).read_bytes()
        if fname.endswith(".diff"):
            target = fname[:-5]
            if only and target not in only:
                continue
            exp = hashes["patched"].get(target)
            if exp is None:
                errs.append("{}: not listed in overlay_hashes.json".format(fname))
                continue
            errs += check(fname + " (diff)", raw, exp["diff"])
            src = retail_file(img, target)
            errs += check(target + " (retail input)", src, exp["retail"])
            res = vcdiff.decode(src, raw)
            errs += check(target + " (result)", res, exp["result"])
            status = "ok"
            if use_xdelta:
                ref = xdelta_decode(src, raw)
                if ref != res:
                    errs.append("{}: vcdiff.py and xdelta3 disagree".format(target))
                    status = "XDELTA MISMATCH"
                else:
                    status = "ok (== xdelta3)"
            if target in ("SdMenu.usd", "SdMenu.dat"):
                res, fixed = fix_item_names(res, src)
                if fixed:
                    status += ", item-name alignment byte restored"
            seen.add(target)
        else:
            target = fname
            if only and target not in only:
                continue
            exp = hashes["added"].get(target)
            if exp is None:
                errs.append("{}: not listed in overlay_hashes.json".format(fname))
                continue
            errs += check(target + " (added)", raw, exp)
            res = raw
            status = "ok (added file)"
            seen.add(target)
        (out / target).write_bytes(res)
        done[target] = len(res)
        log("{:<20} {:>9} bytes  {}".format(target, len(res), status))
    if not only:
        want = set(hashes["patched"]) | set(hashes["added"])
        for missing in sorted(want - seen):
            errs.append("expected overlay missing from {}: {}".format(overlay_dir, missing))
    img.close()
    return errs, done


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iso", required=True)
    ap.add_argument("--overlays", default=str(DEFAULT_OVERLAYS))
    ap.add_argument("--out", required=True)
    ap.add_argument("--xdelta", action="store_true", help="cross-check every diff with `xdelta3 -d`")
    ap.add_argument("--only", nargs="+")
    args = ap.parse_args(argv)
    if args.xdelta and not shutil.which("xdelta3"):
        print("error: xdelta3 not found", file=sys.stderr)
        return 2
    errs, done = apply_all(args.iso, args.overlays, args.out, args.xdelta, set(args.only or []))
    for e in errs:
        print("ERROR: " + e)
    print("{} overlays written to {}, {} errors".format(len(done), args.out, len(errs)))
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main())
