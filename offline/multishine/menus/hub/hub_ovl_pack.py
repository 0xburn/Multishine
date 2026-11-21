#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and packs the hub overlay ms/hub.ovl (ABI in hub_ovl.h).

  hub_ovl_pack.py build OUT.ovl [--stats ] [--workdir DIR] [--profile P]   compile on_backend.c and od_glue.c with -DHUB_OVL, mnc.c, mw2.c, ms_sha256.c and hub_ovl.c with
                                                                                    the flags of the resident profile, link flat, write the overlay file (sg_ovl.h layout,
                                                                                    state_hint = HUB_KIND)
  hub_ovl_pack.py raw BASE OUT.bin [--workdir DIR] [--profile P]                  the same objects linked at address BASE: the bytes a correct loader must reproduce

The option flags of the resident build (MS_BLOB_OPTS, e.g. labnat=on, which changes the mnc layout) and MS_EXTRA_CFLAGS are applied exactly as offline/multishine/build.py
applies them, except the overlay options themselves (hubovl, menuovl): the overlay and the resident must agree on every struct (the overlay also checks the sizes at enter).
Same rules as the menu and sign-in overlays (menus/ml/ml_ovl_pack.py, menus/sign/sg_ovl_pack.py): every absolute address of the image is in the relocation table, an import
(a branch or address to a symbol outside the image) is a link error, lr_check runs on every object.  Needs devkitPPC (/opt/devkitpro/devkitPPC or $PPC_PREFIX).
"""
import argparse, hashlib, json, os, re, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MS = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(MS, "..", ".."))
sys.path.insert(0, MS)
import build as MSB  # noqa: E402  (offline/multishine/build.py: manifest, stage_tree, subst())

P = os.environ.get("PPC_PREFIX", "/opt/devkitpro/devkitPPC/bin/powerpc-eabi")
UNITS = ["on_backend", "od_glue", "mnc", "mw2", "ms_sha256"]      # manifest units of the profile whose sources go into the overlay (the first two with -DHUB_OVL)
GLUE_SRC = "offline/s17/net/hub_ovl.c"                             # staged path of hub_ovl.c (MANIFEST stage map)
SOC_SRCS = [("soc", "offline/multishine/menus/hub/hub_soc.c", "-std=gnu99"), ("socc", "offline/client_net/socc.c", "-std=gnu99"),
            ("toast", "offline/multishine/menus/sign/toast.c", "-std=gnu99"), ("sis", "offline/multishine/menus/hub/hub_sis.S", "-std=gnu99")]
FPU_OK = ("hs_sis_add",)                                           # hub_sis.S: the two lfs of the SisLib call (the only FPU instructions allowed in the overlay)
# the update check on the title: the UPD_CHECK client and the pure step, compiled from the repository (not staged: the resident never
# links them); the staged hub_ovl.h is found through -I <stage>/offline/s17/net
EXTRA_SRCS = ["offline/client_net/mnu.c", "offline/multishine/menus/upd/ut_check.c"]
ALIGN = 32
PACK_BASE = 0x00100000
BSS_EXTRA = 256                                                    # hub_res.c HUB_BSS_EXTRA: the block holds the file + this many bytes
R_ADDR32, R_HA, R_LO = 0, 1, 2
TYPES = {"R_PPC_ADDR32": R_ADDR32, "R_PPC_ADDR16_HA": R_HA, "R_PPC_ADDR16_LO": R_LO}
SGO_MAGIC = 0x53474F56
SKIP_OPTS = ("hubovl", "menuovl")


def header_const(path, name):
    return int(re.search(r"#define\s+%s\s+(\d+)u?" % name, open(path).read()).group(1))


SGO_VER = header_const(os.path.join(MS, "menus/sign/sg_ovl.h"), "SGO_VER")
HUB_KIND = header_const(os.path.join(HERE, "hub_ovl.h"), "HUB_KIND")


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit("command failed: %s\n%s%s" % (" ".join(cmd[:3]), r.stdout, r.stderr))
    return r.stdout


def option_flags(prof):
    env = dict(kv.split("=", 1) for kv in os.environ.get("MS_BLOB_OPTS", "").split(",") if "=" in kv)
    out = []
    for k, o in prof.get("options", {}).items():
        if k in SKIP_OPTS:
            continue
        out += o["when"].get(env.get(k, o["default"]), [])
    return out


def compile_all(work, profile):
    m = MSB.manifest()
    prof = m["profiles"][profile]
    stage, _srcs = MSB.stage_tree(prof, work)
    flags = MSB.subst(list(prof["flags"]) + option_flags(prof), work, stage) + m["lr_cflags"] + os.environ.get("MS_EXTRA_CFLAGS", "").split()
    flags += m["common_cflags"] + ["-fno-jump-tables", "-DHUB_OVL", "-fno-schedule-insns2", "-DHUB_SOC", "-DMNC_POLL_INVITE", "-DMNC_WATCH_TELL", "-DSOCC_TICK_ONLY", "-DUPD_TITLE"]     # no post-RA scheduling: the code compresses better (stream 16,950 -> 16,250 B = 2 card blocks)
    if not os.environ.get("MS_NO_RKSET"):
        flags += ["-DMNC_RKSET"]     # Grandmaster sets: the ranked ticket announces set support; MS_NO_RKSET=1 builds the console without it (the Bo1 regression card)
    objs = []
    units = {u["obj"]: u for u in prof["units"]}
    for n in UNITS:
        u = units[n]
        o = os.path.join(work, "hub_" + n + ".o")
        run([P + "-gcc"] + flags + u["args"] + ["-c", os.path.join(stage, u["src"]), "-o", o], cwd=stage)
        objs.append(o)
    o = os.path.join(work, "hub_glue.o")
    run([P + "-gcc"] + flags + ["-std=gnu99", "-c", os.path.join(stage, GLUE_SRC), "-o", o], cwd=stage)
    objs.append(o)
    # menu toasts, the Mano a mano answer, the post-fight prompt (hub_soc.c): overlay-only sources, compiled from the tree
    net = os.path.join(stage, os.path.dirname(GLUE_SRC))           # next to the staged hub_ovl.c: the staged headers (hub_ovl.h, od_glue.h, sdshim.h) resolve as for it
    inc = ["-I" + os.path.join(stage, "offline/client_net"), "-I" + os.path.join(REPO, "offline/client_net"), "-I" + os.path.join(MS, "menus/sign"), "-I" + os.path.join(MS, "menus/hub")]
    for name, src, std in SOC_SRCS:
        dst = os.path.join(net, os.path.basename(src))
        shutil.copyfile(os.path.join(REPO, src), dst)
        o = os.path.join(work, "hub_" + name + ".o")
        run([P + "-gcc"] + flags + inc + [std, "-c", dst, "-o", o], cwd=stage)
        objs.append(o)
    for src in EXTRA_SRCS:
        o = os.path.join(work, "hub_x_" + os.path.basename(src)[:-2] + ".o")
        run([P + "-gcc"] + flags + ["-std=gnu99", "-I" + os.path.join(stage, "offline/s17/net"),
             "-c", os.path.join(REPO, src), "-o", o], cwd=stage)
        objs.append(o)
    return objs


def link(work, objs, base):
    elf = os.path.join(work, "hubovl_%08X.elf" % base)
    run([P + "-gcc", "-nostartfiles", "-nostdlib", "-Wl,-q", "-Wl,--gc-sections", "-Wl,--defsym,HUB_BASE=0x%X" % base, "-Wl,-T," + os.path.join(HERE, "hub_ovl.ld"),
         "-Wl,-Map," + elf[:-4] + ".map", "-Wl,--undefined=hub_api"] + objs + ["-lgcc", "-o", elf])
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
    """(offset, type, target) relative to the image start, of every absolute address in the image (the linker's --emit-relocs sections); exits on any import"""
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
            continue
        add = 0
        if len(p) >= 7 and p[-2] in "+-":
            add = int(p[-1], 16) * (1 if p[-2] == "+" else -1)
        elif len(p) >= 6 and p[-1].startswith(("+", "-")):
            add = int(p[-1], 16)
        tgt = (symval + add) & 0xFFFFFFFF
        if typ in ("R_PPC_REL24", "R_PPC_REL14", "R_PPC_REL32"):
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
    work = os.path.abspath(a.workdir or tempfile.mkdtemp(prefix="hubovl-"))
    os.makedirs(work, exist_ok=True)
    objs = compile_all(work, a.profile)
    elf = link(work, objs, PACK_BASE)
    if run([P + "-nm", "-u", elf]).strip():
        sys.exit("unresolved symbols in the overlay:\n" + run([P + "-nm", "-u", elf]))
    sy = symbols(elf)
    size, bss_end = sy["__hub_img_end"] - PACK_BASE, sy["__hub_bss_end"] - PACK_BASE
    api_off = sy["hub_api"] - PACK_BASE
    api_sz = symbols(elf, True)["hub_api"][1]
    if api_off + api_sz > size:
        sys.exit("hub_api (%d bytes at %d) does not lie inside the image (%d bytes)" % (api_sz, api_off, size))
    img = image_bytes(elf, size)
    rel = relocs(elf, PACK_BASE, size)
    rb = b"".join(struct.pack(">II", (o << 2) | t, tg) for o, t, tg in rel)
    rb += b"\0" * (up(len(rb)) - len(rb))
    if up(bss_end - size) > len(rb) + BSS_EXTRA:
        sys.exit("bss %d B exceeds the relocation table %d B + %d: hub_res.c's block would be too small" % (bss_end - size, len(rb), BSS_EXTRA))
    hdr = struct.pack(">8I", SGO_MAGIC, SGO_VER, size, bss_end - size, len(rel), api_off, PACK_BASE, HUB_KIND).ljust(ALIGN, b"\0")
    lr = os.path.join(REPO, "offline/lr_check.py")
    run([sys.executable, lr, "check"] + objs)
    run([sys.executable, lr, "libgcc", elf[:-4] + ".map"])
    d = os.path.join(work, "dis.txt")
    dis = run([P + "-objdump", "-d", elf])
    open(d, "w").write(dis)
    fn = None
    for line in dis.splitlines():                                  # per function: only FPU_OK may hold FPU instructions
        mm = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if mm:
            fn = mm.group(1)
        elif re.search(r"\s(lfs|stfs|lfd|stfd|fmr|fadd|fmul|fctiw|frsp|fsub|fdiv|fcmp)\s", line) and fn not in FPU_OK:
            sys.exit("FPU instruction in the hub overlay (%s)" % fn)
    blob = hdr + img + rb
    open(a.out, "wb").write(blob)
    stats = {"file": os.path.basename(a.out), "image_bytes": size, "bss_bytes": bss_end - size, "nreloc": len(rel), "reloc_bytes": len(rb), "file_bytes": len(blob),
             "block_bytes": 32 + len(blob) + BSS_EXTRA, "api_off": api_off, "api_bytes": api_sz, "sgo_ver": SGO_VER, "kind": HUB_KIND,
             "sha1": hashlib.sha1(blob).hexdigest(), "sha256": hashlib.sha256(blob).hexdigest(), "profile": a.profile,
             "opts": os.environ.get("MS_BLOB_OPTS", ""), "extra_cflags": os.environ.get("MS_EXTRA_CFLAGS", ""),
             "hs_proof_off": (sy["hs_proof"] - PACK_BASE) if "hs_proof" in sy else None}       # hub_soc.c RAM proof words: image address (hub_st[HB_ADDR]) + this
    if a.stats:
        json.dump(stats, open(a.stats, "w"), indent=1)
    print(json.dumps(stats))
    return 0


def raw(a):
    work = os.path.abspath(a.workdir or tempfile.mkdtemp(prefix="hubovl-"))
    os.makedirs(work, exist_ok=True)
    objs = compile_all(work, a.profile)
    base = int(a.base, 0)
    elf0 = link(work, objs, PACK_BASE)
    size = symbols(elf0)["__hub_img_end"] - PACK_BASE
    elf = link(work, objs, base)
    open(a.out, "wb").write(image_bytes(elf, size))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("out"); b.add_argument("--stats"); b.add_argument("--workdir"); b.add_argument("--profile", default="s17-compat-onbc")
    r = sub.add_parser("raw"); r.add_argument("base"); r.add_argument("out"); r.add_argument("--workdir"); r.add_argument("--profile", default="s17-compat-onbc")
    a = ap.parse_args(argv)
    return build(a) if a.cmd == "build" else raw(a)


if __name__ == "__main__":
    sys.exit(main())
