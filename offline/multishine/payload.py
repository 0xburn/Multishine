#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Release step of canonical image 1 'Multishine': offline/multishine blob + codeset profile + boot contract -> patcher payload.

  payload.py build [--contract ] [--out DIR] [--base 0x...]
  payload.py show  DIR

Writes a payload directory in the format reads (codes.bin; format "multishine-payload" v1, kind "engine")
plus (the build info the pair rig and `build_s17_net.py patch|show` read). Nothing here needs the retail disc: the patcher
patches the user's own main.dol later.  The steps are those of offline/s17/net/build_s17_net.py `build` (same hook tables, same
placement loop, same config packers, imported from it), with three differences: the image blob comes from offline/multishine
build.py (profile s17-compat, the copies), the boot contract's cfg words are written into the image, and nothing is written outside --out
(default build/offline/multishine/payload-<build id>).  Deterministic: no clock, no host path, no random value goes into the output.
The retail ISO is never opened; default.bin (SHA-1 68725e80...) is checked before and after.  No emulator, no Docker, no window.
"""
import argparse, hashlib, importlib.util, json, os, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import build as MS  # noqa: E402

DEFAULT_CONTRACT = os.path.join(HERE, "contracts", "multishine.json")
OM_HEAP_HOOK = 0x80015900          # online-menu variant: lbHeap_80015900 entry (om_heap_stub), original word `mflr r0`
OM_HEAP_RETAIL = 0x7C0802A6
# menu layout image (offline/multishine/menus/ml): four hooks in Melee's menu code.  (address, stub symbol, link, retail word)
ML_HOOKS = [
    (0x8022DDA8, "ml_enter_stub", False, 0x7C0802A6),   # mnMain_Scene_OnEnter entry, `mflr r0`
    (0x8022DD38, "ml_frame_stub", False, 0x7C0802A6),   # mnMain_Scene_OnFrame entry, `mflr r0`
    (0x8022B7E0, "ml_label_stub", True, 0x48144155),    # the `bl HSD_JObjReqAnim` (0x8036F934) of the row label inside mn_8022B3A0
    (0x8022A014, "ml_label_hover_stub", True, 0x48145921),    # the same call in mn_80229F60 (hover animation), row in r27
    (0x8022A4D4, "ml_label_unhover_stub", True, 0x48145461),  # ... in mn_8022A440 (unhover animation), row in r28
    (0x8022B3A0, "ml_build_stub", False, 0x7C0802A6),   # mn_8022B3A0 entry, `mflr r0`
    (0x8022C010, "ml_light_stub", False, 0x2C030000),   # mn_8022C010 entry, `cmpwi r3,0`
    (0x80229938, "ml_unlock_stub", False, 0x7C0802A6),  # mn_80229938 entry, `mflr r0`
    (0x803A6368, "ml_text_stub", False, 0x7C0802A6),    # HSD_SisLib_803A6368 entry (fill a text with a SIS string), `mflr r0`
]


# card route RC1 (profile s17-compat-onbc,, offline/multishine/menus/df): the disc-file pack hooks on Melee's file funnel.  (address, stub, retail word)
DF_HOOKS = [
    (0x8033796C, "df_path_stub", 0x7C0802A6),   # DVDConvertPathToEntrynum entry, `mflr r0`
    (0x80337C60, "df_open_stub", 0x2C030000),   # DVDFastOpen entry, `cmpwi r3,0` (the size comes from info.length)
    (0x8038F6D4, "df_req_stub", 0x7C0802A6),    # HSD_DevComRequest entry, `mflr r0`
]


# switch tables of the codeset groups that are built at their retail words and switched at run time: (module path prefix, image symbol, max ops, what)
TOGGLE_GROUPS = [
    ("External/Widescreen/", "ml_ws_tab", 16, "widescreen"),            # the Settings > Game row (ml_glue.c settings_apply)
    ("Binary/FasterMeleeSettings/LaglessFod.bin", "ms_fod_tab", 8, "lagless FoD"),   # on in the online scene only (hub_res.c fod_tick); absent in images without the symbol
]


def ws_fill(con, img, base, syms, rep, codeset_manifest):
    """ws_toggle: the Slippi widescreen group (and Lagless FoD,: online only) is patched (net-menu-ms-ws) and switched at run time.
    -> (image with the switch tables filled in, [(address, retail word)] for the patched DOL = default OFF).  Table = {count, count x (address, on word, off word)}
    at the image symbol of the group (TOGGLE_GROUPS); `on` = the word the patch wrote (04 value, or `b body` for a C2 hook), `off` = the retail word."""
    words = []
    for prefix, sym, nmax, what in TOGGLE_GROUPS:
        if sym not in syms and prefix != TOGGLE_GROUPS[0][0]:
            continue                                                    # an image without this table (older engines) keeps the group patched as before
        ops = []
        for m in codeset_manifest["modules"]:
            if m["path"].startswith(prefix):
                for op in m["ops"]:
                    ops.append(op)
        if not ops:
            if prefix != TOGGLE_GROUPS[0][0]:
                continue                                                # a profile without the group (no Lagless FoD): nothing to switch
        if not ops or len(ops) > nmax:
            sys.exit("ws_toggle: %d %s ops (1..%d expected)" % (len(ops), what, nmax))
        tab = [len(ops)]
        for op in ops:
            a, off = int(op["address"], 16), int(op["retail_word"], 16)
            if op["type"] == "04":
                on = int(op["value"], 16)
            else:
                body = [int(c["body"], 16) for c in rep["c2"] if int(c["hook"], 16) == a]
                if len(body) != 1:
                    sys.exit("ws_toggle: no C2 body for hook %08X" % a)
                on = B_enc_b(a, body[0])
            tab += [a, on, off]
            words.append((a, off))
        o = syms[sym] - base
        if o < 0 or img[o:o + 4 * (1 + 3 * nmax)] != bytes(4 * (1 + 3 * nmax)):
            sys.exit("ws_toggle: %s is not a zero table in the image" % sym)
        img = img[:o] + struct.pack(">%dI" % len(tab), *tab) + img[o + 4 * len(tab):]
    return img, words


def B_enc_b(a, body):
    return 0x48000000 | ((body - a) & 0x03FFFFFC)


def sha1b(b):
    return hashlib.sha1(b).hexdigest()


def load_stage_builder():
    spec = importlib.util.spec_from_file_location("build_s17_net_ref", os.path.join(REPO, "offline/s17/net/build_s17_net.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def title_stay_code():
    """contract key 'title_stay': the title screen never times out into the attract loop; one Gecko 04 code
    (0x801A1C60 ble -> b, tools/title_stay_patch.py).  On the card route it becomes a guarded patch of the code file like every other 04 site."""
    sys.path.insert(0, os.path.join(REPO, "tools"))
    import title_stay_patch
    return title_stay_patch.code04(), title_stay_patch


# Build-time patch of Slippi's online screen (TitleMenu/OnMenuPrep.asm, FN_OnlineSubmenuThink, B handler) in the codeset list: B goes to the screen our
# tree puts above it.  Stock: cur_menu 1 (1-P Mode), hover 2, think from the table entry of kind 1; patched: cur_menu 0 (Main Menu), hover 1 (the Slippi Online row),
# think of kind 0.  Three immediates, found by their exact instruction sequences, each of which must occur exactly once in the list.
SLIPPI_BACK_A = bytes.fromhex("38600005" "38000001" "b06db528" "38600003" "889d0000" "989d0001" "981d0000" "38000002" "b01d0002")   # li r0,1 at +4, li r0,2 at +28
SLIPPI_BACK_B = bytes.fromhex("3c60803f" "3863b6b0" "83830024")                                                                  # lwz r28,0x24(r3) at +8


def slippi_back_patch(prof, hover):
    """Return the codeset list with Slippi's online-screen Back handler pointed at the Main Menu row `hover`; raises when the patterns are not unique."""
    b = bytearray(prof)
    if prof.count(SLIPPI_BACK_A) != 1 or prof.count(SLIPPI_BACK_B) != 1:
        raise SystemExit("Slippi's online-screen Back handler was not found exactly once in the codeset list: not patching")
    a = prof.find(SLIPPI_BACK_A)
    c = prof.find(SLIPPI_BACK_B)
    b[a + 4:a + 8] = bytes.fromhex("38000000")                       # li r0,0: cur_menu = MENU_KIND_MAIN
    b[a + 28:a + 32] = bytes.fromhex("380000%02x" % hover)           # li r0,hover
    b[c + 8:c + 12] = bytes.fromhex("83830010")                      # lwz r28,0x10(r3): the think of kind 0
    assert len(b) == len(prof)
    return bytes(b)


