#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Concatenate flat Gecko lists (04/06/C2) for dol_patch.py, probe first.

  python3 offline/codeset/merge_codes.py --out merged.bin probe.bin profile.bin [more.bin ...]

dol_patch.py stops at the first F0/FF terminator and accepts one optional 00D0C0DE header, so this
strips headers and terminators from every input and joins the bodies in the order given
(argument order = placement order: dol_patch.py places C2 bodies sequentially from its base).
"""
import argparse
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bake"))
import dol_patch  # noqa: E402


def body(data):
    ops, info = dol_patch.parse_gecko(data)       # validates the list
    start = 8 if info["header"] else 0
    end = info["terminator_offset"] if info["terminated"] else len(data)
    return data[start:end]


def main_merge(out_path, inputs):
    out = b"".join(body(open(p, "rb").read()) for p in inputs)
    dol_patch.parse_gecko(out)
    open(out_path, "wb").write(out)
    print("merge_codes: %d inputs, %d bytes -> %s" % (len(inputs), len(out), out_path))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("inputs", nargs="+")
    a = ap.parse_args()
    main_merge(a.out, a.inputs)


if __name__ == "__main__":
    main()
