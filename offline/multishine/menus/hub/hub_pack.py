#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""hub_pack: the own-files container MultishineOvl with BOTH overlays of ours: ms/sign.ovl (the sign-in / Update screens) and ms/hub.ovl (the hub client,
 6.5).  Same format and library as tools/card/build_pack.py --sign-ovl (make_pack with a container_seq); that tool takes one file only.

  hub_pack.py --sign-ovl sign.ovl --hub-ovl hub.ovl [--gns-ovl gns.ovl] [--menu-ovl menu.ovl] [--rep-ovl rep.ovl] --out DIR [--ovl-seq N] [--mtime T]
      --gns-ovl adds the Game Night match-time block ms/gns.ovl (menus/gns/gns_pack.py); without it the container is what it was and the console
      simply has no spectate uploader
      --rep-ovl adds the Slippi game-report client ms/rep.ovl (menus/rep/rep_ovl_pack.py): loaded into the HSD heap only while a report is due in a main-menu
      scene and our server announced HELLO capability bit 15 to this console; an engine built repovl=off ignores the entry.  It is the FIFTH entry: DF_MAX must be at least 5 + the stock pack's 5
      --menu-ovl adds the menu code ms/menu.ovl (menus/ml/ml_ovl_pack.py, an engine built with menuovl=ovl on the card route reads it from here at every menu entry,
       6.4.4); an engine built menuovl=flat ignores the entry.  The container then has FOUR entries: the engine table (df.h DF_MAX) must hold them
      and the five of the stock pack
      writes DIR/MultishineOvl.gci (UNSIGNED container: sign it with tools/card/mscf_sign.py sign ... --seq N) and DIR/ovl.json (entries, sizes, SHA-1)."""
import argparse, hashlib, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "card"))
import build_pack as BP  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sign-ovl", required=True); ap.add_argument("--hub-ovl", required=True); ap.add_argument("--gns-ovl"); ap.add_argument("--menu-ovl"); ap.add_argument("--rep-ovl"); ap.add_argument("--out", required=True)
    ap.add_argument("--ovl-seq", type=int, default=1); ap.add_argument("--mtime", type=float, default=1760000000.0)
    a = ap.parse_args(argv)
    files = {"ms/sign.ovl": open(a.sign_ovl, "rb").read(), "ms/hub.ovl": open(a.hub_ovl, "rb").read()}
    if a.gns_ovl:
        files["ms/gns.ovl"] = open(a.gns_ovl, "rb").read()
    if a.menu_ovl:
        files["ms/menu.ovl"] = open(a.menu_ovl, "rb").read()
    if a.rep_ovl:
        files["ms/rep.ovl"] = open(a.rep_ovl, "rb").read()
    for n, b in files.items():
        if len(b) % 32 or len(b) < 64:
            sys.exit("%s: %d bytes is not a packed overlay" % (n, len(b)))
    os.makedirs(a.out, exist_ok=True)
    gci, man = BP.make_pack(files, {}, {n: "auto" for n in files}, a.mtime, container_seq=a.ovl_seq)       # auto: an entry is MSLZ only when that saves a card block (needs the DF_OVL_SLACK engine, build >= the CARD_COMPONENTS_OPT change)
    open(os.path.join(a.out, BP.FILE_NAME_OVL + ".gci"), "wb").write(gci)
    man["gci_sha1"] = hashlib.sha1(gci).hexdigest()
    man["files_sha1"] = {n: hashlib.sha1(b).hexdigest() for n, b in files.items()}
    json.dump(man, open(os.path.join(a.out, "ovl.json"), "w"), indent=1)
    print("ovl %s: %d blocks (unsigned container, seq %d, entries %s) sha1 %s" % (BP.FILE_NAME_OVL, man["blocks"], a.ovl_seq, ", ".join(sorted(files)), man["gci_sha1"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