# Codes of the codeset list that this image takes out again (04 sites whose retail word is restored): General Codes put "li r0,6" (GM_DEBUG) over the
# "li r0,27" (GM_TOURNAMENT) of the VS. Mode > Tournament Melee row (0x8022D638, :2484-2489), so the row opened the developer debug menu. Found in
# the console image.  The restored word is read from the retail DOL and checked.
# 0x803A69C0: Slippi's ParseNumbersBetter replaces the retail `bne +0x3C` of the SIS text encoder by `b +0x3C`, so digit runs lose their tight-kerning opcode (0A F4 00 00 00)
# and every number Melee draws gets wide spacing: the 1-P CSS HIGH SCORE digits ran over their labels.  Restored here.
RESTORE_RETAIL_04 = {0x8022D638: 0x3800001B, 0x803A69C0: 0x4082003C}


def restore_retail_codes(prof, sites):
    b = bytearray(prof)
    for addr in sites:
        want = RESTORE_RETAIL_04[addr]
        pat = struct.pack(">I", 0x04000000 | (addr & 0x01FFFFFF))
        hits = [i for i in range(0, len(b) - 7, 4) if b[i:i + 4] == pat]
        if not hits and addr == 0x803A69C0:
            continue                                                      # Slippi 3.7.0 (slippi-ssbm-asm d0ce23a) dropped ParseNumbersBetter itself: nothing to take out
        if len(hits) != 1:
            raise SystemExit("04 code for %08X found %d times in the codeset list, expected once" % (addr, len(hits)))
        b[hits[0] + 4:hits[0] + 8] = struct.pack(">I", want)
    return bytes(b)


