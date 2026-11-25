#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Offline Slippi codeset builder (driver behind offline/codeset/build_codeset.sh).

Pipeline (all out of tree, under build/offline/codeset/):
  1. copy third_party/slippi-ssbm-asm (no .git/Output) + offline/codeset/asm (as Offline/) to work/asm
  2. self-check: `gecko build -batched -defsym STG_EXIIndex=1 -c must reproduce the
     shipped Slippi 3.6.4 GALE01r2.ini (SHA-1 f0ece3c9...) byte for byte
  3. universe: the same with path-tagged annotations -> every netplay module and its ops
     (checked byte-identical to step 2)
  4. resolve the profile (configs/profiles.json) against the decision table (configs/modules.json),
     which must cover every universe module exactly
  5. compose a gecko config (one `inject`/`binary` entry per module), build .txt/.bin/.gct
  6. provenance: every non-Offline/ module's words must equal its words in the universe build
  7. manifest JSON + static dependency audit (lib/gecko_audit.py); audit FAIL => build fails
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geckolib as gl  # noqa: E402
import gecko_audit  # noqa: E402

STOCK_INI_SHA1 = "7a8f410f04f30813e00210383bc0762dc11f517a"   # Slippi Dolphin 3.7.0 Sys/GameSettings/GALE01r2.ini (3.6.4: f0ece3c9...)
RETAIL_DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"  # GALE01 rev2 main.dol
# retail words under the six card-silent-create 04 writes (asm/Card): gm_801AF250 case 4 + memcard scene state 7
CARD_SILENT_RETAIL = {0x801AF3A0: 0x80BE007C, 0x801AF3A8: 0x80BE0020, 0x801AF3AC: 0x38600001,
                      0x801AF3B0: 0x3880000A, 0x801AF3B8: 0x38000005, 0x801AFAD4: 0x41820014}
# retail words under the own training C2 hooks (asm/Training): bl ftCo_LandingAir_EnterWithMsidLag; lbz r0,0x21FC(r3);
TRAINING_RETAIL = {0x8008D6EC: 0x4800001D, 0x800805F0: 0x880321FC,
                   0x800693AC: 0x7C0802A6, 0x80175D14: 0x80010034,   # + tech card: mflr r0; lwz r0,0x34(r1);
                   0x801A5C14: 0x7F63DB78}                            # mr r3,r27
# retail word under the card-route start combo hook (asm/CardStart): mnMain_Scene_OnFrame entry, mflr r0
CARDSTART_RETAIL = {0x8022DD38: 0x7C0802A6}
ASM_COMMIT = "d0ce23a"   # slippi-ssbm-asm as shipped in Slippi 3.7.0 (3.6.4: fcf47f1)
# the checkout: third_party/slippi-ssbm-asm, or MS_ASM_SRC (a separate clone at ASM_COMMIT while the submodule of the main tree still sits at the old revision)
ASM_SRC = os.environ.get("MS_ASM_SRC") or None
GECKO_FLAGS = ["-batched", "-defsym", "STG_EXIIndex=1"]   # exactly as slippi-ssbm-asm Makefile `ini`
GENERATED_DELAY = "Offline/Generated/InitializeInGameDelayConst.asm"
DELAY_SRC = "Common/UseInGameDelay/InitializeInGameDelay.asm"
# flag fp-align (S8): Slippi's user display keeps two floats at LDB+0xA/+0xE (2 mod 4) in a 32-byte aligned
# HSD_MemAlloc buffer and accesses them with lfs/stfs.  The PowerPC 750 raises an alignment exception for a floating-point
# load/store that is not word aligned (Dolphin's interpreter models this; its JIT does not, so Slippi Dolphin never sees it).
# The variant only re-packs the private LDB layout (same instructions, same hooks)/.
GENERATED_FPALIGN = "Offline/Generated/UserDisplayFunctionsFpAlign.asm"
FPALIGN_SRC = "Online/Static/UserDisplayFunctions.asm"
FPALIGN_OLD = [".set LDB_TEXT_STRUCT_ADDR, LDB_PREV_STATE + 1 # u32",
               ".set LDB_DISPLAY_MODE, LDB_TEXT_STRUCT_ADDR + 4 # u8",
               ".set LDB_X_POS, LDB_DISPLAY_MODE + 1 # float"]
FPALIGN_NEW = [".set LDB_TEXT_STRUCT_ADDR, LDB_PREV_STATE + 4 # u32 (Multishine fp-align: was +1, now word aligned)",
               ".set LDB_DISPLAY_MODE, LDB_TEXT_STRUCT_ADDR + 4 # u8",
               ".set LDB_X_POS, LDB_DISPLAY_MODE + 4 # float (Multishine fp-align: was +1, now word aligned)"]

# More layout variants of the same flag (found by scanning every .float / lfs data block of the pinned Slippi tree the
# Teams row crash on a GameCube): a one-byte field in front of floats puts them at 1 mod 4 (InitTeamToggleButton: Teams row, entering the
# online CSS in Teams mode), and a 13 byte string in front of the next text entry puts three floats at 1 mod 4 (StartEngineLoop: the
# "DESYNC DETECTED" / "Desync Risk" text of a match).  Same instructions, same hooks, same module size (the data block ends with .align 2).
# Each edit replaces exactly one source line (stripped match, must occur exactly once).
FPALIGN_EXTRA = [
    {"src": "Online/Menus/CSS/Teams/InitTeamToggleButton.asm", "generated": "Offline/Generated/InitTeamToggleButtonFpAlign.asm",
     "edits": [(".byte 1 # Will be used by SceneLoadCSS.asm", ".byte 1 # Will be used by SceneLoadCSS.asm\n.byte 0, 0, 0 # (Multishine fp-align: floats word aligned)"),
               (".set TPO_BOUNDS_ICON_TOP, EXT_TEAM_IDX + 1", ".set TPO_BOUNDS_ICON_TOP, EXT_TEAM_IDX + 4 # (Multishine fp-align: was + 1)")],
     "layout": "team idx byte at 0, floats from 4 (were from 1)"},
    {"src": "Online/Core/StartEngineLoop.asm", "generated": "Offline/Generated/StartEngineLoopFpAlign.asm",
     "edits": [('.string "DISCONNECTED" # text', '.string "DISCONNECTED" # text\n.byte 0, 0, 0 # (Multishine fp-align: next entry word aligned)'),
               (".set DOFST_DISCONNECT_TEXT_ENTRY_SIZE, 16 + 13", ".set DOFST_DISCONNECT_TEXT_ENTRY_SIZE, 16 + 16 # (Multishine fp-align: was 16 + 13)")],
     "layout": "text entries at 0, 32, 64 (were 0, 29, 61)"},
    # Slippi 3.7.0 renamed the online character screen's data file (slpCSS.dat -> SlippiCSS_gui.dat, a file Dolphin builds by resolving links).  Our pack, the disc
    # gates and the seed tables carry the file under its old name with the same symbol (slpCSS); the variant keeps the old name in the same 18 bytes.
    {"src": "Online/Menus/CSS/SceneLoadCSS.asm", "generated": "Offline/Generated/SceneLoadCSSOldName.asm",
     "edits": [('.string "SlippiCSS_gui.dat"', '.string "slpCSS.dat"\n.byte 0, 0, 0, 0, 0, 0, 0 # (Multishine: the 3.6.4 file name in the room of the 3.7.0 one)')],
     "layout": "file name slpCSS.dat, padded to the 18 bytes of SlippiCSS_gui.dat"},
]

# flag ms-names: the online character select header word ("Unranked Mode" / "Direct Mode" / "Ranked Mode") comes from
# Slippi's packed string table in the CSS text module (three `addi r6, REG_TEXT_PROPERTIES, TPO_STRING_*` sites).  The Multishine flows (hub rows) must read Dojo Mode /
# Mano a mano Mode / Grandmaster Mode, Slippi's own flow (the bridge) must stay byte for byte as it is.  The variant adds (1) a body header: `b START`, a `blrl` (body+4), a
# marker word (body+8) that the engine (ml_glue.c) writes when a hub row starts an online mode (0 = Slippi's flow, 1 Grandmaster, 2 Dojo, 3 Mano a mano) and the three 12 byte
# names (body+12), (2) a 13 instruction helper MS_NM_PICK that points r6 at one of the names when the marker is set and does nothing otherwise, (3) one `bl MS_NM_PICK` after
# each of the three original `addi r6` (Teams and Party are not touched).  Same hook, same op count.  Labels are never subtracted (the assembler cannot resolve that here):
# the helper finds the header the way Slippi's own text table is found, `bl` to a `blrl` (LR = the marker word).
GENERATED_NAMES = "Offline/Generated/LoadCSSTextNames.asm"
NAMES_SRC = "Online/Menus/CSS/LoadCSSText.asm"
NAMES_BLRL = 0x4E800021          # body+4
NAMES_GRAN = 0x4772616E          # body+12: 'Gran' of "Grandmaster"
NAMES_HEAD = """# --- Multishine names (flag ms-names): body header, engine marker, names, header-word helper ---
b MS_NM_START
MS_NM_DATA:
blrl # body+4: `bl MS_NM_DATA` comes straight back with LR = MS_NM_FLAG
MS_NM_FLAG:
.long 0 # body+8, marker in the low byte (written by the engine): 0 = Slippi's own flow, 1 = Grandmaster, 2 = Dojo, 3 = Mano a mano, 4 = Game Night
.string "Grandmaster" # body+12, four 12 byte slots (marker 1, 2, 3, 4)
.string "Dojo"
.space 7
.string "Mano a mano"
.string "Game Night"
.space 1
.string "%s" # flag + 52: header format when the marker is 1..4 (no " Mode" suffix: names are proper nouns)
.string "" # flag + 55: empty string
.string "Starting the match..." # flag + 56 (22 bytes): Game Night call-up status line
.space 2
.align 2
MS_NM_PICK: # r6 = our name when the marker is 1..4, else untouched; uses r0 and r7 only
mflr r0
bl MS_NM_DATA
mflr r7
mtlr r0
lbz r0, 3(r7)
cmplwi r0, 0
beqlr
cmplwi r0, 4
bgtlr
mulli r0, r0, 12
add r6, r7, r0
addi r6, r6, -8 # flag + 4 + 12 * (marker - 1)
addi r5, r7, 52 # header format "%s"
blr
MS_NM_LINE1: # Game Night (marker 4): r5 = "Starting the match..." (replaces "Character selected"); else untouched
mflr r0
bl MS_NM_DATA
mflr r7
mtlr r0
lbz r0, 3(r7)
cmplwi r0, 4
bnelr
addi r5, r7, 56
blr
MS_NM_LINE2: # Game Night (marker 4): r5 = empty string (replaces "Press START to search"); else untouched
mflr r0
bl MS_NM_DATA
mflr r7
mtlr r0
lbz r0, 3(r7)
cmplwi r0, 4
bnelr
addi r5, r7, 55
blr
MS_NM_START:"""
NAMES_SITES = [("UPDATE_HEADER_UNRANKED:", "addi r6, REG_TEXT_PROPERTIES, TPO_STRING_UNRANKED"),
               ("UPDATE_HEADER_DIRECT:", "addi r6, REG_TEXT_PROPERTIES, TPO_STRING_DIRECT"),
               ("UPDATE_HEADER_RANKED:", "addi r6, REG_TEXT_PROPERTIES, TPO_STRING_RANKED")]

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))     # offline/codeset
REPO = os.path.dirname(os.path.dirname(HERE))


def log(*a):
    print("[codeset]", *a, file=sys.stderr)


def die(msg):
    print("[codeset] ERROR:", msg, file=sys.stderr)
    sys.exit(1)


def rel(p):
    return os.path.relpath(p, REPO)


def _tempish(asm_root):
    return {os.path.join(dp, f) for dp, _, fs in os.walk(asm_root) for f in fs
            if f.endswith((".asmtemp", ".out")) or f == "compiled.elf"}


def run_gecko(gecko, asm_root, cfg_path):
    before = _tempish(asm_root)  # upstream tracks one stray .asmtemp (External/59.94Hz Engine)
    env = dict(os.environ)
    env["PATH"] = os.environ["DKP_BIN"] + os.pathsep + env.get("PATH", "")
    r = subprocess.run([gecko, "build"] + GECKO_FLAGS + ["-c", cfg_path], cwd=asm_root, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode != 0 or "Failed" in r.stdout:
        die("gecko failed for %s:\n%s" % (cfg_path, r.stdout))
    leftovers = sorted(_tempish(asm_root) - before)
    if leftovers:
        die("gecko left temp files behind: %s" % leftovers[:5])
    return r.stdout


# --------------------------------------------------------------------------- work tree

def prepare_work(work):
    asm_src = ASM_SRC or os.path.join(REPO, "third_party", "slippi-ssbm-asm")
    head = subprocess.run(["git", "-C", asm_src, "rev-parse", "--short=7", "HEAD"], stdout=subprocess.PIPE,
                          text=True).stdout.strip()
    if head != ASM_COMMIT:
        die("third_party/slippi-ssbm-asm is at %s, expected %s" % (head, ASM_COMMIT))
    dirty = subprocess.run(["git", "-C", asm_src, "status", "--porcelain"], stdout=subprocess.PIPE,
                           text=True).stdout.strip()
    if dirty:
        die("third_party/slippi-ssbm-asm has local changes:\n" + dirty)
    if os.path.exists(work):
        shutil.rmtree(work)
    asm = os.path.join(work, "asm")
    shutil.copytree(asm_src, asm, ignore=shutil.ignore_patterns(".git", "Output"))
    if os.path.exists(os.path.join(asm, "Offline")):
        die("asm tree already has an Offline/ folder")
    shutil.copytree(os.path.join(HERE, "asm"), os.path.join(asm, "Offline"))
    s2 = os.path.join(REPO, "offline", "s2spike", "asm")   # S2 spike own asm (opt-in profiles only)
    if os.path.isdir(s2):
        shutil.copytree(s2, os.path.join(asm, "OfflineS2"))
    return asm


# --------------------------------------------------------------------------- self-check + universe

def self_check(gecko, asm):
    run_gecko(gecko, asm, "netplay.json")
    ini = os.path.join(asm, "Output", "Netplay", "GALE01r2.ini")
    got = gl.sha1_file(ini)
    if got != STOCK_INI_SHA1:
        die("toolchain parity FAILED: rebuilt GALE01r2.ini sha1 %s != %s" % (got, STOCK_INI_SHA1))
    vend = os.path.join(REPO, "vendor", "ishiiruka", "Data", "Sys", "GameSettings", "GALE01r2.ini")
    vend_sha = gl.sha1_file(vend) if os.path.exists(vend) else None
    log("self-check OK: rebuilt netplay GALE01r2.ini sha1 %s (vendor/ishiiruka copy: %s)" % (got, vend_sha))
    return {"rebuilt_ini": "work/asm/Output/Netplay/GALE01r2.ini", "sha1": got, "expected_sha1": STOCK_INI_SHA1,
            "vendor_ishiiruka_ini_sha1": vend_sha, "match": True}, ini


def build_universe(gecko, asm, stock_ini):
    netplay = json.load(open(os.path.join(asm, "netplay.json")))
    cfg = gl.normalize_annotations(netplay)
    cfg["outputFiles"] = [{"file": "_universe/universe.txt"}]
    with open(os.path.join(asm, "_universe.json"), "w") as f:
        json.dump(cfg, f, indent=1)
    run_gecko(gecko, asm, "_universe.json")
    mods = gl.parse_gecko_text(open(os.path.join(asm, "_universe", "universe.txt")).read())
    # byte identity with the stock ini
    allw = [w for m in mods for w in m["words"]]
    if gl.words_to_bytes(allw) != gl.ini_code_bytes(open(stock_ini).read()):
        die("universe build differs from the stock netplay ini code bytes")
    # module sequence == expansion of 
    expect = []
    for code in netplay["codes"]:
        for b in code["build"]:
            for p, kind in gl.expand_build(asm, b):
                expect.append((code["name"], p, kind, b.get("annotation", "")))
    got = [(m["section"], m["path"]) for m in mods]
    if got != [(s, p) for s, p, _, _ in expect]:
        for a, b in zip(got, expect):
            if a != b[:2]:
                die("universe module order mismatch: built %r vs expected %r" % (a, b[:2]))
        die("universe module count mismatch: %d vs %d" % (len(got), len(expect)))
    uni = []
    for m, (sec, p, kind, ann) in zip(mods, expect):
        uni.append({"path": p, "section": sec, "kind": kind, "build_annotation": ann,
                    "words": m["words"], "ops": gl.decode_ops(m["words"])})
    paths = [u["path"] for u in uni]
    if len(set(paths)) != len(paths):
        die("duplicate module paths in netplay.json")
    return uni


# --------------------------------------------------------------------------- profile resolution

def load_json(p):
    with open(p) as f:
        return json.load(f)


def resolve_options(profiles, profile, sets, flags):
    spec = profiles["options"]
    if profile not in profiles["profiles"]:
        die("unknown profile %r (have %s)" % (profile, ", ".join(profiles["profiles"])))
    opts = {k: v["default"] for k, v in spec.items()}
    prof = profiles["profiles"][profile]
    for k, v in prof.get("options", {}).items():
        opts[k] = v
    active = set(prof.get("flags", []))
    for kv in sets:
        if "=" not in kv:
            die("--set expects NAME=VALUE, got %r" % kv)
        k, v = kv.split("=", 1)
        if k not in spec:
            die("unknown option %r" % k)
        opts[k] = int(v) if spec[k]["type"] == "int" else v
    active |= set(flags)
    # expand flag groups
    groups = profiles.get("flag_groups", {})
    changed = True
    while changed:
        changed = False
        for g, members in groups.items():
            if g in active and not set(members) <= active:
                active |= set(members)
                changed = True
    known = set(profiles["flags"]) | set(groups)
    bad = active - known
    if bad:
        die("unknown flag(s) %s" % sorted(bad))
    for k, v in opts.items():
        s = spec[k]
        if s["type"] == "int":
            if not isinstance(v, int) or not (s["min"] <= v <= s["max"]):
                die("option %s=%r out of range [%d,%d]" % (k, v, s["min"], s["max"]))
        elif v not in s["values"]:
            die("option %s=%r not one of %s" % (k, v, s["values"]))
    return opts, sorted(active - set(groups))


def cond_true(expr, opts, flags):
    """'always' | 'never' | 'flag:NAME' | 'option:NAME>0' | 'option:NAME==VALUE'"""
    if expr == "always":
        return True
    if expr == "never":
        return False
    if expr.startswith("flag:"):
        return expr[5:] in flags
    m = re.match(r"option:(\w+)(>|==|!=)(.+)$", expr)
    if m:
        v = opts[m.group(1)]
        rhs = m.group(3)
        if m.group(2) == ">":
            return v > int(rhs)
        if m.group(2) == "==":
            return str(v) == rhs
        return str(v) != rhs
    die("bad condition %r" % expr)


def select(uni, decisions, opts, flags):
    dmods = decisions["modules"]
    upaths = [u["path"] for u in uni]
    missing = [p for p in upaths if p not in dmods]
    extra = [p for p in dmods if p not in upaths]
    if missing or extra:
        die("configs/modules.json does not match the netplay universe:\n  missing: %s\n  unknown: %s"
            % (missing, extra))
    for p, d in dmods.items():
        if d["decision"] not in ("INCLUDE", "OMIT", "OPTIONAL", "PARITY", "REPLACE"):
            die("bad decision for %s: %r" % (p, d["decision"]))
        if d["decision"] in ("OPTIONAL", "PARITY") and not d.get("when"):
            die("%s: %s needs a 'when' condition" % (p, d["decision"]))
        if d["decision"] == "REPLACE" and "replace_with" not in d:
            die("%s: REPLACE needs replace_with" % p)
    sel = []
    for u in uni:
        d = dmods[u["path"]]
        dec = d["decision"]
        if d.get("unless") and cond_true(d["unless"], opts, flags):
            continue   # 'unless' switches a module off (e.g. flag:no-pdvb)
        if dec == "INCLUDE" or (dec in ("OPTIONAL", "PARITY") and cond_true(d["when"], opts, flags)):
            sel.append({"path": u["path"], "kind": u["kind"], "origin": u["section"], "decision": dec,
                        "group": d.get("group", ""), "build_annotation": u["build_annotation"], "own": False})
        elif dec == "REPLACE":
            rw = d["replace_with"]
            v = str(opts[rw["option"]])
            target = rw["map"].get(v, rw["map"].get("*"))
            if target:
                sel.append({"path": target, "kind": "asm", "origin": u["section"], "decision": "REPLACE",
                            "replaces": u["path"], "group": d.get("group", ""), "build_annotation": "",
                            "own": True})
    for x in decisions.get("extra_modules", []):   # own additive modules (no netplay counterpart), flag-gated
        if cond_true(x["when"], opts, flags):
            sel.append({"path": x["path"], "kind": "asm", "origin": x["origin"], "decision": "EXTRA",
                        "group": x.get("group", ""), "build_annotation": "", "own": True, "extra": True})
    return sel


def output_group(s):
    if s.get("variant_of"):
        s = dict(s, own=False)   # a layout variant stays in its Slippi module's output group
    if s.get("extra"):
        return s["origin"]
    if s["own"]:
        return "Offline: replacements for %s" % s["origin"]
    if s["decision"] == "PARITY":
        return "Offline parity toggles (from %s)" % s["origin"]
    if s["origin"] in ("Required: Slippi Recording", "Required: Slippi Online"):
        return "Offline: EXI-free modules of %s" % s["origin"]
    if s["origin"] == "Recommended: Apply Delay to all In-Game Scenes":
        return "Offline: constant in-game delay (from %s)" % s["origin"]
    return s["origin"]


def compose(sel, netplay, name, opts, flags):
    authors = {c["name"]: c.get("authors", []) for c in netplay["codes"]}
    codes = []
    index = {}
    members = []
    for s in sel:
        g = output_group(s)
        if g not in index:
            index[g] = len(codes)
            codes.append({"name": g, "authors": authors.get(s["origin"], []), "build": []})
            members.append([])
        members[index[g]].append(s)
        if s["kind"] == "bin":
            ann = s["build_annotation"]
            e = {"type": "binary", "sourceFile": s["path"],
                 "annotation": ("%s | %s" % (ann, s["path"])) if ann else s["path"]}
        else:
            e = {"type": "inject", "sourceFile": s["path"]}
        codes[index[g]]["build"].append(e)
    header = ["# Multishine Offline codeset '%s' (GALE01 rev2)" % name,
              "# generated by offline/codeset/build_codeset.sh from slippi-ssbm-asm@%s - DO NOT EDIT" % ASM_COMMIT,
              "# options: %s ; flags: %s" % (json.dumps(opts, sort_keys=True), ",".join(flags) or "-"),
              ""]
    cfg = {"outputFiles": [{"file": "Output/Offline/%s.txt" % name, "header": header},
                           {"file": "Output/Offline/%s.bin" % name},
                           {"file": "Output/Offline/%s.gct" % name}],
           "codes": codes}
    return cfg, [s for grp in members for s in grp]


# --------------------------------------------------------------------------- delay variant

def generate_delay_variant(asm, delay):
    """Derive the EXI-free InitializeInGameDelay from the pinned submodule source at build time:
    the EXI 0xD5 request/response (two FN_EXITransferBuffer calls + the response byte load) is
    replaced by loading the constant `delay`; everything else (b CODE_START / blrl / .long header,
    the clamp, the 'Delay: %df' HUD text) is untouched. Anchors must match exactly once."""
    src = os.path.join(asm, DELAY_SRC)
    lines = open(src).read().split("\n")
    start = [i for i, l in enumerate(lines) if l.strip() == "li r3, CONST_SlippiCmdGetDelay"]
    end = [i for i, l in enumerate(lines) if l.strip() == "lbz REG_DELAY_RESULT, 0x1(REG_IGDB_ADDR)"]
    if len(start) != 1 or len(end) != 1 or not start[0] < end[0]:
        die("delay variant anchors not found exactly once in %s" % DELAY_SRC)
    cut = lines[start[0]:end[0] + 1]
    if sum("FN_EXITransferBuffer" in l for l in cut) != 2 or any("FN_EXITransferBuffer" in l for l in lines[:start[0]] + lines[end[0] + 1:]):
        die("unexpected EXI call layout in %s" % DELAY_SRC)
    new = lines[:start[0]] + [
        "# --- Multishine offline: EXI delay request removed; constant delay (build option delay=%d) ---" % delay,
        "li REG_DELAY_RESULT, %d" % delay,
    ] + lines[end[0] + 1:]
    out = os.path.join(asm, GENERATED_DELAY)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(new))
    return {"source": DELAY_SRC, "generated": GENERATED_DELAY, "delay_frames": delay,
            "removed_lines": [start[0] + 1, end[0] + 1]}


def generate_fpalign_variant(asm):
    """flag fp-align: derive UserDisplayFunctions with a word-aligned private LDB layout from the pinned submodule
    source at build time (only the three .set lines change; LDB_SIZE follows).  Anchors must match exactly once."""
    lines = open(os.path.join(asm, FPALIGN_SRC)).read().split("\n")
    for old, new in zip(FPALIGN_OLD, FPALIGN_NEW):
        idx = [i for i, l in enumerate(lines) if l.strip() == old]
        if len(idx) != 1:
            die("fp-align anchor %r not found exactly once in %s" % (old, FPALIGN_SRC))
        lines[idx[0]] = new
    out = os.path.join(asm, GENERATED_FPALIGN)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(lines))
    return {"source": FPALIGN_SRC, "generated": GENERATED_FPALIGN,
            "layout": "LDB: osb 0, prev_state 4, text_struct 8, display_mode 0xC, x 0x10, y 0x14, size 0x18 (was 5, 9, 0xA, 0xE, 0x12)"}


def generate_fpalign_extra(asm, v):
    lines = open(os.path.join(asm, v["src"])).read().split("\n")
    for old, new in v["edits"]:
        idx = [i for i, l in enumerate(lines) if l.strip() == old]
        if len(idx) != 1:
            die("fp-align anchor %r not found exactly once in %s" % (old, v["src"]))
        lines[idx[0]] = new
    out = os.path.join(asm, v["generated"])
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(lines))
    return {"source": v["src"], "generated": v["generated"], "layout": v["layout"]}


NAMES_STATUS_SITES = [("UPDATE_CHAR_SELECTED:", "addi r5, REG_TEXT_PROPERTIES, TPO_STRING_CHARACTER_SELECTED", "MS_NM_LINE1"),
                      (None, "addi r6, REG_TEXT_PROPERTIES, TPO_STRING_SEARCH", "MS_NM_LINE2"),
                      (None, "addi r6, REG_TEXT_PROPERTIES, TPO_STRING_ENTER_CODE", "MS_NM_LINE2")]


def generate_names_variant(asm):
    """flag ms-names: derive the CSS text module with the Multishine header names from the pinned submodule source (see NAMES_* above).
    Every anchor must match exactly once; the three original `addi r6` lines stay, a `bl MS_NM_PICK` follows each."""
    lines = open(os.path.join(asm, NAMES_SRC)).read().split("\n")

    def once(text):
        idx = [i for i, l in enumerate(lines) if l.strip() == text]
        if len(idx) != 1:
            die("ms-names anchor %r not found exactly once in %s" % (text, NAMES_SRC))
        return idx[0]
    # bottom-up so the indices stay valid
    for label, addi in NAMES_SITES:
        i = once(label)
        if lines[i + 1].strip() != addi or lines[i + 2].strip() != "b UPDATE_HEADER":
            die("ms-names: unexpected lines after %s in %s" % (label, NAMES_SRC))
    for label, addi in sorted(NAMES_SITES, key=lambda t: -once(t[0])):
        lines.insert(once(label) + 2, "bl MS_NM_PICK # Multishine names: r6 = our name in the Multishine flows")
    for label, addi, helper in NAMES_STATUS_SITES:
        i = once(addi)
        if label and lines[i - 1].strip() != label:
            die("ms-names: unexpected line before %s in %s" % (addi, NAMES_SRC))
        lines.insert(i + 1, "bl %s # Multishine names: Game Night call-up status text" % helper)
    i = once("# Ensure that this is an online CSS")
    lines[i:i] = NAMES_HEAD.split("\n")
    out = os.path.join(asm, GENERATED_NAMES)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(lines))
    return {"source": NAMES_SRC, "generated": GENERATED_NAMES, "layout": "body+0 b START, body+4 blrl, body+8 marker word (byte 11), body+12 four 12 byte names, helper MS_NM_PICK"}


# flag ms-hud-delay: Slippi's in-match "Delay: %df" HUD text (Online/Menus/InGame/InitInGame.asm, hook 0x802F6694) is shown only while our
# "Show technical info" setting (the Debug Lines bit, ml_glue.c settings_apply) is on.  The variant puts a 16 byte header in front of the body: `b START`, `blrl` (body+4),
# a flag word (body+8, low byte written by the engine: 0 = hidden, 1 = shown; the default is hidden) and the magic 'MSHD' (body+12) the engine checks.  The two original
# statements that create the delay subtext ("Initialize header" Text_InitializeSubtext and "Set header text size" Text_UpdateSubtextSize) are skipped with one `beq` when the flag
# is 0, so Text_UpdateSubtextSize never runs on a subtext that does not exist (the empty text struct is harmless: nothing else reads it).  Same hook, same C2.
GENERATED_HD = "Offline/Generated/InitInGameHudDelay.asm"
HD_SRC = "Online/Menus/InGame/InitInGame.asm"
HD_BLRL = 0x4E800021
HD_MAGIC = 0x4D534844            # 'MSHD' at body+12
HD_HEAD = """# --- Multishine delay HUD (flag ms-hud-delay): body header, engine flag ---
b MS_HD_START
MS_HD_DATA:
blrl # body+4: `bl MS_HD_DATA` comes straight back with LR = MS_HD_FLAG
MS_HD_FLAG:
.long 0 # body+8, low byte written by the engine: 0 = no "Delay: %df" text (default), 1 = shown (Settings > Show technical info)
.long 0x4D534844 # body+12, 'MSHD': what the engine looks for
MS_HD_START:"""
HD_SKIP_HEAD = """# Multishine delay HUD: flag 0 -> create no delay subtext (the text struct stays empty)
bl MS_HD_DATA
mflr r4
lwz r4, 0(r4)
cmpwi r4, 0
beq MS_HD_SKIP"""


def generate_hd_variant(asm):
    """flag ms-hud-delay: derive InitInGame with the flag-gated delay text from the pinned submodule source (see HD_* above).  Anchors must match exactly once."""
    lines = open(os.path.join(asm, HD_SRC)).read().split("\n")

    def once(text):
        idx = [i for i, l in enumerate(lines) if l.strip() == text]
        if len(idx) != 1:
            die("ms-hud-delay anchor %r not found exactly once in %s" % (text, HD_SRC))
        return idx[0]
    ha = [i for i, l in enumerate(lines) if l.strip() == "# Initialize header"]      # twice in the file: the delay text (first) and the player names header
    hb = [i for i, l in enumerate(lines) if l.strip() == "branchl r12, Text_UpdateSubtextSize"]
    if len(ha) != 2 or len(hb) != 2 or not ha[0] < hb[0] < ha[1] < hb[1]:
        die("ms-hud-delay: the delay text is not the first of two header blocks in %s" % HD_SRC)
    a, b = ha[0], hb[0]
    if not a < b or lines[b - 1].strip() != "lfs f2, DOFST_TEXT_SIZE(REG_DATA_ADDR)" or lines[a + 4].strip() != "addi r4, REG_DATA_ADDR, DOFST_TEXT_DELAYSTRING":
        die("ms-hud-delay: unexpected layout around the delay text in %s" % HD_SRC)
    lines[b + 1:b + 1] = ["MS_HD_SKIP:"]
    lines[a:a] = HD_SKIP_HEAD.split("\n")
    i = once("# Ensure that this is an online in-game")
    lines[i:i] = HD_HEAD.split("\n")
    out = os.path.join(asm, GENERATED_HD)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(lines))
    return {"source": HD_SRC, "generated": GENERATED_HD, "layout": "body+0 b START, body+4 blrl, body+8 flag word (byte 11), body+12 'MSHD'"}


# flag ms-record (LOCAL FORK): Slippi's recording modules without Dolphin.  Each entry: source, edits = [(kind, anchor, replacement)]; every anchor must match exactly once.
#   sub  one stripped line -> lines      cut  from the first line to the line before the second -> lines      after  insert after the line
RECORD_LC = [("after", '.include "Recording/Recording.s"', ".set LCancelStatus,0x23EC # ms-record: the padding after the 0x23EC byte fighter block (its heap cell holds 0x2400)")]
RECORD_VARIANTS = {
    "Recording/ShouldRecord.asm": [("cut", "getMinorMajor r3", "ReturnFalse:", """b MS_RC_START
MS_RC_DATA:
blrl # body+4
MS_RC_FLAG:
.long 0 # body+8, written by the engine: 1 = record (a Slippi match, a recorder buffer, an upload key)
.long 0x4D535243 # body+12 'MSRC'
MS_RC_START:
  mflr r12
  bl MS_RC_DATA
  mflr r3
  mtlr r12
  lwz r3,0(r3)
  cmpwi r3,0
  beq ReturnFalse
  getMinorMajor r3
  cmpwi r3, SCENE_ONLINE_IN_GAME
  beq ReturnTrue
""")],
    "Common/IncrementFrameIndex.asm": [("cut", "# Check if its the first frame (initialize timer)", "Exit:", """# ms-record: the index follows the scene frame counter (a retail word the snapshot holds): nothing of ours to roll back
  lis r3,0x8048
  lwz r3,-0x62A8(r3)
  addi r3,r3,CONST_FirstFrameIdx
  stw r3,frameIndex(r13)
""")],
    "Recording/SendGameInfo.asm": [
        ("cut", "#------------- DETERMINE SIZE OF GECKO CODE SECTION -----------------", "#------------- WRITE OUT COMMAND SIZES -------------", "  li REG_GeckoListSize, 4 # ms-record: no code list on this route; the recorder drops its announcement\n"),
        ("cut", "#-------------- Transfer Gecko List ---------------", "# run macro to create the SendFrameStart process", ""),
        ("sub", "li  r3,FULL_FRAME_DATA_BUF_LENGTH", "  li  r3,0x2420 # ms-record: a cell above 0x2400 bytes is not part of the rollback snapshot (the buffer is empty at every capture)"),
        ("sub", "lbz r3,PALToggle(rtoc)", "  li r3,0 # ms-record: the recording globals past .sdata2 exist on Slippi's Dolphin only")],
    "Recording/SendGamePostFrame.asm": RECORD_LC,
    "Recording/GetLCancelStatus/GetLCancelStatus.asm": RECORD_LC,
    "Recording/GetLCancelStatus/ResetLCancelStatus.asm": RECORD_LC,
    "Recording/Stages/SendDreamlandInfo.asm": [("cut", "bl STATIC_PREVIOUS_VALUE", "#------------- INITIALIZE -------------", "  lwz r5,0xdc(r31) # ms-record: every frame, the recorder keeps the changes\n")],
    "Recording/Stages/SendStadiumInfo.asm": [("cut", "bl STATIC_PREVIOUS_VALUE", "#------------- INITIALIZE -------------", "  lwz r5,0xdc(r31) # ms-record: every frame, the recorder keeps the changes\n")],
}


def generate_record_variant(asm, path):
    lines = open(os.path.join(asm, path)).read().split("\n")

    def once(text, frm=0):
        idx = [i for i, l in enumerate(lines) if l.strip() == text and i >= frm]
        if len(idx) != 1:
            die("ms-record anchor %r not found exactly once in %s" % (text, path))
        return idx[0]
    for e in RECORD_VARIANTS[path]:
        if e[0] == "cut":
            a, b = once(e[1]), once(e[2])
            if not a < b:
                die("ms-record: %s: anchors out of order" % path)
            lines[a:b] = e[3].split("\n")
        elif e[0] == "sub":
            lines[once(e[1])] = e[2]
        else:
            lines.insert(once(e[1]) + 1, e[2])
    name = "Offline/Generated/Record_" + os.path.basename(path)
    out = os.path.join(asm, name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("\n".join(lines))
    return name


# --------------------------------------------------------------------------- manifest

def build_manifest(name, sel, built, uni_by_path, dol, opts, flags, tools, selfcheck, delay_info, sizes):
    mods = []
    off = 0
    for s, b in zip(sel, built):
        ops = gl.decode_ops(b["words"])
        src = os.path.join(ASM_SRC or os.path.join(REPO, "third_party", "slippi-ssbm-asm"), s["variant_of"]) if s.get("variant_of") \
            else os.path.join(REPO, "offline", "s2spike", "asm", s["path"][len("OfflineS2/"):]) if s["own"] and s["path"].startswith("OfflineS2/") \
            else os.path.join(REPO, "offline", "codeset", "asm", s["path"][len("Offline/"):]) if s["own"] and s["path"] != GENERATED_DELAY \
            else os.path.join(ASM_SRC or os.path.join(REPO, "third_party", "slippi-ssbm-asm"), s["path"] if not s["own"] else DELAY_SRC)
        mop = []
        for op in ops:
            d = {"type": op["type"], "address": "0x%08X" % op["address"], "bin_offset": off,
                 "code_bytes": op["code_bytes"]}
            if dol:
                d["dol_section"] = dol.section_of(op["address"])
                rw = dol.word(op["address"])
                d["retail_word"] = None if rw is None else "0x%08X" % rw
                d["runtime_store_required"] = d["dol_section"] == "BSS"
            if op["type"] == "04":
                d["value"] = "0x%08X" % op["value"]
            elif op["type"] == "C2":
                d["payload_lines"] = op["lines"]
                d["payload_bytes"] = op["lines"] * 8
            off += op["code_bytes"]
            mop.append(d)
        entry = {
            "path": s["path"],
            "source_file": rel(src),
            "source_sha256": gl.sha256_file(src) if os.path.exists(src) else None,
            "kind": s["kind"],
            "origin_section": s["origin"],
            "decision": s["decision"],
            "group": s["group"],
            "hooks": ["0x%08X" % o["address"] for o in ops],
            "op_types": sorted({o["type"] for o in ops}),
            "count_04": sum(o["type"] == "04" for o in ops),
            "count_C2": sum(o["type"] == "C2" for o in ops),
            "c2_payload_bytes": sum(o["lines"] * 8 for o in ops if o["type"] == "C2"),
            "code_bytes": sum(o["code_bytes"] for o in ops),
            "provenance": "identical to shipped Slippi ini" if not s["own"] else "Multishine offline (own)",
            "ops": mop,
        }
        if s.get("replaces"):
            entry["replaces"] = s["replaces"]
        if s.get("variant_of"):
            entry["variant_of"] = s["variant_of"]
            entry["provenance"] = "Multishine layout variant of the Slippi module (flag %s), generated at build time" % s.get("variant_flag", "fp-align")
        mods.append(entry)
    return {
        "profile": name,
        "options": opts,
        "flags": flags,
        "game": "GALE01 rev2 (NTSC-U 1.02)",
        "source": {"slippi_ssbm_asm": ASM_COMMIT, "netplay_config": "netplay.json"},
        "toolchain": tools,
        "self_check": selfcheck,
        "retail_dol": {"path": rel(dol.path), "sha1": dol.sha1} if dol else None,
        "delay_variant": delay_info,
        "totals": sizes,
        "modules": mods,
    }


def sizes_of(ops, bin_bytes, gct_bytes, nmods):
    return {"modules": nmods,
            "ops": len(ops),
            "count_04": sum(o["type"] == "04" for o in ops),
            "count_C2": sum(o["type"] == "C2" for o in ops),
            "count_06": sum(o["type"] == "06" for o in ops),
            "c2_payload_bytes": sum(o["lines"] * 8 for o in ops if o["type"] == "C2"),
            "bin_bytes": bin_bytes, "gct_bytes": gct_bytes}


# --------------------------------------------------------------------------- audit self-test

def audit_selftest(uni, included, excluded, exi, dol):
    """Prove the audit can fail: (a) the stock Recording/Online/Apply-Delay modules must trip
    F1 (EXITransferBuffer slot), F2 (GeckoHeapPtr) and F5 (r2 m-ex globals); (b) the selected set
    with IngameCheckIfFrozen removed must trip F4 on 0x801D457C (if the frozen group is selected)."""
    stock = [{"path": u["path"], "section": u["section"], "ops": u["ops"]} for u in uni
             if u["section"] in ("Required: Slippi Recording", "Required: Slippi Online",
                                 "Recommended: Apply Delay to all In-Game Scenes")]
    ra = gecko_audit.audit(stock, [], exi, [], dol)
    checks = {c["check"] for c in ra["fail"]}
    res = {"stock_exi_sections": {"result": ra["result"], "fail_checks": sorted(checks), "fail_count": len(ra["fail"])}}
    if not {"F1", "F2", "F5"} <= checks:
        die("audit self-test: stock Recording/Online did not trip F1/F2/F5 (got %s)" % sorted(checks))
    frozen = "Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm"
    if any(m["path"] == frozen for m in included):
        inc2 = [m for m in included if m["path"] != frozen]
        exc2 = excluded + [m for m in included if m["path"] == frozen]
        rb = gecko_audit.audit(inc2, exc2, exi, [], dol)
        hit = [c for c in rb["fail"] if c["check"] == "F4" and c["address"] == "0x801D457C"]
        res["frozen_anchor_removed"] = {"result": rb["result"], "f4_hits": len(hit)}
        if not hit:
            die("audit self-test: removing IngameCheckIfFrozen did not trip F4 at 0x801D457C")
    return res


# --------------------------------------------------------------------------- docs

def render_modules_md(uni, decisions):
    dm = decisions["modules"]
    counts = {}
    for u in uni:
        counts[dm[u["path"]]["decision"]] = counts.get(dm[u["path"]]["decision"], 0) + 1
    out = []
    out.append("# Offline codeset: decision for every netplay module")
    out.append("")
    out.append("Generated by `offline/codeset/build_codeset.sh --update-docs` from `configs/modules.json` and the")
    out.append("gecko build of `third_party/slippi-ssbm-asm@%s/netplay.json` (the build that reproduces the shipped" % ASM_COMMIT)
    out.append("Slippi 3.6.4 `GALE01r2.ini`, SHA-1 `%s`). Do not edit by hand; edit `configs/modules.json`." % STOCK_INI_SHA1)
    out.append("")
    out.append("A *module* is one unit gecko builds: one `.asm` file or one `.bin` file. %d modules in %d netplay sections."
               % (len(uni), len({u["section"] for u in uni})))
    out.append("")
    out.append("Decisions: " + ", ".join("%s %d" % (k, counts[k]) for k in sorted(counts)) + ".")
    out.append("")
    out.append("- **INCLUDE**: always in every profile.")
    out.append("- **OPTIONAL** / **PARITY**: off by default; enabled by the flag/option in the *When* column.")
    out.append("- **REPLACE**: the Slippi module is dropped and an offline replacement is built instead (see *When*).")
    out.append("- **OMIT**: never built.")
    out.append("")
    out.extend(decisions.get("notes_md", []))
    out.append("")
    sec = None
    n = 0
    for u in uni:
        if u["section"] != sec:
            sec = u["section"]
            out.append("")
            out.append("## %s" % sec)
            out.append("")
            out.append("| # | Module (source path) | Hooks | Ops | Decision | When | Reason |")
            out.append("|---|---|---|---|---|---|---|")
        n += 1
        d = dm[u["path"]]
        ops = u["ops"]
        hooks = ", ".join("%08X" % o["address"] for o in ops[:6]) + (" (+%d)" % (len(ops) - 6) if len(ops) > 6 else "")
        c4 = sum(o["type"] == "04" for o in ops)
        c2 = sum(o["type"] == "C2" for o in ops)
        opstr = " ".join(x for x in (("04x%d" % c4) if c4 else "", ("C2x%d" % c2) if c2 else "") if x)
        when = d.get("when", "")
        if d["decision"] == "REPLACE":
            rw = d["replace_with"]
            when = "; ".join("%s=%s -> %s" % (rw["option"], k, v or "(none)") for k, v in rw["map"].items())
        if d["decision"] == "INCLUDE":
            when = "always" if not d.get("unless") else "unless " + d["unless"]
        if d["decision"] == "OMIT":
            when = "never"
        out.append("| %d | `%s` | %s | %s | **%s** | %s | %s |" % (
            n, u["path"], hooks, opstr, d["decision"], when.replace("|", "\\|"), d.get("reason", "").replace("|", "\\|")))
    out.append("")
    return "\n".join(out)


# --------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", default="default")
    ap.add_argument("--set", action="append", default=[], metavar="OPT=VAL")
    ap.add_argument("--flag", action="append", default=[])
    ap.add_argument("--name", help="output basename (default: profile name, or profile+suffix with ad-hoc flags)")
    ap.add_argument("--out", default=os.path.join(REPO, "build", "offline", "codeset"))
    ap.add_argument("--self-check", action="store_true", help="only rebuild the stock netplay ini and verify its SHA-1")
    ap.add_argument("--update-configs", action="store_true", help="rewrite configs/gecko/<profile>.json")
    ap.add_argument("--update-docs", action="store_true", help="rewrite MODULES.md")
    ap.add_argument("--list", action="store_true", help="list profiles/options/flags and exit")
    a = ap.parse_args()
    prof_name = a.profile   # `a` is shadowed by loop variables further down

    profiles = load_json(os.path.join(HERE, "configs", "profiles.json"))
    decisions = load_json(os.path.join(HERE, "configs", "modules.json"))
    if a.list:
        print("profiles:", ", ".join(profiles["profiles"]))
        for k, v in profiles["options"].items():
            print("option %s (default %r): %s" % (k, v["default"], v["doc"]))
        for k, v in profiles["flags"].items():
            print("flag %s: %s" % (k, v))
        for k, v in profiles.get("flag_groups", {}).items():
            print("flag group %s: %s" % (k, ", ".join(v)))
        return

    gecko = os.environ.get("GECKO_BIN")
    if not gecko or "DKP_BIN" not in os.environ:
        die("run through build_codeset.sh (GECKO_BIN/DKP_BIN not set)")
    opts, flags = resolve_options(profiles, a.profile, a.set, a.flag)
    adhoc = bool(a.set or a.flag)
    name = a.name or (a.profile if not adhoc else a.profile + "+" + "+".join(
        [s.replace("=", "") for s in a.set] + sorted(a.flag)))
    if not re.match(r"^[A-Za-z0-9_.+=-]+$", name):
        die("bad output name %r" % name)
    out = os.path.abspath(a.out)
    work = os.path.join(out, "work")
    asm = prepare_work(work)
    tools = {k: os.environ.get(k) for k in ("TOOLS_GO_VERSION", "TOOLS_GECKO_COMMIT", "TOOLS_GECKO_SHA256",
                                            "TOOLS_AS_VERSION", "TOOLS_AS_SHA256", "TOOLS_OBJCOPY_SHA256")}
    selfcheck, stock_ini = self_check(gecko, asm)
    if a.self_check:
        print(json.dumps(selfcheck, indent=1))
        return
    uni = build_universe(gecko, asm, stock_ini)
    uni_by_path = {u["path"]: u for u in uni}
    log("universe: %d modules, %d ops" % (len(uni), sum(len(u["ops"]) for u in uni)))

    md = render_modules_md(uni, decisions)
    md_path = os.path.join(HERE, "MODULES.md")
    if a.update_docs:
        with open(md_path, "w") as f:
            f.write(md)
        log("wrote", rel(md_path))
    elif not os.path.exists(md_path) or open(md_path).read() != md:
        log("WARNING: MODULES.md is stale (run with --update-docs)")


    sel = select(uni, decisions, opts, flags)
    delay_info = None
    if any(s["path"] == GENERATED_DELAY for s in sel):
        delay_info = generate_delay_variant(asm, opts["delay"])
    fpalign_info = None
    fpalign_more = []
    if "fp-align" in flags:
        for s in sel:
            if s["path"] == FPALIGN_SRC:
                s.update(path=GENERATED_FPALIGN, own=True, variant_of=FPALIGN_SRC)
                fpalign_info = generate_fpalign_variant(asm)
            for v in FPALIGN_EXTRA:
                if s["path"] == v["src"]:
                    s.update(path=v["generated"], own=True, variant_of=v["src"])
                    fpalign_more.append(generate_fpalign_extra(asm, v))
    names_info = None
    if "ms-names" in flags:
        for s in sel:
            if s["path"] == NAMES_SRC:
                s.update(path=GENERATED_NAMES, own=True, variant_of=NAMES_SRC, variant_flag="ms-names")
                names_info = generate_names_variant(asm)
        if names_info is None:
            die("flag ms-names needs the CSS text module (flag net-online)")
    hd_info = None
    if "ms-hud-delay" in flags:
        for s in sel:
            if s["path"] == HD_SRC:
                s.update(path=GENERATED_HD, own=True, variant_of=HD_SRC, variant_flag="ms-hud-delay")
                hd_info = generate_hd_variant(asm)
        if hd_info is None:
            die("flag ms-hud-delay needs the in-game HUD module (flag net-online-spike)")
    record_info = None
    if "ms-record" in flags:
        record_info = {}
        for s in sel:
            if s["path"] in RECORD_VARIANTS:
                src = s["path"]
                s.update(path=generate_record_variant(asm, src), own=True, variant_of=src, variant_flag="ms-record")
                record_info[src] = s["path"]
        if set(record_info) != set(RECORD_VARIANTS):
            die("flag ms-record: missing modules %s" % sorted(set(RECORD_VARIANTS) - set(record_info)))
    netplay = load_json(os.path.join(asm, "netplay.json"))
    cfg, sel = compose(sel, netplay, name, opts, flags)   # sel re-ordered to gecko output order

    committed = os.path.join(HERE, "configs", "gecko", "%s.json" % a.profile)
    if not adhoc:
        text = json.dumps(cfg, indent=1) + "\n"
        if a.update_configs:
            os.makedirs(os.path.dirname(committed), exist_ok=True)
            with open(committed, "w") as f:
                f.write(text)
            log("wrote", rel(committed))
        elif not os.path.exists(committed) or open(committed).read() != text:
            die("%s is missing/stale for profile %s (run with --update-configs and review the diff)"
                % (rel(committed), a.profile))

    cfg_path = os.path.join(asm, "_profile_%s.json" % name)
    with open(cfg_path, "w") as f:
        json.dump(cfg, f, indent=1)
    run_gecko(gecko, asm, os.path.basename(cfg_path))
    outdir = os.path.join(asm, "Output", "Offline")
    built = gl.parse_gecko_text(open(os.path.join(outdir, name + ".txt")).read())
    if [b["path"] for b in built] != [s["path"] for s in sel]:
        bp, sp = [b["path"] for b in built], [s["path"] for s in sel]
        diff = [(i, x, y) for i, (x, y) in enumerate(zip(bp, sp)) if x != y][:3]
        die("built module sequence differs from the selection (%d vs %d): %s" % (len(bp), len(sp), diff))
    binb = open(os.path.join(outdir, name + ".bin"), "rb").read()
    gctb = open(os.path.join(outdir, name + ".gct"), "rb").read()
    if gl.words_to_bytes([w for b in built for w in b["words"]]) != binb:
        die(".txt and .bin disagree")
    if gctb != b"\x00\xD0\xC0\xDE\x00\xD0\xC0\xDE" + binb + b"\xF0\x00\x00\x00\x00\x00\x00\x00":
        die(".gct is not header + .bin + F0 footer")

    # provenance: Slippi modules byte-identical to the shipped ini; checks on own modules
    for s, b in zip(sel, built):
        if not s["own"]:
            if b["words"] != uni_by_path[s["path"]]["words"]:
                die("provenance: %s differs from the shipped ini build" % s["path"])
        elif s.get("variant_of"):
            orig, new = uni_by_path[s["variant_of"]]["ops"], gl.decode_ops(b["words"])
            if s.get("variant_flag") == "ms-names":
                # the body grows (header, helper, names) but stays ONE C2 at the same hook, and starts with the header the engine looks for
                if [(o["type"], o["address"]) for o in orig] != [(o["type"], o["address"]) for o in new] or new[0]["type"] != "C2":
                    die("ms-names variant changed the hook of %s" % s["variant_of"])
                if new[0]["payload"][1] != NAMES_BLRL or new[0]["payload"][3] != NAMES_GRAN or (new[0]["payload"][0] >> 26) != 18:
                    die("ms-names variant header is not `b START; blrl; marker; Gran..`")
            elif s.get("variant_flag") == "ms-record":
                if [(o["type"], o["address"]) for o in orig] != [(o["type"], o["address"]) for o in new]:
                    die("ms-record variant changed the hook of %s" % s["variant_of"])
                if s["variant_of"] == "Recording/ShouldRecord.asm" and (new[0]["payload"][1] != HD_BLRL or new[0]["payload"][3] != 0x4D535243 or (new[0]["payload"][0] >> 26) != 18):
                    die("ms-record ShouldRecord header is not `b START; blrl; flag; MSRC`")
            elif s.get("variant_flag") == "ms-hud-delay":
                if [(o["type"], o["address"]) for o in orig] != [(o["type"], o["address"]) for o in new] or new[0]["type"] != "C2":
                    die("ms-hud-delay variant changed the hook of %s" % s["variant_of"])
                if new[0]["payload"][1] != HD_BLRL or new[0]["payload"][3] != HD_MAGIC or (new[0]["payload"][0] >> 26) != 18:
                    die("ms-hud-delay variant header is not `b START; blrl; flag; MSHD`")
            elif [(o["type"], o["address"], o["code_bytes"]) for o in orig] != [(o["type"], o["address"], o["code_bytes"]) for o in new]:
                die("fp-align variant changed the op layout of %s" % s["variant_of"])
        elif s["path"] == GENERATED_DELAY:
            orig = uni_by_path[DELAY_SRC]["ops"][0]
            new = gl.decode_ops(b["words"])
            if len(new) != 1 or new[0]["type"] != "C2" or new[0]["address"] != orig["address"]:
                die("delay variant is not a single C2 at the original hook")
            if new[0]["payload"][:3] != orig["payload"][:3]:
                die("delay variant changed the 'b CODE_START; blrl; .long' header that ApplyInGameDelay decodes")
            if new[0]["payload"][2] != 0x80000000:
                die("delay variant pointer placeholder is not at payload +8")
        else:
            ops = gl.decode_ops(b["words"])
            if any(o["type"] != "04" for o in ops) and not s["path"].startswith(("OfflineS2/", "Offline/Rules/", "Offline/Training/", "Offline/CardStart/", "Offline/Results/", "Offline/Rumble/", "Offline/Hud/", "Offline/Sss/")):
                die("own module %s must only contain 04 writes" % s["path"])

    dol_path = os.path.join(REPO, "vendor", "melee", "orig", "GALE01", "sys", "main.dol")
    dol = None
    if os.path.exists(dol_path):
        dol = gl.Dol(dol_path)
        if dol.sha1 != RETAIL_DOL_SHA1:
            die("retail main.dol sha1 %s != %s" % (dol.sha1, RETAIL_DOL_SHA1))
    else:
        log("WARNING: %s missing: manifest has no DOL section / retail word info" % rel(dol_path))
    for s, b in zip(sel, built):
        if s["path"].startswith("Offline/Boot/") and dol:
            for o in gl.decode_ops(b["words"]):
                if dol.word(o["address"]) != 0x887F0004:
                    die("boot patch target 0x%08X does not hold retail lbz r3,4(r31)" % o["address"])
        if s["path"].startswith("Offline/Card/") and dol:
            for o in gl.decode_ops(b["words"]):
                want = CARD_SILENT_RETAIL.get(o["address"])
                if want is None or dol.word(o["address"]) != want:
                    die("card patch target 0x%08X is not one of the six retail words it expects" % o["address"])

        if s["path"].startswith("Offline/CardStart/") and dol:
            for o in gl.decode_ops(b["words"]):
                want = CARDSTART_RETAIL.get(o["address"])
                if want is None or dol.word(o["address"]) != want:
                    die("card start hook 0x%08X is not the retail word it expects" % o["address"])
        if s["path"].startswith("Offline/Training/") and dol:
            for o in gl.decode_ops(b["words"]):
                want = TRAINING_RETAIL.get(o["address"])
                if want is None or dol.word(o["address"]) != want:
                    die("training hook 0x%08X is not one of the retail words it expects" % o["address"])
    all_ops = [o for b in built for o in gl.decode_ops(b["words"])]
    sizes = sizes_of(all_ops, len(binb), len(gctb), len(sel))
    manifest = build_manifest(name, sel, built, uni_by_path, dol, opts, flags, tools, selfcheck, delay_info, sizes)
    if fpalign_info:
        manifest["fpalign_variant"] = fpalign_info
    if fpalign_more:
        manifest["fpalign_variants_more"] = fpalign_more
    if names_info:
        manifest["names_variant"] = names_info
    if hd_info:
        manifest["hud_delay_variant"] = hd_info
    if record_info:
        manifest["record_variants"] = record_info

    # audit
    sel_paths = {s["path"] for s in sel} | {s["variant_of"] for s in sel if s.get("variant_of")}
    included = [{"path": s["path"], "section": s["origin"], "ops": gl.decode_ops(b["words"])} for s, b in zip(sel, built)]
    excluded = [{"path": u["path"], "section": u["section"], "ops": u["ops"]} for u in uni if u["path"] not in sel_paths]
    exi = gecko_audit.load_exi_symbols(os.path.join(REPO, "vendor", "melee", "config", "GALE01", "symbols.txt"))
    allow = load_json(os.path.join(HERE, "configs", "audit_allow.json"))["waivers"]
    selftest = audit_selftest(uni, included, excluded, exi, dol)
    report = gecko_audit.audit(included, excluded, exi, allow, dol)
    report["selftest"] = selftest
    report["profile"] = name
    # hooks whose patched instruction is read/called by other included code: the patcher must write a
    # relative `b payload` there (not ba / not an inline copy) and keep the payload layout verbatim.
    lc = {}
    for r in report["info"]["references_to_included_hooks"]:
        e = lc.setdefault(r["address"], {"address": r["address"], "hooked_by": r["hooked_by"],
                                         "referenced_by": {}})
        e["referenced_by"].setdefault(r["module"], set()).add(r["kind"])
    layout = []
    for a in sorted(lc):
        e = lc[a]
        kinds = set().union(*e["referenced_by"].values())
        e["constraint"] = ("hook instruction is LOADED and decoded as a relative branch (computeBranchTargetAddress-style): "
                           "must be `b <payload start>`; payload layout/offsets must be preserved and writable"
                           if "mem" in kinds else "hook address is called/branched to by other code")
        e["referenced_by"] = {k: sorted(v) for k, v in sorted(e["referenced_by"].items())}
        layout.append(e)
    for r in report["info"]["slot_references"]:
        e = lc.setdefault(r["slot"], {"address": r["slot"], "hooked_by": [r["defined_by"]], "referenced_by": {},
                                      "constraint": "Slippi static-function slot (extab data): callers bctrl/blrl to "
                                                    "this address, so it must hold `b <payload start>` and the payload "
                                                    "must return with blr"})
        e["referenced_by"].setdefault(r["module"], []).append(r["kind"])
        layout.append(e) if e not in layout else None
    for m in manifest["modules"]:
        for o in m["ops"]:
            if o["address"] in lc:
                o["referenced_by"] = lc[o["address"]]["referenced_by"]
    manifest["layout_constraints"] = layout
    manifest["audit"] = {"result": report["result"], "fail": len(report["fail"]), "warn": len(report["warn"]),
                         "report": "%s.audit.json" % name, "selftest": selftest}

    os.makedirs(out, exist_ok=True)
    for ext in ("txt", "bin", "gct"):
        shutil.copyfile(os.path.join(outdir, "%s.%s" % (name, ext)), os.path.join(out, "%s.%s" % (name, ext)))
    with open(os.path.join(out, "%s.gecko.json" % name), "w") as f:
        json.dump(cfg, f, indent=1)
    with open(os.path.join(out, "%s.manifest.json" % name), "w") as f:
        json.dump(manifest, f, indent=1)
    with open(os.path.join(out, "%s.audit.json" % name), "w") as f:
        json.dump(report, f, indent=1)

    log("profile %s: %d modules, 04=%d C2=%d C2-payload=%d B, bin=%d B, gct=%d B" % (
        name, sizes["modules"], sizes["count_04"], sizes["count_C2"], sizes["c2_payload_bytes"],
        sizes["bin_bytes"], sizes["gct_bytes"]))
    for w in report["warn"]:
        log("audit WARN %s %s %s: %s%s" % (w["check"], w["module"], w["address"], w["detail"],
                                           (" [waived: %s]" % w["waived"]) if w.get("waived") else ""))
    for f_ in report["fail"]:
        log("audit FAIL %s %s %s: %s" % (f_["check"], f_["module"], f_["address"], f_["detail"]))
    if report["info"]["unused_waivers"]:
        log("note: %d waiver(s) not needed by this profile" % len(report["info"]["unused_waivers"]))
    log("audit %s -> %s" % (report["result"], rel(os.path.join(out, "%s.audit.json" % name))))
    if report["result"] != "PASS":
        if profiles["profiles"][prof_name].get("allow_audit_fail"):
            log("profile %s has allow_audit_fail: outputs written despite audit FAIL (spike, NOT a supported build)" % prof_name)
            return
        sys.exit(2)


if __name__ == "__main__":
    main()
