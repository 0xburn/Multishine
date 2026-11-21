# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Static dependency audit for a selected Gecko codeset (adapted from the research script
 / , rewritten).

What counts as a *reference* made by an included code:
  - C2 payloads: absolute addresses built with `lis rX,hi` + `ori/addi rY,rX,lo` (branchl/load
    macros), D-form loads/stores off a lis/ori-built base, absolute branches (AA=1),
    raw payload words that fall inside a forbidden range (data tables / blrl pointers);
  - 04 writes: a written branch instruction (b/bl relative to the target) and a written value
    that looks like a pointer.

FAIL conditions (any => audit fails unless an entry of the allow-list waives that exact
address for that exact module):
  F1  Slippi static-function slot window 0x800055EC-0x800056C0 referenced, but the slot is
      not defined (C2/04 target) by an included code;
  F2  GeckoHeapPtr 0x80005600 referenced (never allowed, never waivable);
  F3  any EXI SDK function referenced (address range from symbols.txt), or patched;
  F4  a hook address of an excluded netplay module referenced (calling it would run the
      retail instruction the excluded module would have replaced / decoding a branch that is
      not there);
  F5  Slippi-dolphin-only memory referenced: codehandler/bootloader area 0x80001800-0x80003000,
      or the r2 region past the end of .sdata2 (0x804DEC00-0x804DFC00, m-ex/recording globals),
      including r2-relative accesses into it;
  F6  two included ops patch the same address with different contents.
WARN conditions (reported, do not fail):
  W1  r13-relative access to an offset that Slippi repurposes (online/recording globals);
  W2  a relative branch in a C2 payload that leaves the payload (or data that decodes as one);
  W3  a 04 write into .bss (cannot be patched into the DOL image; needs a runtime store);
  W4  a raw payload/04 word equal to a forbidden address (lwz encodes as 0x80xxxxxx, so this
      is usually coincidence; listed for manual review).