def to_int(v):
    return int(v, 0) if isinstance(v, str) else int(v)


def load_contract(path=DEFAULT_CONTRACT):
    with open(path, encoding="utf-8") as f:
        c = json.load(f)
    c["cfg"] = {k: to_int(v) for k, v in c["cfg"].items()}
    return c


def sym_table(nm_path):
    syms = {}
    for line in open(nm_path).read().split("\n"):
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    return syms


def apply_cfg(B, img, kv):
    """Same block packers as `build_s17_net.py patch`, on the in-memory image: kv = {key: int}."""
    d = B.cfg_unpack(img[B.CFG_OFF:B.CFG_OFF + B.CFG_LEN])
    xb = img[B.XCFG_OFF:B.XCFG_OFF + B.XCFG_LEN]; x = B.xcfg_unpack(xb)
    zb = img[B.S10CFG_OFF:B.S10CFG_OFF + B.S10CFG_LEN]; z = B.s10_unpack(zb)
    yb = img[B.S11CFG_OFF:B.S11CFG_OFF + B.S11CFG_LEN]; y = B.s11_unpack(yb)
    v = B.s12_unpack(img[B.S12CFG_OFF:B.S12CFG_OFF + B.S12CFG_LEN])
    both = dict(d); both.update(x); both.update(z); both.update(y); both.update(v)
    for k, val in kv.items():
        if k not in both:
            raise SystemExit("unknown cfg key " + k)
        both[k] = val
    d = {k: both[k] for k in d}; x = {k: both[k] for k in x}; z = {k: both[k] for k in z}; y = {k: both[k] for k in y}
    v = {k: both[k] for k in v}
    out = bytearray(img)
    out[B.CFG_OFF:B.CFG_OFF + B.CFG_LEN] = B.cfg_pack(d)
    out[B.XCFG_OFF:B.XCFG_OFF + B.XCFG_LEN] = B.xcfg_pack(x, xb[40:])
    out[B.S10CFG_OFF:B.S10CFG_OFF + B.S10CFG_LEN] = B.s10_pack(z, zb[52:])
    out[B.S11CFG_OFF:B.S11CFG_OFF + B.S11CFG_LEN] = B.s11_pack(y, yb[56:])
    out[B.S12CFG_OFF:B.S12CFG_OFF + B.S12CFG_LEN] = B.s12_pack(v)
    return bytes(out), both


