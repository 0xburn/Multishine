# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""ONE card configuration schema over the existing card files (three-file policy).

  {"format": "multishine-card-config", "version": 1,
   "options": {"widescreen": 1, "delay": 3, "pool": 1, "ucf": null, ...},   # MultishineOptions value bytes (SDOF v1); null = back to the patched default
   "network": {"mm_ip": "A.B.C.D", "status_ip": "A.B.C.D", "status_port": 43150, "dns_ip": "A.B.C.D"},   # DEV/LAB ONLY: section inside MultishineBridge
   "online":  "",                                                  # MultishineOnline (hub server setup, offline/multishine/menus/on/onfile.py)
   "online_server": {"host": "lab.example.org", "ip": "A.B.C.D", "port": 43150}}   # optional: replaces the server keys of; "host" is resolved by DNS at menu
                                                                             # time on the console, a numeric "ip" wins when both are given

Every key is classed: user-level (options), dev-only (network), lab setup (online).  --release refuses the dev-only part.  Unknown keys are refused
by this tool (a typo must not be silently dropped); the console readers ignore unknown option ids and unknown sections.  Option bytes are edited in place
in MultishineOptions: the comment, icon, quick-chat table and untouched values stay, only the CRC is recomputed (like the console's own write)."""
import os
import struct
import sys
import zlib

import vmc_card as C
import vmc_netcfg as NC

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "offline", "options")))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "offline", "provision")))
import optfile  # noqa: E402
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "offline", "multishine", "menus", "on")))
import onfile  # noqa: E402

PAYLOAD = 0x1000
# Option ids beyond optfile.RANGES (the Settings screen,, offline/multishine/menus/ml/ml_set.h): id 13 matchmaking pool
# 0 Multishine and Slippi, 1 Multishine only, 2 Slippi only.  The another module owns new ids; extend here in the same commit that extends ml_set.h.
EXTRA = {"pool": (13, 0, 2)}
KEYS = dict(optfile.RANGES)
KEYS.update(EXTRA)
USER_KEYS = sorted(KEYS)
DEV_SECTIONS = ("network",)
TOP = {"format", "version", "options", "network", "online", "online_server", "_comment"}


class ConfigError(Exception):
    pass


def parse(d, release=False):
    """Validate a config dict -> normalised {'options': {key: int|None}, 'network': {..}, 'online': path|None}."""
    if not isinstance(d, dict) or d.get("format") != "multishine-card-config" or d.get("version") != 1:
        raise ConfigError('need "format": "multishine-card-config", "version": 1')
    extra = set(d) - TOP
    if extra:
        raise ConfigError("unknown keys: " + ", ".join(sorted(extra)))
    out = {"options": {}, "network": {}, "online": None, "online_server": None}
    for k, v in (d.get("options") or {}).items():
        if k not in KEYS:
            raise ConfigError("unknown option %r (known: %s)" % (k, ", ".join(USER_KEYS)))
        i, lo, hi = KEYS[k]
        if v is None:
            out["options"][k] = None
        elif not isinstance(v, int) or isinstance(v, bool) or not lo <= v <= hi:
            raise ConfigError("option %s: %r outside %d..%d" % (k, v, lo, hi))
        else:
            out["options"][k] = v
    net = d.get("network") or {}
    if net:
        if release:
            raise ConfigError("'network' is a DEV/LAB-only section; a release image ignores it (drop it, or omit --release)")
        extra = set(net) - {"mm_ip", "status_ip", "status_port", "dns_ip"}
        if extra:
            raise ConfigError("network: unknown keys %s (the matchmaker name, its port and the flags can never come from the card)" % ", ".join(sorted(extra)))
        NC.encode(net)     # validates
        out["network"] = dict(net)
    if d.get("online"):
        if not isinstance(d["online"], str):
            raise ConfigError('"online" is the path of an online setup JSON')
        out["online"] = d["online"]
    srv = d.get("online_server")
    if srv:
        if not d.get("online"):
            raise ConfigError('"online_server" needs "online" (the setup JSON that holds the key and the identity)')
        if not isinstance(srv, dict) or set(srv) - {"host", "ip", "port"} or not (srv.get("host") or srv.get("ip")):
            raise ConfigError('"online_server" is {"host": "name", "ip": "A.B.C.D", "port": N} with at least a host or an ip')
        if srv.get("host") and not onfile.host_ok(str(srv["host"]).lower()):
            raise ConfigError("online_server.host must be 1..%d characters of letters, digits, dots and hyphens (labels 1..63, no hyphen at a label edge)" % onfile.HOST_MAX)
        if srv.get("port") is not None and (not isinstance(srv["port"], int) or isinstance(srv["port"], bool) or not 1 <= srv["port"] <= 65535):
            raise ConfigError("online_server.port must be 1..65535")
        out["online_server"] = {k: srv[k] for k in ("host", "ip", "port") if srv.get(k)}
    return out


def read_options(block):
    """block = first block of MultishineOptions -> (state, values list)  state: ok | damaged | newer"""
    p = bytes(block[PAYLOAD:PAYLOAD + optfile.SIZE])
    if len(p) < optfile.SIZE or p[0:4] != optfile.MAGIC:
        return "damaged", None
    if p[4] > optfile.VERSION or p[5] > optfile.NVAL:
        return "newer", None
    if p[4] != optfile.VERSION or p[5] != optfile.NVAL or struct.unpack(">I", p[8:12])[0] != zlib.crc32(p[0x0C:]) & 0xFFFFFFFF:
        return "damaged", None
    return "ok", list(p[0x0C:0x0C + optfile.NVAL])


def write_options(block, values):
    b = bytearray(block)
    p = bytearray(b[PAYLOAD:PAYLOAD + optfile.SIZE])
    p[0x0C:0x0C + optfile.NVAL] = bytes(values)
    struct.pack_into(">I", p, 8, zlib.crc32(bytes(p[0x0C:])) & 0xFFFFFFFF)
    b[PAYLOAD:PAYLOAD + optfile.SIZE] = p
    return bytes(b)


def apply_options(values, opts):
    v = list(values)
    for k, x in opts.items():
        v[KEYS[k][0]] = optfile.UNSET if x is None else x
    return v


def describe_options(values):
    by_id = {i: k for k, (i, lo, hi) in KEYS.items()}
    return {by_id[i]: (None if values[i] == optfile.UNSET else values[i]) for i in sorted(by_id) if i < len(values)}
