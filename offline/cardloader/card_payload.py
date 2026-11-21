#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""card_payload: build a patcher payload (offline/multishine/payload.py) with the new text section placed at the CARD HOLE instead of 0x804F0C20.

  card_payload.py --contract C.json --out DIR [--hole-base 0x81734000]

It changes no file of the payload builder: payload.py reads dol_patch.DEFAULT_BASE / DEFAULT_MAX_END at every call, so setting them here makes
the same placement loop (the image is linked at the address its C2 body lands on) converge at the hole.  The resulting codes.bin is then
exported with card_export.py.  Whether the image WORKS at the new base is a separate question:
the engine has hard-coded assumptions about 0x804F0C20 (sd_melee.c BAKED_LO, s12_gc.c ranges)."""
import argparse, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "offline", "dolpatch"))
sys.path.insert(0, os.path.join(REPO, "offline", "multishine"))
sys.path.insert(0, HERE)
import dol_patch as BD  # noqa: E402
import payload as PAY  # noqa: E402
import mscf as M  # noqa: E402

# The loader's own work area + header block, [CL_HOLE_MAX-0xC000, CL_HOLE_MAX) (cl_loader.c: wa = top - CL_WA_SIZE, hb = wa - one block), lies directly ABOVE the hole the
# loader claims, i.e. above the lowered arena_hi / AllM end, and is unused once the loader has returned.  From ENGINE_MEMORY_BREAKDOWN step 2b the engine's CARD work area
# (on_card + sbr_card, one shared 49,152 B area) lives there instead of inside the image.  The address depends on the layout only (CL_LAYOUT stock / generic / low),
# so the payload must be built for the layout of the loader that will load it, exactly like --hole-base.
WA_ADDR = M.HOLE_MAX - M.WA_SIZE - M.BLOCK


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--contract", required=True); ap.add_argument("--out", required=True)
    ap.add_argument("--hole-base", type=lambda x: int(x, 0), default=0x81734000)
    a = ap.parse_args()
    df = ""
    if os.environ.get("MS_DF_PUBKEYS"):       # the build's pinned public keys for MultishineOvl: 32-byte keys as hex, comma separated
        df += " -DDF_PUBKEYS=" + ",".join("0x%02X" % b for k in os.environ["MS_DF_PUBKEYS"].split(",") for b in bytes.fromhex(k))
    if os.environ.get("MS_DF_NOVERIFY"):      # size-constrained LAB builds: the engine does not verify MultishineOvl at load (the installer still does)
        df += " -DDF_NO_VERIFY"
    if os.environ.get("MS_DF_UNSIGNED"):      # lab / dev cards: an unsigned container is accepted too
        df += " -DDF_ALLOW_UNSIGNED"
    if os.environ.get("MS_MATCH_MSG"):        # prototype switch: friend messages during the match
        df += " -DMS_MATCH_MSG=1"
    if os.environ.get("MS_XCF"):              # lab only: more compile flags for the engine (the cost runs: -DMM_FTSTATS=1, the frame period line of the post-match readout)
        df += " " + os.environ["MS_XCF"]
    os.environ.setdefault("MS_EXTRA_CFLAGS", "-DSD_CARD_HOLE -DMS_CARD_WA_ADDR=0x%08Xu" % WA_ADDR + df)      # engine/glue: patched-code tests use the loader's hole bounds (sd_melee.c, s12_gc.c), not 0x804F0C20
    BD.DEFAULT_BASE = a.hole_base
    BD.DEFAULT_MAX_END = WA_ADDR              # the hole ends where the loader's work area starts (stock layout: 0x817E4000, as before)
    BD.SWISS_MAX_END = 0x81800000
    return PAY.main(["build", "--contract", a.contract, "--out", a.out, "--base", "0x%X" % (a.hole_base + 0xA9C0), "--force"])


if __name__ == "__main__":
    sys.exit(main() or 0)
