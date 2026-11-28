#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and packs the report overlay ms/rep.ovl (ABI in rep_ovl.h).

  rep_ovl_pack.py build OUT.ovl [--stats ] [--workdir DIR]      compile with devkitPPC, link flat at PACK_BASE, write the overlay file (sg_ovl.h layout, state_hint = REP_OVL_KIND)
  rep_ovl_pack.py raw BASE OUT.bin [--workdir DIR]                       the same objects linked at address BASE

Same rules as the hub, gns and sign overlays (whose helpers this reuses): every absolute address of the image is in the relocation table, an import is a link error (the overlay
reaches the resident image only through struct rep_ovl_env), no FPU instruction, lr_check on every object.  The compile flags are the sign overlay's, with the engine's sdnet layout
(-DSDNET_MAX_SOCKETS=4 -DSDNET_SOCK_SLOTS=8 -DSDNET_SOCK_PAYLOAD=1200 -DSDNET_ENABLE_TCP=1: the block refuses a resident whose struct sdnet differs) and the WebPKI mode of sdtls.
Needs devkitPPC (/opt/devkitpro/devkitPPC or $PPC_PREFIX)."""
import argparse, hashlib, json, os, re, struct, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MS = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(MS, "..", ".."))
sys.path.insert(0, os.path.join(MS, "menus", "hub"))
import hub_ovl_pack as H  # noqa: E402  (helpers: run, symbols, image_bytes, relocs, up, constants)

P = H.P
ALIGN = H.ALIGN
BASE = H.PACK_BASE
NET, TLS = os.path.join(REPO, "offline/net"), os.path.join(REPO, "offline/net/tls")
BRIDGE = os.path.join(REPO, "offline/bridge")      # the 3.7.0 matchmaker client (sbr_ws, sbr_stun, sbr_codec) and the mailbox header live with the bridge
SRC = [os.path.join(HERE, f) for f in ("rep_body.c", "lucky_roots.c", "rep_q.c", "rep_cli.c", "rep_dns.c", "rep_glue.c", "rep_mm.c", "rec_up.c", "rep_ovl.c")] + \
      [os.path.join(BRIDGE, f) for f in ("sbr_ws.c", "sbr_stun.c", "sbr_codec.c")] + [os.path.join(NET, "ms_ws.c")] + \
      [os.path.join(NET, "sddns.c"), os.path.join(NET, "sdtcp.c"), os.path.join(REPO, "offline/client_net/mw2_crypto.c")] + \
      [os.path.join(TLS, f) for f in ("sdent.c", "sdcrypto.c", "sdc25519.c", "sdtls.c", "sdsec.c", "sdhttp.c", "sdpk.c", "sdx509.c", "sdroots.c")]
# LAB/DOLPHIN ONLY: REP_TEST_ROOTS= (tests/mk_test_pki.py) replaces the web roots (sdroots.c) by a test root; the file carries the marker "REP-TEST-ROOTS", the stats say test_roots refuses it
TEST_ROOTS = os.environ.get("REP_TEST_ROOTS", "")
if TEST_ROOTS:
    SRC = [TEST_ROOTS if os.path.basename(f) == "sdroots.c" else f for f in SRC]
FLAGS = ["-Os", "-mcpu=750", "-ffreestanding", "-fno-builtin", "-msdata=none", "-G0", "-fno-pic", "-msoft-float", "-ffixed-lr",
         "-fno-tree-loop-distribute-patterns", "-fno-jump-tables", "-fno-asynchronous-unwind-tables", "-ffunction-sections", "-fdata-sections",
         "-fno-schedule-insns2",      # no post-RA scheduling: same semantics, the MSLZ entry compresses better
         "-DSDNET_CONSOLE", "-DSDNET_ENABLE_TCP=1", "-DSDNET_MAX_SOCKETS=4", "-DSDNET_SOCK_SLOTS=8", "-DSDNET_SOCK_PAYLOAD=1200", "-DSDTLS_WEBPKI", "-DREP_OVL"]
FLAGS += os.environ.get("REP_EXTRA_CFLAGS", "").split()
# budgets: the HSD block while the menu scene is up (main menu 4.29 MB free; the overlay is never loaded on the online CSS), and the card (MSLZ shrinks the entry)
BLOCK_BUDGET = 160 * 1024
FILE_BUDGET = 120 * 1024


def kind():
    return int(re.search(r"#define\s+REP_OVL_KIND\s+(\d+)u?", open(os.path.join(HERE, "rep_ovl.h")).read()).group(1))


def compile_all(work):
    objs = []
    for s in SRC:
        o = os.path.join(work, "rep_" + os.path.basename(s)[:-2] + ".o")
        H.run([P + "-gcc"] + FLAGS + ["-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I" + HERE, "-I" + BRIDGE, "-I" + NET, "-I" + TLS, "-I" + os.path.join(REPO, "offline/client_net"), "-c", s, "-o", o])
        objs.append(o)
    return objs


def link(work, objs, base):
    elf = os.path.join(work, "repovl_%08X.elf" % base)
    H.run([P + "-gcc", "-nostartfiles", "-nostdlib", "-Wl,-q", "-Wl,--gc-sections", "-Wl,--defsym,REP_BASE=0x%X" % base, "-Wl,-T," + os.path.join(HERE, "rep_ovl.ld"),
           "-Wl,-Map," + elf[:-4] + ".map", "-Wl,--undefined=rep_api"] + objs + ["-lgcc", "-o", elf])
    return elf


def build(a):
    work = os.path.abspath(a.workdir or tempfile.mkdtemp(prefix="repovl-"))
    os.makedirs(work, exist_ok=True)
    objs = compile_all(work)
    elf = link(work, objs, BASE)
    und = H.run([P + "-nm", "-u", elf]).strip()
    if und:
        sys.exit("unresolved symbols in the block (an import: the overlay may only call back through struct rep_ovl_env):\n" + und)
    sy = H.symbols(elf)
    size, bss_end = sy["__rep_img_end"] - BASE, sy["__rep_bss_end"] - BASE
    api_off = sy["rep_api"] - BASE
    api_sz = H.symbols(elf, True)["rep_api"][1]
    if api_off + api_sz > size:
        sys.exit("rep_api does not lie inside the image")
    img = H.image_bytes(elf, size)
    rel = H.relocs(elf, BASE, size)
    rb = b"".join(struct.pack(">II", (o << 2) | t, tg) for o, t, tg in rel)
    rb += b"\0" * (H.up(len(rb)) - len(rb))
    hdr = struct.pack(">8I", H.SGO_MAGIC, H.SGO_VER, size, bss_end - size, len(rel), api_off, BASE, kind()).ljust(ALIGN, b"\0")
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
            sys.exit("FPU instruction in the report block (%s)" % fn)
    blob = hdr + img + rb
    open(a.out, "wb").write(blob)
    area = max(H.up(bss_end - size), len(rb))
    stats = {"file": os.path.basename(a.out), "image_bytes": size, "bss_bytes": bss_end - size, "nreloc": len(rel), "reloc_bytes": len(rb), "file_bytes": len(blob),
             "block_bytes": 32 + size + area + 32, "api_off": api_off, "api_bytes": api_sz, "sgo_ver": H.SGO_VER, "kind": kind(),
             "sha1": hashlib.sha1(blob).hexdigest(), "text_bytes": int(H.run([P + "-size", elf]).splitlines()[1].split()[0]), "test_roots": bool(TEST_ROOTS)}
    if ((stats["bss_bytes"] + 31) & ~31) > stats["reloc_bytes"] + 0xC000:           # the resident's own test (rep_res.c rp_load): bss <= relocation table + RP_BSS_BLK (the table's room is reused)
        sys.exit("bss %d exceeds the room rep_res.c allocates (relocation table %d + RP_BSS_BLK 0xC000)" % (stats["bss_bytes"], stats["reloc_bytes"]))
    if stats["block_bytes"] > BLOCK_BUDGET or stats["file_bytes"] > FILE_BUDGET:
        sys.exit("report overlay over budget: %s (block %d > %d or file %d > %d)" % (json.dumps(stats), stats["block_bytes"], BLOCK_BUDGET, stats["file_bytes"], FILE_BUDGET))
    if a.stats:
        json.dump(stats, open(a.stats, "w"), indent=1)
    print(json.dumps(stats))
    return 0


def raw(a):
    """the same objects linked at address BASE: the bytes a correct loader must reproduce ( compares the relocated image with it, word for word)"""
    work = os.path.abspath(a.workdir or tempfile.mkdtemp(prefix="repovl-"))
    os.makedirs(work, exist_ok=True)
    objs = compile_all(work)
    elf0 = link(work, objs, BASE)
    size = H.symbols(elf0)["__rep_img_end"] - BASE
    base = int(a.base, 0)
    elf = link(work, objs, base)
    open(a.out, "wb").write(H.image_bytes(elf, size))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("out"); b.add_argument("--stats"); b.add_argument("--workdir")
    r = sub.add_parser("raw"); r.add_argument("base"); r.add_argument("out"); r.add_argument("--workdir")
    a = ap.parse_args(argv)
    return build(a) if a.cmd == "build" else raw(a)


if __name__ == "__main__":
    sys.exit(main())
