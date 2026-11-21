#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Menu layout reader, validator and build-table generator (step 2).

Input: a `multishine-menu-layout` v1 file as saved by the menu tool, either the bare layout object or the
tool's wrapper `{"layout": {...}, "savedAt": ...}`.  Output: a list of problems (error / warn / info) and a compact build table
(`multishine-menu-build` v1) that the image builder and the Melee glue can read: one record per screen with its Melee menu kind
(or none), its style, and one record per row (position, label plan, what the row does).

Pure Python 3 standard library, no network, nothing drawn.  The facts it encodes about retail 1.02 (kind numbers, start frames, hidden
slots, the 10 row limit, the label character set) are cited and.

  python3 tools/menu_layout.py check FILE [--style VIEW=native|text|page ...]     problems, exit 1 when there is an error
  python3 tools/menu_layout.py table FILE [--out FILE] [--style ...]              the build table as JSON (refuses when there is an error)
  python3 tools/menu_layout.py outline FILE                                       a readable numbered tree
  python3 tools/menu_layout.py cheader FILE --screens main,v3 [--simplify v1=online,v2=kind:2] [--out ml_table.h]
                                                                                   the C table of the runtime module (offline/multishine/menus/ml)
"""
import argparse
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

FORMAT = "multishine-menu-layout"
VERSION = 1
TABLE_FORMAT = "multishine-menu-build"
TABLE_VERSION = 1

# ---- facts about retail 1.02  -------------------------------------------------------
# view id of the stock tree -> Melee menu kind (mn_803EB6B0 index).  Views that are separate scenes (Multi-Man, Records detail) have no kind.
STOCK_KIND = {"main": 0, "1p": 1, "vs": 2, "toy": 3, "options": 4, "data": 5, "reg": 6, "online": 8, "stadium": 9, "special": 12, "records": 28}
# label art start frame of each kind (table field +0x04; online = Slippi's, only in their MnMaAll)
START_FRAME = {0: 0, 1: 20, 2: 40, 3: 60, 4: 80, 5: 100, 6: 120, 8: 140, 9: 160, 12: 200, 28: 240}
# row id -> (home kind, selection index in the retail table, hidden slots counted)
STOCK_SEL = {
    "main.1p": (0, 0), "main.vs": (0, 1), "main.toy": (0, 2), "main.options": (0, 3), "main.data": (0, 4),
    "1p.reg": (1, 0), "1p.event": (1, 1), "1p.online": (1, 2), "1p.stadium": (1, 3), "1p.training": (1, 4),
    "vs.melee": (2, 0), "vs.melee2": (2, 0), "vs.tournament": (2, 1), "vs.special": (2, 2), "vs.rules": (2, 3), "vs.name": (2, 4),
    "toy.gallery": (3, 0), "toy.lottery": (3, 1), "toy.collection": (3, 3),
    "options.rumble": (4, 0), "options.sound": (4, 1), "options.display": (4, 2), "options.multishine": (4, 3),
    "options.language": (4, 4), "options.erase": (4, 5),
    "data.snap": (5, 0), "data.archives": (5, 1), "data.sound": (5, 2), "data.records": (5, 3), "data.special": (5, 4),
    "reg.classic": (6, 0), "reg.adventure": (6, 1), "reg.allstar": (6, 2),
    "stadium.target": (9, 0), "stadium.homerun": (9, 1), "stadium.multiman": (9, 2),
    "records.vs": (28, 0), "records.bonus": (28, 1), "records.misc": (28, 2),
    "online.ranked": (8, 0), "online.unranked": (8, 1), "online.direct": (8, 2), "online.teams": (8, 3), "online.party": (8, 4),
}
for _i, _n in enumerate(["camera", "stamina", "ssd", "giant", "tiny", "invisible", "fixedcam", "singlebutton", "lightning", "slomo"]):
    STOCK_SEL["special." + _n] = (12, _i)
# Free table slots (all-zero entries in the retail DOL) that are safe to use as new native screens, in order of preference.  10, 11 and 14 have
# a light colour in mn_8022C010; 7 and 13-18 belong to the event / rules scenes and are not used.
FREE_KINDS = [14, 11, 10, 22, 27, 29, 30, 31, 32]    # the Online submenu (view v1, kind 10) is gone, the first level holds the modes.  The More screen takes 14 and the Original Menu (view v3) keeps 11: the order the layout opens them (Main Menu More row, then More > Original Menu); 10 stays a free slot
MAX_ROWS = 10            # row slot table mn_803EAE68 has 10 entries
LABEL_MAX = 11           # OPTROW_LABEL_MAX: characters of a label we draw ourselves (provisional for the main menu, measured in step 3)
LABEL_CHARSET = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,'-:\"!?;()[]+/%#&*@$=<>~|_^{}")   # the last group: ml_sis_expand (full-width Shift-JIS codes)
TEXT_ITEM_MAX = 32       # ui_strings.json caps.item: a selectable row of one of our text screens
# custom rows that do what a retail row does (our Couch Melee is the retail VS "Melee" row, which his layout turned into the Local Melee folder)
CUSTOM_ALIAS = {"couch melee": {"do": "stock", "kind": 2, "sel": 0, "alias_of": "vs.melee"}}
# second entry ids of a retail row (the the classic Melee row back in VS. Mode while Couch on the Main Menu is the same retail row): same STOCK_SEL slot
STOCK_ALIAS_IDS = {"vs.melee2"}
ONLINE_MODES = {"ranked": 0, "unranked": 1, "direct": 2, "teams": 3, "party": 4}   # Slippi's OPTION_*_IDX
# The hub rows that were named like Slippi's modes keep their mode by ROW ID, so a rename does not change what they do
# (c2 Grandmaster = Ranked, c3 Dojo = Unranked, c4 Mano a mano = Direct; Slippi's own submenu keeps Slippi's names)
MODE_BY_ID = {"c2": 0, "c3": 1, "c4": 2}

# View styles.  native = a Melee menu screen with plates (needs a kind), stock = a retail kind, scene = a retail scene we only enter,
# text = a text screen of ours ( kind T), page = a page of the options page.
DEFAULT_STYLE = {"v2": "native", "v3": "native", "more": "native", "mt": "text", "acct": "text", "fr": "text"}
PAGE_ROOT_ROW = "options.multishine"        # every view below this row is a page of the options page


class Problem(object):
    def __init__(self, level, code, msg, where=""):
        self.level, self.code, self.msg, self.where = level, code, msg, where

    def as_dict(self):
        return {"level": self.level, "code": self.code, "where": self.where, "msg": self.msg}

    def __str__(self):
        return "%-5s %-18s %s%s" % (self.level, self.code, (self.where + ": ") if self.where else "", self.msg)


def load(path):
    """Read a layout file; accepts the tool's wrapper.  Raises ValueError for anything that is not a v1 layout."""
    with open(path, "rb") as f:
        raw = f.read()
    try:
        data = json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as e:
        raise ValueError("not JSON: %s" % e)
    if isinstance(data, dict) and "layout" in data and "views" not in data:
        data = data["layout"]
    if not isinstance(data, dict):
        raise ValueError("not a layout object")
    if data.get("format") != FORMAT:
        raise ValueError("wrong format %r (want %r)" % (data.get("format"), FORMAT))
    if data.get("version") != VERSION:
        raise ValueError("unsupported version %r (this reader knows %d)" % (data.get("version"), VERSION))
    data["_sha1"] = hashlib.sha1(raw).hexdigest()
    return data


