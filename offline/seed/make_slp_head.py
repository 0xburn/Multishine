#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""make_slp_head: /ms/slp/head.bin of the earlier image = the 0x40 byte GCI directory entry and block 0 (file comments + icon, OURS) of the stock MultishinePack, exactly as tools/card/build_pack.py
writes them; the system-files builder adds the table, the header and the data. The block count in the entry is the stock pack's (SLP_PACK_BLOCKS, 29); the system-files builder patches the real one.
  make_slp_head.py OUT [--mtime T] [--blocks N]"""
import argparse, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "card"))
import build_pack  # noqa: E402  (puts the provision helpers on the path)
P = build_pack.P


def head(mtime=1760000000.0, blocks=29):
    blk = bytearray(build_pack.BLOCK)
    build_pack.card_text.put(blk, "Pack")
    P.put_icon(blk, build_pack.FILE_NAME)
    return P.make_dir_entry(build_pack.FILE_NAME, 0, blocks, P.PERM_PUBLIC, mtime) + bytes(blk)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--mtime", type=float, default=1760000000.0)
    ap.add_argument("--blocks", type=int, default=29)
    a = ap.parse_args(argv)
    d = head(a.mtime, a.blocks)
    open(a.out, "wb").write(d)
    print("head.bin %d bytes" % len(d))
    return 0


if __name__ == "__main__":
    sys.exit(main())
