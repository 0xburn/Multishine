#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""S16: copy of for stage S16 (sources offline/s17/net, output build/offline/s17/net, image
s17.bin, ISO s17.iso, default relay 10.213.101.20; three more keys in the S11 config block: s16_flags s16_arena_kb s16_log_pct).
S12: copy of (HEAD 1817288) for stage S12, with the S10-based S12
WIP (dd4796b) merged in (sources, output build/offline/s17/net,
image s12.bin; fourth config block "S12CFG01" at image +0xB80 (keys s12_flags x_fn0..x_fn7 early_guard early_pct
early_poll_us loc_sel loc_lo), S12 telemetry at +0xBC0 ("y12_addr", 128 words), code from +0xDC0; more 04 codes (retail
words checked): `bl s12_idle_stub` at 0x801A4DA8 (scene-loop wait loop), `bl s12_render_stub` at 0x801A5030 (render
part), `b s12_cam0_stub` at 0x8002A4AC (Camera_8002A4AC entry) and `b s12_cam1_stub` at 0x80086A8C (ftLib_80086A8C
entry); default relay 10.213.97.20).

S11: copy of for stage S11 (fingerprint capture; engine; output
build/offline/s11/net, image s11.bin, ISO s11.iso; fourth config block "S11CFG01" at image +0xA40 (keys fp fp_bits fpv
fpv_fix cold cold_n site_prof site_frame), S11 telemetry at +0xA80 ("x11_addr"), code from +0xB80; default relay
10.213.95.20).  Everything S11 is off by default (fp 0): the image then behaves as the S10 image.

S10: copy of for stage S10 (sources, output build/, image
s10.bin; third config block "S10CFG01" at image +0x900 (keys s10_flags trim_lo trim_hi prof_frame rmode rs_sel nwx
trim_fn0..3), S10 telemetry at +0x940 ("x10_addr"), code from +0xA40; one more 04 code: `bl s10_invoke` at 0x80390DFC
(the `blrl` of HSD_GObj_RunProcs that calls proc->on_invoke; retail word 0x4E800021 checked); default relay 10.213.93.20).

S7: copy of for stage S7 (sources, output build/, image
s7.bin; second config block "S7CFG001" at image +0x800 (keys sel0 sel1 ts_mode ts_thr_us ts_gap s7_flags script_seed
connect_data) and the S7 wire status block at +0x840 ("xs_addr"), code from +0x900; default relay 10.213.87.20).
The patch command accepts keys of both blocks.

S6D: copy of for stage S6d (sources, output build/;
cfg keys cost_pct ovr_pct ovr_every cap_hyst s6d_flags sfx_dump at +0xA8 (CFG_LEN 0xC0); S6d telemetry block "dtel_addr" at
image +0x700, code from +0x800; TEST-ONLY SFX-log hooks at 0x8038CFF4 / 0x8038D29C / 0x800236B8 (always installed, inert
unless cfg s6d_flags bit1); `build --profile-bin FILE` patches a saved codeset list instead of build/offline/codeset/<profile>.bin
(used for the "before" image without the Slippi Sound modules)).

S6C: copy of for stage S6c (sources and output, build/;
cfg keys stride s6c_flags cap_min hw_pct busy_hi busy_lo hyst budget_pct stride_max at +0x84; telemetry block address
"tel_addr"; `build --dirty default|swiss` = TEST-ONLY stale-RAM variant).

Build the S6b rollback-netplay DOL/ISO ( lockstep wire + Slippi prediction/rollback with the delta snapshot
engine), and patch per-run config variants of it.

  python3 build [--profile net-online-spike-boot] [--profile-bin FILE] [--name s6d] [--out build/offline/s6d/net/iso]
         [--relay 10.213.79.20] [--no-iso]
  python3  patch BASE.iso OUT.iso [key=value ...]   (APFS clone + in-place cfg patch)
  python3 show IMAGE_OR_ISO

Opt-in only: reads build/offline/codeset/<profile>.bin (never writes it), so default.bin (SHA-1 checked) and the
default patch are untouched.  The list is: profile + one C2 at 0x801A4D98 whose payload is the whole image (tramp at +0)
+ 04 codes: `b s3_exi_entry` at 0x800055F0 (FN_EXITransferBuffer), `b s6n_iter_stub` at 0x801A4DE8 (after Slippi's
StartEngineLoop), `bl s6n_end_stub` at 0x801A4FF0 (end of logic), and the snapshot engine's allocator hooks
(OSAllocFromHeap 0x80343EF0, OSFreeToHeap 0x80343FEC, HSD_ObjAllocInit 0x8037AD48).  Two-pass link at the address the
patcher assigns to the C2 body.

