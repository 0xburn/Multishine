#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and packs the Game Night match-time block ms/gns.ovl (ABI in gns_ovl.h).

  gns_pack.py build OUT.ovl [--stats ] [--workdir DIR] [--profile P]   compile gnrec.c, gnw.c, mw2.c and gns_ovl.c with the flags of the resident profile, link flat, write the
                                                                               overlay file (sg_ovl.h layout, state_hint = GNS_KIND)

Same rules as the hub overlay (menus/hub/hub_ovl_pack.py, whose helpers this reuses): every absolute address of the image is in the relocation table, an import is a link
error, lr_check runs on every object, no FPU instruction.  The block is loaded by gns_res.c into AllM, which sizes the block from the header itself (image + max(bss, relocs)),
so unlike hub.ovl there is no bss bound.  Needs devkitPPC (/opt/devkitpro/devkitPPC or $PPC_PREFIX)."""
import argparse, hashlib, json, os, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MS = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(MS, "..", ".."))
sys.path.insert(0, os.path.join(MS, "menus", "hub"))
sys.path.insert(0, MS)
import hub_ovl_pack as H  # noqa: E402  (helpers: run, symbols, image_bytes, relocs, up, option_flags, constants)
import build as MSB       # noqa: E402

P = H.P
GNS_BASE = H.PACK_BASE
ALIGN = H.ALIGN
SGO_MAGIC = H.SGO_MAGIC
SGO_VER = H.SGO_VER


def gns_kind():
    return int(re.search(r"#define\s+GNS_KIND\s+(\d+)u?", open(os.path.join(HERE, "gns_ovl.h")).read()).group(1))


def compile_all(work, profile):
    m = MSB.manifest()
    prof = m["profiles"][profile]
    stage, _srcs = MSB.stage_tree(prof, work)
    flags = MSB.subst(list(prof["flags"]) + H.option_flags(prof), work, stage) + m["lr_cflags"] + os.environ.get("MS_EXTRA_CFLAGS", "").split()
    flags += m["common_cflags"] + ["-fno-jump-tables", "-fno-schedule-insns2"]
    units = {u["obj"]: u for u in prof["units"]}
    ccdir = os.path.join(stage, "offline", "client_net")
    gdir = os.path.join(stage, "offline", "multishine", "menus", "gns")
    os.makedirs(gdir, exist_ok=True)
    srcs = []
    for src, dst in (("offline/client_net/gnrec.c", ccdir), ("offline/client_net/gnrec.h", ccdir), ("offline/client_net/gnw.c", ccdir), ("offline/client_net/gnw.h", ccdir), ("offline/client_net/gnzk.c", ccdir), ("offline/client_net/gnzk.h", ccdir),
                     ("offline/multishine/menus/gns/gns_ovl.c", gdir), ("offline/multishine/menus/gns/gns_ovl.h", gdir),
                     ("offline/multishine/menus/sign/frd_tab_art.h", gdir), ("offline/multishine/menus/sign/toast_pill.h", gdir), ("offline/multishine/menus/sign/keyhint.h", gdir)):
        d = os.path.join(dst, os.path.basename(src))
        shutil.copyfile(os.path.join(REPO, src), d)
        srcs.append(d)
    inc = ["-I" + ccdir, "-I" + gdir]
    objs = []
    jobs = [("gnrec", os.path.join(ccdir, "gnrec.c"), ["-std=gnu99"]),
            ("gnw", os.path.join(ccdir, "gnw.c"), ["-std=gnu99"]),
            ("gnzk", os.path.join(ccdir, "gnzk.c"), ["-std=gnu99"]),
            ("gns", os.path.join(gdir, "gns_ovl.c"), ["-std=gnu99"]),
            ("mw2", os.path.join(stage, units["mw2"]["src"]), units["mw2"]["args"])]
    for name, src, args in jobs:
        o = os.path.join(work, "gns_" + name + ".o")
        H.run([P + "-gcc"] + flags + inc + args + ["-c", src, "-o", o], cwd=stage)
        objs.append(o)
    return objs


def link(work, objs, base):
    elf = os.path.join(work, "gnsovl_%08X.elf" % base)
    H.run([P + "-gcc", "-nostartfiles", "-nostdlib", "-Wl,-q", "-Wl,--gc-sections", "-Wl,--defsym,GNS_BASE=0x%X" % base, "-Wl,-T," + os.path.join(HERE, "gns_ovl.ld"),
           "-Wl,-Map," + elf[:-4] + ".map", "-Wl,--undefined=gns_api"] + objs + ["-lgcc", "-o", elf])
    return elf


def build(a):
    work = os.path.abspath(a.workdir or tempfile.mkdtemp(prefix="gnsovl-"))
    os.makedirs(work, exist_ok=True)
    objs = compile_all(work, a.profile)
    elf = link(work, objs, GNS_BASE)
    if H.run([P + "-nm", "-u", elf]).strip():
        sys.exit("unresolved symbols in the block:\n" + H.run([P + "-nm", "-u", elf]))
    sy = H.symbols(elf)
    size, bss_end = sy["__gns_img_end"] - GNS_BASE, sy["__gns_bss_end"] - GNS_BASE
    api_off = sy["gns_api"] - GNS_BASE
    api_sz = H.symbols(elf, True)["gns_api"][1]
    if api_off + api_sz > size:
        sys.exit("gns_api does not lie inside the image")
    img = H.image_bytes(elf, size)
    rel = H.relocs(elf, GNS_BASE, size)
    rb = b"".join(struct.pack(">II", (o << 2) | t, tg) for o, t, tg in rel)
    rb += b"\0" * (H.up(len(rb)) - len(rb))
    hdr = struct.pack(">8I", SGO_MAGIC, SGO_VER, size, bss_end - size, len(rel), api_off, GNS_BASE, gns_kind()).ljust(ALIGN, b"\0")
    lr = os.path.join(REPO, "offline/lr_check.py")
    H.run([sys.executable, lr, "check"] + objs)
    H.run([sys.executable, lr, "libgcc", elf[:-4] + ".map"])
    dis = H.run([P + "-objdump", "-d", elf])
    open(os.path.join(work, "dis.txt"), "w").write(dis)
    fn = None
    for line in dis.splitlines():
        mm = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if mm:
            fn = mm.group(1)
        elif re.search(r"\s(lfs|stfs|lfd|stfd|fmr|fadd|fmul|fctiw|frsp|fsub|fdiv|fcmp)\s", line):
            sys.exit("FPU instruction in the gns block (%s)" % fn)
    blob = hdr + img + rb
    open(a.out, "wb").write(blob)
    area = max(H.up(bss_end - size), len(rb))
    stats = {"file": os.path.basename(a.out), "image_bytes": size, "bss_bytes": bss_end - size, "nreloc": len(rel), "reloc_bytes": len(rb), "file_bytes": len(blob),
             "block_bytes": 32 + size + area + 32, "api_off": api_off, "api_bytes": api_sz, "sgo_ver": SGO_VER, "kind": gns_kind(),
             "sha1": hashlib.sha1(blob).hexdigest(), "profile": a.profile, "opts": os.environ.get("MS_BLOB_OPTS", ""),
             "text_bytes": int(H.run([P + "-size", elf]).splitlines()[1].split()[0])}
    if a.stats:
        json.dump(stats, open(a.stats, "w"), indent=1)
    print(json.dumps(stats))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("out"); b.add_argument("--stats"); b.add_argument("--workdir"); b.add_argument("--profile", default="s17-compat-onbc")
    a = ap.parse_args(argv)
    return build(a)


if __name__ == "__main__":
    sys.exit(main())