def _opens(e):
    o = e.get("opens")
    return o if o else None          # None and "" both mean "no submenu"


def stock_rows():
    """Row ids of the retail tree (and Slippi's online screen) from the menu tool's stock data, or None when the tool is not importable."""
    try:
        import site_menu_mockup as mm
    except Exception:
        return None
    st = mm.stock_state()
    return {e["id"]: v["id"] for v in st["views"] for e in v["entries"]}


def analyse(layout, style_override=None):
    """Return (problems, info) where info holds the derived structure used by build_table."""
    P = []
    views = layout.get("views")
    if not isinstance(views, list) or not views:
        return [Problem("error", "no-views", "the layout has no views")], None
    vmap, emap, owner = {}, {}, {}
    for v in views:
        vid = v.get("id")
        if not vid or not isinstance(vid, str):
            P.append(Problem("error", "view-id", "a view has no id"))
            continue
        if vid in vmap:
            P.append(Problem("error", "dup-view", "view id used twice", vid))
            continue
        vmap[vid] = v
        for e in v.get("entries") or []:
            eid = e.get("id")
            if not eid or not isinstance(eid, str):
                P.append(Problem("error", "row-id", "a row has no id", vid))
                continue
            if eid in emap:
                P.append(Problem("error", "dup-row", "row id used twice (also in %s)" % owner[eid], eid))
                continue
            emap[eid] = e
            owner[eid] = vid
            if not isinstance(e.get("label"), str) or not e["label"].strip():
                P.append(Problem("error", "label-empty", "row has no label", eid))
    root = layout.get("root")
    if root not in vmap:
        P.append(Problem("error", "no-root", "root view %r does not exist" % (root,)))
        return P, None

    # links
    parent = {}                                         # view id -> row id that opens it
    for vid, v in vmap.items():
        for e in v.get("entries") or []:
            o = _opens(e)
            if o is None:
                continue
            if o not in vmap:
                P.append(Problem("error", "bad-link", "opens unknown view %r" % o, e.get("id")))
                continue
            if o in parent:
                P.append(Problem("error", "two-parents", "view %r is opened by %s and %s; the build needs a tree" % (o, parent[o], e.get("id")), o))
                continue
            if o == root:
                P.append(Problem("error", "opens-root", "a row opens the root view", e.get("id")))
                continue
            parent[o] = e["id"]
    # reachability and cycles
    reach, stack = set(), [root]
    while stack:
        x = stack.pop()
        if x in reach:
            continue
        reach.add(x)
        for e in vmap[x].get("entries") or []:
            o = _opens(e)
            if o in vmap:
                stack.append(o)
    for vid in vmap:
        if vid not in reach:
            P.append(Problem("warn", "unreachable-view", "no row leads to this view", vid))
    depth = {root: 0}
    order = [root]
    for x in order:
        for e in vmap[x].get("entries") or []:
            o = _opens(e)
            if o in vmap and o not in depth:
                depth[o] = depth[x] + 1
                order.append(o)

    # stock rows must all exist, exactly once
    sr = stock_rows()
    if sr is not None:
        for rid in sr:
            if rid not in emap:
                P.append(Problem("error", "stock-missing", "retail row is gone (hide it instead of removing it)", rid))
        for eid, e in emap.items():
            if e.get("origin") == "stock" and eid not in sr and eid not in STOCK_ALIAS_IDS:
                P.append(Problem("warn", "stock-unknown", "row is marked stock but the tool's retail tree does not have it", eid))
    else:
        P.append(Problem("info", "no-stock-data", "tools/site_menu_mockup.py not importable: stock rows were not cross-checked"))

    # styles
    pagenodes = set()
    if PAGE_ROOT_ROW in emap and _opens(emap[PAGE_ROOT_ROW]):
        st = [_opens(emap[PAGE_ROOT_ROW])]
        while st:
            x = st.pop()
            if x in pagenodes or x not in vmap:
                continue
            pagenodes.add(x)
            for e in vmap[x].get("entries") or []:
                if _opens(e):
                    st.append(_opens(e))
    style = {}
    for vid, v in vmap.items():
        if style_override and vid in style_override:
            style[vid] = style_override[vid]
        elif vid in pagenodes:
            style[vid] = "page"
        elif vid in STOCK_KIND:
            style[vid] = "stock"
        elif vid == "multiman":
            style[vid] = "scene"
        elif vid in DEFAULT_STYLE:
            style[vid] = DEFAULT_STYLE[vid]
        else:
            style[vid] = "text"
            if v.get("origin") != "stock":
                P.append(Problem("info", "style-default", "custom view treated as a text screen (use --style %s=native to make it a Melee menu)" % vid, vid))
    if style_override:
        for vid in style_override:
            if vid not in vmap:
                P.append(Problem("warn", "style-unknown-view", "--style names a view that does not exist", vid))

    # per-view checks
    kinds, free = {}, list(FREE_KINDS)
    for vid in order + [x for x in vmap if x not in depth]:
        v = vmap[vid]
        ents = v.get("entries") or []
        vis = [e for e in ents if not e.get("hidden")]
        s = style[vid]
        if not vis:
            P.append(Problem("warn" if vid != root else "error", "empty-view", "no visible row", vid))
        if v.get("default") is not None and v["default"] not in [e.get("id") for e in ents]:
            P.append(Problem("error", "bad-default", "default row %r is not in this view" % v["default"], vid))
        elif v.get("default") is not None and emap[v["default"]].get("hidden"):
            P.append(Problem("warn", "default-hidden", "the default row is hidden", vid))
        labels = {}
        for e in vis:
            labels.setdefault(e.get("label", "").strip().lower(), []).append(e["id"])
        for lab, ids in labels.items():
            if len(ids) > 1:
                P.append(Problem("warn", "dup-label", "visible rows share the label %r: %s" % (lab, ", ".join(ids)), vid))
        if s in ("stock", "native"):
            if len(vis) > MAX_ROWS:
                P.append(Problem("error", "too-many-rows", "%d visible rows, the row slot table has %d" % (len(vis), MAX_ROWS), vid))
            if s == "native" and len(ents) > MAX_ROWS:
                P.append(Problem("error", "too-many-slots", "%d rows including hidden ones, %d slots" % (len(ents), MAX_ROWS), vid))
            if s == "native":
                if not free:
                    P.append(Problem("error", "no-free-kind", "no unused Melee menu table slot left", vid))
                else:
                    kinds[vid] = free.pop(0)
            else:
                kinds[vid] = STOCK_KIND.get(vid)
        elif s == "text":
            for e in vis:
                if len(e["label"]) > TEXT_ITEM_MAX:
                    P.append(Problem("error", "item-too-long", "%d characters, a text screen item holds %d" % (len(e["label"]), TEXT_ITEM_MAX), e["id"]))
        for e in ents:
            lab = e.get("label", "")
            drawn = s in ("native", "text", "page")
            if drawn or _label_is_ours(e, s):
                bad = sorted(set(c for c in lab if c not in LABEL_CHARSET))
                if bad:
                    P.append(Problem("error", "label-chars", "characters the menu font cannot draw: %s" % "".join(bad), e["id"]))
            if s in ("native", "stock") and not e.get("hidden") and _needs_text(e, v) and len(lab) > LABEL_MAX:
                P.append(Problem("warn", "label-long", "%d characters, about %d fit at the row font (%s)" % (len(lab), LABEL_MAX, "provisional, measured in Dolphin"), e["id"]))
    # stock screens: does the retail think still do the job?  Rows that left a stock screen are hidden in place (the unlock function),
    # which the retail think tolerates; a foreign row, an added row or a different order needs our own think.
    stock_changed, hidden_in_place = {}, {}
    for vid, v in vmap.items():
        if style.get(vid) != "stock" or vid not in STOCK_KIND:
            continue
        k = STOCK_KIND[vid]
        ents = v.get("entries") or []
        why = []
        slots = []
        for e in ents:
            sk = STOCK_SEL.get(e["id"])
            if sk is None or sk[0] != k:
                why.append("%s is not a retail row of this screen" % e["id"])
            else:
                slots.append(sk[1])
        if slots != sorted(slots):
            why.append("order differs from retail")
        present = set(e["id"] for e in ents)
        gone = [rid for rid, sk in STOCK_SEL.items() if sk[0] == k and rid not in present and emap.get(rid, {}).get("origin") == "stock"]
        if k != 8:
            hidden_in_place[vid] = sorted(STOCK_SEL[r][1] for r in gone)
        if why and k != 8:
            stock_changed[vid] = why
    info = {"vmap": vmap, "emap": emap, "owner": owner, "parent": parent, "style": style, "kinds": kinds, "depth": depth,
            "order": order, "stock_changed": stock_changed, "hidden_in_place": hidden_in_place}
    # hiding information
    nh = sum(1 for e in emap.values() if e.get("hidden"))
    P.append(Problem("info", "counts", "%d views, %d rows, %d hidden, %d native screens need a free table slot" % (
        len(vmap), len(emap), nh, sum(1 for s in style.values() if s == "native"))))
    return P, info