"""
import re

SLOT_LO, SLOT_HI = 0x800055EC, 0x800056C0
GECKO_HEAP_PTR = 0x80005600
CODEHANDLER = (0x80001800, 0x80003000)
R2_SLIPPI = (0x804DEC00, 0x804DFC00)
# r2 (= _SDA2_BASE_) as set by __init_registers @0x80005340 in retail GALE01 main.dol
R2_VALUE = 0x804DF9E0
SDATA2_END = 0x804DEC00

# r13 offsets that Slippi repurposes (Online/Online.s:12-24, Recording/Recording.s:624-632 of
# slippi-ssbm-asm@fcf47f1). Retail meaning of these slots: mnCharSel / Tournament-mode vars.
R13_SLIPPI = {
    -0x49E4: "OFST_R13_ODB_ADDR", -0x5060: "OFST_R13_ONLINE_MODE", -0x505F: "OFST_R13_APP_STATE",
    -0x505E: "OFST_R13_FORCE_MENU_CLEAR", -0x505D: "OFST_R13_NAME_ENTRY_MODE",
    -0x49EC: "OFST_R13_SWITCH_TO_ONLINE_SUBMENU", -0x5018: "OFST_R13_CALLBACK",
    -0x5038: "OFST_R13_ISPAUSE", -0x5037: "OFST_R13_ISWINNER", -0x5036: "OFST_R13_CHOSESTAGE",
    -0x5035: "OFST_R13_NAME_ENTRY_INDEX_FLAG", -0x5014: "OFST_R13_USE_PREMADE_TEXT",
    -0x5020: "OFST_R13_ISWIDESCREEN", -0x503C: "OFST_R13_SB_ADDR",
    -0x49B4: "primaryDataBuffer", -0x49B0: "bufferOffset", -0x49AC: "frameIndex",
    -0x5040: "playbackDataBuffer",
}

DFORM_LOAD = set(range(32, 36)) | set(range(40, 44)) | {46} | set(range(48, 52))
DFORM_STORE = set(range(36, 40)) | set(range(44, 46)) | {47} | set(range(52, 56))
DFORM = DFORM_LOAD | DFORM_STORE


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


def load_exi_symbols(symbols_txt):
    """EXI SDK function ranges from doldecomp symbols.txt (EXI*, __EXI*)."""
    out = []
    rx = re.compile(r"^(__EXI\w*|EXI\w*) = \.text:0x([0-9A-Fa-f]{8}); // type:function size:0x([0-9A-Fa-f]+)")
    for line in open(symbols_txt):
        m = rx.match(line)
        if m:
            a = int(m.group(2), 16)
            out.append((m.group(1), a, a + int(m.group(3), 16)))
    if not out:
        raise RuntimeError("no EXI symbols found in %s" % symbols_txt)
    return sorted(out, key=lambda x: x[1])


def payload_refs(words):
    """Absolute references made by a relocatable C2 payload. Returns list of
    (kind, value, word_index) with kind in const|mem|absbranch|word|r2mem|r13|relbranch_out."""
    refs = []
    hi = {}    # reg -> upper half from lis
    full = {}  # reg -> full constant built with lis+ori/addi
    n = len(words)
    for i, w in enumerate(words):
        op = w >> 26
        rt = (w >> 21) & 31
        ra = (w >> 16) & 31
        imm = w & 0xFFFF
        if op == 15:  # addis
            full.pop(rt, None)
            if ra == 0:
                hi[rt] = (imm << 16) & 0xFFFFFFFF
            else:
                hi.pop(rt, None)
        elif op == 24:  # ori rA,rS,uimm  (rt field = rS)
            src = rt
            if src in hi:
                v = hi[src] | imm
                refs.append(("const", v, i))
                full[ra] = v
            else:
                full.pop(ra, None)
            hi.pop(ra, None)
        elif op == 14:  # addi rD,rA,simm
            if ra in hi:
                v = (hi[ra] + s16(imm)) & 0xFFFFFFFF
                refs.append(("const", v, i))
                full[rt] = v
            else:
                full.pop(rt, None)
            hi.pop(rt, None)
        elif op in DFORM:
            base = None
            if ra in hi:
                base = hi[ra]
            elif ra in full:
                base = full[ra]
            if base is not None:
                refs.append(("mem", (base + s16(imm)) & 0xFFFFFFFF, i))
            if ra == 2:
                refs.append(("r2mem", (R2_VALUE + s16(imm)) & 0xFFFFFFFF, i))
            if ra == 13 and s16(imm) in R13_SLIPPI:
                refs.append(("r13", s16(imm), i))
            if op in DFORM_LOAD and op not in (46,):
                hi.pop(rt, None)
                full.pop(rt, None)
        elif op == 18:  # b/bl
            li = w & 0x03FFFFFC
            if li & 0x02000000:
                li -= 0x04000000
            if w & 2:
                refs.append(("absbranch", li & 0xFFFFFFFF, i))
            else:
                tgt = i + li // 4
                if not (0 <= tgt <= n):
                    refs.append(("relbranch_out", li, i))
        elif op == 16:  # bc
            bd = w & 0xFFFC
            if bd & 0x8000:
                bd -= 0x10000
            if w & 2:
                refs.append(("absbranch", bd & 0xFFFFFFFF, i))
            else:
                tgt = i + bd // 4
                if not (0 <= tgt <= n):
                    refs.append(("relbranch_out", bd, i))
        # raw words (data tables, blrl pointer tables)
        if 0x80000000 <= w < 0x81800000:
            refs.append(("word", w, i))
    return refs


def op_refs(op):
    if op["type"] == "C2":
        return payload_refs(op["payload"])
    if op["type"] == "04":
        v = op["value"]
        out = []
        if (v >> 26) == 18:
            li = v & 0x03FFFFFC
            if li & 0x02000000:
                li -= 0x04000000
            tgt = (li if v & 2 else op["address"] + li) & 0xFFFFFFFF
            out.append(("branch04", tgt, 0))
        if 0x80000000 <= v < 0x81800000:
            out.append(("word", v, 0))
        return out
    return []


def audit(included, excluded, exi_syms, allow=None, dol=None):
    """included / excluded: lists of module dicts {path, section, ops:[...]}.
    allow: list of {module, address, reason} waivers (F1/F3/F4/F5 only).
    Returns report dict with 'fail' list, 'warn' list and 'info'."""
    allow = allow or []
    fails, warns = [], []
    defined_slots = {}
    inc_targets = {}
    for m in included:
        for op in m["ops"]:
            inc_targets.setdefault(op["address"], []).append((m["path"], op))
            if SLOT_LO <= op["address"] < SLOT_HI:
                defined_slots[op["address"]] = m["path"]
    excl_hooks = {}
    for m in excluded:
        for op in m["ops"]:
            if op["address"] not in inc_targets:
                excl_hooks.setdefault(op["address"], []).append(m["path"])

    def exi_hit(a):
        for name, lo, hi_ in exi_syms:
            if lo <= a < hi_:
                return name
        return None

    def waived(path, addr):
        for w in allow:
            if w["module"] == path and int(w["address"], 16) == addr:
                return w
        return None

    used_waivers = set()
    slot_refs = []
    hook_refs = []

    def fail(code, m, addr, why, kind):
        w = waived(m["path"], addr) if code != "F2" else None
        rec = {"check": code, "module": m["path"], "address": "0x%08X" % addr, "kind": kind, "detail": why}
        if w:
            rec["waived"] = w["reason"]
            used_waivers.add((w["module"], w["address"].lower()))
            warns.append(rec)
        else:
            fails.append(rec)

    for m in included:
        for op in m["ops"]:
            a = op["address"]
            name = exi_hit(a)
            if name:
                fail("F3", m, a, "op patches EXI SDK function %s" % name, "target")
            if CODEHANDLER[0] <= a < CODEHANDLER[1]:
                fail("F5", m, a, "op patches the codehandler/bootloader area", "target")
            if dol is not None and op["type"] == "04" and dol.section_of(a) == "BSS":
                warns.append({"check": "W3", "module": m["path"], "address": "0x%08X" % a,
                              "detail": "04 write into .bss: needs a runtime store after BSS init"})
            seen = set()
            for kind, v, idx in op_refs(op):
                key = (kind, v)
                if key in seen:
                    continue
                seen.add(key)
                if kind == "relbranch_out":
                    warns.append({"check": "W2", "module": m["path"], "address": "0x%08X" % a,
                                  "detail": "payload word %d: relative branch/data leaving payload (offset %d)" % (idx, v)})
                    continue
                if kind == "r13":
                    warns.append({"check": "W1", "module": m["path"], "address": "0x%08X" % a,
                                  "detail": "payload word %d: r13%+#x (%s, Slippi-repurposed slot)" % (idx, v, R13_SLIPPI[v])})
                    continue
                if kind == "r2mem":
                    if v >= SDATA2_END and v < R2_SLIPPI[1]:
                        fail("F5", m, v, "r2-relative access past .sdata2 end (Slippi/m-ex globals)", kind)
                    continue
                if kind == "word":
                    # A raw word in the forbidden ranges. lwz rN,d(rA) encodes as 0x80..-0x83..,
                    # so this is usually a coincidental instruction encoding: warn only.
                    why = None
                    if v == GECKO_HEAP_PTR or SLOT_LO <= v < SLOT_HI:
                        why = "static slot window"
                    elif exi_hit(v):
                        why = "EXI SDK function %s" % exi_hit(v)
                    elif v in excl_hooks:
                        why = "hook of excluded %s" % excl_hooks[v]
                    elif CODEHANDLER[0] <= v < CODEHANDLER[1] or R2_SLIPPI[0] <= v < R2_SLIPPI[1]:
                        why = "Slippi-Dolphin-only memory"
                    if why:
                        warns.append({"check": "W4", "module": m["path"], "address": "0x%08X" % a,
                                      "detail": "raw word %d = 0x%08X matches %s (pointer or coincidental encoding)" % (idx, v, why)})
                    continue
                if v == GECKO_HEAP_PTR:
                    fail("F2", m, v, "references GeckoHeapPtr (bootloader-only)", kind)
                    continue
                if SLOT_LO <= v < SLOT_HI:
                    slot_refs.append({"module": m["path"], "slot": "0x%08X" % v, "kind": kind,
                                      "defined_by": defined_slots.get(v)})
                    if v not in defined_slots:
                        fail("F1", m, v, "static slot not defined by any included code", kind)
                    continue
                name = exi_hit(v)
                if name:
                    fail("F3", m, v, "references EXI SDK function %s" % name, kind)
                    continue
                if CODEHANDLER[0] <= v < CODEHANDLER[1] or R2_SLIPPI[0] <= v < R2_SLIPPI[1]:
                    fail("F5", m, v, "references Slippi-Dolphin-only memory", kind)
                    continue
                if v in excl_hooks:
                    fail("F4", m, v, "references hook address of excluded module(s) %s" % excl_hooks[v], kind)
                    continue
                if v in inc_targets and kind in ("const", "mem", "absbranch", "branch04"):
                    others = sorted({p for p, _ in inc_targets[v] if p != m["path"]})
                    if others:
                        hook_refs.append({"module": m["path"], "address": "0x%08X" % v, "kind": kind,
                                          "hooked_by": others,
                                          "hook_type": sorted({o["type"] for _, o in inc_targets[v]})})

    # F6: conflicting writes among included ops
    conflicts = []
    for a, lst in sorted(inc_targets.items()):
        if len(lst) > 1:
            bodies = {(o["type"], o.get("value"), tuple(o.get("payload", ()))) for _, o in lst}
            rec = {"address": "0x%08X" % a, "modules": [p for p, _ in lst], "identical": len(bodies) == 1}
            conflicts.append(rec)
            if len(bodies) > 1:
                fails.append({"check": "F6", "module": ", ".join(p for p, _ in lst), "address": "0x%08X" % a,
                              "kind": "target", "detail": "different contents written to the same address"})

    unused = [w for w in allow if (w["module"], w["address"].lower()) not in used_waivers]
    return {
        "result": "FAIL" if fails else "PASS",
        "fail": fails,
        "warn": warns,
        "info": {
            "defined_slots": {"0x%08X" % k: v for k, v in sorted(defined_slots.items())},
            "slot_references": slot_refs,
            "references_to_included_hooks": hook_refs,
            "duplicate_targets": conflicts,
            "excluded_hook_addresses": len(excl_hooks),
            "exi_symbols": [{"name": n, "start": "0x%08X" % lo, "end": "0x%08X" % hi_} for n, lo, hi_ in exi_syms],
            "unused_waivers": unused,
        },
    }
