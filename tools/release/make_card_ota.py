#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""make_card_ota: turn a finished tester card (a raw card with our five files: the tester card plus the standard Melee save, see tools/card/standard_save.py apply) into the signed OTA file crd-N.mscf that the Nintendont shell's Memory card option
downloads.

  make_card_ota.py --card MemoryCardA.USA.raw --seq N --key PATH_TO_PROD_KEY --out-dir DIR
  make_card_ota.py --card MemoryCardA.USA.raw --seq N --dev --out-dir DIR                  LAB: signs with the published RFC 8032 test key

N is the card sequence: any integer above the last published one (the shell refuses a lower number than the highest it ever used).  The raw card must hold exactly the five Multishine files
(loader save, MultishineCode, MultishineOvl, MultishineState, MultishineMelee = the standard Melee save): no account, no Slippi login.  Publish with:  (the publish step, outside this repository) DIR/crd-N.mscf"""
import argparse, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "card"))
sys.path.insert(0, os.path.join(REPO, "offline", "cardloader"))
import crd_data as K        # noqa: E402
import mscf_sign as S       # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--card", required=True)
    ap.add_argument("--seq", type=int, required=True)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--key")
    g.add_argument("--dev", action="store_true")
    ap.add_argument("--out-dir", required=True)
    a = ap.parse_args(argv)
    try:
        gci = K.build_gci(K.build_payload(K.gcis_from_card(open(a.card, "rb").read())), a.seq)
    except K.CrdError as e:
        raise SystemExit("REFUSED: %s" % e)
    with tempfile.TemporaryDirectory() as t:
        src, dst = os.path.join(t, "c.gci"), os.path.join(t, "c.signed.gci")
        open(src, "wb").write(gci)
        cmd = [sys.executable, os.path.join(REPO, "tools", "card", "mscf_sign.py"), "sign", src, "--out", dst, "--seq", str(a.seq)] + (["--dev"] if a.dev else ["--key", a.key])
        subprocess.check_call(cmd)
        signed = open(dst, "rb").read()
    served = signed[K.GCI_HDR:]
    pubs = [bytes.fromhex(S.DEV_PUB)] if a.dev else [bytes.fromhex(k.split()[1]) for k in open(os.path.join(REPO, "tools", "release", "prod_pubkeys.txt")) if k.strip() and not k.startswith("#")]
    r = S.check(signed, pubs, min_seq=1, abi=K.CRD_ABI)
    ok, msg = K.guard(served)
    if r != "OK" or not ok:
        raise SystemExit("not publishable: signature check %s, %s (is this the production key?)" % (r, msg))
    os.makedirs(a.out_dir, exist_ok=True)
    out = os.path.join(a.out_dir, "crd-%d.mscf" % a.seq)
    open(out, "wb").write(served)
    print("%s: %d bytes, %s\npublish: (the publish step, outside this repository) %s" % (out, len(served), msg, out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