def _stock_order(vid):
    """Row ids of a stock view in retail order (hidden retail slots excluded), from the tool's tree; None when unknown."""
    try:
        import site_menu_mockup as mm
    except Exception:
        return None
    for v in mm.stock_state()["views"]:
        if v["id"] == vid:
            return [e["id"] for e in v["entries"]]
    return None


def _label_is_ours(e, style):
    return e.get("origin") in ("ours", "custom")


def _has_stock_art(e):
    """True when the row keeps its retail label (so the plate shows retail art and no text is drawn)."""
    if e["id"] not in STOCK_SEL or e.get("origin") == "custom":
        return False
    if e["id"] == "options.multishine" or e["id"] == "1p.online":
        return False       # ours: text over an empty plate (Options row, N2) or Slippi's own art in their screen
    return not e.get("stockLabel") or e["stockLabel"] == e["label"]


def _needs_text(e, view):
    """Row of a native screen whose label we have to draw ourselves."""
    return not _has_stock_art(e)


def action_of(e, info):
    """What the row does, for the build table."""
    emap, style, kinds = info["emap"], info["style"], info["kinds"]
    o = _opens(e)
    if o:
        return {"do": "open", "view": o, "style": style[o], "kind": kinds.get(o)}
    eid = e["id"]
    if style.get(info["owner"][eid]) == "scene":
        return {"do": "scene"}            # a row of a retail scene we only enter (Multi-Man list), not changed by this build
    if eid == PAGE_ROOT_ROW:
        return {"do": "page"}
    if eid in STOCK_SEL and e.get("origin") in ("stock", "ours"):
        k, s = STOCK_SEL[eid]
        if k == 8:
            return {"do": "online_mode", "mode": s}
        return {"do": "stock", "kind": k, "sel": s}
    if e.get("origin") == "custom":
        alias = CUSTOM_ALIAS.get(e["label"].strip().lower())
        if alias:
            return dict(alias)
        mode = MODE_BY_ID.get(eid, ONLINE_MODES.get(e["label"].strip().lower()))
        v = info["owner"][eid]
        if mode is not None and v in ("main", "more") and style.get(v) in ("native", "stock"):
            return {"do": "online_mode", "mode": mode, "ours": True}
        return {"do": "stub"}
    return {"do": "stub"}


