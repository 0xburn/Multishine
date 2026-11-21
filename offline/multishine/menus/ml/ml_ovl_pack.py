#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and packs the menu overlay ( step 3, "MENU OVERLAY").

  ml_ovl_pack.py build OUT.ovl [--stats ] [--workdir DIR] [--profile P]   compile ml_glue.c (-DML_OVL), ml_core.c, sg_ovl.c, sg_hook.c and ml_ovl_asm.S with the flags of the
                                                                                    resident profile, link flat, write the overlay file (sg_ovl.h layout, state_hint = MLO_KIND)
  ml_ovl_pack.py raw BASE OUT.bin [--workdir DIR]                                  the same objects linked at address BASE: the bytes a correct loader must reproduce
                                                                                    ( compares the relocated image with it, word for word)

Same file format, relocation scheme and rules as sg_ovl_pack.py (the sign-in overlay): code, rodata and data linked at PACK_BASE and stored as offsets; every absolute address in
it (R_PPC_ADDR32 words, R_PPC_ADDR16_HA / _LO halves) is in the relocation table; PC-relative calls inside the image need nothing; an import (a branch or address to a symbol
outside the image) is a link error - the overlay reaches the resident image only through struct mlo_env (ml_ovl.h).  The sources are staged exactly as the resident build stages
them (build.py stage_tree of the profile), so includes and flags cannot drift from the blob.  Needs devkitPPC (/opt/devkitpro/devkitPPC or $PPC_PREFIX).
"""
import argparse, json, os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MS = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(MS, "..", ".."))
sys.path.insert(0, MS)
import build as MSB  # noqa: E402  (offline/multishine/build.py: manifest, stage_tree, subst())

P = os.environ.get("PPC_PREFIX", "/opt/devkitpro/devkitPPC/bin/powerpc-eabi")
UNITS = ["ml_core", "ml_glue", "ml_set", "sg_ovl", "sg_hook"]        # manifest unit names of the profile whose sources go into the overlay
ALIGN = 32
PACK_BASE = 0x00100000      # not 0: relocations of garbage-collected sections carry offsets near 0 and would pass the range filter
R_ADDR32, R_HA, R_LO = 0, 1, 2
TYPES = {"R_PPC_ADDR32": R_ADDR32, "R_PPC_ADDR16_HA": R_HA, "R_PPC_ADDR16_LO": R_LO}
SGO_MAGIC = 0x53474F56


def header_const(path, name):
    return int(re.search(r"#define\s+%s\s+(\d+)u?" % name, open(path).read()).group(1))


SGO_VER = header_const(os.path.join(MS, "menus/sign/sg_ovl.h"), "SGO_VER")
MLO_KIND = header_const(os.path.join(HERE, "ml_ovl.h"), "MLO_KIND")


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit("command failed: %s\n%s%s" % (" ".join(cmd[:3]), r.stdout, r.stderr))
    return r.stdout


def build_flags(m, prof, work, stage):
    """(flags, unit_extra) exactly as offline/multishine/build.py blob derives them for the resident build: the profile flags, the option flags of MS_BLOB_OPTS (the overlay options
    themselves excepted: the overlay is compiled with -DML_OVL, not -DML_OVL_RES), the per-unit flags of the options (units_when), lr_cflags and MS_EXTRA_CFLAGS (the card route:
    -DSD_CARD_HOLE -DMS_CARD_WA_ADDR=..., which turns on the Update / identity / Game Night services of the menu code).  The overlay and the resident must agree on every option."""
    env = dict(kv.split("=", 1) for kv in os.environ.get("MS_BLOB_OPTS", "").split(",") if "=" in kv)
    flags, unit_extra = list(prof["flags"]), {}
    for k, o in prof.get("options", {}).items():
        v = env.get(k, o["default"])
        if k != "menuovl":
            flags += o["when"].get(v, [])
        for un, uf in o.get("units_when", {}).get(v, {}).items():
            unit_extra.setdefault(un, []).extend(uf)
    flags = MSB.subst(flags, work, stage) + m["lr_cflags"] + os.environ.get("MS_EXTRA_CFLAGS", "").split()
    return flags, unit_extra


def compile_all(work, profile):
    m = MSB.manifest()
    prof = m["profiles"][profile]
    stage, _srcs = MSB.stage_tree(prof, work)
    flags, unit_extra = build_flags(m, prof, work, stage)
    flags += m["common_cflags"] + ["-fno-jump-tables", "-fno-schedule-insns2", "-DML_OVL"]     # no post-RA scheduling: same semantics, the MSLZ entry compresses better
    objs, cobjs = [], []
    units = {u["obj"]: u for u in prof["units"]}
    for n in UNITS:
        u = units[n]
        o = os.path.join(work, n + ".o")
        run([P + "-gcc"] + flags + u["args"] + unit_extra.get(n, []) + ["-c", os.path.join(stage, u["src"]), "-o", o], cwd=stage)
        objs.append(o)
        cobjs.append(o)
    ao = os.path.join(work, "ml_ovl_asm.o")
    run([P + "-gcc", "-mcpu=750", "-msoft-float", "-c", os.path.join(HERE, "ml_ovl_asm.S"), "-o", ao])
    objs.append(ao)
    return objs, cobjs, [ao]


def link(work, objs, base):
    elf = os.path.join(work, "mlovl_%08X.elf" % base)
    run([P + "-gcc", "-nostartfiles", "-nostdlib", "-Wl,-q", "-Wl,--gc-sections", "-Wl,--defsym,MLO_BASE=0x%X" % base, "-Wl,-T," + os.path.join(HERE, "ml_ovl.ld"),
         "-Wl,-Map," + elf[:-4] + ".map", "-Wl,--undefined=mlo_api"] + objs + ["-lgcc", "-o", elf])
    return elf


def symbols(elf, sized=False):
    out = {}
    for line in run([P + "-nm"] + (["-S"] if sized else []) + [elf]).splitlines():
        p = line.split()
        if sized and len(p) == 4:
            out[p[3]] = (int(p[0], 16), int(p[1], 16))
        elif not sized and len(p) == 3:
            out[p[2]] = int(p[0], 16)
    return out


def image_bytes(elf, size):
    binf = elf[:-4] + ".bin"
    run([P + "-objcopy", "-O", "binary", "-j", ".image", elf, binf])
    data = open(binf, "rb").read()
    if len(data) != size:
        sys.exit("image size %d != %d" % (len(data), size))
    return data


def relocs(elf, base, size):
    """(offset, type, target), offsets and targets relative to the image start, of every absolute address in the image, from the linker's --emit-relocs sections"""
    out, ext = [], []
    cur = None
    for line in run([P + "-readelf", "-rW", elf]).splitlines():
        if line.startswith("Relocation section"):
            mm = re.search(r"'\.rela(\.\S+)'", line)
            cur = mm.group(1) if mm else None
            continue
        p = line.split()
        if len(p) < 5 or not re.match(r"^[0-9a-f]{8}$", p[0]) or cur is None:
            continue
        off, typ, symval = int(p[0], 16), p[2], int(p[3], 16)
        if not (base <= off < base + size):
            continue                              # a relocation in a section not in the image (debug)
        add = 0
        if len(p) >= 7 and p[-2] in "+-":
            add = int(p[-1], 16) * (1 if p[-2] == "+" else -1)
        elif len(p) >= 6 and p[-1].startswith(("+", "-")):
            add = int(p[-1], 16)
        tgt = (symval + add) & 0xFFFFFFFF
        if typ == "R_PPC_REL24":
            if not (base <= tgt < base + size):
                ext.append((off, typ, p[4] if len(p) > 4 else "?"))
            continue
        if typ in TYPES:
            if not (base <= tgt < base + size + 0x200000):
                ext.append((off, typ, "target %08X outside the image" % tgt))
            out.append((off - base, TYPES[typ], tgt - base))
        else:
            ext.append((off, typ, "unsupported type"))
    if ext:
        sys.exit("overlay has references outside the image or unsupported relocations:\n" + "\n".join("  %08X %s %s" % e for e in ext[:20]))
    return sorted(set(out))


