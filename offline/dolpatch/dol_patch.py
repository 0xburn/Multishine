#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Statically patch a Slippi-style Gecko code list (04 / C2 only) into main.dol.

The result reproduces what Slippi Dolphin's one-shot code application leaves in
RAM (third_party/slippi-ssbm-asm/Bootloader/main.asm, Callback_ProcessGeckoCode,
driven by Common/Gecko/ProcessCodeList.asm), but with the C2 bodies in a new
DOL text section instead of a runtime heap:

* 04 into a file-backed DOL section  -> word written in place in the DOL file.
* 04 into the header BSS range       -> stored at runtime by an init stub that
  is hooked (C2-style) at 0x80375380 (HSD_OSInit+0x7C, retail
  `bl OSSetArenaLo`), the same point at which Slippi's bootloader runs.  The
  stub replays the original `bl` first, as the bootloader does
  (main.asm "Original codeline").
* C2 -> payload copied verbatim into the new section (32-byte aligned bodies),
  hook = relative `b body` (main.asm HANDLE_C2 step 1), and the payload's final
  word (Gecko terminator slot) replaced by `b hook+4` (HANDLE_C2 step 2, which
  writes at code + 8*lines + 4 regardless of its previous value).
* 06 into a file-backed section is supported (bytes written in place).

OSArenaLo is raised to the 32-aligned end of the new section by patching the
two guarded lis/addi pairs in OSInit.  ArenaHi is left alone.

No cache maintenance is emitted: every patched byte is loaded by the apploader
(dcbi over the destination before each DVD DMA, apploader 0x81200d10) and
__start -> __init_hardware (0x80005230) -> __OSPSInit (0x8000542C) ->
ICFlashInvalidate (0x803436EC) invalidates the whole i-cache before any game
code runs.  The init stub only stores data words into .bss.