def build_table(layout, style_override=None):
    problems, info = analyse(layout, style_override)
    if info is None or any(p.level == "error" for p in problems):
        return problems, None
    vmap, emap, style, kinds, order = info["vmap"], info["emap"], info["style"], info["kinds"], info["order"]
    screens = []
    for vid in order + [x for x in vmap if x not in info["depth"]]:
        v = vmap[vid]
        s = style[vid]
        ents = v.get("entries") or []
        rows = []
        for pos, e in enumerate(ents):
            r = {"id": e["id"], "pos": pos, "label": e["label"], "hidden": bool(e.get("hidden")), "do": action_of(e, info)}
            if s == "stock" and e["id"] in STOCK_SEL and STOCK_SEL[e["id"]][0] == kinds.get(vid):
                r["slot"] = STOCK_SEL[e["id"]][1]
            elif s in ("native", "stock"):
                r["slot"] = pos
            if s in ("native", "stock"):
                if _has_stock_art(e):
                    k, sel = STOCK_SEL[e["id"]]
                    r["label_art"] = START_FRAME[k] + 2 * sel
                else:
                    r["label_text"] = e["label"]
            rows.append(r)
        sc = {"id": vid, "title": v.get("title", vid), "style": s, "kind": kinds.get(vid),
              "parent_row": info["parent"].get(vid), "default_row": v.get("default"),
              "remember_last": bool(v.get("rememberLast")), "rows": rows}
        if s in ("native", "stock"):
            vis = [r for r in rows if not r["hidden"]]
            sc["visible"] = len(vis)
            frames = [r.get("label_art") for r in rows]
            if s == "stock" and vid in STOCK_KIND:
                sc["hidden_in_place"] = info["hidden_in_place"].get(vid, [])
                if kinds.get(vid) != 8 and vid in info["stock_changed"]:
                    sc["why_ours"] = info["stock_changed"][vid]
            if all(f is not None for f in frames) and frames and all(frames[i + 1] - frames[i] == 2 for i in range(len(frames) - 1)):
                sc["label_plan"] = {"mode": "table", "start_frame": frames[0]}
            elif all(f is None for f in frames):
                sc["label_plan"] = {"mode": "text"}
            else:
                sc["label_plan"] = {"mode": "mixed"}
            if s == "stock":
                sc["think"] = "slippi" if kinds.get(vid) == 8 else ("ours" if vid in info["stock_changed"] else "stock")
            else:
                sc["think"] = "ours"
        screens.append(sc)
    table = {"format": TABLE_FORMAT, "version": TABLE_VERSION, "source_sha1": layout.get("_sha1", ""), "root": layout["root"], "screens": screens}
    return problems, table


