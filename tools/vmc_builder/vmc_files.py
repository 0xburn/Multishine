# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the Multishine card files for the VMC builder.  The file formats stay single-sourced in the offline tools:
offline/options/optfile.py (options + optional quick-chat table, 'SDOF') and offline/provision/multishine_provision.py
(identity, 'SDID').  This module only turns their GCI output into (directory entry, data blocks) pairs and parses them back."""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
for sub in ("offline", "offline/options", "offline/provision", "offline/multishine/menus/om", "offline/multishine/menus/on"):
    sys.path.insert(0, os.path.join(REPO, sub))
import gen_chat  # noqa: E402
import multishine_provision as P  # noqa: E402
import optfile  # noqa: E402
import codehist  # noqa: E402
import onfile  # noqa: E402
import projname  # noqa: E402

OPTIONS_NAME = optfile.FILE_NAME
IDENTITY_NAME = P.DEFAULT_NAME
GCI_ENTRY = P.GCI_ENTRY
ONLINE_NAME = onfile.FILE_NAME             # online setup (server, pinned key, identity) read by the console (offline/multishine/menus/on/on_card.c); holds a secret
CODES_NAME = projname.SHORT + "Codes"      # direct-code history written by the console (offline/multishine/menus/om/om_card.c): codes only


BRIDGE_NAME = projname.SHORT + "Bridge"    # the bridge's cache of the signed status document, written only by the console (offline/bridge/sbr_card.h): never built here, only moved with export/import
import card_text  # noqa: E402
OMC_LINE1, OMC_LINE2 = card_text.pair("Codes")     # comment lines of MultishineCodes (om_card.c OMC_LINE1/2 mirror this)


class FileError(Exception):
    pass


def _json(path, what):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        raise FileError("%s: cannot read %s as JSON (%s)" % (path, what, getattr(e, "strerror", None) or "invalid JSON"))


def options_file(options_json, chat_json=None, mtime=None):
    """-> (entry, data) for MultishineOptions from an options JSON path (and an optional chat JSON path)."""
    try:
        vals = optfile.values_from_json(_json(options_json, "options"))
        chat = None
        if chat_json:
            chat = gen_chat.pack(gen_chat.texts_from_json(_json(chat_json, "chat")), custom=True)
        gci = optfile.build_gci(optfile.pack(vals, chat), mtime=mtime)
    except (optfile.OptFileError, gen_chat.ChatError) as e:
        raise FileError(str(e))
    return gci[:GCI_ENTRY], gci[GCI_ENTRY:]


def identity_file(user_json, mtime=None):
    """-> (entry, data, connectCode, displayName) for MultishineIdentity from the user's OWN user.json path (never searched for)."""
    try:
        ident = P.load_user_json(user_json)
        gci = P.build_gci(ident, mtime=mtime)
    except P.ProvisionError as e:
        raise FileError(str(e))
    return gci[:GCI_ENTRY], gci[GCI_ENTRY:], ident["connectCode"], ident["displayName"]


def online_file(setup_json, mtime=None, server=None):
    """-> (entry, data, connectCode, server) for MultishineOnline from a setup JSON path (format in offline/multishine/menus/on/onfile.py).
    server: optional {"host": "name", "ip": "A.B.C.D", "port": N} from the card config ("online_server", vmc_config.py): replaces the setup's server keys."""
    try:
        spec = dict(onfile.spec_from_json(_json(setup_json, "online setup")))
        if server:
            for k in ("server", "server_ip", "server_host", "server_port"):
                spec.pop(k, None)
            if server.get("ip"):
                spec["server"] = server["ip"]
            if server.get("host"):
                spec["server_host"] = server["host"]
            if server.get("port"):
                spec["server_port"] = server["port"]
        rec = onfile.pack(spec)
    except (onfile.SetupError, ValueError) as e:
        raise FileError("%s: %s" % (setup_json, e))
    data = bytearray(P.BLOCK)
    data[0:32] = onfile.LINE1.encode().ljust(32, b"\0")
    data[32:64] = onfile.LINE2.encode().ljust(32, b"\0")
    P.put_icon(data, ONLINE_NAME)
    data[P.PAYLOAD_OFFSET:P.PAYLOAD_OFFSET + onfile.REC_SIZE] = rec
    ext = onfile.pack_ext(spec)                       # the optional server NAME; old readers never look at it
    if ext:
        data[P.PAYLOAD_OFFSET + onfile.EXT_OFF:P.PAYLOAD_OFFSET + onfile.EXT_END] = ext
    try:                                              # optional "slot": the identity also goes into that environment's slot
        data[P.PAYLOAD_OFFSET:] = onfile.add_slot(bytes(data[P.PAYLOAD_OFFSET:]), spec)[:len(data) - P.PAYLOAD_OFFSET]
    except onfile.SetupError as e:
        raise FileError("%s: %s" % (setup_json, e))
    pub = onfile.unpack(bytes(data[P.PAYLOAD_OFFSET:P.PAYLOAD_OFFSET + onfile.EXT_END]))
    return P.make_dir_entry(ONLINE_NAME, 0, 1, P.PERM_PUBLIC, mtime), bytes(data), pub["code"], pub["server"]


