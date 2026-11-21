#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Guard rail of the per-match rules record: match-time code never reads the SEARCH mode (od_o.mode) or writes the record anywhere but its writers.

Why: od_o.mode is the mode of the last search REQUEST (written by od_request only).  The bridge route never writes it (0 = MNC_MODE_RANKED there) and a finished
search leaves it behind, so a match-time rule keyed on it ran for the wrong match.  Every
in-match policy asks the record (offline/multishine/menus/od/ms_match_rules.h) through the ms_rules_* accessors; search-time code asks od_search_mode / od_req_mode.

Rules (comments are ignored; a line carrying the marker MR-WRITE in its comment is the one allowed read of the writer):
  R1  `od_o.mode`, `od_o->mode`, `o->mode` of a struct od, `MNC_MODE_*` compared to a mode read: only in the SEARCH layer (SEARCH_LAYER below: od_glue, the backends, the hub's
      menu code), never in the engine glue, the overlays or the bridge;
  R2  the record is written only by ms_rules_begin_ours / ms_rules_begin_slippi / ms_rules_clear(&ms_rules), only in WRITER_FILE, and begin_ours / begin_slippi exactly once each;
  R3  a policy site that used to read the mode must not come back: no `== MNC_MODE_` / `!= MNC_MODE_` comparisons outside the search layer.
Exit 1 and the offending lines when a rule fails.  Stdlib only, read only.

  tools/check_match_rules.py              check the tree
  tools/check_match_rules.py --self-test mutation check: the rules do fire on a planted violation
"""
import os, re, sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ROOTS = ("offline/multishine", "offline/bridge", "offline/s17", "offline/s12", "offline/s7")
EXT = (".c", ".h", ".S")
# the search / report / menu layer: the mode of the request is its subject (od_request writes it, the menus and the report path read it)
SEARCH_LAYER = (
    "offline/multishine/menus/od/",            # od_glue.c / .h / od_backend.c: the request itself and its accessors
    "offline/multishine/menus/on/on_backend.c",  # the online backend: B4 rows, B3 texts, the Ranked / Game night report (BD routed by the bridge dispatcher)
    "offline/multishine/menus/hub/hub_soc.c",    # the hub overlay's menu code (search status lines, the post-fight prompt)
    "offline/multishine/menus/sign/",            # toast / friend prompts keyed on the request mode
    "offline/client_net/",                       # the mnc client itself
    "/tests/", "/test/",                         # host tests
)
WRITER_FILE = "offline/multishine/glue/s17od/s12_gc.c"
RAW_MODE = re.compile(r"\bod_o\s*(?:\.|->)\s*mode\b|\bod_mode\b")
MODE_CMP = re.compile(r"[=!]=\s*MNC_MODE_[A-Z]+|MNC_MODE_[A-Z]+\s*[=!]=")
WRITE = re.compile(r"\bms_rules_(begin_ours|begin_slippi|clear)\s*\(\s*&\s*ms_rules\b")
BEGIN_DEF = re.compile(r"\bms_rules_(begin_ours|begin_slippi)\s*\(")


def strip_comments(t):
    out, i, n, st = [], 0, len(t), None
    while i < n:
        c = t[i]
        if st is None:
            if t.startswith("/*", i): st = "b"; i += 2; continue
            if t.startswith("//", i): st = "l"; i += 2; continue
            if c == '"':
                j = i + 1
                while j < n and t[j] != '"':
                    j += 2 if t[j] == "\\" else 1
                out.append(t[i:j + 1]); i = j + 1; continue
            out.append(c); i += 1
        elif st == "b":
            if t.startswith("*/", i): st = None; i += 2
            else:
                if c == "\n": out.append("\n")
                i += 1
        else:
            if c == "\n": st = None; out.append("\n")
            i += 1
    return "".join(out)


def in_search_layer(rel):
    return any(p in rel if p.startswith("/") else rel.startswith(p) for p in SEARCH_LAYER)


def check_text(rel, raw):
    """Findings [(line, rule, text)] for one file."""
    res = []
    code = strip_comments(raw).split("\n")
    rawl = raw.split("\n")
    layer = in_search_layer(rel)
    for i, ln in enumerate(code):
        raw_ln = rawl[i] if i < len(rawl) else ""
        marked = "MR-WRITE" in raw_ln
        if not layer:
            if RAW_MODE.search(ln) and not (marked and rel == WRITER_FILE):
                res.append((i + 1, "R1", "match-time code reads the search mode (od_o.mode): ask the record (ms_rules_*), or od_search_mode()/od_req_mode() at search time: " + raw_ln.strip()[:110]))
            elif MODE_CMP.search(ln) and not rel.endswith("ms_match_rules.h") and "od_req_mode(" not in ln and "od_search_mode(" not in ln and "ms_mode_numbers_match" not in ln:
                res.append((i + 1, "R3", "mode compared outside the search layer: " + raw_ln.strip()[:110]))
        m = WRITE.search(ln)
        if m and rel != WRITER_FILE:
            res.append((i + 1, "R2", "the record is written outside its writer file %s: %s" % (WRITER_FILE, raw_ln.strip()[:100])))
    return res


def walk():
    for root in ROOTS:
        for dp, dn, fn in os.walk(os.path.join(REPO, root)):
            dn[:] = [d for d in dn if d not in (".git", "__pycache__", ".analysis-deps")]
            for f in fn:
                if f.endswith(EXT):
                    p = os.path.join(dp, f)
                    yield os.path.relpath(p, REPO).replace(os.sep, "/"), p


def main(argv):
    if "--self-test" in argv:
        bad = "static void f(void){ if (od_o.mode == MNC_MODE_RANKED) x(); }\n"
        ok = "static void f(void){ /* od_o.mode in a comment */ if (ms_rules_flag(&ms_rules, 1)) x(); }\n"
        w = "static void g(void){ (void)ms_rules_begin_ours(&ms_rules, od_o.mode, 0);   /* MR-WRITE */ }\n"
        res = [bool(check_text("offline/multishine/glue/s17od/foo.c", bad)), not check_text("offline/multishine/glue/s17od/foo.c", ok),
               not check_text(WRITER_FILE, w), bool(check_text("offline/multishine/glue/s17od/foo.c", w)), not check_text("offline/multishine/menus/od/od_glue.c", bad)]
        print("self-test:", "OK" if all(res) else "FAILED %s" % res)
        return 0 if all(res) else 1
    finds, nfiles = [], 0
    begins = {"begin_ours": 0, "begin_slippi": 0}
    for rel, p in walk():
        nfiles += 1
        try:
            raw = open(p, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        for ln, rule, msg in check_text(rel, raw):
            finds.append("%s:%d: %s %s" % (rel, ln, rule, msg))
        if rel == WRITER_FILE:
            for m in WRITE.finditer(strip_comments(raw)):
                if m.group(1) in begins: begins[m.group(1)] += 1
    for k, v in begins.items():
        if v != 1:
            finds.append("%s: R2 ms_rules_%s(&ms_rules ...) must be called exactly once (found %d)" % (WRITER_FILE, k, v))
    for f in finds:
        print(f)
    print("match-rules check: %s (%d files scanned)" % ("FAIL, %d finding(s)" % len(finds) if finds else "PASS", nfiles))
    return 1 if finds else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