# Wording: every string the runtime draws comes from docs/data/ui_strings.json (ids below), so the table of strings stays the one place for text.
DESC_ID = {      # row id -> ui string id (the line under the panel while the row is hovered; only rows whose label we draw)
    "1p.online": "ml.d_slippi", "vs.melee": "ml.d_local", "c9": "ml.d_misc", "c10": "ml.d_orig", "mk": "ml.d_mockups", "st": "ml.d_standings", "tn": "ml.d_tourney",
    "c2": "ml.d_ranked", "c3": "ml.d_unranked", "c4": "ml.d_direct", "c5": "ml.d_tournament", "c6": "ml.d_teams", "c7": "ml.d_party",
    "fr.f": "ml.d_friends", "set.f": "ml.d_settings",
}
PAGE_ROW_ID = {   # stub text screen row id -> ui string id (wording of docs/data/ui_strings.json, not the layout's names)
    "mt.join": "ml.r_mt_join", "mt.matches": "ml.r_mt_matches", "mt.bracket": "ml.r_mt_bracket", "mt.results": "ml.r_mt_results",
    "acct.qr": "li.r_qr", "acct.code": "li.r_code", "acct.email": "li.r_mail", "acct.new": "ac.t", "acct.name": "li.r_new", "acct.out": "li.t_out",
    "fr.list": "ml.r_fr_list", "fr.inv": "ml.r_fr_invites", "fr.add": "fr.r_add", "fr.block": "ml.r_fr_blocked",
    "acct.m": "ml.r_acm", "acct.s": "ml.r_acs",
    "mo.g": "op.tab_game", "mo.o": "op.tab_online", "mo.n": "op.tab_network", "mo.t": "op.tab_training", "mo.s": "op.tab_system", "mo.k": "ml.r_gecko",
}
PAGE_NOTE_ID = {"mt": "ml.n_mt", "acct": "ml.n_acct", "fr": "ml.n_fr"}     # note under the title of a stub text screen (Settings "mo": none)
UI_STRINGS = os.path.join(os.path.dirname(HERE), "docs", "data", "ui_strings.json")
DESC_MAX = 46
# Plate label art.  Slippi's own label images that exist in the user's patched MnMaAll (frame of the label request)
# and the free 8 x 4 placeholder images of the retail label animation we may fill with our own rendering at run time: (frame, image index).
SLIPPI_ART = {"ranked": 140, "unranked": 142, "direct": 144, "teams": 146, "party": 148}
DONORS = [(220, 49), (222, 50), (224, 51), (226, 52), (180, 38), (182, 39), (184, 40), (64, 17), (86, 22), (150, 59), (152, 60), (154, 61)]    # the last three are Slippi's Log-in, Log-out and Update plates (never shown: 176 x 30, overwritten in place)
# The hub's Grandmaster / Dojo / Mano a mano rows (c2-c4, ex Ranked / Unranked / Direct) used to borrow Slippi's label images; with their own names they draw into
# the three plates Slippi's screen never shows, and every other own-art row keeps the placeholder it had
DONOR_BY_ID = {}
# (rule: nothing we ship may need Slippi's or Nintendo's art): the hub rows no longer borrow Slippi's Log-in / Log-out / Update / Ranked plates (images 59-61 and 33
# exist only in Slippi's 64-image archive).  Every own-art row draws into a retail 8 x 4 placeholder, and the placeholders are SHARED between screens: each screen picks its donors from
# the same pool (below) and the runtime repoints the placeholder descriptors to the screen's own label buffers when the screen is built (ml_glue.c tex_point).
_OLD_DONOR_BY_ID = {"c2": (150, 59), "c3": (152, 60), "c4": (154, 61),
               "st": (140, 33)}    # hub row Standings (the Main Menu row Mockups had it before): Slippi's Ranked plate (frame 140, image 33 of their 64-image table),
                                   # hidden in Slippi's screen and no longer drawn by our hub since the Grandmaster rename; overwritten in place like Log-in/Log-out/Update, only
                                   # when the table is Slippi's (ml_glue.c g_slippi_ok)
