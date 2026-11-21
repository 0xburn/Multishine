#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Quick-chat tables for offline/chat, generated from ONE source file: chat_presets.json.

  python3 offline/chat/gen_chat_tables.py check                 # the presets file is consistent
  python3 offline/chat/gen_chat_tables.py check-font [--dol D]  # every drawable character has a glyph in the retail DOL
  python3 offline/chat/gen_chat_tables.py header --out FILE     # C header for the console image and the host tests
  python3 offline/chat/gen_chat_tables.py lobby-array [--json ]   # our 16 strings as the JSON array the console sends
  python3 offline/chat/gen_chat_tables.py doc-table             # the Markdown table
  python3 offline/chat/gen_chat_tables.py doc-check [FILE]      # that table in the doc equals doc-table (run_tests.sh)
  python3 offline/chat/gen_chat_tables.py show [--json ]

Data flow: chat_presets.json -> header (ids, menu groups, default texts, charset) compiled into the console image;
 (optional, the player's own 16 texts) -> own_texts -> patches them into the image's chat block
-> the console sends them to our lab lobby in create-ticket (user.chatMessages) -> the lobby hands them to the opponent in
get-ticket-resp (players[].chatMessages) -> the opponent shows OUR text for an id we send.

 keeps the format of offline/options/gen_chat.py (that tool is not changed and not used here):
  {"format": "multishine-chat", "version": 1, "messages": {GROUP: {SLOT: TEXT}}}   GROUP, SLOT in up/left/right/down.
Every one of the 16 slots must be present, 1-25 characters from the presets' charset.  Unknown keys are refused, keys that
look like account data (uid, playKey, connectCode, displayName, ...) are refused with a pointed message: a chat file carries
message texts only, never credentials ( rule 5).
"""
import argparse
import json
import pathlib
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
PRESETS = HERE / "chat_presets.json"
RETAIL_DOL = ROOT / "vendor/melee/orig/GALE01/sys/main.dol"
FONT_LUT, FONT_LUT_LEN = 0x8040C8C0, 0x240            # lbl_8040C8C0: Shift-JIS code of every glyph
CREDENTIAL_KEYS = {"uid", "playkey", "play_key", "connectcode", "connect_code", "displayname", "display_name",
                   "latestversion", "token", "password", "secret", "user", "userjson"}


class PresetError(Exception):
    pass


def charset(p):
    """The drawable characters: the encoder's own ASCII ones plus the Shift-JIS punctuation."""
    return set(p["charset"]["ascii_direct"]) | set(p["charset"]["sjis"])


def load_presets(path=PRESETS):
    """Read and fully check chat_presets.json.  Returns the parsed dict (ids as ints in 'id_int')."""
    d = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    if d.get("format") != "multishine-chat-presets" or d.get("version") != 2:
        raise PresetError("not a multishine-chat-presets v2 file")
    bits, order = d["dpad_bits"], d["menu_order"]
    if sorted(order) != sorted(bits) or len(order) != 4 or sorted(bits.values()) != [1, 2, 4, 8]:
        raise PresetError("menu_order must list the four D-pad directions, dpad_bits the four D-pad bits")
    direct, sj = d["charset"]["ascii_direct"], d["charset"]["sjis"]
    if set(direct) & set(sj) or set('"\\`') & (set(direct) | set(sj)):
        raise PresetError("charset: a character twice, or one of the excluded \" \\ `")
    for ch, code in sj.items():
        v = int(code, 16)
        if len(ch) != 1 or not 0x8140 <= v <= 0x81FF:
            raise PresetError("charset sjis %r: one ASCII character -> one 0x81xx pair" % ch)
    cs, maxc = charset(d), int(d["max_chars"])
    if maxc != 25:
        raise PresetError("max_chars must be 25 (Slippi's MAX_MESSAGE_LENGTH)")
    msgs = d["messages"]
    if len(msgs) != 16:
        raise PresetError("need exactly 16 messages")
    seen = set()
    for i, m in enumerate(msgs):
        if m["index"] != i:
            raise PresetError("messages must be listed in index order")
        g, s = m["group"], m["slot"]
        if order.index(g) * 4 + order.index(s) != i:          # GetChatText: (groupIndex * 4) + (index - 1)
            raise PresetError("index %d does not match group %s / slot %s" % (i, g, s))
        want = (bits[g] << 4) | bits[s]                      # UpdateChatWindow: (groupId << 4) + chatInput
        got = int(m["id"], 16)
        if got != want:
            raise PresetError("id %s of %s/%s should be 0x%02X" % (m["id"], g, s, want))
        if got in seen:
            raise PresetError("duplicate id")
        seen.add(got)
        check_text(m["text"], cs, maxc, "text of %s" % m["id"])
        m["id_int"] = got
    n = d["notice_disabled"]
    n["id_int"] = int(n["id"], 16)
    if n["id_int"] != 0x10 or n["id_int"] in seen:
        raise PresetError("notice_disabled must be id 0x10")
    check_text(n["text"], cs, maxc, "notice_disabled")
    for g in order:
        check_text(d["groups"][g]["header"], cs, maxc, "header of %s" % g)
    return d


def check_text(t, cs, maxc, what):
    if not isinstance(t, str) or not 1 <= len(t) <= maxc:
        raise PresetError("%s: 1-%d characters" % (what, maxc))
    bad = sorted(set(t) - cs)
    if bad:
        raise PresetError("%s: characters Melee's font cannot draw here: %r" % (what, "".join(bad)))
    if t != t.strip() or "  " in t:
        raise PresetError("%s: no leading/trailing or double spaces (the console squeezes them)" % what)


def _scan_credentials(obj, path="$"):
    if isinstance(obj, dict):
        for k, v in obj.items():
            if str(k).replace("-", "_").lower() in CREDENTIAL_KEYS:
                raise PresetError("%s.%s: chat.json carries message texts only, never account data or credentials" % (path, k))
            _scan_credentials(v, path + "." + str(k))
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            _scan_credentials(v, "%s[%d]" % (path, i))


def texts_from_chat_json(d, presets):
    """The 16 texts of a, in table order; every rule of the presets applies."""
    _scan_credentials(d)
    if not isinstance(d, dict):
        raise PresetError("top level must be an object")
    extra = set(d) - {"format", "version", "messages", "_comment"}
    if extra:
        raise PresetError("unknown keys: %s" % ", ".join(sorted(extra)))
    if d.get("format") != "multishine-chat" or d.get("version") != 1:
        raise PresetError('need "format": "multishine-chat", "version": 1')
    order, m = presets["menu_order"], d.get("messages")
    if not isinstance(m, dict) or set(m) != set(order):
        raise PresetError("messages must have exactly the groups up, left, right, down")
    out, cs, maxc = [], charset(presets), int(presets["max_chars"])
    for g in order:
        if not isinstance(m[g], dict) or set(m[g]) != set(order):
            raise PresetError("messages.%s must have exactly the slots up, left, right, down" % g)
        for s in order:
            check_text(m[g][s], cs, maxc, "messages.%s.%s" % (g, s))
            out.append(m[g][s])
    return out


def own_texts(presets, chat_json=None):
    """Our 16 strings in table order: the presets, or a custom set."""
    if chat_json is None:
        return [m["text"] for m in presets["messages"]]
    return texts_from_chat_json(json.loads(pathlib.Path(chat_json).read_text(encoding="utf-8")), presets)


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def header(p):
    msgs, order = p["messages"], p["menu_order"]
    sj = sorted(p["charset"]["sjis"].items())
    out = ["/* GENERATED by offline/chat/gen_chat_tables.py from offline/chat/chat_presets.json: do not edit. */",
           "#ifndef CHAT_PRESETS_GEN_H", "#define CHAT_PRESETS_GEN_H", "",
           "#define CP_N 16",
           "#define CP_MAX_CHARS %d                /* Slippi MAX_MESSAGE_LENGTH */" % p["max_chars"],
           "#define CP_ID_DISABLED 0x%02X            /* SlippiPremadeText.h CHAT_MSG_CHAT_DISABLED */" % p["notice_disabled"]["id_int"],
           "#define CP_NOTICE_DISABLED %s" % c_str(p["notice_disabled"]["text"]), "",
           "/* characters Melee's ASCII-to-SIS encoder maps itself (hsd_3A64.c HSD_SisLib_803A67EC) */",
           "#define CP_ASCII_DIRECT %s" % c_str(p["charset"]["ascii_direct"]),
           "/* punctuation drawn through a Shift-JIS pair (low byte; the high byte is always 0x81) */",
           "#define CP_SJIS_N %d" % len(sj),
           "static const char cp_sjis_char[CP_SJIS_N] = {%s};" % ", ".join("'%s'" % (c if c != "'" else "\\'") for c, _ in sj),
           "static const unsigned char cp_sjis_lo[CP_SJIS_N] = {%s};" % ", ".join("0x%s" % v[2:] for _, v in sj), "",
           "/* D-pad bits (GameCube pad word, low byte) in menu order: up, left, right, down */",
           "static const unsigned char cp_menu_bits[4] = {%s};" % ", ".join("0x%X" % p["dpad_bits"][g] for g in order),
           "static const char *const cp_group_header[4] = {%s};" % ", ".join(c_str(p["groups"][g]["header"]) for g in order),
           "/* table index -> wire id (index = group * 4 + slot, menu order) */",
           "static const unsigned char cp_ids[CP_N] = {%s};" % ", ".join("0x%02X" % m["id_int"] for m in msgs),
           "/* table index -> our default text */",
           "static const char *const cp_default_text[CP_N] = {"]
    for m in msgs:
        out.append("    %s, /* 0x%02X %s/%s, meaning: %s */" % (c_str(m["text"]), m["id_int"], m["group"], m["slot"], m["meaning"]))
    out += ["};", "", "#endif", ""]
    return "\n".join(out)


def doc_table(p):
    rows = ["| index | wire id | first D-pad (group) | second D-pad (message) | our default text | meaning (same as Slippi's default) | 0x84 packet bytes |",
            "|---|---|---|---|---|---|---|"]
    for m in p["messages"]:
        rows.append("| %d | `0x%02X` | %s | %s | %s | %s | `84 00 00 00 %02X pp` |" % (
            m["index"], m["id_int"], m["group"].capitalize(), m["slot"].capitalize(), m["text"], m["meaning"], m["id_int"]))
    n = p["notice_disabled"]
    rows.append("| - | `0x10` | (automatic) | (automatic) | NAME %s | %s | `84 00 00 00 10 pp` |" % (n["text"], n["meaning"]))
    return "\n".join(rows)


def dol_read(dol, addr, n):
    offs = struct.unpack(">18I", dol[0:72])
    addrs = struct.unpack(">18I", dol[72:144])
    sizes = struct.unpack(">18I", dol[144:216])
    for o, a, s in zip(offs, addrs, sizes):
        if a <= addr and addr + n <= a + s:
            return dol[o + addr - a:o + addr - a + n]
    raise PresetError("0x%08X not in the DOL" % addr)


def check_font(p, dol_path):
    """Every character of the charset maps to a glyph of Melee's font (the retail DOL's lookup table)."""
    lut = dol_read(pathlib.Path(dol_path).read_bytes(), FONT_LUT, FONT_LUT_LEN)
    glyphs = {(lut[i] << 8) | lut[i + 1] for i in range(0, FONT_LUT_LEN, 2)}

    def sis_code(c):                              # the encoder's own mapping
        o = ord(c)
        fixed = {" ": 0x8140, "'": 0x8166, ",": 0x8143, "-": 0x817C, ".": 0x8144, ":": 0x8146}
        if c in fixed:
            return fixed[c]
        if "0" <= c <= "9" or "A" <= c <= "Z":
            return 0x8200 | (o + 0x1F)
        if "a" <= c <= "z":
            return 0x8200 | (o + 0x20)
        raise PresetError("no direct mapping for %r" % c)
    missing = [c for c in p["charset"]["ascii_direct"] if sis_code(c) not in glyphs]
    missing += [c for c, v in p["charset"]["sjis"].items() if int(v, 16) not in glyphs]
    if missing:
        raise PresetError("no glyph in Melee's font for %r" % "".join(missing))
    return len(glyphs)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    sp.add_parser("check")
    cf = sp.add_parser("check-font")
    cf.add_argument("--dol", default=str(RETAIL_DOL))
    h = sp.add_parser("header")
    h.add_argument("--out", required=True)
    la = sp.add_parser("lobby-array")
    la.add_argument("--json")
    sp.add_parser("doc-table")
    dc = sp.add_parser("doc-check")
    dc.add_argument("file", nargs="?", default=str(ROOT / "docs/design/QUICK_CHAT.md"))
    sh = sp.add_parser("show")
    sh.add_argument("--json")
    a = ap.parse_args(argv)
    try:
        p = load_presets()
        if a.cmd == "check":
            print("chat_presets.json: ok (16 ids, %d-char limit, %d drawable characters)" % (p["max_chars"], len(charset(p))))
        elif a.cmd == "check-font":
            n = check_font(p, a.dol)
            print("check-font: all %d characters have a glyph (font table of %d glyphs)" % (len(charset(p)), n))
        elif a.cmd == "header":
            pathlib.Path(a.out).parent.mkdir(parents=True, exist_ok=True)
            pathlib.Path(a.out).write_text(header(p))
        elif a.cmd == "lobby-array":
            print(json.dumps(own_texts(p, a.json), separators=(",", ":")))
        elif a.cmd == "doc-table":
            print(doc_table(p))
        elif a.cmd == "doc-check":
            text = pathlib.Path(a.file).read_text(encoding="utf-8")
            begin, end = "<!-- BEGIN gen_chat_tables.py doc-table -->", "<!-- END gen_chat_tables.py doc-table -->"
            got = text[text.index(begin) + len(begin):text.index(end)].strip()
            if got != doc_table(p).strip():
                raise PresetError("%s: the id table differs from chat_presets.json (regenerate with doc-table)" % a.file)
            print("doc-check: %s table matches chat_presets.json" % a.file)
        else:
            texts = own_texts(p, a.json)
            for m, t in zip(p["messages"], texts):
                print("%2d 0x%02X %-5s %-5s %-25s (%s)" % (m["index"], m["id_int"], m["group"], m["slot"], t, m["meaning"]))
            print("   0x%02X notice      NAME %s" % (p["notice_disabled"]["id_int"], p["notice_disabled"]["text"]))
    except (PresetError, KeyError, ValueError, OSError) as e:
        print("gen_chat_tables: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