Python 3, stdlib only.  Exit status 0 on success, 1 on a PatchError.
"""

import argparse
import hashlib
import json
import struct
import sys

RETAIL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
DEFAULT_BASE = 0x804F0C20
DEFAULT_MAX_END = 0x80700000      # retail apploader section limit
SWISS_MIN_ADDR = 0x80003100       # swiss-gc valid_dol_file
SWISS_MAX_END = 0x81200000
DOL_HDR = 0x100
N_TEXT, N_DATA = 7, 11

# [start, end) ranges a new section must never touch.
FORBIDDEN = [
    (0x80000000, 0x80003100, "low memory / exception vectors / codehandler"),
    (0x804DEC00, 0x804F0C20, "main stack, debugger stack, Slippi r2 globals, 32B gap"),
]

INIT_HOOK = 0x80375380            # HSD_OSInit+0x7C: bl OSSetArenaLo
ARENA_PATCHES = [                 # (addr, guard_lis, guard_addi) in OSInit
    (0x80343094, 0x3C60804F, 0x38630C00),
    (0x803430CC, 0x3C60804F, 0x3863EC00),
]
# Clamp mode (default): the first ArenaLo site is replaced by
#   lis r4,ha(L); addi r4,r4,lo(L); cmplw r3,r4; bge- +8; mr r3,r4
# at 0x8034308C (r3 = OSBootInfo.ArenaLo, r4 is dead), so the value passed to
# OSSetArenaLo is max(BootInfo.ArenaLo, L) whatever the loader left in
# 0x80000030 (0 -> L, exactly as the legacy lis/addi patch).  The second site
# (0x803430CC) only runs when BootInfo.ArenaLo == 0 and stays a lis/addi patch.
ARENA_CLAMP_ADDR = 0x8034308C
ARENA_CLAMP_GUARD = [0x28030000,   # cmplwi r3,0
                     0x40820010,   # bne- 0x803430A0
                     0x3C60804F,   # lis r3,0x804F
                     0x38630C00,   # addi r3,r3,0xC00
                     0x48000004]   # b 0x803430A0
ARENA_MODES = ("clamp", "legacy")

GECKO_HEADER = b"\x00\xd0\xc0\xde\x00\xd0\xc0\xde"
NOP = 0x60000000


class PatchError(Exception):
    pass


def h32(x):
    return "0x%08X" % (x & 0xFFFFFFFF)


def align_up(x, a):
    return (x + a - 1) & ~(a - 1)


# --------------------------------------------------------------------------- #
# PowerPC encoding
# --------------------------------------------------------------------------- #

BRANCH_MIN = -0x2000000
BRANCH_MAX = 0x1FFFFFC


def enc_b(src, dst, link=False):
    """Relative I-form branch from src to dst. Raises if unencodable."""
    if src & 3 or dst & 3:
        raise PatchError("unaligned branch %s -> %s" % (h32(src), h32(dst)))
    off = dst - src
    if off < BRANCH_MIN or off > BRANCH_MAX:
        raise PatchError("branch %s -> %s out of +-32MiB range" % (h32(src), h32(dst)))
    return 0x48000000 | (off & 0x03FFFFFC) | (1 if link else 0)


def dec_b(src, word):
    """Decode a relative/absolute I-form branch: returns (target, link, absolute) or None."""
    if (word >> 26) != 18:
        return None
    off = word & 0x03FFFFFC
    if off & 0x02000000:
        off -= 0x04000000
    aa = bool(word & 2)
    tgt = (off & 0xFFFFFFFF) if aa else (src + off) & 0xFFFFFFFF
    return tgt, bool(word & 1), aa


def ha16(x):
    return ((x >> 16) + ((x >> 15) & 1)) & 0xFFFF


def lo16(x):
    return x & 0xFFFF


def enc_lis(rd, imm):
    return (15 << 26) | (rd << 21) | (imm & 0xFFFF)


def enc_addi(rd, ra, imm):
    return (14 << 26) | (rd << 21) | (ra << 16) | (imm & 0xFFFF)


def enc_ori(ra, rs, imm):
    return (24 << 26) | (rs << 21) | (ra << 16) | (imm & 0xFFFF)


def enc_stw(rs, d, ra):
    return (36 << 26) | (rs << 21) | (ra << 16) | (d & 0xFFFF)


# --------------------------------------------------------------------------- #
# DOL
# --------------------------------------------------------------------------- #

class Dol:
    def __init__(self, data):
        if len(data) < DOL_HDR:
            raise PatchError("DOL too short")
        self.data = bytearray(data)
        w = struct.unpack(">64I", bytes(data[:DOL_HDR]))
        self.sections = []
        for i in range(N_TEXT + N_DATA):
            off, addr, size = w[i], w[18 + i], w[36 + i]
            kind = "T%d" % i if i < N_TEXT else "D%d" % (i - N_TEXT)
            self.sections.append({"slot": i, "name": kind, "offset": off,
                                  "addr": addr, "size": size})
        self.bss_addr, self.bss_size, self.entry = w[54], w[55], w[56]
        for s in self.loaded():
            if s["offset"] + s["size"] > len(data):
                raise PatchError("section %s extends past end of file" % s["name"])

    def loaded(self):
        return [s for s in self.sections if s["size"] and s["offset"]]

    def section_of(self, addr, n=1):
        for s in self.loaded():
            if s["addr"] <= addr and addr + n <= s["addr"] + s["size"]:
                return s
        return None

    def overlaps_section(self, addr, n=1):
        return [s for s in self.loaded()
                if addr < s["addr"] + s["size"] and s["addr"] < addr + n]

    def in_bss(self, addr, n=1):
        return self.bss_addr <= addr and addr + n <= self.bss_addr + self.bss_size

    def file_off(self, addr, n=1):
        s = self.section_of(addr, n)
        if s is None:
            raise PatchError("address %s not file-backed" % h32(addr))
        return s["offset"] + addr - s["addr"]

    def read32(self, addr):
        o = self.file_off(addr, 4)
        return struct.unpack(">I", self.data[o:o + 4])[0]

    def header_bytes(self):
        vals = [0] * 64
        for s in self.sections:
            i = s["slot"]
            vals[i], vals[18 + i], vals[36 + i] = s["offset"], s["addr"], s["size"]
        vals[54], vals[55], vals[56] = self.bss_addr, self.bss_size, self.entry
        return struct.pack(">64I", *vals)


def swiss_valid(dol, file_size):
    """swiss-gc cube/swiss/source/gcm.c:133-172 valid_dol_file, returns list of problems."""
    errs = []
    if file_size % 32 or file_size < DOL_HDR:
        errs.append("file size %d not a multiple of 32" % file_size)
    entry_ok = False
    for s in dol.sections:
        if not s["offset"]:
            continue
        if s["offset"] % 32 or s["offset"] < DOL_HDR or s["offset"] + s["size"] > file_size:
            errs.append("%s file offset/size invalid" % s["name"])
        if s["addr"] % 32 or s["addr"] < SWISS_MIN_ADDR or s["addr"] + s["size"] > SWISS_MAX_END:
            errs.append("%s address invalid (%s)" % (s["name"], h32(s["addr"])))
        if s["slot"] < N_TEXT and s["addr"] <= dol.entry < s["addr"] + s["size"]:
            entry_ok = True
    if dol.bss_size and (dol.bss_addr % 32 or dol.bss_addr < SWISS_MIN_ADDR
                         or dol.bss_addr + dol.bss_size > SWISS_MAX_END):
        errs.append("bss invalid")
    if not entry_ok:
        errs.append("entry point not inside a text section")
    return errs


# --------------------------------------------------------------------------- #
# Gecko code list
# --------------------------------------------------------------------------- #

def parse_gecko(data):
    """Parse a flat Gecko list the way ProcessCodeList.asm walks it.

    Returns (ops, info).  Each op: dict(index, offset, type, addr, ...).
    The codetype byte is masked with 0xFE (ProcessCodeList.asm LOOP_START),
    so 05/C3 are 04/C2 with address bit 24 set.
    """
    if len(data) % 4:
        raise PatchError("code list length %d not a multiple of 4" % len(data))
    pos = 0
    info = {"header": False, "terminated": False, "terminator_offset": None,
            "trailing_bytes": 0, "size": len(data)}
    if data[:8] == GECKO_HEADER:
        info["header"] = True
        pos = 8
    ops = []
    while True:
        if pos == len(data):
            break
        if pos + 8 > len(data):
            raise PatchError("truncated code line at offset 0x%X" % pos)
        w0, w1 = struct.unpack(">II", data[pos:pos + 8])
        if (w0 >> 28) == 0xF and (w0 & 0x00FFFFFF) == 0 and w1 == 0:
            info["terminated"] = True
            info["terminator_offset"] = pos
            info["trailing_bytes"] = len(data) - pos - 8
            break
        ctype = (w0 >> 24) & 0xFE
        addr = (w0 & 0x01FFFFFF) | 0x80000000
        op = {"index": len(ops), "offset": pos, "type": "%02X" % ctype,
              "raw_type": "%02X" % (w0 >> 24), "addr": addr}
        if ctype == 0x04:
            op["value"] = w1
            pos += 8
        elif ctype == 0x06:
            n = w1
            if pos + 8 + n > len(data):
                raise PatchError("06 at offset 0x%X overruns list" % pos)
            op["bytes"] = bytes(data[pos + 8:pos + 8 + n])
            pos += 8 + align_up(n, 8)
        elif ctype == 0xC2:
            n = w1
            if n == 0:
                raise PatchError("C2 at offset 0x%X has zero lines" % pos)
            if pos + 8 + 8 * n > len(data):
                raise PatchError("C2 at offset 0x%X overruns list" % pos)
            op["lines"] = n
            op["payload"] = bytes(data[pos + 8:pos + 8 + 8 * n])
            pos += 8 + 8 * n
        else:
            raise PatchError("unsupported codetype %02X at offset 0x%X (only 04/06/C2)"
                            % (w0 >> 24, pos))
        ops.append(op)
    return ops, info


def origin(op):
    return "op#%d %s @list+0x%X" % (op["index"], op["type"], op["offset"])


# --------------------------------------------------------------------------- #
# Patch
# --------------------------------------------------------------------------- #

def bake(dol_bytes, code_bytes, base=DEFAULT_BASE, max_end=DEFAULT_MAX_END,
         expected_sha1=RETAIL_SHA1, arena_patch=True, arena_mode="clamp", reserve=0):
    """Return (out_dol_bytes, report_dict). Raises PatchError.

    reserve: extra bytes kept free after the new section (not in the DOL file, not cleared by OSInit's
    ClearArena): ArenaLo becomes align32(section end + reserve).  Default 0 = unchanged behaviour."""
    in_sha1 = hashlib.sha1(dol_bytes).hexdigest()
    if expected_sha1 is not None and in_sha1 != expected_sha1.lower():
        raise PatchError("input DOL SHA-1 %s != expected %s" % (in_sha1, expected_sha1))
    dol = Dol(dol_bytes)
    retail = Dol(dol_bytes)
    ops, info = parse_gecko(code_bytes)
    warnings = []
    if not info["terminated"]:
        warnings.append("code list has no F0/FF terminator; end of file used as end of list")
    if info["trailing_bytes"]:
        warnings.append("%d bytes after terminator ignored" % info["trailing_bytes"])

    # ---- sequential application model (later op wins), byte granular ----
    owner = {}       # addr -> (op index, role) for file-backed bytes
    newbyte = {}     # addr -> value (None for C2 hook bytes, filled after placement)
    bss = {}         # word addr -> (value, op index)
    bss_hist = {}
    c2 = {}          # op index -> op

    def claim(addr, values, op, role):
        prev = set()
        for i, v in enumerate(values):
            a = addr + i
            if a in owner and owner[a][0] != op["index"]:
                prev.add(owner[a][0])
            owner[a] = (op["index"], role)
            newbyte[a] = v
        for p in sorted(prev):
            po = ops[p]
            same = False
            if po["type"] == op["type"] == "C2":
                same = po["payload"] == op["payload"]
            elif po["type"] == op["type"] == "04":
                same = po["value"] == op["value"]
            warnings.append("duplicate target %s: %s overrides %s%s"
                            % (h32(addr), origin(op), origin(po),
                               " (identical)" if same else " (DIFFERENT)"))

    for op in ops:
        a = op["addr"]
        if op["type"] in ("04", "C2") and a & 3:
            raise PatchError("%s target %s not word aligned" % (origin(op), h32(a)))
        n = len(op["bytes"]) if op["type"] == "06" else 4
        sec = dol.section_of(a, n)
        if sec is None:
            if op["type"] == "04" and dol.in_bss(a, 4) and not dol.overlaps_section(a, 4):
                if a in bss and bss[a][1] != op["index"]:
                    po = ops[bss[a][1]]
                    warnings.append("duplicate target %s: %s overrides %s%s"
                                    % (h32(a), origin(op), origin(po),
                                       " (identical)" if po["value"] == op["value"]
                                       else " (DIFFERENT)"))
                bss[a] = (op["value"], op["index"])
                bss_hist.setdefault(a, []).append(op["index"])
                continue
            where = "BSS" if dol.in_bss(a, n) else "outside every DOL section and BSS"
            raise PatchError("%s target %s (%d bytes) is %s; unsupported for this codetype"
                            % (origin(op), h32(a), n, where))
        if op["type"] == "04":
            claim(a, struct.pack(">I", op["value"]), op, "04")
        elif op["type"] == "06":
            claim(a, op["bytes"], op, "06")
        else:
            claim(a, [None] * 4, op, "C2")
            c2[op["index"]] = op

    # ---- which C2 hooks survive ----
    live = []
    for idx in sorted(c2):
        op = c2[idx]
        owners = [owner[op["addr"] + i] for i in range(4)]
        n_own = sum(1 for o in owners if o == (idx, "C2"))
        if n_own == 4:
            live.append(op)
        elif n_own == 0:
            warnings.append("%s hook at %s fully overridden; body not placed"
                            % (origin(op), h32(op["addr"])))
        else:
            raise PatchError("%s hook at %s partially overwritten by later op"
                            % (origin(op), h32(op["addr"])))

    # ---- guards ----
    if INIT_HOOK in owner or any(INIT_HOOK + i in owner for i in range(4)):
        if bss:
            raise PatchError("a code writes %s, which the BSS init stub needs as its hook"
                            % h32(INIT_HOOK))
    if arena_mode not in ARENA_MODES:
        raise PatchError("arena_mode must be one of %s" % ", ".join(ARENA_MODES))
    arena_addrs = [a + d for a, _, _ in ARENA_PATCHES for d in range(8)]
    arena_addrs += [ARENA_CLAMP_ADDR + d for d in range(4 * len(ARENA_CLAMP_GUARD))]
    if arena_patch and any(a in owner for a in arena_addrs):
        raise PatchError("a code writes the OSInit ArenaLo immediates")

    # ---- runtime init stub for BSS writes ----
    stub = None
    if bss:
        orig = retail.read32(INIT_HOOK)
        d = dec_b(INIT_HOOK, orig)
        if d is None or not d[1] or d[2]:
            raise PatchError("retail word at %s is %s, expected a relative bl"
                            % (h32(INIT_HOOK), h32(orig)))
        words = [None]  # slot 0: replayed bl, encoded after placement
        stores = []
        for a in sorted(bss):
            v = bss[a][0]
            words += [enc_lis(12, ha16(a)), enc_lis(11, v >> 16),
                      enc_ori(11, 11, v & 0xFFFF), enc_stw(11, lo16(a), 12)]
            stores.append({"address": h32(a), "value": h32(v),
                           "origin": [origin(ops[i]) for i in bss_hist[a]]})
        if len(words) % 2 == 0:
            words.append(NOP)
        words.append(0)  # Gecko terminator slot -> b hook+4
        stub = {"hook": INIT_HOOK, "orig_word": orig, "orig_target": d[0],
                "words": words, "stores": stores}

    # ---- placement ----
    bodies = []
    cur = base
    if base & 31:
        raise PatchError("section base %s not 32-byte aligned" % h32(base))
    for op in live:
        bodies.append({"op": op, "addr": cur, "size": len(op["payload"])})
        cur = align_up(cur + len(op["payload"]), 32)
    if stub:
        stub["addr"] = cur
        stub["size"] = 4 * len(stub["words"])
        cur = align_up(cur + stub["size"], 32)
    sec_size = cur - base
    sec_end = base + sec_size

    section = None
    if sec_size:
        if base < SWISS_MIN_ADDR:
            raise PatchError("section base below 0x80003100")
        if sec_end > max_end:
            raise PatchError("new section end %s exceeds limit %s (%d bytes over)"
                            % (h32(sec_end), h32(max_end), sec_end - max_end))
        if sec_end > SWISS_MAX_END:
            raise PatchError("new section end beyond 0x81200000")
        ov = dol.overlaps_section(base, sec_size)
        if ov:
            raise PatchError("new section overlaps %s" % ", ".join(s["name"] for s in ov))
        if base < dol.bss_addr + dol.bss_size and dol.bss_addr < sec_end:
            raise PatchError("new section overlaps BSS")
        for lo, hi, why in FORBIDDEN:
            if base < hi and lo < sec_end:
                raise PatchError("new section overlaps %s-%s (%s)" % (h32(lo), h32(hi - 1), why))
        slot = None
        for s in dol.sections[:N_TEXT]:
            if s["offset"] == 0 and s["size"] == 0 and s["addr"] == 0:
                slot = s
                break
        if slot is None:
            raise PatchError("no free text section slot in DOL header")
        section = slot

    # ---- materialise ----
    img = bytearray(sec_size)
    c2_report = []
    for b in bodies:
        op = b["op"]
        o = b["addr"] - base
        img[o:o + b["size"]] = op["payload"]
        last_addr = b["addr"] + b["size"] - 4
        orig_last = struct.unpack(">I", op["payload"][-4:])[0]
        back = enc_b(last_addr, op["addr"] + 4)
        img[o + b["size"] - 4:o + b["size"]] = struct.pack(">I", back)
        hook = enc_b(op["addr"], b["addr"])
        for i, v in enumerate(struct.pack(">I", hook)):
            newbyte[op["addr"] + i] = v
        if orig_last != 0:
            warnings.append("%s payload final word is %s, not 00000000; overwritten by "
                            "branch-back as Slippi does" % (origin(op), h32(orig_last)))
        c2_report.append({"origin": origin(op), "hook": h32(op["addr"]),
                          "hook_section": dol.section_of(op["addr"], 4)["name"],
                          "hook_word": h32(hook), "body": h32(b["addr"]),
                          "body_size": b["size"], "lines": op["lines"],
                          "payload_last_word_orig": h32(orig_last),
                          "branch_back": h32(back), "branch_back_at": h32(last_addr)})
    stub_report = None
    if stub:
        w = list(stub["words"])
        w[0] = enc_b(stub["addr"], stub["orig_target"], link=True)
        w[-1] = enc_b(stub["addr"] + stub["size"] - 4, INIT_HOOK + 4)
        o = stub["addr"] - base
        img[o:o + stub["size"]] = struct.pack(">%dI" % len(w), *w)
        hook = enc_b(INIT_HOOK, stub["addr"])
        for i, v in enumerate(struct.pack(">I", hook)):
            owner[INIT_HOOK + i] = (-1, "init-hook")
            newbyte[INIT_HOOK + i] = v
        stub_report = {"hook": h32(INIT_HOOK), "hook_word": h32(hook),
                       "retail_word": h32(stub["orig_word"]),
                       "replayed_call": h32(stub["orig_target"]),
                       "replayed_bl_word": h32(w[0]),
                       "body": h32(stub["addr"]), "body_size": stub["size"],
                       "words": [h32(x) for x in w], "stores": stub["stores"]}

    arena_report = None
    if section is not None and arena_patch:
        if reserve < 0 or reserve % 32:
            raise PatchError("reserve must be a non-negative multiple of 32")
        new_lo = align_up(sec_end + reserve, 32)
        if reserve and new_lo > SWISS_MAX_END:
            raise PatchError("ArenaLo with reserve beyond 0x81200000")
        arena_report = {"new_arena_lo": h32(new_lo), "mode": arena_mode, "patches": []}
        if reserve:
            arena_report["reserve"] = {"start": h32(align_up(sec_end, 32)), "size": reserve}
        n1, n2 = enc_lis(3, ha16(new_lo)), enc_addi(3, 3, lo16(new_lo))
        if arena_mode == "clamp":
            sites = [(ARENA_CLAMP_ADDR, ARENA_CLAMP_GUARD,
                      [enc_lis(4, ha16(new_lo)), enc_addi(4, 4, lo16(new_lo)),
                       0x7C032040,      # cmplw r3,r4
                       0x40800008,      # bge- +8
                       0x7C832378]),    # mr r3,r4
                     (0x803430CC, [0x3C60804F, 0x3863EC00], [n1, n2])]
        else:
            sites = [(a, [g1, g2], [n1, n2]) for a, g1, g2 in ARENA_PATCHES]
        for a, guard, new_words in sites:
            cur = [retail.read32(a + 4 * i) for i in range(len(guard))]
            if cur != guard:
                raise PatchError("ArenaLo guard failed at %s: %s (expected %s)"
                                % (h32(a), " ".join(h32(x) for x in cur),
                                   " ".join(h32(x) for x in guard)))
            for i, v in enumerate(struct.pack(">%dI" % len(new_words), *new_words)):
                owner[a + i] = (-2, "arena")
                newbyte[a + i] = v
            arena_report["patches"].append({"address": h32(a), "old": [h32(x) for x in guard],
                                            "new": [h32(x) for x in new_words]})

    # apply in-place bytes
    for a, v in newbyte.items():
        if v is None:
            raise PatchError("internal: unresolved byte at %s" % h32(a))
        dol.data[dol.file_off(a)] = v

    # per-word write report
    writes = []
    for wa in sorted({a & ~3 for a in newbyte}):
        old = retail.read32(wa)
        new = struct.unpack(">I", dol.data[dol.file_off(wa):dol.file_off(wa) + 4])[0]
        ows = sorted({owner[wa + i] for i in range(4) if wa + i in owner})
        org = []
        for idx, role in ows:
            if idx >= 0:
                org.append(origin(ops[idx]))
            else:
                org.append(role)
        writes.append({"address": h32(wa), "file_offset": "0x%X" % dol.file_off(wa),
                       "section": dol.section_of(wa, 4)["name"], "old": h32(old),
                       "new": h32(new), "kind": "+".join(sorted({r for _, r in ows})),
                       "origin": org, "changed": old != new})

    # ---- new section into the file ----
    out = dol.data
    sec_report = None
    if section is not None:
        off = align_up(len(out), 32)
        out += bytes(off - len(out))
        out += img
        section["offset"], section["addr"], section["size"] = off, base, sec_size
        out[:DOL_HDR] = dol.header_bytes()
        sec_report = {"slot": section["name"], "addr": h32(base), "end": h32(sec_end),
                      "size": sec_size, "file_offset": "0x%X" % off,
                      "headroom_to_limit": max_end - sec_end,
                      "bodies": len(bodies), "init_stub": bool(stub)}
    out = bytes(out)
    if len(out) % 32:
        out += bytes(32 - len(out) % 32)

    # ---- final validation of the output header ----
    chk = Dol(out)
    errs = swiss_valid(chk, len(out))
    if errs:
        raise PatchError("output DOL fails Swiss valid_dol_file: " + "; ".join(errs))

    counts = {}
    for op in ops:
        counts[op["type"]] = counts.get(op["type"], 0) + 1
    report = {
        "tool": "offline/dolpatch/dol_patch.py",
        "input_dol_sha1": in_sha1,
        "input_dol_size": len(dol_bytes),
        "codes_sha1": hashlib.sha1(code_bytes).hexdigest(),
        "codes_size": len(code_bytes),
        "codes_info": info,
        "op_counts": counts,
        "output_dol_sha1": hashlib.sha1(out).hexdigest(),
        "output_dol_size": len(out),
        "section": sec_report,
        "arena": arena_report,
        "c2": c2_report,
        "c2_live": len(live),
        "c2_payload_bytes": sum(b["size"] for b in bodies),
        "bss_init_stub": stub_report,
        "writes": writes,
        "warnings": warnings,
    }
    return out, report


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--dol", required=True, help="retail main.dol")
    ap.add_argument("--codes", required=True, help="flat Gecko list (.bin/.gct)")
    ap.add_argument("--out", required=True, help="patched main.dol to write")
    ap.add_argument("--report", help="JSON build report path")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=DEFAULT_BASE)
    ap.add_argument("--max-end", type=lambda s: int(s, 0), default=DEFAULT_MAX_END)
    ap.add_argument("--expected-sha1", default=RETAIL_SHA1,
                    help="required SHA-1 of --dol ('none' to skip)")
    ap.add_argument("--arena-mode", choices=ARENA_MODES, default="clamp",
                    help="clamp: OSInit sets ArenaLo=max(BootInfo.ArenaLo, section end) "
                         "(default); legacy: only the BootInfo.ArenaLo==0 path is patched "
                         "(unsafe if a loader sets 0x80000030; kept for tests)")
    ap.add_argument("--reserve", type=lambda s: int(s, 0), default=0,
                    help="bytes reserved after the new section (ArenaLo raised over them; default 0)")
    a = ap.parse_args(argv)
    exp = None if a.expected_sha1.lower() == "none" else a.expected_sha1
    try:
        with open(a.dol, "rb") as f:
            dol = f.read()
        with open(a.codes, "rb") as f:
            codes = f.read()
        out, rep = bake(dol, codes, a.base, a.max_end, exp, arena_mode=a.arena_mode, reserve=a.reserve)
    except (PatchError, OSError) as e:
        print("dol_patch: ERROR: %s" % e, file=sys.stderr)
        return 1
    with open(a.out, "wb") as f:
        f.write(out)
    if a.report:
        with open(a.report, "w") as f:
            json.dump(rep, f, indent=1)
    for w in rep["warnings"]:
        print("dol_patch: warning: %s" % w, file=sys.stderr)
    s = rep["section"]
    print("dol_patch: ops=%s c2_live=%d payload=%dB section=%s writes=%d bss_stores=%d "
          "arena_lo=%s out=%s sha1=%s"
          % (rep["op_counts"], rep["c2_live"], rep["c2_payload_bytes"],
             ("%s %s..%s (%d B, headroom %d B)" % (s["slot"], s["addr"], s["end"], s["size"],
                                                   s["headroom_to_limit"])) if s else "none",
             sum(1 for w in rep["writes"] if w["changed"]),
             len(rep["bss_init_stub"]["stores"]) if rep["bss_init_stub"] else 0,
             rep["arena"]["new_arena_lo"] if rep["arena"] else "unchanged",
             a.out, rep["output_dol_sha1"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