ART_FRAMES = [0] + [f for f, _ in DONORS] + sorted(SLIPPI_ART.values())          # table of the label stub (ml_stub.S ml_ftab), index 0 = none
ACT = {"none": 0, "kind": 1, "online": 2, "stub": 3, "mode": 4, "page": 5, "scene": 6, "sign": 7}
GM_DEBUG = 6
GM_TOURNAMENT = 0x1B
# Game Night (c5) no longer starts GM_TOURNAMENT: on the card route the exploit save copies the Gecko codehandler over the Tournament Melee code
# (game text 0x801910E0..), so Tournament Melee jumps into garbage and the console freezes.  The row opens
# its own text page (mt, "Server not available.") until the Game night overlay exists.  The stock VS. Mode > Tournament Melee row stays redirected to VS by
# the exploit's own framing (0x8022D638 li r0,2).
ROW_SCENE = {}
SG_MOCK = 10         # offline/multishine/menus/sign/sg_ui.h: the Mockups page of the sign overlay
SG_GN = 18           # sg_ui.h SG_GN: the Game Night browser (the retail Event Match screen with the rooms server's rooms, never scene 0x1B)
SG_STAND = 21        # sg_ui.h SG_STAND: the Grandmaster standings (the retail Event Match screen, STANDINGS_LIST 0x33)
SG_MOCK_SCREENS = frozenset(range(SG_MOCK, 18)) | {19}      # sg_ui.h SG_MOCK .. SG_MK_REC and SG_MK_GNG: example-data screens, never opened by a default build's table
SG_TOURN = 25        # sg_ui.h SG_TOURN: the Tournament screens (the retail Event Match screen)
SG_FRD = 22          # sg_ui.h SG_FRD: the Friends screen
ROW_SIGN = {"mk": SG_MOCK, "c5": SG_GN, "st": SG_STAND, "fr.f": SG_FRD, "tn": SG_TOURN}     # rows that open the sign overlay on a given page (ML_A_SIGN)
# Development-only rows (Mockups left the Main Menu of the real game): emitted under `#if ML_MOCKUPS` (default 0) at the end of their screen, so
# -DML_MOCKUPS=1 brings the row back for development.  (view, row id, label, action, arg, plate donor (frame, image)): the donor is the one plate no default row uses.
DEV_ROWS = [("main", "mk", "Mockups", "sign", SG_MOCK, (86, 22))]
GM_VS = 2            # Melee game mode number of the stock VS. Mode > Melee row (decomp gm/forward.h GameModeKind)


def ui_text(sid):
    with open(UI_STRINGS, "r", encoding="utf-8") as f:
        for s_ in json.load(f)["strings"]:
            if s_["id"] == sid:
                return s_["text"]
    raise ValueError("ui string %r is not in docs/data/ui_strings.json" % sid)


def _c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def hides_of(layout, table, keep=()):
    """(kind, slot) pairs the runtime hides in retail screens: rows our hid in a stock screen (Slippi's online screen), and stock or Slippi rows
    that live elsewhere in our tree (Slippi's Online Play row in 1-P Mode).  `keep` lists row ids that stay visible in their home screen because
    something still leads to them (the first image opens VS Mode for Local Melee, so the Melee row stays)."""
    out = set()
    placed = {}
    for sc_ in table["screens"]:
        for r in sc_["rows"]:
            placed[r["id"]] = sc_
            if sc_["style"] == "stock" and r["hidden"] and r["id"] in STOCK_SEL and STOCK_SEL[r["id"]][0] == sc_["kind"]:
                out.add(STOCK_SEL[r["id"]])
    for rid, (k, slot) in STOCK_SEL.items():
        sc_ = placed.get(rid)
        if sc_ is None or rid in keep or k == 0:
            continue
        home_view = [v for v, kk in STOCK_KIND.items() if kk == k]
        if sc_["id"] not in home_view and sc_["kind"] != k:
            out.add((k, slot))
    for rid in STOCK_ALIAS_IDS:        # a second entry id of a retail row, standing in the retail screen (VS. Mode > Melee): the slot stays visible
        sc_ = placed.get(rid)
        if sc_ is not None and sc_["style"] == "stock" and sc_["kind"] == STOCK_SEL[rid][0] and not [r for r in sc_["rows"] if r["id"] == rid][0]["hidden"]:
            out.discard(STOCK_SEL[rid])
    return sorted(out)


