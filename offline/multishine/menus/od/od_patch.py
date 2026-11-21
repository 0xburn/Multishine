#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Patch the 'ODCFG001' configuration block of a 'Multishine online-direct' ISO.

  od_patch.py BASE.iso OUT.iso --server 10.213.110.2 [--port 43150] --pin HEX64 [--pin2 HEX64] --dev HEX16 --tok HEX32 --code AB#1234 --name Alice
                                [--host NAME] [--find-ms N] [--connect-ms N] [--net-ms N] [--unranked] [--lax] [--codeset N] [--rules N] [--seed N]
  od_patch.py show IMAGE.iso

The block (struct od_cfg in od_glue.h, 232 bytes, big endian; the last 48 are the optional server host name) is found by its magic bytes 'ODCFG001' inside the main.dol of the ISO, exactly
once, like build_s17_net.py does for the engine's own cfg blocks.  OUT is an APFS clone of BASE (no space until written), never BASE itself.
This is the TEST path: the image on the card gets the same block from the memory-card record, which is the only
place the device secret may live in production.  Nothing here prints the secret.
"""
import argparse
import os
import struct
import subprocess
import sys

MAGIC = b"ODCFG001"
SIZE = 232
OLD_SIZE = 184           # before the host name field (an all-zero tail = no host)
HOST_OFF, HOST_LEN = 184, 48
FLAG_ID, FLAG_LAX, FLAG_UNRANKED, FLAG_HOST = 1, 2, 4, 16


def pack(server_ip, port=0, find_ms=0, connect_ms=0, net_ms=0, build_id=0x00010000, codeset=0xC0DE5E7, rules=0x201E5, seed=0,
         pin=b"", pin2=b"", dev=b"", tok=b"", code="", name="", flags=0, host=""):
    if len(pin) not in (0, 32) or len(pin2) not in (0, 32) or len(dev) not in (0, 8) or len(tok) not in (0, 16):
        raise ValueError("pin 32, dev 8, tok 16 bytes")
    if len(code) > 23 or len(name) > 23:
        raise ValueError("code and name at most 23 characters")
    b = MAGIC + struct.pack(">10I", flags, server_ip, port, find_ms, connect_ms, net_ms, build_id, codeset, rules, seed)
    b += pin.ljust(32, b"\0") + pin2.ljust(32, b"\0") + dev.ljust(8, b"\0") + tok.ljust(16, b"\0")
    b += code.encode("ascii").ljust(24, b"\0") + name.encode("ascii").ljust(24, b"\0")
    if host:
        if len(host) > HOST_LEN - 1 or not host.isascii():
            raise ValueError("host at most 47 ASCII characters")
        flags |= FLAG_HOST
        b = b[:8] + struct.pack(">I", flags) + b[12:]
    b += host.encode("ascii").ljust(HOST_LEN, b"\0")
    assert len(b) == SIZE, len(b)
    return b


def unpack(b):
    assert b[:8] == MAGIC and len(b) >= SIZE
    f, ip, port, fm, cm, nm, bid, cs, ru, seed = struct.unpack(">10I", b[8:48])
    return {"flags": f, "server_ip": ip, "server_port": port, "find_ms": fm, "connect_ms": cm, "net_ms": nm, "build_id": bid, "codeset": cs, "rules": ru, "seed": seed,
            "pin": b[48:80].hex(), "pin2": b[80:112].hex(), "dev": b[112:120].hex(), "tok_set": any(b[120:136]), "code": b[136:160].rstrip(b"\0").decode(),
            "name": b[160:184].rstrip(b"\0").decode(), "host": b[HOST_OFF:HOST_OFF + HOST_LEN].rstrip(b"\0").decode() if len(b) >= SIZE else ""}


def find_block(path):
    """file offset of the block inside an ISO (main.dol located through the disc header) or a raw image"""
    with open(path, "rb") as f:
        head = f.read(0x440)
        if path.endswith(".iso"):
            dol_off = struct.unpack(">I", head[0x420:0x424])[0]
            f.seek(dol_off)
            blob, base = f.read(8 << 20), dol_off
        else:
            f.seek(0)
            blob, base = f.read(), 0
    i = blob.find(MAGIC)
    if i < 0 or blob.find(MAGIC, i + 1) >= 0:
        raise SystemExit("config block magic not found exactly once in " + path)
    return base + i


def ip_int(s):
    p = [int(x) for x in s.split(".")]
    if len(p) != 4 or any(not 0 <= x <= 255 for x in p):
        raise ValueError("bad address " + s)
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]


def patch_iso(base, out, **kw):
    if os.path.abspath(base) == os.path.abspath(out):
        raise SystemExit("OUT must not be BASE")
    if os.path.exists(out):
        os.unlink(out)
    r = subprocess.run(["cp", "-c", base, out], capture_output=True)
    if r.returncode:
        subprocess.check_call(["cp", base, out])
    os.chmod(out, 0o644)
    off = find_block(out)
    with open(out, "r+b") as f:
        f.seek(off)
        f.write(pack(**kw))
    return off


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "show":
        with open(argv[1], "rb") as f:
            off = find_block(argv[1])
            f.seek(off)
            print(unpack(f.read(SIZE)))
        return 0
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base")
    ap.add_argument("out")
    ap.add_argument("--server", default="0.0.0.0", help="numeric server address (wins over --host when not 0.0.0.0)")
    ap.add_argument("--host", default="", help="server host name, resolved by DNS at menu time (docs/design/HUB_SERVER_NAME.md)")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--pin", required=True)
    ap.add_argument("--pin2", default="")
    ap.add_argument("--dev", required=True)
    ap.add_argument("--tok", required=True)
    ap.add_argument("--code", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--find-ms", type=int, default=0)
    ap.add_argument("--connect-ms", type=int, default=0)
    ap.add_argument("--net-ms", type=int, default=0)
    ap.add_argument("--unranked", action="store_true")
    ap.add_argument("--lax", action="store_true")
    ap.add_argument("--codeset", type=lambda x: int(x, 0), default=0xC0DE5E7)
    ap.add_argument("--rules", type=lambda x: int(x, 0), default=0x201E5)
    ap.add_argument("--seed", type=lambda x: int(x, 0), default=0)
    a = ap.parse_args(argv)
    flags = FLAG_ID | (FLAG_LAX if a.lax else 0) | (FLAG_UNRANKED if a.unranked else 0)
    off = patch_iso(a.base, a.out, server_ip=ip_int(a.server), port=a.port, find_ms=a.find_ms, connect_ms=a.connect_ms, net_ms=a.net_ms,
                    codeset=a.codeset, rules=a.rules, seed=a.seed, pin=bytes.fromhex(a.pin), pin2=bytes.fromhex(a.pin2) if a.pin2 else b"", host=a.host,
                    dev=bytes.fromhex(a.dev), tok=bytes.fromhex(a.tok), code=a.code, name=a.name, flags=flags)
    print("patched %s at file offset 0x%X" % (a.out, off))
    return 0


if __name__ == "__main__":
    sys.exit(main())