def up(n):
    return (n + ALIGN - 1) // ALIGN * ALIGN


def build(a):
    work = os.path.abspath(a.workdir or tempfile.mkdtemp(prefix="mlovl-"))
    os.makedirs(work, exist_ok=True)
    objs, cobjs, aobjs = compile_all(work, a.profile)
    elf = link(work, objs, PACK_BASE)
    sy = symbols(elf)
    size, bss_end = sy["__mlo_img_end"] - PACK_BASE, sy["__mlo_bss_end"] - PACK_BASE
    api_off = sy["mlo_api"] - PACK_BASE
    api_sz = symbols(elf, True)["mlo_api"][1]
    if api_off + api_sz > size:
        sys.exit("mlo_api (%d bytes at %d) does not lie inside the image (%d bytes)" % (api_sz, api_off, size))
    img = image_bytes(elf, size)
    rel = relocs(elf, PACK_BASE, size)
    rb = b"".join(struct.pack(">II", (o << 2) | t, tg) for o, t, tg in rel)
    rb += b"\0" * (up(len(rb)) - len(rb))
    hdr = struct.pack(">8I", SGO_MAGIC, SGO_VER, size, bss_end - size, len(rel), api_off, PACK_BASE, MLO_KIND).ljust(ALIGN, b"\0")
    # the same safety checks the resident build runs on its objects (offline/)
    lr = os.path.join(REPO, "offline/lr_check.py")
    run([sys.executable, lr, "check"] + [o for o in cobjs])
    run([sys.executable, lr, "verify"] + aobjs)
    run([sys.executable, lr, "libgcc", elf[:-4] + ".map"])
    open(a.out, "wb").write(hdr + img + rb)
    stats = {"image_bytes": size, "bss_bytes": bss_end - size, "nreloc": len(rel), "reloc_bytes": len(rb), "file_bytes": ALIGN + size + len(rb),
             "block_bytes_min": size + max(up(bss_end - size), len(rb)), "api_off": api_off, "api_bytes": api_sz, "sgo_ver": SGO_VER, "kind": MLO_KIND,
             "by_type": {"addr32": sum(1 for r in rel if r[1] == 0), "ha": sum(1 for r in rel if r[1] == 1), "lo": sum(1 for r in rel if r[1] == 2)}}
    if a.stats:
        json.dump(stats, open(a.stats, "w"), indent=1)
    print(json.dumps(stats))
    return 0


def raw(a):
    work = os.path.abspath(a.workdir or tempfile.mkdtemp(prefix="mlovl-"))
    os.makedirs(work, exist_ok=True)
    objs, _c, _a = compile_all(work, a.profile)
    base = int(a.base, 0)
    elf0 = link(work, objs, PACK_BASE)
    size = symbols(elf0)["__mlo_img_end"] - PACK_BASE
    elf = link(work, objs, base)
    open(a.out, "wb").write(image_bytes(elf, size))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("out"); b.add_argument("--stats"); b.add_argument("--workdir"); b.add_argument("--profile", default="s17-compat-onb")
    r = sub.add_parser("raw"); r.add_argument("base"); r.add_argument("out"); r.add_argument("--workdir"); r.add_argument("--profile", default="s17-compat-onb")
    a = ap.parse_args(argv)
    return build(a) if a.cmd == "build" else raw(a)


if __name__ == "__main__":
    sys.exit(main())
