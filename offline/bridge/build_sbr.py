#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Build the Slippi-bridge LAB DOL/ISO ("sbr"), patch per-run config variants of it, or show its config.

  python3 offline/bridge/build_sbr.py build [--profile net-menu] [--name sbr] [--out build/offline/bridge/iso] [--no-iso] [key=value ...]
  python3 offline/bridge/build_sbr.py patch BASE.iso OUT.iso [key=value ...]     (APFS clone + in-place cfg patch)
  python3 offline/bridge/build_sbr.py show ISO_OR_DOL

Opt-in LAB build, never a release: reads build/offline/codeset/<profile>.bin (never writes it; net-menu = Slippi's own online
menus with the fp-align fix, README 0.3) and checks that build/offline/codeset/default.bin keeps its SHA-1.
Code list: the profile, one C2 at 0x801A4D98 whose payload is the bridge image,
04 `b sbr_exi_entry` at 0x800055F0 (FN_EXITransferBuffer), 04 `b sbr_unlock_stub` at 0x80229938 (mn_80229938, retail word
`mflr r0` asserted in the patched DOL: Ranked, Teams and Party do not exist on the bridge's online submenu).  No identity C2: the
bridge reads MultishineIdentity itself (offline/bridge/sbr_card.c). ISO: with Slippi's GameFiles overlays from the
local Slippi Dolphin install; the result must never be distributed ( rule 7).

cfg keys (struct sbra_cfg, sbr_app.h): flags mm_ip mm_port dns_ip local_port dns_lport start_frame connect_ms create_ms peer_ms reticket_max
platform delay stub_ready_ms stub_sel dns_port app_version (the ticket's appVersion, "3.7.0") mm_name hist0..hist3.  (Since there is no status server:
no status_ip/status_port/status_lport/status_ms/status_tries/own_build/doc_pub/time_pub keys; the cfg block shrank from 272 to 200 bytes.)  Flags: 1 no-net, 2 stub match (LAB), 4 peer stub (LAB), 8 DNS answer from any source (LAB/Dolphin BuiltIn BBA quirk),
0x10 stop right after the assignment (first live step option: no peer contact).
Engine flags word (image block S11CFG01, s17_flags): patch and live set SKIPCAP 0x40000000 (skip the rollback frame's own capture)
unless --no-skipcap; `show` prints engine_s17_flags.  Policy bit only: it changes when the engine saves states, never the state reached.
  python3 offline/bridge/build_sbr.py live BASE.iso OUT_LIVE.iso [--peer-handoff] [--app-version 3.7.0]
      EXPLICIT live mode (off unless this subcommand is used; the output file name must contain LIVE): the real matchmaker name matchmaking.slippi.gg
      (port 443) resolved by the DHCP resolver (dns_ip 0, dns_port 0), no LAB flags, flags = 0x10 exactly (stop after the assignment, first live
      step) or 0x20 with --peer-handoff, reticket_max 0.  No status server, no keys.  See check_live.
Address rule of this LAB builder: every address in the cfg is 0 or private (RFC 1918); mm_name may never be mm2.slippi.gg.
"""
import argparse
import hashlib
import ipaddress
import json
import os
import pathlib
import re
import socket
import struct
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent
for p in ("offline/dolpatch", "offline/codeset", "offline/iso", "offline"):
    sys.path.insert(0, str(REPO / p))
import dol_patch  # noqa: E402
import merge_codes  # noqa: E402
sys.path.insert(0, str(REPO / "offline/multishine"))
import payload as msp  # noqa: E402  (ML_HOOKS, restore_retail_codes: our menu layout image)

HOOK = 0x801A4D98
EXI_SLOT = 0x800055F0
UNLOCK = 0x80229938
UNLOCK_WORD = 0x7C0802A6            # mflr r0 (retail entry of mn_80229938)
RETAIL_DOL = REPO / "vendor/melee/orig/GALE01/sys/main.dol"
OUTB = REPO / "build/offline/bridge"
DEFAULT_SHA1 = "3b6c8e4d0cf38b944f388cd69ff28f45208cb919"
CFG_OFF, STATUS_OFF = 0x40, 0x180   # the cfg block (200 bytes) ends well before the status block
MAGIC = b"SBRCFG01"
INTS = ["flags", "mm_ip", "mm_port", "dns_ip", "local_port", "dns_lport", "start_frame",
        "connect_ms", "create_ms", "peer_ms", "reticket_max", "platform", "delay",
        "stub_ready_ms", "stub_sel", "dns_port"]
KEYS = []                       # the 32-byte key fields (doc_pub, time_pub) are gone with the status server; kept as an empty list for tools that loop over it
STRS = [("app_version", 16), ("mm_name", 64), ("hist0", 12), ("hist1", 12), ("hist2", 12), ("hist3", 12)]
CFG_LEN = 8 + 4 * len(INTS) + 32 * len(KEYS) + sum(n for _, n in STRS)
assert CFG_LEN == 200
APP_VERSION_RE = re.compile(r"^\d{1,5}\.\d{1,5}\.\d{1,5}$")
STATUS = ("magic net ip app_state ident ident_ok ident_parse ident_slot cache cache_slot cache_dirty cache_writes cache_wstatus "
          "cache_flags q_state q_sent q_rx q_bad q_last time_ok now_unix doc_src gate gate_evals blocked dns_state dns_err "
          "dns_lookups mm_ip dns_server dns_rx dns_ignored dns_bad dns_last_src state err mode searches tickets assigned retickets cancels wipes peer peer_ip peer_port peer_ok "
          "peer_fail b9 b4 b3 b5 ba be b0 other refused hidden_rows local_ready remote_ready local_idx is_host mm_seen lock_fail "
          "tick_max menu_ba").split()
assert len(STATUS) == 66
IP_KEYS = ("mm_ip", "dns_ip")


def ip2u(s):
    return struct.unpack(">I", socket.inet_aton(s))[0]


def u2ip(v):
    return socket.inet_ntoa(struct.pack(">I", v))


def cfg_unpack(b):
    assert b[:8] == MAGIC, b[:8]
    d = dict(zip(INTS, struct.unpack(">%dI" % len(INTS), b[8:8 + 4 * len(INTS)])))
    o = 8 + 4 * len(INTS)
    for k in KEYS:
        d[k] = b[o:o + 32].hex()
        o += 32
    for k, n in STRS:
        d[k] = b[o:o + n].split(b"\0", 1)[0].decode("ascii")
        o += n
    return d


def check_cfg(d):
    for k in IP_KEYS:
        if d[k] and not ipaddress.ip_address(u2ip(d[k])).is_private:
            raise SystemExit("refused: %s %s is not a private lab address (this is the LAB builder)" % (k, u2ip(d[k])))
    if d["mm_name"].lower().rstrip(".") in ("mm2.slippi.gg", "matchmaking-dev.slippi.gg"):
        raise SystemExit("refused: mm2.slippi.gg and matchmaking-dev.slippi.gg are Slippi's internal test environments (charter: never a target)")
    if d["flags"] & ~0x3F:
        raise SystemExit("unknown flag bits")
    if not APP_VERSION_RE.match(d["app_version"]):
        raise SystemExit("refused: app_version must be a semver a.b.c (got %r)" % d["app_version"])


LIVE_MM = "matchmaking.slippi.gg"


def check_live(d, peer_handoff=False):
    """The live image: everything the lab builder forbids is allowed only here, and only in this exact shape."""
    if d["mm_name"].lower().rstrip(".") != LIVE_MM or d["mm_name"] != LIVE_MM:
        raise SystemExit("live: mm_name must be exactly %s (mm2.slippi.gg and every other name are refused)" % LIVE_MM)
    if d["mm_port"] != 443:
        raise SystemExit("live: mm_port must be 443")
    if d["mm_ip"] or d["dns_ip"] or d["dns_port"]:
        raise SystemExit("live: mm_ip, dns_ip and dns_port must be 0 (the name is resolved by the resolver the router hands out by DHCP)")
    if d["flags"] != (0x20 if peer_handoff else 0x10):
        raise SystemExit("live: flags must be exactly 0x10 (stop after the assignment; no LAB flags, no peer contact in this first step)"
                         if not peer_handoff else "live: with --peer-handoff flags must be exactly 0x20 (SBRA_F_ENGINE_PEER, no LAB flags, no stop flag)")
    if d["reticket_max"] != 0:
        raise SystemExit("live: reticket_max must be 0 (one ticket per search, no retry)")
    check_cfg(dict(d, mm_name=LIVE_MM, flags=0x10))      # the lab address rule still applies to every address field


def cfg_pack(d, live=False, peer_handoff=False):
    if live:
        check_live(d, peer_handoff)
    else:
        check_cfg(d)
    b = MAGIC + struct.pack(">%dI" % len(INTS), *[d[k] for k in INTS])
    for k in KEYS:
        kb = bytes.fromhex(d[k]) if d[k] else b""
        assert len(kb) in (0, 32), k
        b += kb.ljust(32, b"\0")
    for k, n in STRS:
        s = d[k].encode("ascii")
        assert len(s) < n, (k, d[k])
        b += s + b"\0" * (n - len(s))
    assert len(b) == CFG_LEN
    return b


def cfg_apply(d, kv):
    for item in kv:
        k, v = item.split("=", 1)
        if k not in d:
            raise SystemExit("unknown cfg key " + k)
        if k in INTS:
            d[k] = ip2u(v) if k.endswith("_ip") and "." in v else int(v, 0)
        else:
            d[k] = v
    return d


def sha1(b):
    return hashlib.sha1(b).hexdigest()


def nm(name):
    for line in (OUTB / "sbr.nm").read_text().split("\n"):
        p = line.split()
        if len(p) == 3 and p[2] == name:
            return int(p[0], 16)
    raise KeyError(name)


LAYOUT = False
LAYOUT_RESTORE = [0x8022D638]       # Tournament Melee: retail `li r0,27` (the contract's restore_retail_04)
ML_SITES = [h for h in msp.ML_HOOKS if h[0] != UNLOCK]     # the unlock site belongs to sbr_unlock_stub (it asks ml_unlock_override for every kind but 8)


def blob(base):
    subprocess.check_call([str(HERE / "build_blob.sh"), "0x%08X" % base], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                          env=dict(os.environ, OUT=str(OUTB), ML="1" if LAYOUT else "0"))
    return (OUTB / "sbr.bin").read_bytes()


def c2(hook, payload):
    assert len(payload) % 8 == 0
    return struct.pack(">II", 0xC2000000 | (hook & 0x01FFFFFF), len(payload) // 8) + payload


def code04(addr, target, link=False):
    return struct.pack(">II", 0x04000000 | (addr & 0x01FFFFFF), 0x48000000 | ((target - addr) & 0x03FFFFFC) | (1 if link else 0))


def ramvars(info):
    base = json.loads((REPO / "offline/bridge/ramvars_base.json").read_text())
    v = base["vars"]
    st = int(info["status_addr"], 16)
    for i, n in enumerate(STATUS):
        v["sbr_" + n] = {"addr": "%08X" % (st + 4 * i), "src": "bridge status word (offline/bridge/sbr_app.h struct sbra_status)"}
    v["online_mode"] = {"addr": "804D6640", "shift": 24, "mask": "FF000000", "src": "r13-0x5060 OFST_R13_ONLINE_MODE"}
    v["online_app_state"] = {"addr": "804D6640", "shift": 16, "mask": "00FF0000", "src": "r13-0x505F OFST_R13_APP_STATE"}
    v["name_entry_mode"] = {"addr": "804D6640", "shift": 0, "mask": "000000FF", "src": "r13-0x505D OFST_R13_NAME_ENTRY_MODE"}
    v["css_req"] = {"addr": "804D6CF4", "shift": 8, "mask": "0000FF00", "src": "r13-0x49AA CSS scene request (4 = name entry)"}
    v["css_char_sel"] = {"addr": "804D6CF4", "shift": 0, "mask": "000000FF", "src": "r13-0x49A9 character selected"}
    return base


def build(a):
    global OUTB, LAYOUT
    if a.layout:
        LAYOUT = True
        OUTB = REPO / "build/offline/bridge_layout"
        if a.profile == "net-menu":
            a.profile = "net-menu-ms"
        if a.name == "sbr":
            a.name = "sbr_couch"
        if a.out == str(REPO / "build/offline/bridge/iso"):
            a.out = str(REPO / "build/offline/bridge_layout/iso")
    OUTB.mkdir(parents=True, exist_ok=True)
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    work = out / ".work"
    work.mkdir(exist_ok=True)
    dpath = REPO / "build/offline/codeset/default.bin"
    dsha = sha1(dpath.read_bytes())
    if dsha != DEFAULT_SHA1:
        raise SystemExit("default.bin SHA-1 %s != %s" % (dsha, DEFAULT_SHA1))
    prof = (REPO / "build/offline/codeset" / (a.profile + ".bin")).read_bytes()
    if LAYOUT:
        prof = msp.restore_retail_codes(prof, LAYOUT_RESTORE)
    if b"\x04\x22\x99\x38" in prof or b"\xC2\x22\x99\x38" in prof:
        raise SystemExit("the profile already patches 0x80229938")
    print("profile %s sha1 %s (read only); default.bin %s" % (a.profile, sha1(prof), dsha))
    dol = RETAIL_DOL.read_bytes()

    def merged(img, base):
        if base:
            parts = [prof, c2(HOOK, img), code04(EXI_SLOT, nm("sbr_exi_entry")), code04(UNLOCK, nm("sbr_unlock_stub"))]
            if LAYOUT:
                parts += [code04(a_, nm(s_), l_) for a_, s_, l_, _w in ML_SITES]
        else:
            parts = [prof, c2(HOOK, img), code04(EXI_SLOT, EXI_SLOT + 0x100), code04(UNLOCK, UNLOCK + 0x100)]
            if LAYOUT:
                parts += [code04(a_, a_, l_) for a_, s_, l_, _w in ML_SITES]
        ts = []
        for i, b in enumerate(parts):
            t = work / ("part%d.bin" % i)
            t.write_bytes(b)
            ts.append(str(t))
        o = work / "list.bin"
        merge_codes.main_merge(str(o), ts)
        return o.read_bytes()

    # the retail word at the unlock site, before our 04 (the profile alone)
    plain, _ = dol_patch.bake(dol, prof, dol_patch.DEFAULT_BASE, dol_patch.DEFAULT_MAX_END, dol_patch.RETAIL_SHA1)

    def word_of(dbytes, addr):
        dd = dol_patch.Dol(dbytes)
        for s in dd.loaded():
            if s["addr"] <= addr < s["addr"] + s["size"]:
                o = s["offset"] + addr - s["addr"]
                return struct.unpack(">I", dbytes[o:o + 4])[0]
    if word_of(plain, UNLOCK) != UNLOCK_WORD:
        raise SystemExit("0x80229938 is not `mflr r0` in the profile build: %08X" % word_of(plain, UNLOCK))
    base = 0x80500000
    img = blob(base)
    for _ in range(4):
        _, rep = dol_patch.bake(dol, merged(img, base), dol_patch.DEFAULT_BASE, dol_patch.DEFAULT_MAX_END, dol_patch.RETAIL_SHA1)
        hit = [c for c in rep["c2"] if c["hook"] == "0x%08X" % HOOK]
        got = int(hit[-1]["body"], 16)
        if got == base:
            break
        base = got
        img = blob(base)
    else:
        raise SystemExit("placement did not converge")
    d = cfg_unpack(img[CFG_OFF:CFG_OFF + CFG_LEN])
    d = cfg_apply(d, a.kv)
    img = img[:CFG_OFF] + cfg_pack(d) + img[CFG_OFF + CFG_LEN:]
    baked, rep = dol_patch.bake(dol, merged(img, base), dol_patch.DEFAULT_BASE, dol_patch.DEFAULT_MAX_END, dol_patch.RETAIL_SHA1)
    entry, stub = nm("sbr_exi_entry"), nm("sbr_unlock_stub")
    assert word_of(baked, EXI_SLOT) == 0x48000000 | ((entry - EXI_SLOT) & 0x03FFFFFC), hex(word_of(baked, EXI_SLOT))
    assert word_of(baked, HOOK) == 0x48000000 | ((base - HOOK) & 0x03FFFFFC), hex(word_of(baked, HOOK))
    assert word_of(baked, UNLOCK) == 0x48000000 | ((stub - UNLOCK) & 0x03FFFFFC), hex(word_of(baked, UNLOCK))
    if LAYOUT:
        for a_, s_, l_, w_ in ML_SITES:
            assert word_of(plain, a_) == w_, "menu hook site %08X is not the retail word in the profile build" % a_
            assert word_of(baked, a_) == 0x48000000 | ((nm(s_) - a_) & 0x03FFFFFC) | (1 if l_ else 0), s_
    dolp = out / (a.name + ".dol")
    dolp.write_bytes(baked)
    info = {"profile": a.profile, "image_base": "0x%08X" % base, "image_size": len(img), "exi_entry": "0x%08X" % entry,
            "unlock_stub": "0x%08X" % stub, "cfg_addr": "0x%08X" % (base + CFG_OFF), "status_addr": "0x%08X" % (base + STATUS_OFF),
            "section": rep["section"], "arena": rep["arena"], "dol_sha1": sha1(baked), "profile_sha1": sha1(prof),
            "default_bin_sha1": dsha, "cfg": d, "c2_hooks": [c["hook"] for c in rep["c2"]]}
    if LAYOUT:
        info["layout"] = True
        info["ml_status_addr"] = "0x%08X" % nm("ml_st")
    (out / (a.name + ".json")).write_text(json.dumps(info, indent=1))
    (out / (a.name + ".ramvars.json")).write_text(json.dumps(ramvars(info), indent=1))
    print(json.dumps({k: v for k, v in info.items() if k not in ("arena", "c2_hooks")}, indent=1))
    for f in work.iterdir():
        f.unlink()
    if sha1(dpath.read_bytes()) != DEFAULT_SHA1:
        raise SystemExit("default.bin changed during the build")
    return 0


def find_cfg(path):
    with open(path, "rb") as f:
        head = f.read(0x440)
        if str(path).endswith(".iso"):
            dol_off = struct.unpack(">I", head[0x420:0x424])[0]
            f.seek(dol_off)
            blob_ = f.read(8 << 20)
            base = dol_off
        else:
            f.seek(0)
            blob_ = f.read()
            base = 0
    i = blob_.find(MAGIC)
    assert i >= 0 and blob_.find(MAGIC, i + 1) < 0, "cfg magic not found exactly once"
    return base + i


# ---- engine flags word (struct s11_cfg.s17_flags of the rollback engine, image config block "S11CFG01"): SKIPCAP  ----
S11_MAGIC = b"S11CFG01"
S11_S17_FLAGS_OFF = 8 + 11 * 4            # build_s17_net.py S11FIELDS: s17_flags is the 12th word after the magic
SKIPCAP = 0x40000000                      # SD_S17_SKIPCAP: skip the rollback frame's own capture; engine policy bit only


def find_s11(path):
    """File offset of the S11 config block, or None when the image has none (the unit tests' fake images)."""
    with open(path, "rb") as f:
        head = f.read(0x440)
        if str(path).endswith(".iso"):
            dol_off = struct.unpack(">I", head[0x420:0x424])[0]
            f.seek(dol_off)
            blob_ = f.read(8 << 20)
            base = dol_off
        else:
            f.seek(0)
            blob_ = f.read()
            base = 0
    i = blob_.find(S11_MAGIC)
    if i < 0:
        return None
    assert blob_.find(S11_MAGIC, i + 1) < 0, "S11 cfg magic found more than once"
    return base + i


def engine_flags(path):
    off = find_s11(path)
    if off is None:
        return None
    with open(path, "rb") as f:
        f.seek(off + S11_S17_FLAGS_OFF)
        return struct.unpack(">I", f.read(4))[0]


def set_skipcap(path, enable=True):
    """Make the engine flags word of the image carry SKIPCAP (idempotent; images built from the online contracts already have it).
    Only the policy bit is touched: with the default LIVE flags (0x10, stop after the assignment) the engine never runs, so nothing changes there."""
    off = find_s11(path)
    if off is None:
        return None
    with open(path, "r+b") as f:
        f.seek(off + S11_S17_FLAGS_OFF)
        w = struct.unpack(">I", f.read(4))[0]
        w2 = (w | SKIPCAP) if enable else (w & ~SKIPCAP)
        if w2 != w:
            f.seek(off + S11_S17_FLAGS_OFF)
            f.write(struct.pack(">I", w2))
        return w2


def patch(a):
    if os.path.exists(a.out):
        os.unlink(a.out)
    subprocess.check_call(["cp", "-c", a.base, a.out])
    os.chmod(a.out, 0o644)
    off = find_cfg(a.out)
    with open(a.out, "r+b") as f:
        f.seek(off)
        d = cfg_apply(cfg_unpack(f.read(CFG_LEN)), a.kv)
        f.seek(off)
        f.write(cfg_pack(d))
    set_skipcap(a.out, not a.no_skipcap)
    print(json.dumps(d))
    return 0


BAKE_MAGIC = b"SBRBID01"
ID_LEN, ID_FILE_OFF = 0x12C, 0x1000


def bake_ident(a):
    """Patch the player's identity payload (0x12C bytes: 'SDID' v1, the same bytes as the card file MultishineIdentity at file offset 0x1000) into the image's
    identity slot (br_glue.c sbr_baked).  The image then holds a play key: mode 0600, never shared (the RC build notes say so for the lab secret already).
    Lab/dev images read the patched identity first, release images the card first (sbra_set_baked_ident).  Prints no secret."""
    import zlib
    raw = open(a.src, "rb").read()
    pl = raw[ID_FILE_OFF:ID_FILE_OFF + ID_LEN] if a.blk else raw
    if len(pl) != ID_LEN or pl[:4] != b"SDID" or pl[4] != 1:
        raise SystemExit("bake-ident: not an identity payload (need 0x12C bytes, SDID v1)")
    if struct.unpack(">I", pl[-4:])[0] != (zlib.crc32(pl[:-4]) & 0xFFFFFFFF):
        raise SystemExit("bake-ident: CRC mismatch")
    with open(a.image, "rb") as f:
        head = f.read(0x440)
        if a.image.endswith(".iso"):
            base = struct.unpack(">I", head[0x420:0x424])[0]
            f.seek(base)
            blob_ = f.read(8 << 20)
        else:
            base = 0
            f.seek(0)
            blob_ = f.read()
    i = blob_.find(BAKE_MAGIC)
    if i < 0 or blob_.find(BAKE_MAGIC, i + 1) >= 0:
        raise SystemExit("bake-ident: identity slot magic not found exactly once (image without the baked-identity slot)")
    with open(a.image, "r+b") as f:
        f.seek(base + i + 8)
        f.write(pl)
    print("baked identity payload (%d bytes) into %s at 0x%X" % (ID_LEN, os.path.basename(a.image), base + i + 8))
    return 0


def live(a):
    if "LIVE" not in os.path.basename(a.out):
        raise SystemExit("live: the output file name must contain LIVE")
    off = find_cfg(a.base)
    with open(a.base, "rb") as f:
        f.seek(off)
        d = cfg_unpack(f.read(CFG_LEN))
    d.update({k: 0 for k in ("mm_ip", "dns_ip", "dns_port", "reticket_max")})
    d.update({"flags": 0x20 if a.peer_handoff else 0x10, "mm_name": LIVE_MM, "mm_port": 443, "hist0": "", "hist1": "", "hist2": "", "hist3": ""})
    if a.app_version:
        d["app_version"] = a.app_version
    packed = cfg_pack(d, live=True, peer_handoff=a.peer_handoff)                      # refuses before any file is written
    if os.path.exists(a.out):
        os.unlink(a.out)
    subprocess.check_call(["cp", "-c", a.base, a.out])
    os.chmod(a.out, 0o600)
    off = find_cfg(a.out)
    with open(a.out, "r+b") as f:
        f.seek(off)
        f.write(packed)
    set_skipcap(a.out, not a.no_skipcap)
    print(json.dumps({k: (u2ip(v) if k.endswith("_ip") and v else v) for k, v in d.items()}))
    return 0


def show(a):
    off = find_cfg(a.path)
    with open(a.path, "rb") as f:
        f.seek(off)
        d = cfg_unpack(f.read(CFG_LEN))
    ef = engine_flags(a.path)
    if ef is not None:
        d["engine_s17_flags"] = "0x%08X" % ef
    print(json.dumps(d, indent=1))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("--profile", default="net-menu")
    b.add_argument("--name", default="sbr")
    b.add_argument("--out", default=str(OUTB / "iso"))
    b.add_argument("--overlays", default="all")
    b.add_argument("--no-iso", action="store_true")
    b.add_argument("--layout", action="store_true", help="couch variant: the menu layout (offline/multishine/menus/ml) + the bridge; output build/offline/bridge_layout")
    b.add_argument("kv", nargs="*")
    p = sp.add_parser("patch")
    p.add_argument("base")
    p.add_argument("out")
    p.add_argument("--no-skipcap", action="store_true", help="clear the engine flag SKIPCAP (0x40000000) in the output (default: set; A/B and rollback to the old behaviour)")
    p.add_argument("kv", nargs="*")
    lv = sp.add_parser("live", help="EXPLICIT live image (matchmaking.slippi.gg via the DHCP resolver, stop after the assignment); output name must contain LIVE")
    lv.add_argument("base")
    lv.add_argument("out")
    lv.add_argument("--app-version", default="", help="the ticket's appVersion (default: what the base image carries, SBRA_APP_VERSION in sbr_app.h)")
    lv.add_argument("--peer-handoff", action="store_true", help="LATER step only: flag 0x10 OFF so the opponent is contacted (needs the engine hand-off, sbr_engine.h hook); never for the first search")
    lv.add_argument("--no-skipcap", action="store_true", help="clear the engine flag SKIPCAP (0x40000000) in the output (default: set)")
    bi = sp.add_parser("bake-ident", help="bake the identity payload into the image's identity slot (in place)")
    bi.add_argument("image")
    bi.add_argument("src", help="a 0x12C-byte payload file, or with --blk the 0x2000-byte card file block (payload at 0x1000)")
    bi.add_argument("--blk", action="store_true")
    s = sp.add_parser("show")
    s.add_argument("path")
    a = ap.parse_args(argv)
    return {"build": build, "patch": patch, "show": show, "live": live, "bake-ident": bake_ident}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
