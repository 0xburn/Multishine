#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""dol_diff: what a patched main.dol changes against the retail one (module inventory for the card route).

  dol_diff.py RETAIL.iso PATCHED.iso [--json OUT] [--words OUT.bin]
Reads main.dol out of each GameCube ISO (header word 0x420), parses the DOL section table and classifies every difference:
  * words changed in sections that exist in the retail DOL (text hooks / data patches: applicable at runtime by plain stores + icache flush)
  * new sections (the patched section T2): bytes that need memory and, if absolute, a base address
Read-only on both inputs; stdlib only.
"""
import argparse, json, struct, sys


def dol_from_iso(path):
    with open(path, "rb") as f:
        f.seek(0x420)
        off = struct.unpack(">I", f.read(4))[0]
        f.seek(off)
        hdr = f.read(0x100)
        offs = struct.unpack(">18I", hdr[0:72]); addrs = struct.unpack(">18I", hdr[0x48:0x90]); sizes = struct.unpack(">18I", hdr[0x90:0xD8])
        end = max(o + s for o, s in zip(offs, sizes) if s)
        f.seek(off)
        return f.read(end)


def sections(d):
    offs = struct.unpack(">18I", d[0:72]); addrs = struct.unpack(">18I", d[0x48:0x90]); sizes = struct.unpack(">18I", d[0x90:0xD8])
    out = []
    for i in range(18):
        if sizes[i]:
            out.append({"idx": i, "kind": "text" if i < 7 else "data", "addr": addrs[i], "size": sizes[i], "off": offs[i]})
    return out


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("retail"); ap.add_argument("baked"); ap.add_argument("--json"); ap.add_argument("--words")
    a = ap.parse_args()
    r, b = dol_from_iso(a.retail), dol_from_iso(a.baked)
    rs, bs = sections(r), sections(b)
    rmap = {(s["addr"], s["size"]): s for s in rs}
    res = {"retail_sections": len(rs), "baked_sections": len(bs), "patched": [], "new": [], "words": []}
    for s in bs:
        key = (s["addr"], s["size"])
        if key in rmap:
            o = rmap[key]
            ra, ba = r[o["off"]:o["off"] + o["size"]], b[s["off"]:s["off"] + s["size"]]
            ch = [i for i in range(0, s["size"] - 3, 4) if ra[i:i + 4] != ba[i:i + 4]]
            for i in ch:
                res["words"].append((s["addr"] + i, struct.unpack(">I", ra[i:i + 4])[0], struct.unpack(">I", ba[i:i + 4])[0]))
            if ch:
                res["patched"].append({"idx": s["idx"], "kind": s["kind"], "addr": s["addr"], "size": s["size"], "changed_words": len(ch)})
        else:
            res["new"].append({"idx": s["idx"], "kind": s["kind"], "addr": s["addr"], "size": s["size"], "end": s["addr"] + s["size"]})
    ws = res["words"]
    res["changed_words_total"] = len(ws)
    # hooks into the new section: changed words that are b/bl to an address inside a new section
    def tgt(addr, w):
        op = w >> 26
        if op == 18:
            off = w & 0x03FFFFFC
            if off & 0x02000000: off -= 0x04000000
            return addr + off if not (w & 2) else off & 0xFFFFFFFF
        return None
    inn = lambda t: t is not None and any(n["addr"] <= t < n["end"] for n in res["new"])
    res["branches_into_new"] = sum(1 for (ad, o, n) in ws if inn(tgt(ad, n)))
    res["new_bytes"] = sum(n["size"] for n in res["new"])
    out = {k: v for k, v in res.items() if k != "words"}
    print(json.dumps(out, indent=1, default=hex))
    if a.json:
        json.dump(res, open(a.json, "w"), default=hex)
    if a.words:
        with open(a.words, "wb") as f:
            for ad, o, n in ws: f.write(struct.pack(">III", ad, o, n))


if __name__ == "__main__":
    main()