def build(a):
    B = load_stage_builder()
    ms = MS.manifest()
    MS.check_default(ms)
    con = load_contract(a.contract)
    if con["engine_profile"] == "s17-compat-onbc":   # card release size options (manifest options fp / s17x, default off = compiled out): refuse a contract that needs what they remove
        bo = dict(kv.split("=", 1) for kv in os.environ.get("MS_BLOB_OPTS", "").split(",") if "=" in kv)
        s17f = int(str(con["cfg"].get("s17_flags", 0)), 0)
        if bo.get("s17x", "off") != "on" and s17f & ~0x40000007:
            sys.exit("contract s17_flags 0x%X uses engine bits the default build compiles out (G|H|U + SKIPCAP = 0x40000007 only): build with MS_BLOB_OPTS s17x=on" % s17f)
        for k, ok in (("s12_flags", 0x4020304E), ("s10_flags", 0x101), ("s16_flags", 0), ("flags", 0), ("s6d_flags", 0), ("s6c_flags", 2)):
            v = int(str(con["cfg"].get(k, 0)), 0)
            if bo.get("s17x", "off") != "on" and v & ~ok:
                sys.exit("contract %s 0x%X uses bits the default build compiles out (allowed 0x%X): build with MS_BLOB_OPTS s17x=on" % (k, v, ok))
        if bo.get("fp", "off") != "on" and int(str(con["cfg"].get("fp", 0)), 0) & 1 and not s17f & 1:
            sys.exit("contract cfg fp=1 without s17_flags G needs the fingerprint code the default build compiles out: build with MS_BLOB_OPTS fp=on")
    if con["engine_profile"] not in ("s17-compat", "s17-compat-om", "s17-compat-od", "s17-compat-on", "s17-compat-ond", "s17-compat-hil", "s17-compat-hp", "s17-compat-ml", "s17-compat-onb", "s17-compat-onbc"):
        sys.exit("this builder knows the profiles s17-compat, s17-compat-om, s17-compat-od, s17-compat-on, s17-compat-ond, s17-compat-hil (lab only), s17-compat-hp (heap probe, lab only), s17-compat-ml (menu layout) and s17-compat-onb (online + layout + Slippi bridge) only")
    work = tempfile.mkdtemp(prefix="payload-", dir=os.path.join(REPO, ".analysis-deps/work")) if os.path.isdir(
        os.path.join(REPO, ".analysis-deps/work")) else tempfile.mkdtemp(prefix="payload-")
    try:
        pbin = os.path.join(REPO, "build/offline/codeset", con["codeset"] + ".bin")
        # Stale codeset guard: this builder only READS the prebuilt list.  Releases 412..457 carried the 3-slot ms-names C2 body for two days because
        # build/offline/codeset/<profile>.bin (09:22 on 10-14) was older than codeset.py (573c7a51, 22:55: Game Night slot, marker 4): the console header read Unranked Mode
        # and START asked for a plain Unranked ticket.  Rebuild with offline/codeset/build_codeset.sh --profile <codeset>  (MS_ALLOW_STALE_CODESET=1 overrides).
        if not os.environ.get("MS_ALLOW_STALE_CODESET") and os.path.isfile(pbin):
            ins = [os.path.join(REPO, "offline/codeset/lib/codeset.py"), os.path.join(REPO, "offline/codeset/configs/profiles.json")]
            newer = [os.path.relpath(f, REPO) for f in ins if os.path.isfile(f) and os.path.getmtime(f) > os.path.getmtime(pbin) + 1]
            if newer:
                sys.exit("the codeset list %s is older than %s: run  offline/codeset/build_codeset.sh --profile %s  first (MS_ALLOW_STALE_CODESET=1 to override)"
                         % (os.path.relpath(pbin, REPO), ", ".join(newer), con["codeset"]))
        prof = open(pbin, "rb").read()
        prof_file_sha1 = sha1b(prof)                       # the list as built by offline/codeset (before our build-time patch)
        if con.get("restore_retail_04"):
            prof = restore_retail_codes(prof, [int(x, 0) for x in con["restore_retail_04"]])
        if con.get("slippi_online_back") == "main":
            prof = slippi_back_patch(prof, con.get("slippi_online_back_hover", 1))
        om = con["engine_profile"] in ("s17-compat-om", "s17-compat-od", "s17-compat-on", "s17-compat-ond", "s17-compat-ml", "s17-compat-onb", "s17-compat-onbc")
        ml = con["engine_profile"] in ("s17-compat-ml", "s17-compat-onb", "s17-compat-onbc")
        df = con["engine_profile"] == "s17-compat-onbc"
        ps = con["engine_profile"] in ("s17-compat-od", "s17-compat-on", "s17-compat-ond", "s17-compat-onb", "s17-compat-onbc")     # S19b: glue/s17od carries ps_request / ps_done (deterministic Stadium transformations)      # od / on = the online-direct / online variants: same heap hook, other backend
        dol = open(B.RETAIL_DOL, "rb").read()
        if sha1b(dol) != B.dol_patch.RETAIL_SHA1:
            sys.exit("retail main.dol in vendor/ is not the retail 1.02 DOL")

        def merged(img, base, syms):
            parts = [prof, B.c2(B.HOOK, img)]
            if con.get("title_stay"):
                parts += [title_stay_code()[0]]
            if syms:
                parts += [B.code04(B.EXI_SLOT, syms["s3_exi_entry"]), B.code04(B.ITER_HOOK, syms["s6n_iter_stub"]),
                          B.code04(B.END_HOOK, syms["s6n_end_stub"], link=True)]
                parts += [B.code04(a_, syms[s_]) for a_, s_ in sorted(B.SDHOOKS.items())]
                parts += [B.code04(B.INVOKE_HOOK, syms["s10_invoke"], link=True)]
                parts += [B.code04(a_, syms[s_], link=True) for a_, s_ in sorted(B.S12_BL_HOOKS.items())]
                parts += [B.code04(a_, syms[s_]) for a_, s_ in sorted(B.S12_B_HOOKS.items())]
                if ps:
                    parts += [B.code04(a_, syms[s_], link=True) for a_, s_ in sorted(B.PS_BL_HOOKS.items())]
                if om:
                    parts += [B.code04(OM_HEAP_HOOK, syms["om_heap_stub"])]
                if ml:
                    parts += [B.code04(a_, syms[s_], link=l_) for a_, s_, l_, _w in ML_HOOKS]
                if df:
                    parts += [B.code04(a_, syms[s_]) for a_, s_, _w in DF_HOOKS]
            else:      # placement pass (no symbols yet): the same 04 sites pointing at themselves
                parts += [B.code04(B.EXI_SLOT, B.EXI_SLOT + 0x100), B.code04(B.ITER_HOOK, B.ITER_HOOK), B.code04(B.END_HOOK, B.END_HOOK, link=True)]
                parts += [B.code04(a_, a_) for a_ in sorted(B.SDHOOKS)]
                parts += [B.code04(B.INVOKE_HOOK, B.INVOKE_HOOK, link=True)]
                parts += [B.code04(a_, a_, link=True) for a_ in sorted(B.S12_BL_HOOKS)]
                parts += [B.code04(a_, a_) for a_ in sorted(B.S12_B_HOOKS)]
                if ps:
                    parts += [B.code04(a_, a_, link=True) for a_ in sorted(B.PS_BL_HOOKS)]
                if om:
                    parts += [B.code04(OM_HEAP_HOOK, OM_HEAP_HOOK)]
                if ml:
                    parts += [B.code04(a_, a_, link=l_) for a_, s_, l_, _w in ML_HOOKS]
                if df:
                    parts += [B.code04(a_, a_) for a_, s_, _w in DF_HOOKS]
            ts = []
            for i, b in enumerate(parts):
                t = os.path.join(work, "part%d.bin" % i)
                open(t, "wb").write(b); ts.append(t)
            o = os.path.join(work, "list.bin")
            B.merge_codes.main_merge(o, ts)
            return open(o, "rb").read()

        base = int(a.base, 0) if a.base else 0x804F7380
        blobres = None
        for _ in range(5):
            blobres = MS.blob(ms, con["engine_profile"], base=("0x%08X" % base), out=os.path.join(work, "ms"), quiet=True)
            img = open(blobres["bin"], "rb").read()
            _, rep = B.dol_patch.bake(dol, merged(img, base, None), B.dol_patch.DEFAULT_BASE, B.dol_patch.DEFAULT_MAX_END, B.dol_patch.RETAIL_SHA1)
            got = int([c for c in rep["c2"] if c["hook"] == "0x%08X" % B.HOOK][-1]["body"], 16)
            if got == base:
                break
            base = got
        else:
            sys.exit("placement did not converge")
        syms = sym_table(os.path.join(blobres["obj_dir"], ms["profiles"][con["engine_profile"]]["image"] + ".nm"))
        # contract words, then the lab relay placeholder (both peers send to one address, as build_s17_net.py does)
        kv = dict(con["cfg"])
        kv["peer0_ip"] = kv["peer1_ip"] = B.ip2u(con["relay_default"])
        img, both = apply_cfg(B, img, kv)
        codes = merged(img, base, syms)
        baked, rep = B.dol_patch.bake(dol, codes, B.dol_patch.DEFAULT_BASE, B.dol_patch.DEFAULT_MAX_END, B.dol_patch.RETAIL_SHA1)
        dol_words = []
        if con.get("ws_toggle"):          # widescreen group patched in, default OFF: the patched DOL gets the retail words back, the image holds the switch table
            cm = json.load(open(os.path.join(REPO, "build/offline/codeset", con["codeset"] + ".manifest.json")))
            img, dol_words = ws_fill(con, img, base, syms, rep, cm)
            codes = merged(img, base, syms)
            baked, rep2 = B.dol_patch.bake(dol, codes, B.dol_patch.DEFAULT_BASE, B.dol_patch.DEFAULT_MAX_END, B.dol_patch.RETAIL_SHA1)
            if [c["body"] for c in rep2["c2"]] != [c["body"] for c in rep["c2"]]:
                sys.exit("ws_toggle: filling the table moved a code body")
            rep = rep2
            Dtmp, bk = B.dol_patch.Dol(baked), bytearray(baked)
            for a_, w_ in dol_words:
                o_ = Dtmp.file_off(a_, 4)
                bk[o_:o_ + 4] = struct.pack(">I", w_)
            baked = bytes(bk)
        # the displaced instructions of every 04 site are the retail ones, and the hooks point where the symbol table says
        dd = B.dol_patch.Dol(baked)
        rd = B.dol_patch.Dol(dol)

        def word(D, raw, addr):
            for s in D.loaded():
                if s["addr"] <= addr < s["addr"] + s["size"]:
                    o = s["offset"] + addr - s["addr"]
                    return struct.unpack(">I", raw[o:o + 4])[0]
        for a_, w in B.SFX_RETAIL.items():
            assert word(rd, dol, a_) == w, hex(a_)
        if om:
            assert word(rd, dol, OM_HEAP_HOOK) == OM_HEAP_RETAIL, "lbHeap_80015900 does not start with mflr r0"
            assert word(dd, baked, OM_HEAP_HOOK) == 0x48000000 | ((syms["om_heap_stub"] - OM_HEAP_HOOK) & 0x03FFFFFC), "heap hook"
        if ml:
            for a_, s_, l_, w_ in ML_HOOKS:
                assert word(rd, dol, a_) == w_, "menu hook site %08X is not the retail word" % a_
                assert word(dd, baked, a_) == 0x48000000 | ((syms[s_] - a_) & 0x03FFFFFC) | (1 if l_ else 0), s_
        if df:
            for a_, s_, w_ in DF_HOOKS:
                assert word(rd, dol, a_) == w_, "disc-file hook site %08X is not the retail word" % a_
                assert word(dd, baked, a_) == 0x48000000 | ((syms[s_] - a_) & 0x03FFFFFC), s_
        assert word(dd, baked, B.HOOK) == 0x48000000 | ((base - B.HOOK) & 0x03FFFFFC), "image hook"
        if ps:
            for a_, s_ in B.PS_BL_HOOKS.items():
                assert word(dd, baked, a_) == 0x48000001 | ((syms[s_] - a_) & 0x03FFFFFC), s_
        assert word(dd, baked, B.EXI_SLOT) == 0x48000000 | ((syms["s3_exi_entry"] - B.EXI_SLOT) & 0x03FFFFFC), "exi slot"
        if con.get("title_stay"):      # the title never times out: exactly the word of tools/title_stay_patch.py, on the retail context
            tsc, tsm = title_stay_code()
            tsm.check_retail(dol)
            assert word(dd, baked, tsm.ADDR) == tsm.NEW, "title_stay word"
        bid_src = hashlib.sha1()
        for part in (blobres["build_id"].encode(), json.dumps(con["cfg"], sort_keys=True).encode(), con["codeset"].encode(),
                     sha1b(prof).encode(), sha1b(dol).encode(), b"relax" if con.get("relax_stub") else b"norelax") + ((b"noprog",) if con.get("noprog") else ()) + ((b"titlestay",) if con.get("title_stay") else ()):
            bid_src.update(part + b"\n")
        bid = bid_src.hexdigest()[:8]
        title = con["title"].format(id=bid)
        assert len(title) < 64
        out = os.path.abspath(a.out or os.path.join(REPO, "build/offline/multishine", "payload-" + bid))
        if os.path.exists(os.path.join(out, "manifest.json")) and not a.force:
            sys.exit("%s exists (same build id: nothing changed; use --force to rewrite)" % out)
        os.makedirs(out, exist_ok=True)
        hit = [c for c in rep["c2"] if c["hook"] == "0x%08X" % B.HOOK][-1]
        at = codes.find(img)
        assert at >= 0 and codes.find(img, at + 1) < 0, "image not found exactly once in the merged list"
        info = {"profile": con["codeset"], "image_base": "0x%08X" % base, "image_size": len(img), "exi_entry": "0x%08X" % syms["s3_exi_entry"],
                "cfg_addr": "0x%08X" % (base + B.CFG_OFF), "status_addr": "0x%08X" % (base + B.STATUS_OFF),
                "trace_addr": "0x%08X" % (base + B.TRACE_OFF), "cks_addr": "0x%08X" % (base + B.CKS_OFF), "rb_addr": "0x%08X" % (base + B.RB_OFF),
                "tel_addr": "0x%08X" % (base + B.TEL_OFF), "dtel_addr": "0x%08X" % (base + B.DTEL_OFF), "code_addr": "0x%08X" % (base + B.CODE_OFF),
                "xcfg_addr": "0x%08X" % (base + B.XCFG_OFF), "xs_addr": "0x%08X" % (base + B.XST_OFF),
                "xcfg": B.xcfg_unpack(img[B.XCFG_OFF:B.XCFG_OFF + B.XCFG_LEN]),
                "s10cfg_addr": "0x%08X" % (base + B.S10CFG_OFF), "x10_addr": "0x%08X" % (base + B.S10TEL_OFF),
                "s10cfg": B.s10_unpack(img[B.S10CFG_OFF:B.S10CFG_OFF + B.S10CFG_LEN]), "invoke": "0x%08X" % syms["s10_invoke"],
                "s11cfg_addr": "0x%08X" % (base + B.S11CFG_OFF), "x11_addr": "0x%08X" % (base + B.S11TEL_OFF),
                "s11cfg": B.s11_unpack(img[B.S11CFG_OFF:B.S11CFG_OFF + B.S11CFG_LEN]),
                "s12cfg_addr": "0x%08X" % (base + B.S12CFG_OFF), "y12_addr": "0x%08X" % (base + B.S12TEL_OFF),
                "x17_addr": "0x%08X" % syms["s17_tel"], "s17_lc_words": {hex(a_): hex(w_) for a_, w_ in B.S17_RETAIL.items()},
                "s12cfg": B.s12_unpack(img[B.S12CFG_OFF:B.S12CFG_OFF + B.S12CFG_LEN]),
                "dirty": None, "relax_patch": None, "section": rep["section"], "arena": rep["arena"],
                "dol_sha1": sha1b(baked), "profile_sha1": prof_file_sha1, "codeset_patch": ("slippi_online_back" if con.get("slippi_online_back") == "main" else ("restore_retail_04" if con.get("restore_retail_04") else None)), "profile_bin": "build/offline/codeset/%s.bin" % con["codeset"],
                "default_bin_sha1": MS.sha1(MS.DEFAULT_BIN), "cfg": both, "build_id": bid, "title": title, "contract": con["image"]}
        if "s7_setup_info" in syms:    # MATCH SETUP CONFORMANCE: the engine's setup digest words (s7_core.h s7_setup_info[4])
            info["setup_info_addr"] = "0x%08X" % syms["s7_setup_info"]
        if "om_st" in syms:            # online-menu variant: the RAM proof block of offline/multishine/menus/om (struct om_status)
            info["om_status_addr"] = "0x%08X" % syms["om_st"]
        if "om_heap_st" in syms:
            info["om_heap_addr"] = "0x%08X" % syms["om_heap_st"]
        if "ml_st" in syms:             # menu layout variant: the RAM proof block of menus/ml/ml_glue.c (ml_st[16])
            info["ml_status_addr"] = "0x%08X" % syms["ml_st"]
        if "omc_st" in syms:            # direct-code history block of om_card.c (struct omc_status)
            info["omc_status_addr"] = "0x%08X" % syms["omc_st"]
        if "od_st" in syms:             # online-direct variant: od_glue's RAM proof block (struct od_status) and the config block (patched by od_patch.py)
            info["od_status_addr"] = "0x%08X" % syms["od_st"]
            info["od_cfg_addr"] = "0x%08X" % syms["od_cfg"]
            info["od_ctl_addr"] = "0x%08X" % syms["od_o"]
        if "hp_st_w" in syms:           # heap probe variant: the probe's config and status blocks
            info["hp_cfg_addr"] = "0x%08X" % syms["hp_cfg_w"]
            info["hp_status_addr"] = "0x%08X" % syms["hp_st_w"]
        if "on_st" in syms:             # online variant (menus/on): the backend's report block (struct on_status) and the card setup block (struct onc_status)
            info["on_status_addr"] = "0x%08X" % syms["on_st"]
            info["onc_status_addr"] = "0x%08X" % syms["onc_st"]
        if "br_st" in syms:             # online + layout + Slippi bridge (offline/bridge/br_glue.c): the combination's proof words, the bridge's cfg and status blocks
            info["br_status_addr"] = "0x%08X" % syms["br_st"]
            info["sbr_cfg_addr"] = "0x%08X" % syms["sbr_cfg"]
            info["sbr_status_addr"] = "0x%08X" % syms["sbr_status"]
        if "upd_bst" in syms:           # OTA automatic rollback, engine side (menus/upd/upd_host.c upd_bst[8]: 'UBT1', phase, notes, count result, good result, loader status valid, build_seq)
            info["upd_bst_addr"] = "0x%08X" % syms["upd_bst"]
        if "df_st" in syms:             # card route: the disc-file pack's proof words (menus/df/df_glue.c)
            info["df_status_addr"] = "0x%08X" % syms["df_st"]
        if "tt_st" in syms:             # card route: the title text band's proof words (menus/tt/tt_glue.c: 'TTST', state, installs, refusals, why, overlap fix, buffer, image FNV, visits)
            info["tt_status_addr"] = "0x%08X" % syms["tt_st"]
        for _n in ("wa_p", "wa_at", "ops", "guard_fail", "alloc_fail", "refused", "conflicts", "owner"):     # shared CARD work area words (menus/on/on_hist.c, ENGINE_MEMORY_BREAKDOWN 2b)
            if "ms_card_" + _n in syms:
                info["mscw_" + _n + "_addr"] = "0x%08X" % syms["ms_card_" + _n]
        if "bh_st" in syms:             # online variant: hold B 3 s to leave the CSS (menus/on/on_bhold.h struct bh_status)
            info["bh_status_addr"] = "0x%08X" % syms["bh_st"]
        if "tmv_st" in syms:            # the retail Tourney scene as a view of a server bracket (menus/on/tmv.h): request / phase words and the data block
            info["tmv_status_addr"] = "0x%08X" % syms["tmv_st"]
            info["tmv_in_addr"] = "0x%08X" % syms["tmv_in"]
        if "qc_st" in syms:             # qchat=on|demo: the quick chat status words (menus/qc/qc_gc.h QS_*, menus/qc/qc_ramvars.py)
            info["qc_status_addr"] = "0x%08X" % syms["qc_st"]
        if "hub_st" in syms:            # hubovl=ovl: the hub overlay's loader words (menus/hub/hub_ovl.h HB_*)
            info["hub_status_addr"] = "0x%08X" % syms["hub_st"]
        if "gns_st" in syms:            # hubovl=ovl: the Game Night match-time block's loader / uploader words (menus/gns/gns_ovl.h GS_*)
            info["gns_status_addr"] = "0x%08X" % syms["gns_st"]
        if "rep_st" in syms:            # repovl=on: the Slippi report overlay's loader words and the resident report counters (menus/rep/rep_ovl.h RP_*)
            info["rep_status_addr"] = "0x%08X" % syms["rep_st"]
        if "rbr_mm" in syms:            # MM_FTSTATS: the frame period statistics of the last match (s12_gc.c rbr_mm[8])
            info["rbr_mm_addr"] = "0x%08X" % syms["rbr_mm"]
        if "rbr_sp" in syms:            # : the long passes of the last match (s12_gc.c rbr_sp[56])
            info["rbr_sp_addr"] = "0x%08X" % syms["rbr_sp"]
        # code_probe: the 8 watched code words, from the multishine symbol table (build_s17_net.code_probes reads its own OUTB)
        offs = [B.CODE_OFF] + [syms[n] - base for n in ("s3_exi_entry", "s6n_iter", "s6n_end", "s3_tick", "sd_capture", "sd_restore", "s10_invoke") if n in syms]
        info["code_probe"] = [["0x%08X" % (base + o), "0x%08X" % struct.unpack(">I", img[o:o + 4])[0]] for o in offs]
        with open(os.path.join(out, "codes.bin"), "wb") as f:
            f.write(codes)
        man = {"format": "multishine-payload", "version": 1, "kind": "engine", "name": con.get("name", "multishine-image-1"), "stub": False,
               "title": title, "build_id": bid, "notes": "canonical image 1 (docs/CANONICAL_IMAGES.md); built by offline/multishine/payload.py from the s17-compat blob. Contains GPL-3.0-derived machine code (Slippi codeset): not for redistribution.",
               "retail_dol_sha1": B.dol_patch.RETAIL_SHA1, "reserve": 0,
               "dol_words": [["0x%08X" % a_, "0x%08X" % w_] for a_, w_ in dol_words],
               "codes": {"file": "codes.bin", "sha256": hashlib.sha256(codes).hexdigest()},
               "image": {"offset": at, "size": len(img), "base": "0x%08X" % base},
               "table": [], "overlays_needed": ["MnSlMap.dat", "MnSlMap.usd"], "overlays_mode": con.get("overlays", "needed"),
               "expect_dol_sha1": sha1b(baked), "blob_sha1": blobres["sha1"], "blob_build_id": blobres["build_id"],
               "codeset_profile": con["codeset"], "codeset_sha1": prof_file_sha1, "codeset_patch": ("slippi_online_back" if con.get("slippi_online_back") == "main" else ("restore_retail_04" if con.get("restore_retail_04") else None)), "contract": {k: con[k] for k in ("image", "status", "cfg")}}
        with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(man, f, indent=1, sort_keys=True); f.write("\n")
        with open(os.path.join(out, "image.json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(info, f, indent=1, sort_keys=True); f.write("\n")
        MS.check_default(ms)
        print("payload %s\n  build id %s  title %r\n  image base 0x%08X size 0x%X, blob %s\n  DOL (baked from vendor retail) %s\n  contract cfg %s" % (
            out, bid, title, base, len(img), blobres["sha1"], sha1b(baked), json.dumps(con["cfg"], sort_keys=True)))
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


def show(a):
    m = json.load(open(os.path.join(a.dir, "manifest.json")))
    print(json.dumps({k: m[k] for k in ("name", "kind", "title", "build_id", "expect_dol_sha1", "blob_sha1", "codeset_profile", "contract")}, indent=1, sort_keys=True))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("--contract", default=DEFAULT_CONTRACT)
    b.add_argument("--out")
    b.add_argument("--base")
    b.add_argument("--force", action="store_true")
    s = sp.add_parser("show"); s.add_argument("dir")
    a = ap.parse_args(argv)
    return {"build": build, "show": show}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
