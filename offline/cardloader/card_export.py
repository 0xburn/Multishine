#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""card_export: turn a patcher payload (a Gecko list of 04/06/C2 codes, e.g. offline/multishine/payload.py output codes.bin or a codeset .bin)
into the card code file "MultishineCode" by running the SAME patcher (offline/dolpatch/dol_patch.py) with the new section placed at the card hole.

  card_export.py CODES.bin --retail-dol main.dol|--retail-iso ISO --out code.gci [--hole-base 0x...] [--seq N] [--mtime T] [--report R.json]

What the card route needs from dol_patch (and gets from its report, so semantics are exactly the patched ones):
  * the new text section (C2 bodies + the bss init stub) -> the MSCF image, read to the hole;
  * every changed word of a file-backed section (hooks `b body`, 04 writes) -> the patch table; the INIT hook at HSD_OSInit and the ArenaLo
    clamp words are NOT exported (HSD_OSInit already ran: the loader claims the hole itself);
  * the .bss stores the init stub would do (patch report bss_init_stub.stores) -> patch table entries, applied by the loader after BSS init is long over.
Guard: the CRC of the retail words at the file-backed patch addresses (a different region/version refuses to patch)."""
import argparse, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, "offline", "dolpatch"))
import dol_patch as BD  # noqa: E402
import mscf as M  # noqa: E402
import dol_diff as D  # noqa: E402

# words the exploit's Gecko framing (UCF 0.84: card-unplug guard, no game-data re-init, Tournament Melee loads VS) has already written when our C0 runs: not guarded
PRE_APPLIED = (0x8001C638, 0x8001C63C, 0x801A4234, 0x8022D638)
# words changed on purpose by images already in the field: the same card file must load on them, so they are applied, not part of the version guard
VARIANT_WORDS = (0x801BFA20, 0x801B1360, 0x8022E924)
FROZEN_WORDS = frozenset((0x801AF3A0, 0x801AF3A8, 0x801AF3AC, 0x801AF3B0, 0x801AF3B8, 0x801AFAD4, 0x801AF568, 0x801AFAFC, 0x801A4578, 0x803BAC5C, 0x803AD120, 0x801BFA20))


def disc_variant_words(dol, addrs):
    """-> the subset of `addrs` that earlier images already change (applied, not guarded)."""
    return {a for a in addrs if a in FROZEN_WORDS}


ARENA_SITES = (0x8034308C, 0x80343090, 0x80343094, 0x80343098, 0x8034309C, 0x803430CC, 0x803430D0)
# CONDITIONAL hooks: hook words that are NOT in the code file's static patch table / guard CRC, but applied by the entry stub only if every
# word it names is still RETAIL at that moment.  Nintendont replaces the whole PADControlMotor (0x8034DEBC..0x8034DEDC) with its own 9-word stub, so Slippi's HandleRumble hook (0x8034DED8) must not
# land there (r29 clobber, branch into the gutted function's epilogue) and, being a guarded word, made the loader's guard CRC fail on the Wii.  One code file for every platform: the loader's guard
# covers the other words (a non-retail game is still refused), the stub decides on the spot.  (hook address, extra retail words that must also be present: the function entry and the word the hook returns to)
COND_HOOKS = ((0x8034DED8, (0x8034DEBC, 0x8034DEDC)),)
COND_MAGIC = 0x434F4E44      # 'COND': record header word of the image-resident status block (cond_blob)
COND_APPLIED, COND_SKIPPED = 1, 2


def cond_blob(base_addr, entries):
    """-> bytes of the status record, 32-byte padded: [COND, n, (hook address, status 0/1/2) per entry].  Status: 0 stub not run, 1 hook applied (retail words), 2 SKIPPED (a checked word is not retail, e.g. Nintendont's PADControlMotor stub)."""
    w = [COND_MAGIC, len(entries)]
    for a, _new, _chk in entries:
        w += [a, 0]
    w += [0] * (-len(w) % 8)
    return b"".join(struct.pack(">I", x) for x in w)


def cond_words(rec_addr, entries):
    """entry-stub words (r5-r7 free): per entry: compare every checked word with its retail value; all equal -> store the hook word, cache maintenance (dcbst/sync/icbi/sync/isync), status 1; otherwise nothing is written, status 2."""
    out = []
    for i, (addr, new, chk) in enumerate(entries):
        st = rec_addr + 8 + 8 * i + 4
        tail_apply = _ld32(5, addr) + _ld32(6, new) + [0x90C50000, 0x7C00286C, 0x7C0004AC, 0x7C002FAC, 0x7C0004AC, 0x4C00012C] + _ld32(5, st) + [0x38C00000 | COND_APPLIED, 0x90C50000]
        tail_skip = _ld32(5, st) + [0x38C00000 | COND_SKIPPED, 0x90C50000]
        checks = []
        for a, rw in chk:
            checks += _ld32(5, a) + [0x80C50000] + _ld32(7, rw) + [0x7C063800, None]          # lwz r6,0(r5); cmpw r6,r7; bne skip (patched below)
        n = len(checks) + len(tail_apply) + 1                                                  # + the `b end` after the apply tail
        skip_at = n                                                                            # index of the first skip word
        for k, x in enumerate(checks):
            if x is None:
                checks[k] = 0x40820000 | (4 * (skip_at - k) & 0xFFFC)                          # bne skip
        w = checks + tail_apply + [0x48000000 | (4 * (len(tail_skip) + 1) & 0x03FFFFFC)] + tail_skip
        out += w
    return out


# ---------------------------------------------------------------------------------------------------------------------------------------
# one-shot hooks: boot-time Gecko hooks (e.g. 0x801A45B8 "Slippi Online Scene/main.asm", which registers major scene 8) sit in functions that ran
# long before the loader.  The loader's entry (flag F_ENTRY) runs each such body once, as if the hook had fired: the stub finds the body from the
# patched hook word (`b body`), temporarily turns the word AFTER the hook into `b ret` (the body's last instruction is `b hook+4`), jumps to the
# body, and on `ret` puts the retail word back.  Registers (r3-r31, CR, LR) are saved and restored.  Position independent of the hole base
# (the return branch is computed from the stub's own address, which is known when the stub is generated).
def _lis(rd, v): return (15 << 26) | (rd << 21) | ((v >> 16) & 0xFFFF)
def _ori(ra, v): return (24 << 26) | (ra << 21) | (ra << 16) | (v & 0xFFFF)
def _ld32(r, v): return [_lis(r, v), _ori(r, v)]
def oneshot_stub(base_addr, hooks, goto=None, scratch30=True, pre=(), post=()):
    """-> bytes of the entry stub placed at base_addr (32-byte aligned).  hooks: retail addresses whose patched word is `b body`.  pre: extra words run first (lab trace install);
    post: position-independent words run last, before the epilogue (the exploit-exit redirect, exit_mode_words)."""
    SZ = 0xC0
    def one(h, ret_addr):
        w = []
        w += _ld32(5, h)
        w += [0x80C50000,                                  # lwz r6,0(r5)
              (21 << 26) | (6 << 21) | (6 << 16) | (0 << 11) | (6 << 6) | (29 << 1),   # rlwinm r6,r6,0,6,29
              (21 << 26) | (6 << 21) | (6 << 16) | (6 << 11) | (0 << 6) | (25 << 1),   # slwi r6,r6,6
              (31 << 26) | (6 << 21) | (6 << 16) | (6 << 11) | (824 << 1),             # srawi r6,r6,6
              (31 << 26) | (6 << 21) | (6 << 16) | (5 << 11) | (266 << 1)]             # add r6,r6,r5  -> body
        w += _ld32(7, h + 4)
        w += [0x81070000, 0x910100B8]                      # lwz r8,0(r7); stw r8,0xB8(r1)
        w += _ld32(9, 0x48000000 | ((ret_addr - (h + 4)) & 0x03FFFFFC))
        w += [(36 << 26) | (9 << 21) | (7 << 16),   # stw r9,0(r7)
              0x7C00386C, 0x7C0004AC, 0x7C003FAC, 0x7C0004AC, 0x4C00012C,   # dcbst 0,r7; sync; icbi 0,r7; sync; isync
              0x7CC903A6, 0x4E800420]                      # mtctr r6; bctr
        if scratch30:      # DEFAULT ON since (console-proven): the hook body may end with `stb r3,0(r30)` (Slippi Online Scene: ExitSceneID) and r30 is the loader's slot number (0): a store to address 0 faults on hardware
            w[-2:-2] = [0x3BC100BC]                    # addi r30,r1,0xBC: a free 4-byte slot of the stub's own frame
        return w
    def ret(h):
        w = _ld32(7, h + 4)
        w += [0x810100B8, (36 << 26) | (8 << 21) | (7 << 16),     # lwz r8,0xB8(r1); stw r8,0(r7)
              0x7C00386C, 0x7C0004AC, 0x7C003FAC, 0x7C0004AC, 0x4C00012C]
        return w
    words = [0x9421FF40, 0x7C0802A6, 0x900100C4, 0x7C000026, 0x90010008, 0xBC610010]   # stwu r1,-0xC0(r1); mflr r0; stw r0,0xC4(r1); mfcr r0; stw r0,8(r1); stmw r3,0x10(r1)
    words += list(pre)
    for h in hooks:
        # the hook's return label follows its own call block: size of one(h) is fixed (it does not depend on ret_addr), so a first pass finds it
        n_one = len(one(h, 0)); n_ret = len(ret(h))
        ret_addr = base_addr + 4 * (len(words) + n_one)
        words += one(h, ret_addr)
        words += ret(h)
        if h != hooks[-1]:
            raise SystemExit("only one one-shot hook is supported by this stub (chain them in a later version)")
    if goto:      # (mode, r13 offset, value): gm_ChangeGameModeAfterCurrentScene(mode) (0x801A42F8) after presetting a byte at r13+off (online mode DIRECT = 2 at -0x5060)
        mode, off, val = goto
        if off is not None:
            words += [0x38600000 | (val & 0xFFFF), (38 << 26) | (3 << 21) | (13 << 16) | (off & 0xFFFF)]       # li r3,val; stb r3,off(r13)
        words += [0x38600000 | mode, _lis(12, 0x801A42F8), _ori(12, 0x801A42F8), 0x7D8903A6, 0x4E800421]        # li r3,mode; r12 = fn; mtctr; bctrl
    words += list(post)
    words += [0xB8610010, 0x80010008, 0x7C0FF120, 0x800100C4, 0x7C0803A6, 0x38210000 | SZ, 0x4E800020]   # lmw r3,0x10(r1); lwz r0,8(r1); mtcr r0; lwz r0,0xC4(r1); mtlr r0; addi r1,r1,0xC0; blr
    return b"".join(struct.pack(">I", x) for x in words)


# ---------------------------------------------------------------------------------------------------------------------------------------
# LAB ONLY mode trace (--trace-modes): the entry stub redirects the game-mode setters to small trampolines in the
# image that log (id, r3, LR, routing word 0x80479D30, word 0x80479D3C (pending_mode_change), GM_MENU exit data 0x804D68BC, VI retrace) into a table, then run the
# displaced first instruction and continue.  Dolphin's GDB stub does not stop on breakpoints in the harness, so this is how "who sets the next mode after the
# exploit" was answered.  Table: TRACE_N entries of 32 bytes after a count word (+0); read it with harness watches.  Never in a release card.
TRACE_FUNCS = ((1, 0x801A42E8, 0x3C808048),      # gm_SetPendingGameMode(mode)
               (2, 0x801A42F8, 0x3C808048),      # gm_ChangeGameModeAfterCurrentScene(mode)
               (3, 0x801A42D4, 0x3C608048),      # gm_SetNewGameModePending
               (4, 0x801B138C, 0x7C0802A6),      # GM_MENU state onExit (sets the pending mode from the menu exit data)
               (5, 0x801A4B60, 0x3C608048))      # gm_801A4B60: request the end of the current scene
TRACE_N = 12


def _b(src, dst, link=False): return 0x48000000 | ((dst - src) & 0x03FFFFFC) | (1 if link else 0)


def trace_blob(base_addr, funcs=TRACE_FUNCS):
    """-> (bytes, [(func, tramp_addr)], log_addr).  base_addr 32-byte aligned."""
    log = base_addr
    words = [0] * (8 + 8 * TRACE_N)                      # count word + pad to 32, then the entries
    sites = []
    for fid, f, w0 in funcs:
        t = base_addr + 4 * len(words)
        w = _ld32(12, log)
        w += [0x816C0000,                                 # lwz r11,0(r12)
              0x280B0000 | TRACE_N]                       # cmplwi r11,N
        bge_at = len(w); w += [0]
        w += [0x380B0001, 0x900C0000,                     # addi r0,r11,1; stw r0,0(r12)
              0x556B2834, 0x7D8C5A14, 0x398C0020,         # slwi r11,r11,5; add r12,r12,r11; addi r12,r12,32
              0x39600000 | fid, 0x916C0000,               # li r11,id; stw r11,0(r12)
              0x906C0004,                                 # stw r3,4(r12)
              0x7D6802A6, 0x916C0008,                     # mflr r11; stw r11,8(r12)
              0x3D608048, 0x800B9D30, 0x900C000C,         # lis r11,0x8048; lwz r0,-0x62D0(r11); stw r0,12(r12)
              0x800B9D3C, 0x900C0010,                     # lwz r0,-0x62C4(r11); stw r0,16(r12)
              0x3D60804D, 0x800B68BC, 0x900C0014,         # lis r11,0x804D; lwz r0,0x68BC(r11); stw r0,20(r12)
              0x800B7420, 0x900C0018]                     # lwz r0,0x7420(r11) (VI retrace); stw r0,24(r12)
        done = len(w)
        w[bge_at] = 0x40800000 | (((done - bge_at) * 4) & 0xFFFC)     # bge done
        w += [w0]
        w.append(_b(t + 4 * len(w), f + 4))
        words += w
        sites.append((f, t))
    words += [0] * (-len(words) % 8)
    return b"".join(struct.pack(">I", x) for x in words), sites, log


def _install_words(sites):
    """stub words that turn the first word of each traced function into `b tramp` (with the cache maintenance)."""
    w = []
    for f, t in sites:
        w += _ld32(5, f) + _ld32(6, _b(f, t))
        w += [(36 << 26) | (6 << 21) | (5 << 16), 0x7C00286C, 0x7C0004AC, 0x7C002FAC, 0x7C0004AC, 0x4C00012C]   # stw r6,0(r5); dcbst 0,r5; sync; icbi 0,r5; sync; isync
    return w


# ---------------------------------------------------------------------------------------------------------------------------------------
# Exploit exit mode ("the name exploit... takes you to the title page instead of 4P melee").
# What decides the screen after the exploit (measured with --trace-modes in Dolphin, MMU on): after the codehandler (and so our loader entry) returns, the
# exploit's own loader-2 code in the save calls gm_SetPendingGameMode(2 = GM_VS) (0x801A42E8, from 0x8045D9F8) and gm_801A4B60 (end the current scene,
# from 0x8045DA08).  Then either (a) the GM_MENU state's onExit (0x801B138C) runs and overwrites the pending mode with the menu exit data byte (0x804D68BC),
# or (b) the mode-change flag was already set and no onExit runs (BASE-MIN), so the exploit's 2 is final.  Every path reads one of these two bytes, so the
# entry stub sets both to the wanted mode:
#   1. the menu exit data byte, only while the current mode is GM_MENU (0x80479D30 byte 0 == 1);
#   2. a ONE-SHOT redirect of gm_SetPendingGameMode: its first word becomes `b tramp` (only if it still holds the retail word); the trampoline puts the retail
#      word back, records the caller's r3/LR (the 32 bytes before it, evidence), loads r3 = mode and jumps to the restored function.  The exploit's call is
#      the first one after our entry; after it the game's text is retail again.  A --goto-scene call made by the loader entry itself is overwritten by both
#      paths, which is why release 382 (--goto-scene 1) still ended on the Vs CSS.
SET_PENDING, SET_PENDING_W0, MENU_EXIT_DATA, ROUTING = 0x801A42E8, 0x3C808048, 0x804D68BC, 0x80479D30
GM_TITLE = 0


def exit_redirect_blob(base_addr, mode):
    """-> (bytes, tramp_addr, rec_addr): 32-byte record (r3, LR of the redirected call; zero until it fired) + the trampoline.  base_addr 32-byte aligned."""
    rec, t = base_addr, base_addr + 32
    w = [0] * 8
    w += _ld32(12, SET_PENDING) + _ld32(11, SET_PENDING_W0)
    w += [0x916C0000, 0x7C00606C, 0x7C0004AC, 0x7C0067AC, 0x7C0004AC, 0x4C00012C]   # stw r11,0(r12); dcbst 0,r12; sync; icbi 0,r12; sync; isync
    w += _ld32(11, rec) + [0x906B0000, 0x7C0802A6, 0x900B0004]                     # rec: stw r3,0(r11); mflr r0; stw r0,4(r11)
    w += [0x38600000 | (mode & 0xFFFF), 0x7D8903A6, 0x4E800420]                     # li r3,mode; mtctr r12; bctr  (the retail function returns to the caller)
    w += [0] * (-len(w) % 8)
    return b"".join(struct.pack(">I", x) for x in w), t, rec


def exit_mode_words(mode, tramp):
    """entry-stub words (r5-r7 free: the stub saved r3-r31): menu exit data := mode while in GM_MENU; arm the one-shot redirect if the retail word is there."""
    w = [_lis(5, 0x80480000), 0x88C59D30, 0x2C060001]                                # lis r5,0x8048; lbz r6,-0x62D0(r5) (current mode); cmpwi r6,1
    w += [0x40820010, 0x38C00000 | (mode & 0xFFFF), _lis(5, 0x804D0000), 0x98C568BC]   # bne +16; li r6,mode; lis r5,0x804D; stb r6,0x68BC(r5)
    w += _ld32(5, SET_PENDING) + [0x80C50000] + _ld32(7, SET_PENDING_W0) + [0x7C063800]   # lwz r6,0(r5); cmpw r6,r7
    tail = _ld32(6, _b(SET_PENDING, tramp)) + [0x90C50000, 0x7C00286C, 0x7C0004AC, 0x7C002FAC, 0x7C0004AC, 0x4C00012C]   # stw r6,0(r5); dcbst; sync; icbi; sync; isync
    w += [0x40820000 | ((4 * (len(tail) + 1)) & 0xFFFC)] + tail                     # bne past the tail
    return w


def export(codes, dol, hole_size=None, hole_base=None, oneshot=(), goto=None, scratch30=True, *, restore=(), trace=False, exit_mode=None, cond_hooks=COND_HOOKS):
    """-> dict(image, patches[(addr, word)], guard_words, hole_base, hole_size, report)
    restore: [(addr, retail word)] of the contract's `dol_words` (payload ): sites whose retail word the patched ISO puts BACK after the patch (the RC2 widescreen group, default OFF:
    the engine holds the switch table ml_ws_tab and the Settings > Game row writes the words at run time).  The card route has no patched DOL, so such a site simply gets no patch (the loader leaves the
    retail word); a retail word that differs from the manifest is an error.  Without this the widescreen group was ON from the first frame on the card route."""
    BD.SWISS_MAX_END = 0x81800000                  # the DOL validity checks (apploader limits) do not apply: this section never goes into a DOL file
    # pass 1: size
    out, rep = BD.bake(dol, codes, M.HOLE_MIN, 0x81800000, BD.RETAIL_SHA1, arena_patch=False)
    sz = rep["section"]["size"]
    size = hole_size or ((sz + M.BLOCK - 1) // M.BLOCK * M.BLOCK + M.BLOCK)
    top = M.HOLE_MAX - M.WA_SIZE - M.BLOCK
    base = hole_base or ((top - size) & ~31)
    out, rep = BD.bake(dol, codes, base, 0x81800000, BD.RETAIL_SHA1, arena_patch=False)
    sec = rep["section"]
    d = BD.Dol(out)
    t2 = [s for s in d.sections if s["addr"] == base][0]
    image = bytes(out[t2["offset"]:t2["offset"] + t2["size"]])
    entry = 0
    trace_info = exit_info = None
    # conditional hooks: taken out of the static patch table here (so they are not guarded either), applied by the entry stub only on retail words
    cond = []
    pw = {a: w for a, w in ((int(w["address"], 16), int(w["new"], 16)) for w in rep["writes"] if w["changed"] and "init-hook" not in w["kind"] and "arena" not in w["kind"])}
    for ca, extra in cond_hooks:
        if ca in pw:
            rwc = M.retail_words_from_dol(dol, [ca] + list(extra))
            if len(rwc) != 1 + len(extra):
                raise SystemExit("conditional hook %08X: a checked word is not file-backed" % ca)
            cond.append((ca, pw[ca], [(a, rwc[a]) for a in [ca] + list(extra)]))
    cond_info = None
    if oneshot or goto or trace or exit_mode is not None or cond:
        image = image + b"\0" * (-len(image) % 32)
        pre, post = [], []
        if cond:
            rec = base + len(image)
            blob = cond_blob(rec, cond)
            image += blob
            pre += cond_words(rec, cond)
            cond_info = {"record": "0x%08X" % rec, "hooks": ["0x%08X" % c[0] for c in cond], "checked_words": [["0x%08X" % a for a, _ in c[2]] for c in cond], "status": "record+8+8*i+4: 0 not run, 1 applied, 2 skipped (a checked word is not retail)"}
        if trace:
            funcs = [t for t in TRACE_FUNCS if not (exit_mode is not None and t[1] == SET_PENDING)]     # the exit redirect owns that word (its record logs the call)
            rw = M.retail_words_from_dol(dol, [f for _, f, _ in funcs])
            for _, f, w0 in funcs:
                if rw.get(f) != w0:
                    raise SystemExit("trace site %08X: retail word %s, expected %08X" % (f, rw.get(f), w0))
            blob, sites, log = trace_blob(base + len(image), funcs)
            image += blob
            pre += _install_words(sites)
            trace_info = {"log": "0x%08X" % log, "entries": TRACE_N, "entry_bytes": 32, "ids": {str(i): "%08X" % f for i, f, _ in funcs}}
        if exit_mode is not None:
            rw = M.retail_words_from_dol(dol, [SET_PENDING])
            if rw.get(SET_PENDING) != SET_PENDING_W0:
                raise SystemExit("exit-mode site %08X: retail word %s, expected %08X" % (SET_PENDING, rw.get(SET_PENDING), SET_PENDING_W0))
            blob, tramp, rec = exit_redirect_blob(base + len(image), exit_mode)
            image += blob
            post = exit_mode_words(exit_mode, tramp)
            exit_info = {"mode": exit_mode, "tramp": "0x%08X" % tramp, "record": "0x%08X" % rec, "bytes": len(blob) + 4 * len(post)}
        entry = base + len(image)
        image += oneshot_stub(entry, list(oneshot), goto, scratch30, pre, post)
        if len(image) > size:
            size = (len(image) + M.BLOCK - 1) // M.BLOCK * M.BLOCK
    patches = []
    condset = {c[0] for c in cond}
    for w in rep["writes"]:
        a = int(w["address"], 16)
        if not w["changed"] or "init-hook" in w["kind"] or "arena" in w["kind"] or a in ARENA_SITES or a in condset:
            continue
        patches.append((a, int(w["new"], 16)))
    dropped = []
    if restore:
        rwords = M.retail_words_from_dol(dol, [a for a, _ in restore])
        for a, w in restore:
            if a not in rwords or rwords[a] != w:
                raise SystemExit("restore site %08X: the retail DOL word is %s, the manifest says %08X" % (a, ("%08X" % rwords[a]) if a in rwords else "not file-backed", w))
        ra = {a for a, _ in restore}
        dropped = sorted(a for a, _ in patches if a in ra)
        patches = [p for p in patches if p[0] not in ra]
    guard_addrs = [a for a, _ in patches]
    stores = (rep["bss_init_stub"] or {}).get("stores", [])
    bss = [(int(s["address"], 16), int(s["value"], 16)) for s in stores]
    rw = M.retail_words_from_dol(dol, guard_addrs)
    variant = set(VARIANT_WORDS) | disc_variant_words(dol, guard_addrs)           # every word a frozen disc changes, derived from its patch definition
    filebacked = [p for p in patches if p[0] in rw and p[0] not in PRE_APPLIED and p[0] not in variant]
    other = [p for p in patches if p[0] not in rw or p[0] in PRE_APPLIED or p[0] in variant]      # .bss stores, the pre-applied and the disc-variant words: applied, not guarded
    allp = filebacked + other + bss
    return {"image": image, "entry": entry, "patches": allp, "guard_words": [rw[p[0]] for p in filebacked], "hole_base": base, "hole_size": size,
            "report": {"card_wa": {"addr": "0x%08X" % top, "size": "0x%X" % (M.WA_SIZE + M.BLOCK), "doc": "owned by the engine's CARD layer (ENGINE_MEMORY_BREAKDOWN step 2b); the payload must be built with the same layout (card_payload.py -DMS_CARD_WA_ADDR); the hole ends at or below it"},
                       "restored_sites": ["0x%08X" % x for x in dropped], "cond_hooks": cond_info, "mode_trace": trace_info, "exit_mode": exit_info, "section": sec, "c2": len(rep["c2"]), "bss_stores": len(bss), "file_patches": len(filebacked), "warnings": rep["warnings"],
                       "op_counts": rep["op_counts"]}}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("codes"); ap.add_argument("--retail-dol"); ap.add_argument("--retail-iso"); ap.add_argument("--out", required=True)
    ap.add_argument("--hole-base", type=lambda x: int(x, 0)); ap.add_argument("--hole-size", type=lambda x: int(x, 0))
    ap.add_argument("--oneshot", action="append", type=lambda x: int(x, 0), default=[], help="boot-time hook address whose body the loader entry runs once (e.g. 0x801A45B8)"); ap.add_argument("--goto-scene", help="MODE[:R13OFF:VALUE]: the loader entry also calls gm_ChangeGameModeAfterCurrentScene(MODE) after storing VALUE at r13+R13OFF (e.g. 8:-0x5060:2 = Slippi online major scene, DIRECT mode)"); ap.add_argument("--no-scratch30", action="store_true", help="OLD behaviour: do not point r30 at a scratch slot of the stub's frame before the hook body runs. The Slippi Online Scene body ends with `stb r3,0(r30)` and r30 is the loader's slot number: a store to address 0, which hangs the console (docs/design/CARD_ENTRY_STORE.md). Only for reproducing old cards"); ap.add_argument("--seq", type=int, default=1); ap.add_argument("--mtime", type=int); ap.add_argument("--report")
    ap.add_argument("--exit-mode", type=lambda x: int(x, 0), help="game mode the exploit ends in instead of its own GM_VS (0 = GM_TITLE; CARD_RC1_ROUTE.md section 19): the entry stub sets the GM_MENU exit data and arms a one-shot redirect of the exploit's gm_SetPendingGameMode call")
    ap.add_argument("--trace-modes", action="store_true", help="LAB ONLY: log every call of the game-mode setters after the loader entry into a table in the image (CARD_RC1_ROUTE.md section 19; report key mode_trace)")
    ap.add_argument("--restore-manifest", help="payload manifest.json whose dol_words (retail words put back after the bake: the widescreen group, default OFF) get no patch on the card route")
    ap.add_argument("--compress", action="store_true", help="store the image as an MSLZ stream (MSCF v2, docs/design/CARD_COMPRESSION.md); needs a loader built with CL_COMP=1")
    a = ap.parse_args()
    if a.retail_dol:
        dol = open(a.retail_dol, "rb").read()
    else:
        dol = D.dol_from_iso(a.retail_iso)
    codes = open(a.codes, "rb").read()
    goto = None
    if a.goto_scene:
        q = a.goto_scene.split(":"); goto = (int(q[0], 0), int(q[1], 0) if len(q) > 1 else None, int(q[2], 0) if len(q) > 2 else 0)
    restore = []
    if a.restore_manifest:
        import json as _j
        restore = [(int(x, 16), int(y, 16)) for x, y in (_j.load(open(a.restore_manifest)).get("dol_words") or [])]
    r = export(codes, dol, a.hole_size, a.hole_base, a.oneshot, goto, not a.no_scratch30, restore=restore, trace=a.trace_modes, exit_mode=a.exit_mode)
    gci = M.build_gci(r["image"], r["hole_base"], r["hole_size"], r["patches"], mtime=a.mtime, seq=a.seq, guard_words=r["guard_words"], compress=a.compress, **({"flags": M.F_CLAIM | M.F_ENTRY, "entry": r["entry"]} if r["entry"] else {}))
    open(a.out, "wb").write(gci)
    r["report"]["file"] = {"bytes": len(gci), "blocks": (len(gci) - 0x40) // M.BLOCK, "image": len(r["image"]), "hole": "0x%08X+0x%X" % (r["hole_base"], r["hole_size"]),
                           "patches": len(r["patches"]), "compressed": a.compress}
    if a.report:
        import json
        json.dump(r["report"], open(a.report, "w"), indent=1, default=str)
    print("code file: %d bytes (%d blocks), image %d, hole 0x%08X+0x%X, %d patches (%d guarded, %d bss stores), %d C2 bodies" % (
        len(gci), (len(gci) - 0x40) // M.BLOCK, len(r["image"]), r["hole_base"], r["hole_size"], len(r["patches"]), len(r["guard_words"]), r["report"]["bss_stores"], r["report"]["c2"]))


if __name__ == "__main__":
    main()
