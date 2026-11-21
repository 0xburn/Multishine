#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Builds and packs the sign-in overlay.

  sg_ovl_pack.py build OUT.ovl [--stats ] [--workdir DIR]     compile with devkitPPC, link flat at base 0, write the overlay file (sg_ovl.h layout)
  sg_ovl_pack.py raw BASE OUT.bin [--workdir DIR]                    the same objects linked at address BASE: the bytes a correct loader must reproduce
                                                                      (tests/run_ovl_test.sh compares the relocated image with it, word for word)

The image holds code, rodata and data linked at PACK_BASE and stored as offsets; every absolute address in it (R_PPC_ADDR32 words, R_PPC_ADDR16_HA / _LO halves
of lis/addi pairs) is listed in the relocation table.  PC-relative calls inside the image need nothing.  An import (a branch or address to a symbol outside the
image) is an error: the overlay talks to the resident image only through struct sgo_env.  Needs devkitPPC (/opt/devkitpro/devkitPPC or $PPC_PREFIX).
"""
import argparse, json, os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
P = os.environ.get("PPC_PREFIX", "/opt/devkitpro/devkitPPC/bin/powerpc-eabi")
NET, TLS = os.path.join(REPO, "offline/net"), os.path.join(REPO, "offline/net/tls")
SRC = [os.path.join(HERE, "sg_entry.c"), os.path.join(HERE, "sg_ui.c"), os.path.join(HERE, "sg_link.c"), os.path.join(HERE, "sg_delid.c"), os.path.join(HERE, "sg_mockup.c"), os.path.join(HERE, "sg_mocklist.c"),
       # the Game Night browser: the model, the Event Match screen glue (console only, -DGNB_MELEE) and its SisLib shim
       os.path.join(HERE, "gnb.c"), os.path.join(HERE, "stb.c"), os.path.join(HERE, "gnb_ev.c"), os.path.join(HERE, "gnb_art.c"), os.path.join(HERE, "keyhint_ev.c"), os.path.join(HERE, "..", "ml", "ml_core.c"), os.path.join(HERE, "gnb_sis.S"), os.path.join(REPO, "offline/client_net/gnc.c"), os.path.join(REPO, "offline/client_net/gnchat.c"), os.path.join(REPO, "offline/client_net/lnk.c"),
       # the Friends screen: the model, its network (socc), the Event Match screen glue (console only)
       os.path.join(HERE, "gnt.c"), os.path.join(HERE, "gnt_tree.c"), os.path.join(HERE, "gnt_ev.c"), os.path.join(HERE, "frd.c"), os.path.join(HERE, "frd_net.c"), os.path.join(HERE, "frd_ev.c"), os.path.join(HERE, "evr.c"), os.path.join(HERE, "evo.c"), os.path.join(REPO, "offline/client_net/socc.c"), os.path.join(REPO, "offline/qr/qr.c"), os.path.join(TLS, "sdent.c"),
       os.path.join(TLS, "sdcrypto.c"), os.path.join(TLS, "sdc25519.c"), os.path.join(TLS, "sdtls.c"), os.path.join(TLS, "sdsec.c"),
       os.path.join(TLS, "sdhttp.c"), os.path.join(TLS, "hil/sdprobe.c"), os.path.join(NET, "sdtcp.c"), os.path.join(REPO, "offline/client_net/mw2_crypto.c"),
       # the Update screen: the state machine, the UPD_CHECK / UPD_FETCH client, the wire v2 session code, the installer; the resident image holds its own
       # copies of mw2 / mw2_crypto / ms_sha256 (hub client), but the overlay may only call back through struct sgo_env, so it carries its own for the time it is loaded
       os.path.join(HERE, "sgu_core.c"), os.path.join(HERE, "sgu_net.c"), os.path.join(HERE, "sgp_core.c"), os.path.join(REPO, "offline/seed/seed_slp.c"), os.path.join(REPO, "offline/seed/seed_lean.c"), os.path.join(REPO, "offline/seed/seed_plan.c"), os.path.join(REPO, "offline/payload_pack/ms_lz.c"), os.path.join(REPO, "offline/client_net/mnu.c"), os.path.join(REPO, "offline/client_net/mw2.c"),
       os.path.join(REPO, "offline/cardloader/ota/ota_core.c"), os.path.join(REPO, "offline/payload_pack/ms_sha256.c"),
       os.path.join(HERE, "sgx.c"), os.path.join(HERE, "sgr.c"), os.path.join(HERE, "sgc.c"), os.path.join(REPO, "offline/client_net/cred.c")]       # Send Slippi Login
# Card route: SG_UPDHOST=1 links the resident host of the Update row (upd_host.c, upd_card.c) into the overlay and pins the public key(s) given in
# SG_UPD_PUBKEY (comma separated hex, the very keys of the card build's loader); SG_UPD_DEV=1 additionally lets the updater accept unsigned files (only ever honoured when the running
# loader is a dev loader).
if "-DSG_TN_DEMO" in os.environ.get("SG_EXTRA_CFLAGS", ""):      # the Dolphin proof overlay of the tournament tree (demo cups answer the Tournament screens; never a release build)
    SRC += [os.path.join(HERE, "gnt_demo.c")]
UPDHOST = os.environ.get("SG_UPDHOST", "0") == "1"
if UPDHOST:
    SRC += [os.path.join(HERE, "sgp_disc.c"),
            os.path.join(HERE, "..", "upd", "upd_host.c"), os.path.join(HERE, "..", "upd", "upd_card.c"), os.path.join(HERE, "..", "upd", "upd_bak.c")]
SGO_VER = int(re.search(r"#define\s+SGO_VER\s+(\d+)u", open(os.path.join(HERE, "sg_ovl.h")).read()).group(1))     # one definition: sg_ovl.h
# Lab/dev Online rows Server / Slippi / In Use: in exactly the builds whose engine has netcfg=on (MS_BLOB_OPTS) or when SG_NETSEL=1; a release overlay has none of the code or text
_blob_opts = dict(kv.split("=", 1) for kv in os.environ.get("MS_BLOB_OPTS", "").split(",") if "=" in kv)
NETSEL = os.environ.get("SG_NETSEL", "1" if _blob_opts.get("netcfg") == "on" else "0") == "1"
LAB = os.environ.get("SG_LAB", "1") != "0"      # release overlays: SG_LAB=0 (no TCP / TLS test, no stack)
FLAGS = ["-Os", "-mcpu=750", "-ffreestanding", "-fno-builtin", "-msdata=none", "-G0", "-fno-pic", "-msoft-float", "-ffixed-lr",
         "-fno-tree-loop-distribute-patterns", "-fno-jump-tables", "-fno-asynchronous-unwind-tables", "-ffunction-sections", "-fdata-sections",
         "-fno-schedule-insns2",      # no post-RA scheduling: same semantics, the MSLZ entry of the container compresses 3-4 % better
         "-DSG_MOCKUPS=%d" % (1 if LAB else 0),     # the Mockups hub (a DEV Main Menu row) only in lab overlays (sg_mockup.h)
         "-DSG_LAB=%d" % (1 if LAB else 0), "-DSG_NETSEL=%d" % (1 if NETSEL else 0), "-DGNB_MELEE", "-DSDNET_CONSOLE", "-DSDNET_ENABLE_TCP=1", "-DSDNET_MAX_SOCKETS=1", "-DSDNET_SOCK_SLOTS=2", "-DSDNET_SOCK_PAYLOAD=512"]
if UPDHOST:
    _keys = [k.strip() for k in os.environ.get("SG_UPD_PUBKEY", "").split(",") if k.strip()]
    if not _keys or any(len(k) != 64 or re.search(r"[^0-9a-fA-F]", k) for k in _keys):
        sys.exit("SG_UPDHOST=1 needs SG_UPD_PUBKEY=<64 hex>[,<64 hex>]")
    FLAGS += ["-DSG_UPDHOST", "-DUPD_BAK", "-DUPC_ART", "-DSG_UPD_DEV=%d" % (1 if os.environ.get("SG_UPD_DEV", "0") == "1" else 0),
              "-DSG_UPD_KEYS=" + ",".join("{" + ",".join("0x" + k[i:i + 2] for i in range(0, 64, 2)) + "}" for k in _keys)]
FLAGS += os.environ.get("SG_EXTRA_CFLAGS", "").split()      # e.g. -DSG_TITLE='"UPDATE v2"' (a changed own-files build for the OTA proof)
# Lab TCP + TLS console probe: SG_TLSPROBE=1 (with SG_LAB=1, for an engine built with MS_BLOB_OPTS tlsprobe=on) makes struct sdnet the engine's layout (the lab overlay runs on the
# engine's own sdnet) and patches the lab TLS server's pinned public key SG_LAB_PIN=<64 hex> (the tls_pin line)
TLSPROBE = os.environ.get('SG_TLSPROBE', '0') == '1'
if TLSPROBE:
    _pin = os.environ.get('SG_LAB_PIN', '')
    if not LAB or len(_pin) != 64 or re.search(r'[^0-9a-fA-F]', _pin):
        sys.exit('SG_TLSPROBE=1 needs SG_LAB=1 and SG_LAB_PIN=<64 hex>')
    FLAGS = [f for f in FLAGS if not f.startswith(('-DSDNET_MAX_SOCKETS', '-DSDNET_SOCK_SLOTS', '-DSDNET_SOCK_PAYLOAD'))]
    FLAGS += ['-DSDNET_MAX_SOCKETS=4', '-DSDNET_SOCK_SLOTS=8', '-DSDNET_SOCK_PAYLOAD=1200', '-DSG_TLSPROBE=1', '-DSG_LAB_PIN={' + ','.join('0x' + _pin[i:i + 2] for i in range(0, 64, 2)) + '}']
LAB_SRC = ["sdtls.c", "sdsec.c", "sdhttp.c", "sdprobe.c", "sdtcp.c"]     # only the lab TCP and TLS test needs these
ALIGN = 32
PACK_BASE = 0x00100000   # not 0: relocations of garbage-collected sections carry offsets near 0 and would pass the range filter
R_ADDR32, R_HA, R_LO = 0, 1, 2
TYPES = {"R_PPC_ADDR32": R_ADDR32, "R_PPC_ADDR16_HA": R_HA, "R_PPC_ADDR16_LO": R_LO}


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit("command failed: %s\n%s%s" % (" ".join(cmd[:3]), r.stdout, r.stderr))
    return r.stdout


def rooms_art(work):
    """Game Night list header art: generated into the work directory from rooms-beta-2's Rooms_gui.dat when it is on this
    machine (tools/gnb_rooms_art.py --find; GNB_ROOMS_GUI=none builds without it: the list then writes its tab titles in the menu font)"""
    if os.environ.get("GNB_ROOMS_GUI") == "none":
        return []
    tool = os.path.join(REPO, "tools", "gnb_rooms_art.py")
    src = subprocess.run([sys.executable, tool, "--find"], capture_output=True, text=True).stdout.strip()
    if not src:
        sys.stderr.write("note: rooms-beta-2 Rooms_gui.dat not found: Game Night list tabs in the menu font\n")
        return []
    run([sys.executable, tool, src, os.path.join(work, "gnb_rooms_art.h")])
    return ["-DGNB_ROOMS_ART", "-I" + work]


def sheik_delta(work):
    """Game Night pick grid: Sheik's own icon as a delta over the retail Zelda block, generated into the work directory when
    Slippi's GameSetup_gui.dat and the retail MnSlChr.usd are on this machine (tools/gnb_sheik_delta.py --find; GNB_SHEIK_GUI=none: Zelda's icon with a SHEIK caption)"""
    if os.environ.get("GNB_SHEIK_GUI") == "none":
        return []
    tool = os.path.join(REPO, "tools", "gnb_sheik_delta.py")
    f = subprocess.run([sys.executable, tool, "--find"], capture_output=True, text=True).stdout.splitlines()
    if len(f) != 2:
        sys.stderr.write("note: GameSetup_gui.dat or MnSlChr.usd not found: Sheik shows Zelda's icon with a SHEIK caption\n")
        return []
    run([sys.executable, tool, f[0], f[1], os.path.join(work, "gnb_sheik_delta.h")])
    return ["-DGNB_SHEIK_DELTA"]


def setup_art(work):
    """Game Night stage prep screen pictures (tools/gnb_setup_art.py: generated, no input file): into the work directory"""
    tool = os.path.join(REPO, "tools", "gnb_setup_art.py")
    run([sys.executable, tool, os.path.join(work, "gnb_setup_art.h")])
    return ["-DGNB_SETUP_ART"]


def compile_all(work):
    objs = []
    art = rooms_art(work)
    art = art + [x for x in sheik_delta(work) if x not in art] + [x for x in setup_art(work) if x not in art] + (["-I" + work] if "-I" + work not in art else [])
    for s in SRC:
        if not LAB and os.path.basename(s) in LAB_SRC:
            continue
        o = os.path.join(work, os.path.basename(s)[:-2] + ".o")
        run([P + "-gcc"] + FLAGS + (art if os.path.basename(s) == "gnb_art.c" else []) + ["-std=gnu99", "-Wall", "-Wextra", "-Wno-unused-function", "-Werror", "-I" + NET, "-I" + TLS, "-I" + os.path.join(REPO, "offline/client_net"), "-c", s, "-o", o])
        objs.append(o)
    return objs


def link(work, objs, base):
    elf = os.path.join(work, "ovl_%08X.elf" % base)
    run([P + "-gcc", "-nostartfiles", "-nostdlib", "-Wl,-q", "-Wl,--gc-sections", "-Wl,--defsym,SGO_BASE=0x%X" % base, "-Wl,-T," + os.path.join(HERE, "sg_ovl.ld"),
         "-Wl,-Map," + elf[:-4] + ".map", "-Wl,--undefined=sgo_api"] + objs + ["-lgcc", "-o", elf])
    return elf


def symbols(elf):
    out = {}
    for line in run([P + "-nm", elf]).splitlines():
        p = line.split()
        if len(p) == 3:
            out[p[2]] = int(p[0], 16)
    return out


def image_bytes(work, elf, base, size):
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
            m = re.search(r"'\.rela(\.\S+)'", line)
            cur = m.group(1) if m else None
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
    work = a.workdir or tempfile.mkdtemp(prefix="sgovl-")
    os.makedirs(work, exist_ok=True)
    objs = compile_all(work)
    elf = link(work, objs, PACK_BASE)
    sy = symbols(elf)
    size, bss_end = sy["__sgo_img_end"] - PACK_BASE, sy["__sgo_bss_end"] - PACK_BASE
    img = image_bytes(work, elf, PACK_BASE, size)
    rel = relocs(elf, PACK_BASE, size)
    rb = b"".join(struct.pack(">II", (o << 2) | t, tg) for o, t, tg in rel)
    rb += b"\0" * (up(len(rb)) - len(rb))
    hdr = struct.pack(">8I", 0x53474F56, SGO_VER, size, bss_end - size, len(rel), sy["sgo_api"] - PACK_BASE, PACK_BASE, 0).ljust(ALIGN, b"\0")
    open(a.out, "wb").write(hdr + img + rb)
    stats = {"image_bytes": size, "bss_bytes": bss_end - size, "nreloc": len(rel), "reloc_bytes": len(rb), "file_bytes": ALIGN + size + len(rb),
             "block_bytes_min": size + max(up(bss_end - size), len(rb)), "api_off": sy["sgo_api"] - PACK_BASE,
             "by_type": {"addr32": sum(1 for r in rel if r[1] == 0), "ha": sum(1 for r in rel if r[1] == 1), "lo": sum(1 for r in rel if r[1] == 2)}}
    if a.stats:
        json.dump(stats, open(a.stats, "w"), indent=1)
    print(json.dumps(stats))
    return 0


def raw(a):
    work = a.workdir or tempfile.mkdtemp(prefix="sgovl-")
    os.makedirs(work, exist_ok=True)
    objs = compile_all(work)
    base = int(a.base, 0)
    elf0 = link(work, objs, PACK_BASE)
    size = symbols(elf0)["__sgo_img_end"] - PACK_BASE
    elf = link(work, objs, base)
    open(a.out, "wb").write(image_bytes(work, elf, base, size))
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