cfg keys (struct s3_cfg): session delay rng_offset start_frame local_port flags neg_frame spin_us
stall_disc first_wait stocks window hello_ms time_limit, peer0_mac peer0_side peer0_ip peer0_port, peer1_*.
"""
import argparse, hashlib, json, os, pathlib, shutil, socket, struct, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent.parent
sys.path.insert(0, str(REPO / "offline/dolpatch"))
sys.path.insert(0, str(REPO / "offline/codeset"))
sys.path.insert(0, str(REPO / "offline/iso"))
import dol_patch, merge_codes  # noqa: E402

HOOK = 0x801A4D98
EXI_SLOT = 0x800055F0
RETAIL_DOL = REPO / "vendor/melee/orig/GALE01/sys/main.dol"
OUTB = REPO / "build/offline/s17/net"   # S12
ITER_HOOK, END_HOOK = 0x801A4DE8, 0x801A4FF0
SDHOOKS = {0x80343EF0: "sd_osalloc_stub", 0x80343FEC: "sd_osfree_stub", 0x8037AD48: "sd_objinit_stub",
           0x8037ABC8: "sd_hobjalloc_stub", 0x8037AD20: "sd_hobjfree_stub",   # S11: HSD_ObjAlloc / HSD_ObjFree (cold invalidation)
           0x80381FA8: "sd_hpiecealloc_stub", 0x8038216C: "sd_hpiecefree_stub",   # S11: hsdAllocMemPiece / hsdFreeMemPiece
           0x800195FC: "sd_padalarm_stub",
           0x8038CFF4: "sfx_req_stub", 0x8038D29C: "sfx_start_stub", 0x800236B8: "sfx_stop_stub"}   # S6D TEST ONLY SFX log
SFX_RETAIL = {0x8038CFF4: 0x7C0802A6, 0x8038D29C: 0x807D000C, 0x800236B8: 0x7C0802A6}   # S6D: displaced instructions
INVOKE_HOOK = 0x80390DFC                                     # S10: `blrl` in HSD_GObj_RunProcs -> `bl s10_invoke`
SFX_RETAIL[INVOKE_HOOK] = 0x4E800021
S12_BL_HOOKS = {0x801A4DA8: "s12_idle_stub", 0x801A5030: "s12_render_stub"}   # S12: `bl` sites (retail `bl lb_800195D0`)
S12_B_HOOKS = {0x8002A4AC: "s12_cam0_stub", 0x80086A8C: "s12_cam1_stub",       # S12: function entries (`mflr r0`)
               0x8001044C: "s12_dyn_stub"}                                         # S12 (on S11): lb_8001044C (DYNB)
SFX_RETAIL.update({0x801A4DA8: 0x4BE74829, 0x801A5030: 0x4BE745A1, 0x8002A4AC: 0x7C0802A6, 0x80086A8C: 0x7C0802A6,
                   0x8001044C: 0x7C0802A6})
PS_BL_HOOKS = {0x801D4754: "ps_request", 0x801D4760: "ps_done"}   # S19b: deterministic Stadium transformations (s12_gc.c), `bl` sites in grStadium_801D4548
PS_RETAIL = {0x801D4754: 0x4BE41E2D, 0x801D4760: 0x4BFFFB59}       # S19b: retail `bl lbFile_80016580`, `bl grStadium_801D42B8`
SFX_RETAIL.update(PS_RETAIL)
# HIL dev link: MS_HIL_DEV=1 adds the pad-injection hook, `bl PADRead` in HSD_PadRenewRawStatus -> `bl hil_padread_stub`
# (glue/s17od/s12_hooks.S).  Off by default: without the variable the tables are exactly the release tables; with it a profile that lacks the stub
# fails in nm (profile s17-compat-hil is the only one that has it). payload.py read these tables through this module.
if os.environ.get("MS_HIL_DEV") == "1":
    S12_BL_HOOKS[0x80376A24] = "hil_padread_stub"
    SFX_RETAIL[0x80376A24] = 0x4BFD6FDD
SFX_RETAIL[0x8037ABC8] = 0x7C0802A6                         # S11: HSD_ObjAlloc mflr r0 (displaced by sd_hobjalloc_stub)
SFX_RETAIL[0x8037AD20] = 0x80030004                         # S11: HSD_ObjFree lwz r0,4(r3)
SFX_RETAIL[0x80381FA8] = 0x7C0802A6                         # S11: hsdAllocMemPiece mflr r0
SFX_RETAIL[0x8038216C] = 0x7C0802A6                         # S11: hsdFreeMemPiece mflr r0
# S17: the retail words of the SDK locked-cache functions the engine calls through fixed addresses (sd_lc.h): LCEnable `mflr r0`,
# LCDisable `lis r3,0xE000` (also checked by for kbench2)
S17_RETAIL = {0x803449FC: 0x7C0802A6, 0x80344A34: 0x3C60E000}
SFX_RETAIL.update(S17_RETAIL)
DEFAULT_SHA1 = "3b6c8e4d0cf38b944f388cd69ff28f45208cb919"
CFG_OFF, STATUS_OFF, TRACE_OFF, CKS_OFF, RB_OFF, TEL_OFF, DTEL_OFF = 0x40, 0x100, 0x200, 0x480, 0x4C0, 0x600, 0x700
XCFG_OFF, XST_OFF = 0x800, 0x840                           # S7
S10CFG_OFF, S10TEL_OFF = 0x900, 0x940                      # S10
S11CFG_OFF, S11TEL_OFF = 0xA40, 0xA80                      # S11
S11MAGIC = b"S11CFG01"
S11FIELDS = ["fp", "fp_bits", "fpv", "fpv_fix", "cold", "cold_n", "site_prof", "site_frame",
             "s16_flags", "s16_arena_kb", "s16_log_pct", "s17_flags"]   # S11 (+S16: 3 of the 6 reserved words, +S17: s17_flags, +2 reserved)
S11CFG_LEN = 0x40
S12CFG_OFF, S12TEL_OFF, CODE_OFF = 0xB80, 0xBC0, 0xDC0      # S12 (after the S11 blocks)
S12MAGIC = b"S12CFG01"
S12FIELDS = ["s12_flags"] + ["x_fn%d" % i for i in range(8)] + ["early_guard", "early_pct", "early_poll_us", "loc_sel", "loc_lo"]
S12CFG_LEN = 0x40
S10MAGIC = b"S10CFG01"
S10FIELDS = ["s10_flags", "trim_lo", "trim_hi", "prof_frame", "rmode", "rs_sel", "nwx", "trim_fn0", "trim_fn1", "trim_fn2",
             "trim_fn3"]                                    # S10 (+3 reserved)
S10CFG_LEN = 0x40
XMAGIC = b"S7CFG001"                                        # S7
XFIELDS = ["sel0", "sel1", "ts_mode", "ts_thr_us", "ts_gap", "s7_flags", "script_seed", "connect_data"]   # S7 (+6 reserved)
XCFG_LEN = 0x40
MAGIC = b"S3CFG001"
FIELDS = ["session", "delay", "rng_offset", "start_frame", "local_port", "flags", "neg_frame", "spin_us",
          "stall_disc", "first_wait", "stocks", "window", "hello_ms", "time_limit"]
PEER = ["mac", "side", "ip", "port"]
ALLOW = ["allow_ip", "allow_port"]       # cfg +0x60: incoming-peer allow-list (0 = the peer table dst_ip / any port)
S6B = ["rb_depth", "store_size", "sd_flags", "deny", "stage", "dbg_lo", "dbg_n"]   # cfg +0x68
S6C = ["stride", "s6c_flags", "cap_min", "hw_pct", "busy_hi", "busy_lo", "hyst", "budget_pct", "stride_max"]  # +0x84
S6D = ["cost_pct", "ovr_pct", "ovr_every", "cap_hyst", "s6d_flags", "sfx_dump"]  # +0xA8 (S6D)
CFG_LEN = 0xC0


def ip2u(s):
    return struct.unpack(">I", socket.inet_aton(s))[0]


def cfg_unpack(b):
    assert b[:8] == MAGIC, b[:8]
    v = struct.unpack(">14I", b[8:64])
    d = dict(zip(FIELDS, v))
    for i in range(2):
        p = struct.unpack(">4I", b[64 + 16 * i:80 + 16 * i])
        for k, x in zip(PEER, p):
            d["peer%d_%s" % (i, k)] = x
    d.update(zip(ALLOW, struct.unpack(">2I", b[96:104])))
    d.update(zip(S6B, struct.unpack(">7I", b[104:132])))
    d.update(zip(S6C, struct.unpack(">9I", b[132:168])))
    d.update(zip(S6D, struct.unpack(">6I", b[168:192])))
    return d


def cfg_pack(d):
    b = MAGIC + struct.pack(">14I", *[d[k] for k in FIELDS])
    for i in range(2):
        b += struct.pack(">4I", *[d["peer%d_%s" % (i, k)] for k in PEER])
    b += struct.pack(">2I", *[d[k] for k in ALLOW])
    b += struct.pack(">7I", *[d[k] for k in S6B])
    b += struct.pack(">9I", *[d[k] for k in S6C])
    b += struct.pack(">6I", *[d[k] for k in S6D])
    assert len(b) == CFG_LEN
    return b


def cfg_apply(d, kv):
    for item in kv:
        k, v = item.split("=", 1)
        if k not in d:
            raise SystemExit("unknown cfg key " + k)
        d[k] = ip2u(v) if k.endswith("_ip") and "." in v else int(v, 0)
    return d


def xcfg_unpack(b):                                          # S7
    assert b[:8] == XMAGIC, b[:8]
    return dict(zip(XFIELDS, struct.unpack(">8I", b[8:40])))


def xcfg_pack(d, rest):                                      # S7
    b = XMAGIC + struct.pack(">8I", *[d[k] for k in XFIELDS]) + rest
    assert len(b) == XCFG_LEN
    return b


def s10_unpack(b):                                          # S10
    assert b[:8] == S10MAGIC, b[:8]
    return dict(zip(S10FIELDS, struct.unpack(">11I", b[8:52])))


def s10_pack(d, rest):                                       # S10
    b = S10MAGIC + struct.pack(">11I", *[d[k] for k in S10FIELDS]) + rest
    assert len(b) == S10CFG_LEN
    return b


def s12_unpack(b):                                          # S12
    assert b[:8] == S12MAGIC, b[:8]
    return dict(zip(S12FIELDS, struct.unpack(">14I", b[8:64])))


def s12_pack(d):                                             # S12
    b = S12MAGIC + struct.pack(">14I", *[d[k] for k in S12FIELDS])
    assert len(b) == S12CFG_LEN
    return b


def s11_unpack(b):                                          # S11
    assert b[:8] == S11MAGIC, b[:8]
    return dict(zip(S11FIELDS, struct.unpack(">12I", b[8:56])))


def s11_pack(d, rest):                                       # S11
    b = S11MAGIC + struct.pack(">12I", *[d[k] for k in S11FIELDS]) + rest
    assert len(b) == S11CFG_LEN
    return b


def nm(name):
    for line in (OUTB / "s17.nm").read_text().split("\n"):
        p = line.split()
        if len(p) == 3 and p[2] == name:
            return int(p[0], 16)
    raise KeyError(name)


ALLM_SHIFT = 0                                              # S19b: --allm-shift (rig with the online image's in-match memory)
OM_HEAP_HOOK = 0x80015900                                   # lbHeap_80015900 entry (`mflr r0`), the online image's AllM boundary hook


def blob(base):
    env = dict(os.environ)
    if ALLM_SHIFT:
        env["RIG_ALLM_SHIFT"] = "0x%X" % ALLM_SHIFT
    subprocess.check_call([str(HERE / "build_blob.sh"), "0x%08X" % base], stdout=subprocess.DEVNULL, env=env)
    return (OUTB / "s17.bin").read_bytes()


def c2(hook, payload):
    assert len(payload) % 8 == 0
    return struct.pack(">II", 0xC2000000 | (hook & 0x01FFFFFF), len(payload) // 8) + payload


def code04(addr, target, link=False):
    return struct.pack(">II", 0x04000000 | (addr & 0x01FFFFFF),
                       0x48000000 | ((target - addr) & 0x03FFFFFC) | (1 if link else 0))


def build(a):
    global ALLM_SHIFT
    ALLM_SHIFT = int(getattr(a, "allm_shift", "0") or "0", 0)
    out = pathlib.Path(a.out); out.mkdir(parents=True, exist_ok=True)
    work = out / ".work"; work.mkdir(exist_ok=True)
    pbin = pathlib.Path(a.profile_bin) if a.profile_bin else REPO / "build/offline/codeset" / (a.profile + ".bin")
    prof = pbin.read_bytes()
    print("profile %s (%s) sha1 %s (read only)" % (a.profile, pbin, hashlib.sha1(prof).hexdigest()))
    dsha = hashlib.sha1((REPO / "build/offline/codeset/default.bin").read_bytes()).hexdigest()
    print("default.bin sha1 %s (must stay 3b6c8e4d0cf38b944f388cd69ff28f45208cb919)" % dsha)
    if dsha != DEFAULT_SHA1:
        raise SystemExit("default.bin changed")
    dol = RETAIL_DOL.read_bytes()

    def merged(img, base):
        parts = [prof, c2(HOOK, img)]
        if base:
            parts += [code04(EXI_SLOT, nm("s3_exi_entry")), code04(ITER_HOOK, nm("s6n_iter_stub")),
                      code04(END_HOOK, nm("s6n_end_stub"), link=True)]
            parts += [code04(a_, nm(s_)) for a_, s_ in sorted(SDHOOKS.items())]
            parts += [code04(INVOKE_HOOK, nm("s10_invoke"), link=True)]   # S10
            parts += [code04(a_, nm(s_), link=True) for a_, s_ in sorted(S12_BL_HOOKS.items())]   # S12
            parts += [code04(a_, nm(s_)) for a_, s_ in sorted(S12_B_HOOKS.items())]               # S12
            parts += [code04(a_, nm(s_), link=True) for a_, s_ in sorted(PS_BL_HOOKS.items())]    # S19b
            if ALLM_SHIFT:
                parts += [code04(OM_HEAP_HOOK, nm("om_heap_stub"))]
        else:
            parts += [code04(EXI_SLOT, EXI_SLOT + 0x100), code04(ITER_HOOK, ITER_HOOK), code04(END_HOOK, END_HOOK, link=True)]
            parts += [code04(a_, a_) for a_ in sorted(SDHOOKS)]
            parts += [code04(INVOKE_HOOK, INVOKE_HOOK, link=True)]          # S10
            parts += [code04(a_, a_, link=True) for a_ in sorted(S12_BL_HOOKS)]   # S12 (placement pass)
            parts += [code04(a_, a_) for a_ in sorted(S12_B_HOOKS)]
            parts += [code04(a_, a_, link=True) for a_ in sorted(PS_BL_HOOKS)]          # S19b
            if ALLM_SHIFT:
                parts += [code04(OM_HEAP_HOOK, OM_HEAP_HOOK)]
        ts = []
        for i, b in enumerate(parts):
            t = work / ("part%d.bin" % i); t.write_bytes(b); ts.append(str(t))
        o = work / "list.bin"
        merge_codes.main_merge(str(o), ts)
        return o.read_bytes()

    base = 0x804F7380
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
    # default relay/peer table (both sides send to the relay/ "Direct guest-to-guest UDP does not work")
    d = cfg_unpack(img[CFG_OFF:CFG_OFF + CFG_LEN])
    d["peer0_ip"] = d["peer1_ip"] = ip2u(a.relay)
    img = img[:CFG_OFF] + cfg_pack(d) + img[CFG_OFF + CFG_LEN:]
    baked, rep = dol_patch.bake(dol, merged(img, base), dol_patch.DEFAULT_BASE, dol_patch.DEFAULT_MAX_END, dol_patch.RETAIL_SHA1)
    layout_check(img, base)                                    # S6C: fixed blocks must not overlap code
    relax_info = None
    if a.relax_stub:                                           # S17 RELAX: opt-in build (s17r): the stick-compare stub is patched over TriggerSendInput's cmpw
        baked, relax_info = patch_relax(baked, nm("s17_relax_stub"))
    if a.dirty:                                                # S6C: TEST ONLY stale-RAM wrapper (never shipped)
        sys.path.insert(0, str(REPO / "offline/harness/softreset"))
        import dirty_entry
        baked = dirty_entry.add_entry_stub(baked, dirty_entry.PATTERNS[a.dirty])
    dolp = out / (a.name + ".dol"); dolp.write_bytes(baked)
    entry = nm("s3_exi_entry")
    # sanity: the hook words in the patched DOL
    dd = dol_patch.Dol(baked)
    def word(addr):
        for s in dd.loaded():
            if s["addr"] <= addr < s["addr"] + s["size"]:
                o = s["offset"] + addr - s["addr"]
                return struct.unpack(">I", baked[o:o + 4])[0]
    assert word(EXI_SLOT) == 0x48000000 | ((entry - EXI_SLOT) & 0x03FFFFFC), hex(word(EXI_SLOT))
    assert word(HOOK) == 0x48000000 | ((base - HOOK) & 0x03FFFFFC), hex(word(HOOK))
    assert word(ITER_HOOK) == 0x48000000 | ((nm("s6n_iter_stub") - ITER_HOOK) & 0x03FFFFFC)
    assert word(END_HOOK) == 0x48000001 | ((nm("s6n_end_stub") - END_HOOK) & 0x03FFFFFC)
    for a_, s_ in SDHOOKS.items():
        assert word(a_) == 0x48000000 | ((nm(s_) - a_) & 0x03FFFFFC), s_
    assert word(INVOKE_HOOK) == 0x48000001 | ((nm("s10_invoke") - INVOKE_HOOK) & 0x03FFFFFC), "s10_invoke hook"   # S10
    for a_, s_ in S12_BL_HOOKS.items():                                                                             # S12
        assert word(a_) == 0x48000001 | ((nm(s_) - a_) & 0x03FFFFFC), s_
    for a_, s_ in S12_B_HOOKS.items():
        assert word(a_) == 0x48000000 | ((nm(s_) - a_) & 0x03FFFFFC), s_
    for a_, s_ in PS_BL_HOOKS.items():                                                                              # S19b
        assert word(a_) == 0x48000001 | ((nm(s_) - a_) & 0x03FFFFFC), s_
    rd = dol_patch.Dol(dol)                                     # S6D: the displaced instructions are the retail ones
    for a_, w in SFX_RETAIL.items():
        for s in rd.loaded():
            if s["addr"] <= a_ < s["addr"] + s["size"]:
                o = s["offset"] + a_ - s["addr"]
                assert struct.unpack(">I", dol[o:o + 4])[0] == w, hex(a_)
    info = {"allm_shift": "0x%X" % ALLM_SHIFT, "profile": a.profile, "image_base": "0x%08X" % base, "image_size": len(img), "exi_entry": "0x%08X" % entry,
            "cfg_addr": "0x%08X" % (base + CFG_OFF), "status_addr": "0x%08X" % (base + STATUS_OFF),
            "trace_addr": "0x%08X" % (base + TRACE_OFF), "cks_addr": "0x%08X" % (base + CKS_OFF), "rb_addr": "0x%08X" % (base + RB_OFF),
            "tel_addr": "0x%08X" % (base + TEL_OFF), "dtel_addr": "0x%08X" % (base + DTEL_OFF), "code_addr": "0x%08X" % (base + CODE_OFF),
            "xcfg_addr": "0x%08X" % (base + XCFG_OFF), "xs_addr": "0x%08X" % (base + XST_OFF),
            "xcfg": xcfg_unpack(img[XCFG_OFF:XCFG_OFF + XCFG_LEN]),
            "s10cfg_addr": "0x%08X" % (base + S10CFG_OFF), "x10_addr": "0x%08X" % (base + S10TEL_OFF),   # S10
            "s10cfg": s10_unpack(img[S10CFG_OFF:S10CFG_OFF + S10CFG_LEN]), "invoke": "0x%08X" % nm("s10_invoke"),
            "s11cfg_addr": "0x%08X" % (base + S11CFG_OFF), "x11_addr": "0x%08X" % (base + S11TEL_OFF),   # S11
            "s11cfg": s11_unpack(img[S11CFG_OFF:S11CFG_OFF + S11CFG_LEN]),
            "s12cfg_addr": "0x%08X" % (base + S12CFG_OFF), "y12_addr": "0x%08X" % (base + S12TEL_OFF),   # S12
            "x17_addr": "0x%08X" % nm("s17_tel"), "s17_lc_words": {hex(a_): hex(w_) for a_, w_ in S17_RETAIL.items()},   # S17 telemetry (symbol table)
            "s12cfg": s12_unpack(img[S12CFG_OFF:S12CFG_OFF + S12CFG_LEN]),
            "code_probe": [["0x%08X" % (base + o), "0x%08X" % struct.unpack(">I", img[o:o + 4])[0]] for o in code_probes(img, base)],
            "dirty": a.dirty or None, "relax_patch": relax_info,
            "section": rep["section"], "arena": rep["arena"], "dol_sha1": hashlib.sha1(baked).hexdigest(),
            "profile_sha1": hashlib.sha1(prof).hexdigest(), "profile_bin": str(pbin), "default_bin_sha1": dsha, "cfg": d}
    (out / (a.name + ".json")).write_text(json.dumps(info, indent=1))
    print(json.dumps({k: v for k, v in info.items() if k not in ("arena",)}, indent=1))
    return 0


def patch_relax(baked, stub):
    """S17 RELAX: Slippi's TriggerSendInput.asm compares the main + C-stick word with `lwz r3,2(r6); lwz r4,2(r7); cmpw r3,r4; bne ...`
    (third_party/slippi-ssbm-asm Online/Core/TriggerSendInput.asm 'Now do the analog sticks').  Find exactly that sequence in the patched DOL and replace the
    cmpw by `bl stub`: the stub returns cr0 EQ for the pairs the relaxed rule accepts and does the plain compare otherwise."""
    pat = struct.pack(">3I", 0x80660002, 0x80870002, 0x7C032000)
    dd = dol_patch.Dol(baked)
    hits = []
    for sec in dd.loaded():
        blob_ = baked[sec["offset"]:sec["offset"] + sec["size"]]
        i = blob_.find(pat)
        while i >= 0:
            if i % 4 == 0 and (struct.unpack(">I", blob_[i + 12:i + 16])[0] >> 16) == 0x4082:   # followed by bne
                hits.append((sec["addr"] + i + 8, sec["offset"] + i + 8))
            i = blob_.find(pat, i + 1)
    if len(hits) != 1:
        raise SystemExit("relax patch: expected exactly one stick-compare site, found %d" % len(hits))
    addr, off = hits[0]
    word = 0x48000001 | ((stub - addr) & 0x03FFFFFC)
    b = bytearray(baked)
    b[off:off + 4] = struct.pack(">I", word)
    return bytes(b), {"site": "0x%08X" % addr, "stub": "0x%08X" % stub, "word": "0x%08X" % word, "was": "0x7C032000"}


def layout_check(img, base):
    """S6C: no code/data overlap: every fixed data block ends before the next one and before the first code word, and the
    symbols that live in them (nm) are inside their block; the text section starts at CODE_OFF."""
    blocks = [("s3_cfg", CFG_OFF, STATUS_OFF), ("s3_status", STATUS_OFF, TRACE_OFF), ("s6_trace", TRACE_OFF, CKS_OFF),
              ("s3_cks", CKS_OFF, RB_OFF), ("s6_rb", RB_OFF, TEL_OFF), ("s6c_tel", TEL_OFF, DTEL_OFF), ("s6d_tel", DTEL_OFF, XCFG_OFF),
              ("s7_cfg", XCFG_OFF, XST_OFF), ("s7_status", XST_OFF, S10CFG_OFF),   # S7
              ("s10_cfg", S10CFG_OFF, S10TEL_OFF), ("s10_tel", S10TEL_OFF, S11CFG_OFF),   # S10
              ("s11_cfg", S11CFG_OFF, S11TEL_OFF), ("s11_tel", S11TEL_OFF, S12CFG_OFF),   # S11
              ("s12_cfg", S12CFG_OFF, S12TEL_OFF), ("s12_tel", S12TEL_OFF, CODE_OFF)]   # S12
    sizes = {}
    for line in (OUTB / "s17.nm").read_text().split("\n"):
        p = line.split()
        if len(p) == 3:
            sizes[p[2]] = int(p[0], 16)
    out = subprocess.check_output([os.environ.get("PPC_PREFIX", "/opt/devkitpro/devkitPPC/bin/powerpc-eabi") + "-nm", "-S",
                                   str(OUTB / "s17.elf")]).decode()
    sz = {}
    for line in out.split("\n"):
        p = line.split()
        if len(p) == 4:
            sz[p[3]] = (int(p[0], 16), int(p[1], 16))
    for name, lo, hi in blocks:
        a, n = sz[name]
        assert a == base + lo and a + n <= base + hi, "block %s at 0x%X size 0x%X outside [0x%X, 0x%X)" % (name, a, n, base + lo, base + hi)
    assert sizes["__s12_code"] == base + CODE_OFF, hex(sizes["__s12_code"])
    assert struct.unpack(">I", img[TEL_OFF + 63 * 4:TEL_OFF + 64 * 4])[0] == 0x5A5AC0DE, "telemetry canary"
    assert struct.unpack(">I", img[DTEL_OFF + 63 * 4:DTEL_OFF + 64 * 4])[0] == 0x5A5AC0DE, "S6d telemetry canary"
    assert struct.unpack(">I", img[XST_OFF + 47 * 4:XST_OFF + 48 * 4])[0] == 0x5A5AC0DE, "S7 status canary"   # S7
    assert struct.unpack(">I", img[S10TEL_OFF + 63 * 4:S10TEL_OFF + 64 * 4])[0] == 0x5A5AC0DE, "S10 telemetry canary"   # S10
    assert struct.unpack(">I", img[S11TEL_OFF + 63 * 4:S11TEL_OFF + 64 * 4])[0] == 0x5A5AC0DE, "S11 telemetry canary"   # S11
    assert struct.unpack(">I", img[S12TEL_OFF + 127 * 4:S12TEL_OFF + 128 * 4])[0] == 0x5A5AC0DE, "S12 telemetry canary"   # S12
    print("layout ok: data blocks 0x40..0xDC0 disjoint, code from +0xDC0, image 0x%X bytes" % len(img))


def code_probes(img, base):
    """S6C: 8 code words (first code word, function entries of the hot paths) watched at run time: they must never change"""
    syms = {}
    for line in (OUTB / "s17.nm").read_text().split("\n"):
        p = line.split()
        if len(p) == 3 and p[1] in "tT":
            syms[p[2]] = int(p[0], 16)
    offs = [CODE_OFF]
    for n in ("s3_exi_entry", "s6n_iter", "s6n_end", "s3_tick", "sd_capture", "sd_restore", "s10_invoke"):   # S10: 8 words (S12 keeps 8)
        if n in syms:
            offs.append(syms[n] - base)
    return offs


def find_cfg(path, magic=MAGIC):
    """file offset of the cfg block inside an ISO (main.dol located through the disc header) or a raw image/DOL"""
    with open(path, "rb") as f:
        head = f.read(0x440)
        if path.endswith(".iso"):
            dol_off = struct.unpack(">I", head[0x420:0x424])[0]
            f.seek(dol_off); blob_ = f.read(8 << 20); base = dol_off
        else:
            f.seek(0); blob_ = f.read(); base = 0
    i = blob_.find(magic)
    assert i >= 0 and blob_.find(magic, i + 1) < 0, "cfg magic not found exactly once"
    return base + i


def patch(a):
    if os.path.exists(a.out):
        os.unlink(a.out)
    # APFS clone (copy-on-write): costs no space until written
    subprocess.check_call(["cp", "-c", a.base, a.out])
    os.chmod(a.out, 0o644)
    off = find_cfg(a.out)
    xoff = find_cfg(a.out, XMAGIC)                           # S7
    zoff = find_cfg(a.out, S10MAGIC)                         # S10
    yoff = find_cfg(a.out, S11MAGIC)                         # S11
    voff = find_cfg(a.out, S12MAGIC)                         # S12
    with open(a.out, "r+b") as f:
        f.seek(off); d = cfg_unpack(f.read(CFG_LEN))
        f.seek(xoff); xb = f.read(XCFG_LEN); x = xcfg_unpack(xb)
        f.seek(zoff); zb = f.read(S10CFG_LEN); z = s10_unpack(zb)
        f.seek(yoff); yb = f.read(S11CFG_LEN); y = s11_unpack(yb)
        f.seek(voff); v = s12_unpack(f.read(S12CFG_LEN))         # S12
        both = dict(d); both.update(x); both.update(z); both.update(y); both.update(v)
        both = cfg_apply(both, a.kv)
        d = {k: both[k] for k in d}; x = {k: both[k] for k in x}; z = {k: both[k] for k in z}; y = {k: both[k] for k in y}; v = {k: both[k] for k in v}
        f.seek(off); f.write(cfg_pack(d))
        f.seek(xoff); f.write(xcfg_pack(x, xb[40:]))
        f.seek(zoff); f.write(s10_pack(z, zb[52:]))
        f.seek(yoff); f.write(s11_pack(y, yb[56:]))
        f.seek(voff); f.write(s12_pack(v))                       # S12
    print(json.dumps(both))
    return 0


def show(a):
    off = find_cfg(a.path)
    xoff = find_cfg(a.path, XMAGIC)
    zoff = find_cfg(a.path, S10MAGIC)
    with open(a.path, "rb") as f:
        f.seek(off); d = cfg_unpack(f.read(CFG_LEN))
        f.seek(xoff); d.update(xcfg_unpack(f.read(XCFG_LEN)))
        f.seek(zoff); d.update(s10_unpack(f.read(S10CFG_LEN)))
        f.seek(find_cfg(a.path, S11MAGIC)); d.update(s11_unpack(f.read(S11CFG_LEN)))   # S11
        f.seek(find_cfg(a.path, S12MAGIC)); d.update(s12_unpack(f.read(S12CFG_LEN)))   # S12
        print(json.dumps(d, indent=1))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("--profile", default="net-online-spike-boot")
    b.add_argument("--name", default="s17")
    b.add_argument("--profile-bin", default="", help="S6D: bake this saved codeset list (read only) instead of the profile's .bin")
    b.add_argument("--out", default=str(OUTB / "iso"))
    b.add_argument("--relay", default="10.213.101.20")
    b.add_argument("--overlays", default="needed")
    b.add_argument("--no-iso", action="store_true")
    b.add_argument("--relax-stub", action="store_true", help="S17 RELAX: patch bl s17_relax_stub over the stick-compare cmpw of the baked TriggerSendInput (opt-in build; flag s17_flags 0x4000 enables the rule)")
    b.add_argument("--allm-shift", default="0", help="S19b TEST: link the online image's AllM boundary policy (offline/multishine/menus/om/om_heap.c) with this shift, so the rig's online scene has the online image's HSD heap (0x42000 ~ online image 1.02: its +0x5E000 shift minus its 113 KB larger baked section)")
    b.add_argument("--dirty", default="", choices=["", "default", "swiss"], help="TEST ONLY: stale-RAM entry stub")
    p = sp.add_parser("patch")
    p.add_argument("base"); p.add_argument("out"); p.add_argument("kv", nargs="*")
    s = sp.add_parser("show"); s.add_argument("path")
    a = ap.parse_args(argv)
    return {"build": build, "patch": patch, "show": show}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