def cheader(layout, table, screens, simplify=None, keep=()):
    """C source of the runtime table for the listed screens (view ids): ml_screens (Melee menu screens we rebuild) and ml_pages (stub text screens
    they open).  `simplify` maps a view id that the runtime cannot build yet to "online" (Slippi's own online screen) or "kind:N" (a retail screen);
    a row that opens an unbuilt view without a simplification becomes a stub."""
    simplify = simplify or {}
    sc = {s_["id"]: s_ for s_ in table["screens"]}
    built = {sc[v]["kind"] for v in screens}
    pages = []                       # view ids of the text screens, in order of first use
    out = ["/* Generated by tools/menu_layout.py cheader from the menu layout (SHA-1 %s). Do not edit; regenerate. */" % table["source_sha1"],
           "#define ML_TABLE_SOURCE_SHA1 \"%s\"" % table["source_sha1"], "#define ML_NSCREEN %d" % len(screens), ""]
    hides_all = hides_of(layout, table, keep)
    recs = []
    pool = [d_ for d_ in DONORS if d_ not in DONOR_BY_ID.values() and d_ != DEV_ROWS[0][5]]       # per screen: the plates of one screen are never shown together with another screen's
    tex_items = []
    for vid in screens:
        donors = list(pool)
        s_ = sc[vid]
        if s_["style"] not in ("native", "stock") or s_["kind"] is None:
            raise ValueError("%s is not a Melee menu screen" % vid)
        rows = []
        for r in s_["rows"]:
            do = r["do"]
            act, arg, hov = "stub", 0, 0
            if do["do"] == "open":
                tgt = do["view"]
                if do["style"] == "text":
                    if tgt not in pages:
                        pages.append(tgt)
                    act, arg = "page", pages.index(tgt)
                elif do["kind"] is not None and (do["kind"] in built or (do["style"] == "stock" and tgt != "online")):
                    act, arg = "kind", do["kind"]
                elif tgt == "online" or simplify.get(tgt) == "online":
                    act = "online"
                elif simplify.get(tgt, "").startswith("kind:"):
                    act, arg = "kind", int(simplify[tgt].split(":")[1])
            elif do["do"] == "online_mode":
                if do.get("ours"):
                    act, arg = "mode", do["mode"]
                else:
                    act = "online"
            elif r["id"] == "options.debug":
                act, arg = "scene", GM_DEBUG        # Melee's developer debug menu (game mode 6)
            elif do["do"] == "stock" and do["sel"] == 0 and do["kind"] == 2:
                act, arg = "scene", GM_VS         # Couch: the stock VS. Mode > Melee row, straight to the character select
            if r["id"] in ROW_SCENE:
                act, arg = "scene", ROW_SCENE[r["id"]]      # rows that start a stock game mode (hub Tournament: the original Tournament Melee setup)
            if r["id"] in ROW_SIGN:
                act, arg = "sign", ROW_SIGN[r["id"]]        # Main Menu Mockups: the sign overlay opened on its Mockups page
            label = r["label"]
            text = "label_text" in r
            d = ""
            if text and r["id"] in DESC_ID:
                d = ui_text(DESC_ID[r["id"]])
                if len(d) > DESC_MAX or any(c not in LABEL_CHARSET for c in d):
                    raise ValueError("bad description for %s" % r["id"])
            tex, didx, art = 255, 0, 0
            if text and not r["hidden"]:
                if vid in ("main", "more") and label.lower() in SLIPPI_ART:
                    art = SLIPPI_ART[label.lower()]
                elif r["id"] in DONOR_BY_ID:
                    art, didx = DONOR_BY_ID[r["id"]]
                    tex = len(tex_items)
                    tex_items.append((r["id"], label))
                elif donors:
                    art, didx = donors.pop(0)
                    tex = len(tex_items)
                    tex_items.append((r["id"], label))
            if act == "kind":
                hid = set(sl for k_, sl in hides_all if k_ == arg)
                while hov in hid:
                    hov += 1
            rows.append((r["id"], label, d, text, act, arg, hov, r["hidden"], tex, didx, ART_FRAMES.index(art) if art else 0))
        parent = None
        if s_["parent_row"]:
            for pv in table["screens"]:
                for r in pv["rows"]:
                    if r["id"] == s_["parent_row"]:
                        parent = (pv["kind"], r["pos"])
        dev = []
        for dv, rid, label, act, arg, donor in DEV_ROWS:
            if dv != vid:
                continue
            d = ui_text(DESC_ID[rid])
            dev.append((rid, label, d, True, act, arg, 0, False, None, donor[1], ART_FRAMES.index(donor[0])))
        recs.append((vid, s_, rows, parent, dev))
    out.insert(3, "#define ML_NPAGE %d" % len(pages))
    if any(rec[4] for rec in recs):
        out.insert(4, "#ifndef ML_MOCKUPS\n#define ML_MOCKUPS 0        /* 1: the development-only rows (Main Menu Mockups) are built in */\n#endif")
    for vid, s_, rows, parent, dev in recs:
        for i, r in enumerate(dev):
            dev[i] = r[:8] + (len(tex_items),) + r[9:]
            tex_items.append((r[0], r[1]))
        out.append("static const struct ml_row ml_rows_%s[%s] = {" % (vid, ("%d + ML_MOCKUPS" % len(rows)) if dev else "%d" % len(rows)))
        for i, (rid, label, d, text, act, arg, hov, hidden, tex, didx, art) in enumerate(rows + dev):
            if i == len(rows):
                out.append("#if ML_MOCKUPS")
            out.append("    { %s, %s, ML_A_%s, %d, %d, %d, %d, %d, %d, %d }, /* %s%s */" % (
                _c_str(label), _c_str(d), act.upper(), arg, hov, 1 if hidden else 0, 1 if text else 0, tex, didx, art, rid,
                " (development builds only: -DML_MOCKUPS=1)" if i >= len(rows) else ""))
        if dev:
            out.append("#endif")
        out.append("};")
    out.append("")
    out.append("static const struct ml_screen ml_screens[ML_NSCREEN] = {")
    for vid, s_, rows, parent, dev in recs:
        out.append("    { %d, %s, %d, %d, %d, ml_rows_%s }, /* %s %s */" % (
            s_["kind"], ("%d + ML_MOCKUPS" % len(rows)) if dev else "%d" % len(rows), 1 if s_["label_plan"]["mode"] == "text" else 0,
            parent[0] if parent else 255, parent[1] if parent else 0, vid, vid, s_["title"]))
    out.append("};")
    out.append("")
    for pv in pages:
        ps = sc[pv]
        if ps["style"] != "text":
            raise ValueError("%s is not a text screen" % pv)
        for r in ps["rows"]:
            if len(r["label"]) > TEXT_ITEM_MAX or any(c not in LABEL_CHARSET for c in r["label"]):
                raise ValueError("bad page row %s" % r["id"])
        texts = [ui_text(PAGE_ROW_ID[r["id"]]) if r["id"] in PAGE_ROW_ID else r["label"] for r in ps["rows"]]
        for t_ in texts:
            if len(t_) > TEXT_ITEM_MAX or any(c not in LABEL_CHARSET for c in t_):
                raise ValueError("bad page row text %r" % t_)
        out.append("static const char *const ml_page_rows_%s[%d] = { %s };" % (pv, len(ps["rows"]), ", ".join(_c_str(t_) for t_ in texts)))
    hides = hides_of(layout, table, keep)
    out.append("static const unsigned char ml_hide_tab[%d][2] = { %s };" % (max(len(hides), 1), ", ".join("{ %d, %d }" % h for h in hides) if hides else "{ 255, 0 }"))
    out.append("#define ML_NHIDE %d" % len(hides))
    out.append("static const struct ml_page ml_pages[%s] = {" % ("ML_NPAGE" if pages else "1"))
    for pv in pages:
        ps = sc[pv]
        note = ui_text(PAGE_NOTE_ID[pv]) if pv in PAGE_NOTE_ID else ""
        out.append("    { %s, %s, %d, ml_page_rows_%s }, /* %s */" % (_c_str(ps["title"]), _c_str(note), len(ps["rows"]), pv, pv))
    if not pages:
        out.append("    { \"\", \"\", 0, 0 },")
    out.append("};")
    cheader.tex_items = tex_items
    return "\n".join(out) + "\n"


