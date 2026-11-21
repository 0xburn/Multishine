#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""make_generic_base: the 5-file BASE card of a generic-layout release.

  make_generic_base.py --loader-card BUILT.raw --state-card STOCK_5FILES.raw --out DIR/MemoryCardA.USA.raw [--size 2097152]
  make_generic_base.py --art-card BUILT.raw --out OUT.raw      the same card with our art on the loader save (build.sh runs this on every card it writes)

The loader save, and MultishineCode come from the card the generic release build made (tools/card/build_card.sh through tools/release.sh with CL_LAYOUT=generic: the loader there
is the generic, production-pinned, combined loader that matches the engine); MultishineState, the standard Melee save MultishineMelee and a placeholder MultishineOvl come from an existing 5-file card (they do not depend on the
layout; the placeholder overlay is replaced by make_fresh_card_ota.sh / card_refresh.py).  Nothing else is copied.  The result is what tools/release/make_fresh_card_ota.sh takes as BASE_CARD: it swaps in the release's Code/Overlay and signs crd-N.  Checks: all five files present
exactly once, the card is consistent, the loader and the engine claim the generic hole (header base 0x8175A000).
NO NINTENDO BYTES: the built card's loader save carries the disc's banner/icon, so the loader save gets OUR banner/icon/palette and the
real digest here (tools/release/save_art.py ours_full: the form Melee accepts, payload unchanged), and the build FAILS unless the result is our art and the no-Nintendo-bytes scan
(tools/card/scan_windows.c against the retail disc, $MULTISHINE_RETAIL_ISO or the default path) finds 0 windows in it.  MS_NO_DISC_SCAN=1 skips only the disc scan (the art check stays).
Stdlib + cc."""
import argparse, os, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "vmc_builder"))
import vmc_card as C        # noqa: E402
import vmc_gci as G         # noqa: E402
sys.path.insert(0, os.path.join(REPO, "tools", "release"))
import save_art as SA       # noqa: E402

LOADER = "SuperSmashBros0110290334"
FROM_LOADER = (LOADER, "MultishineCode")
FROM_STATE = ("MultishineState", "MultishineMelee", "MultishineOvl")      # the overlay is a separate release file (a built card has none): the stock card's is only a placeholder that card_refresh.py replaces with the release's ovl-N
GENERIC_HOLE = 0x8175A000
RETAIL = "/Applications/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso"


def our_art(g):
    """the loader save with our banner/icon/palette and the real digest; everything after the digest unchanged"""
    o = SA.ours_full(g)
    if len(o) != len(g) or o[:0x40] != g[:0x40] or o[SA.DIGEST_OFF + SA.DIGEST_LEN:] != g[SA.DIGEST_OFF + SA.DIGEST_LEN:] or o[SA.DIGEST_OFF:SA.DIGEST_OFF + SA.DIGEST_LEN] != SA.digest(o):
        raise SystemExit("loader save: the art replacement changed more than the art and the digest")
    return o


def disc_scan(gci):
    """-> number of 64-byte windows the file shares with the retail disc (tools/card/scan_windows.c); SystemExit when the scan cannot run"""
    iso = os.environ.get("MULTISHINE_RETAIL_ISO") or RETAIL
    if not os.path.isfile(iso) or not shutil.which("cc"):
        raise SystemExit("no-Nintendo-bytes scan cannot run (retail disc %s, cc %s); MS_NO_DISC_SCAN=1 skips it" % ("found" if os.path.isfile(iso) else "missing", "found" if shutil.which("cc") else "missing"))
    d = tempfile.mkdtemp(prefix="mgb-scan-")
    try:
        exe, f = os.path.join(d, "scan"), os.path.join(d, "loader.gci")
        subprocess.run(["cc", "-O2", "-o", exe, os.path.join(REPO, "tools", "card", "scan_windows.c")], check=True)
        open(f, "wb").write(gci)
        r = subprocess.run([exe, iso, f], capture_output=True, text=True)
        if r.returncode not in (0, 1) or not r.stdout.strip().splitlines()[-1].startswith("HITS "):
            raise SystemExit("no-Nintendo-bytes scan failed: %s" % (r.stdout + r.stderr)[-300:])
        return int(r.stdout.strip().splitlines()[-1].split()[1])
    finally:
        shutil.rmtree(d, ignore_errors=True)


def gci_of(card, name):
    e = card.find(name)
    if e is None:
        raise SystemExit("%s: no file %s" % (card, name))
    return G.export_gci(card, e)


def check_art(card):
    """SystemExit unless the card's loader save carries our art and (unless MS_NO_DISC_SCAN=1) shares no window with the retail disc"""
    ldr = gci_of(card, LOADER)
    if SA.ours_full(ldr) != ldr:
        raise SystemExit("REFUSED: the loader save on the card does not carry our art")
    if os.environ.get("MS_NO_DISC_SCAN") == "1":
        print("loader save: our art (disc scan skipped: MS_NO_DISC_SCAN=1)")
    else:
        hits = disc_scan(ldr)
        if hits:
            raise SystemExit("REFUSED: the loader save shares %d windows with the retail disc (Nintendo bytes)" % hits)
        print("loader save: our art, no-Nintendo-bytes scan 0 hits")


def art_card(src, dst):
    """src with the loader save replaced by the same save carrying our art; every other file unchanged"""
    c = C.Card(open(src, "rb").read())
    e = c.find(LOADER)
    if e is None:
        raise SystemExit("%s: no file %s" % (src, LOADER))
    g = our_art(G.export_gci(c, e))
    c.remove(e)
    c.add(g[:0x40], g[0x40:])
    probs = c.consistency()
    if probs:
        raise SystemExit("card not consistent after the art swap: %s" % probs)
    check_art(c)
    os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
    open(dst, "wb").write(bytes(c.img))
    print("card %s: our art on the loader save, %s" % (dst, ", ".join(sorted(x["name"] for x in c.entries()))))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--art-card")
    ap.add_argument("--loader-card")
    ap.add_argument("--state-card")
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", type=lambda x: int(x, 0), default=2 * 1024 * 1024)
    a = ap.parse_args(argv)
    if a.art_card:
        art_card(a.art_card, a.out); return
    if not (a.loader_card and a.state_card):
        ap.error("--loader-card and --state-card are required (or --art-card)")
    lc = C.Card(open(a.loader_card, "rb").read())
    sc = C.Card(open(a.state_card, "rb").read())
    code = gci_of(lc, "MultishineCode")
    base = struct.unpack_from(">I", code, 0x40 + 0x1000 + 16)[0]
    if base != GENERIC_HOLE:
        raise SystemExit("MultishineCode claims hole 0x%08X, not the generic 0x%08X: this is not a generic build" % (base, GENERIC_HOLE))
    out = C.format_card(a.size)
    for src, names in ((lc, FROM_LOADER), (sc, FROM_STATE)):
        for n in names:
            g = gci_of(src, n)
            if n == LOADER:
                g = our_art(g)
            out.add(g[:0x40], g[0x40:])
    probs = out.consistency()
    if probs or {e["name"] for e in out.entries()} != set(FROM_LOADER + FROM_STATE):
        raise SystemExit("base card not consistent: %s" % (probs or "wrong file set"))
    check_art(out)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out, "wb").write(bytes(out.img))
    print("generic base card %s: %s, free %d blocks" % (a.out, ", ".join(sorted(e["name"] for e in out.entries())), out.free_blocks()))


if __name__ == "__main__":
    main()