def codes_file(codes, mtime=None):
    """-> (entry, data) for MultishineCodes pre-filled with direct connect codes (newest first; the console normally writes this file itself)."""
    codes = [c.strip().upper() for c in codes if c.strip()]
    bad = [c for c in codes if not codehist.valid(c)]
    if bad or len(codes) > codehist.MAX or len(set(codes)) != len(codes):
        raise FileError("--codes needs up to %d distinct connect codes like ABCD#123 (no spaces)" % codehist.MAX)
    blk = bytearray(codehist.new_block(OMC_LINE1, OMC_LINE2, codes))
    P.put_icon(blk, CODES_NAME)                       # the console rewrites only the payload of an existing file, so the icon stays
    return P.make_dir_entry(CODES_NAME, 0, 1, P.PERM_PUBLIC, mtime), bytes(blk)


def describe_online(data):
    """Online setup file: server, connect code, name and modes only; the pinned key is shown as a short fingerprint, the device secret never."""
    rec = bytes(data[onfile.PAYLOAD_OFF:onfile.PAYLOAD_OFF + onfile.EXT_END])
    v = onfile.check(rec) if len(rec) == onfile.EXT_END else onfile.DAMAGED
    if v == onfile.OK:
        u = onfile.unpack(rec)
        return {"ok": True, "server": u["server"], "server_host": u["server_host"], "code": u["code"], "name": u["name"], "pin": u["pin"][:16] + "...", "unranked": u["unranked"], "ranked": u["ranked"],
                "slots": onfile.slots_view(bytes(data[onfile.PAYLOAD_OFF:onfile.PAYLOAD_OFF + onfile.SL_END]))}   # None = the old single identity (no SDSL yet); never a secret
    return {"ok": False, "error": {onfile.NEWER: "from a newer version (the console ignores it)", onfile.INCOMPLETE: "incomplete (the console refuses online play with a message)"}.get(v, "damaged (the console refuses online play with a message)")}


def describe_options(data):
    """Parse an options file's data blocks: {"ok": True, "options": {...}, "chat": ...} or {"ok": False, "error": str}."""
    try:
        r = optfile.unpack(data[P.PAYLOAD_OFFSET:P.PAYLOAD_OFFSET + optfile.SIZE])
        return {"ok": True, "options": r["options"], "chat": r["chat"]}
    except optfile.OptFileError as e:
        return {"ok": False, "error": str(e)}


def describe_identity(data):
    """Identity file: connect code and display name only; uid and playKey are never returned."""
    try:
        i = P.parse_payload(data[P.PAYLOAD_OFFSET:])
        return {"ok": True, "connectCode": P.safe_text(i["connectCode"]), "displayName": P.safe_text(i["displayName"])}
    except P.ProvisionError as e:
        return {"ok": False, "error": str(e)}


def describe_codes(data):
    """Direct-code history file: the connect codes the player typed in Direct mode (no secrets; a connect code is public) or why it is unusable."""
    rec = bytes(data[codehist.PAYLOAD_OFF:codehist.PAYLOAD_OFF + codehist.REC_SIZE])
    v = codehist.check(rec) if len(rec) == codehist.REC_SIZE else codehist.DAMAGED
    if v == codehist.OK:
        return {"ok": True, "codes": codehist.decode(rec)[0]}
    return {"ok": False, "error": "from a newer version (kept untouched)" if v == codehist.NEWER else "damaged (the console replaces it at the next confirmed code)"}


def options_template():
    """A starting options JSON with every key listed (each set to its lowest legal value; trim the file to the keys you want to change)."""
    return {"format": "multishine-options", "version": 1,
            "_comment": "Delete the keys you do not want to set. Ranges: " + ", ".join(
                "%s %d..%d" % (k, lo, hi) for k, (_, lo, hi) in sorted(optfile.RANGES.items(), key=lambda kv: kv[1][0])),
            "options": {k: lo for k, (_, lo, hi) in sorted(optfile.RANGES.items(), key=lambda kv: kv[1][0])}}
