#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Patch the ODCFG001 block (struct od_cfg, od_glue.h) of a payload codes.bin before it is exported and signed (card route, build_card.sh).

  patch_odcfg.py CODES.bin OUT.bin [--samenat]
      set OD_CFGF_SAMENAT (flag 32) and nothing else (needs an engine built with labnat=on).  No option = this.
  patch_odcfg.py CODES.bin OUT.bin --lock --pin HEX64 [--pin2 HEX64] (--server-ip A.B.C.D | --host NAME) [--server-ip ...] [--port N]
      patch ONE server (the 'online' profile): server address and/or name, port, pinned key(s) and OD_CFGF_SRVLOCK (flag 64).
      The card record (MultishineOnline) then gives only timeouts, modes and the identity; the identity only when its pinned key equals a patched one.
      No identity is patched (flag ID stays off, device id / secret / code / name zero), so the card is still read; the build carries no secret.

Refuses: a block not found exactly once, --lock without a pin or without a server, an all-zero pin, a loopback / multicast / 0.0.0.0 address, a bad name,
--samenat together with --lock.  Prints the resulting flags, address and key prefix (all public values)."""
import argparse
import ipaddress
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "offline", "multishine", "menus", "od"))
import od_patch as P  # noqa: E402  (pack/unpack of the 232-byte block)

F_HOST, F_SAMENAT, F_LOCK = 16, 32, 64
HOST_RE = re.compile(r"^(?=.{1,47}$)(?!.*\.\.)([A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)(\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$")


def host_ok(h):
    """od_glue.c od_host_ok / onfile.host_ok: letters, digits, dots, hyphens, labels 1..63, not all-digit last label"""
    return bool(HOST_RE.match(h)) and not h.rsplit(".", 1)[-1].isdigit()


def ip_ok(s):
    a = ipaddress.ip_address(s)
    if a.version != 4 or a.is_unspecified or a.is_loopback or a.is_multicast or a.is_reserved or a.is_link_local:
        raise ValueError("not a usable server address: %s" % s)
    return int(a)


def pin_of(h, what):
    if not re.fullmatch(r"[0-9a-fA-F]{64}", h or ""):
        raise ValueError("%s must be 64 hex digits" % what)
    b = bytes.fromhex(h)
    if not any(b):
        raise ValueError("%s is all zero" % what)
    return b


def patch(data, samenat=False, lock=False, server_ip=None, host="", port=0, pin=None, pin2=None):
    d = bytearray(data)
    i = d.find(P.MAGIC)
    if i < 0 or d.find(P.MAGIC, i + 1) >= 0:
        raise ValueError("ODCFG001 not found exactly once")
    old = P.unpack(bytes(d[i:i + P.SIZE]))
    if not lock:
        if server_ip or host or pin or pin2 or port:
            raise ValueError("--server-ip/--host/--pin/--port need --lock (a baked server without the lock would be replaced by the card record)")
        f = struct.unpack(">I", d[i + 8:i + 12])[0]
        d[i + 8:i + 12] = struct.pack(">I", f | F_SAMENAT)
        return bytes(d), f, f | F_SAMENAT
    if samenat:
        raise ValueError("--samenat and --lock exclude each other (same-NAT is the lab's; the online server is reached over the internet)")
    if pin is None:
        raise ValueError("--lock needs --pin (the server's public key)")
    if not server_ip and not host:
        raise ValueError("--lock needs --server-ip and/or --host")
    if host and not host_ok(host):
        raise ValueError("bad host name %r" % host)
    if not 0 <= port <= 65535:
        raise ValueError("bad port")
    blk = P.pack(server_ip or 0, port=port, find_ms=old["find_ms"], connect_ms=old["connect_ms"], net_ms=old["net_ms"], build_id=old["build_id"],
                 codeset=old["codeset"], rules=old["rules"], seed=old["seed"], pin=pin, pin2=pin2 or b"", flags=F_LOCK | (old["flags"] & 2), host=host.lower())
    d[i:i + P.SIZE] = blk
    return bytes(d), old["flags"], P.unpack(blk)["flags"]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("codes"); ap.add_argument("out")
    ap.add_argument("--samenat", action="store_true")
    ap.add_argument("--lock", action="store_true")
    ap.add_argument("--server-ip"); ap.add_argument("--host", default=""); ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--pin"); ap.add_argument("--pin2")
    a = ap.parse_args(argv)
    try:
        kw = dict(samenat=a.samenat, lock=a.lock, host=a.host, port=a.port)
        if a.server_ip:
            kw["server_ip"] = ip_ok(a.server_ip)
        if a.pin is not None:
            kw["pin"] = pin_of(a.pin, "--pin")
        if a.pin2 is not None:
            kw["pin2"] = pin_of(a.pin2, "--pin2")
        with open(a.codes, "rb") as f:
            data = f.read()
        out, f0, f1 = patch(data, **kw)
    except (ValueError, OSError) as e:
        print("patch_odcfg: %s" % e, file=sys.stderr)
        return 2
    with open(a.out, "wb") as f:
        f.write(out)
    if a.lock:
        print("ODCFG001 flags 0x%X -> 0x%X (server lock)  server %s%s port %d  pin %s..." % (f0, f1, a.server_ip or "-", (" name " + a.host.lower()) if a.host else "",
                                                                                         a.port or 43150, a.pin[:16].lower()))
    else:
        print("ODCFG001 flags 0x%X -> 0x%X" % (f0, f1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
