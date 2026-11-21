#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Which server a build and the dev OTA loop point at: the 'lab' profile (our Mac lab server, today's behaviour) or the 'online' profile (the server).
One table (the lab stub here; the online/dev server values load from private/ms_target.local.json when it exists), read by tools/card/build_card.sh (--online), tools/dev_ota.sh (--target) and the publish tool (outside this repository);.  Stdlib only, public values only.

  ms_target.py current                 the target in force: $MS_TARGET (lab | online | dev), default lab (our flips the default by exporting MS_TARGET=online)
  ms_target.py show [TARGET]           what the target means (server, key, update source, build flags)
  ms_target.py get TARGET KEY          one value (e.g. update_dir, vps_host, update_pub)
  ms_target.py odcfg-args [TARGET]     the tools/card/patch_odcfg.py arguments of the target's patched server block (online only; lab patches none)

$MS_ONLINE_ADDR = ip (default) | name: how the online build names the server.  ip: the numeric address AND the name are patched; the console uses the address
(a numeric address always wins in od_apply_config, so no DNS is needed; the server address is static).  name: only play.example.org, resolved by the
console's DNS client at menu time (Dolphin-proven, not console-proven).  There is no automatic name-then-address fallback in the engine."""
import json
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT = "lab"

TARGETS = {
    "lab": {
        "what": "a lab server (a lab update server (not in this repository), udp 43150); the server address and pinned key come from the card record MultishineOnline",
        "server": "from the card (MultishineOnline: 192.168.1.32 or lab.example.org, lab key)",
        "server_key": "",
        "bake": False,
        "update_dir": os.path.join(REPO, ".analysis-deps/work/flippy_stage/ota_loop_1/update"),
        "publish": "local directory served by the running lab server (no restart: the catalog rescans on every check)",
        "build_flags": "build_card.sh rc1 --dev (dev_ota.sh)",
    },
}


def _load_private():
    """The server profiles ('online', 'dev') are server specifics: they live in the private tree (private/ms_target.local.json, or $MS_TARGET_LOCAL), not in this public file.
    Same keys as 'lab'; a string containing {REPO} is relative to the repo root, a leading ~ is the home directory.  Without the file only 'lab' exists."""
    path = os.environ.get("MS_TARGET_LOCAL", "") or os.path.join(REPO, "private", "ms_target.local.json")
    if not os.path.isfile(path):
        return
    with open(path) as f:
        data = json.load(f)
    for name, t in data.items():
        for k, v in list(t.items()):
            if isinstance(v, str):
                v = v.replace("{REPO}", REPO)
                t[k] = os.path.expanduser(v) if v.startswith("~") else v
        TARGETS[name] = t


_load_private()


def current():
    t = os.environ.get("MS_TARGET", "") or DEFAULT
    if t not in TARGETS:
        raise SystemExit("MS_TARGET must be one of %s, not %r (online/dev need the private ms_target.local.json)" % (", ".join(TARGETS), t))
    return t


def addr_mode():
    m = os.environ.get("MS_ONLINE_ADDR", "") or "ip"
    if m not in ("ip", "name"):
        raise SystemExit("MS_ONLINE_ADDR must be ip or name, not %r" % m)
    return m


def odcfg_args(target):
    t = TARGETS[target]
    if not t["bake"]:
        return []
    a = ["--lock", "--pin", t["server_key"], "--port", str(t["port"]), "--host", t["host"]]
    if addr_mode() == "ip":
        a += ["--server-ip", t["server_ip"]]
    return a


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    cmd, rest = argv[0], argv[1:]
    if cmd == "current":
        print(current())
    elif cmd == "show":
        name = rest[0] if rest else current()
        if name not in TARGETS:
            raise SystemExit("unknown target %r (lab, online, dev)" % name)
        print("target %s%s" % (name, "  (in force: MS_TARGET)" if name == current() else ""))
        for k, v in TARGETS[name].items():
            print("  %-15s %s" % (k, v))
        if name == "online":
            print("  %-15s %s  (MS_ONLINE_ADDR)" % ("address mode", addr_mode()))
            print("  %-15s %s" % ("odcfg args", " ".join(odcfg_args(name))))
    elif cmd == "get":
        if len(rest) != 2 or rest[0] not in TARGETS or rest[1] not in TARGETS[rest[0]]:
            raise SystemExit("usage: ms_target.py get lab|online|dev KEY")
        print(TARGETS[rest[0]][rest[1]])
    elif cmd == "odcfg-args":
        name = rest[0] if rest else current()
        if name not in TARGETS:
            raise SystemExit("unknown target %r (lab, online, dev)" % name)
        print(" ".join(odcfg_args(name)))
    else:
        raise SystemExit("unknown command %r (current, show, get, odcfg-args)" % cmd)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