def outline(layout):
    vmap = {v["id"]: v for v in layout["views"]}
    out = []

    def walk(vid, ind, seen):
        v = vmap[vid]
        out.append("%s[%s] %s" % ("  " * ind, vid, v.get("title", vid)))
        for n, e in enumerate(v.get("entries") or [], 1):
            mark = "-" if e.get("hidden") else " "
            star = "*" if v.get("default") == e["id"] else " "
            o = _opens(e)
            out.append("%s%s%s%d. %s%s" % ("  " * (ind + 1), mark, star, n, e["label"], (" >" if o else "")))
            if o in vmap and o not in seen:
                walk(o, ind + 2, seen | {o})
    walk(layout["root"], 0, {layout["root"]})
    return "\n".join(out)


def _parse_styles(items):
    d = {}
    for it in items or []:
        if "=" not in it:
            raise SystemExit("--style wants VIEW=native|text|page|stock|scene")
        k, v = it.split("=", 1)
        if v not in ("native", "text", "page", "stock", "scene"):
            raise SystemExit("unknown style %r" % v)
        d[k] = v
    return d


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["check", "table", "outline", "cheader"])
    ap.add_argument("--screens", help="cheader: comma separated view ids")
    ap.add_argument("--simplify", help="cheader: VIEW=online|kind:N,... for views the runtime cannot build yet")
    ap.add_argument("--keep", help="cheader: row ids that stay visible in their retail screen (comma separated)")
    ap.add_argument("file")
    ap.add_argument("--style", action="append", help="VIEW=native|text|page override (repeatable)")
    ap.add_argument("--out", help="table: write here instead of stdout")
    ap.add_argument("--tex-out", help="cheader: also write the label bitmaps (tools/menu_label_art.py) of the rows that get our own plate art")
    a = ap.parse_args(argv)
    try:
        layout = load(a.file)
    except (IOError, OSError, ValueError) as e:
        print("menu_layout: %s" % e, file=sys.stderr)
        return 2
    st = _parse_styles(a.style)
    if a.cmd == "outline":
        print(outline(layout))
        return 0
    problems, table = build_table(layout, st)
    for p in problems:
        print(p, file=sys.stderr if a.cmd in ("table", "cheader") else sys.stdout)
    errs = [p for p in problems if p.level == "error"]
    if a.cmd == "check":
        print("%d errors, %d warnings" % (len(errs), sum(1 for p in problems if p.level == "warn")))
        return 1 if errs else 0
    if errs or table is None:
        print("menu_layout: not writing a table, %d errors" % len(errs), file=sys.stderr)
        return 1
    if a.cmd == "cheader":
        simp = dict(x.split("=", 1) for x in (a.simplify or "").split(",") if x)
        txt = cheader(layout, table, [x for x in (a.screens or "").split(",") if x], simp, [x for x in (a.keep or "").split(",") if x])
    else:
        txt = json.dumps(table, indent=1, sort_keys=True) + "\n"
    if a.cmd == "cheader" and a.tex_out:
        import menu_label_art
        with open(a.tex_out, "w") as f:
            f.write(menu_label_art.header(cheader.tex_items))
    if a.out:
        with open(a.out, "w") as f:
            f.write(txt)
    else:
        sys.stdout.write(txt)
    return 0


if __name__ == "__main__":
    sys.exit(main())
