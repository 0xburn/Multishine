# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""Reference implementation and writer of the online setup card file "MultishineOnline" (on_cfgrec.h / on_cfgrec.c) for the host tests and tools.  Stdlib only.

The record layout is documented in on_cfgrec.h.  The console only READS this file (menus/on/on_card.c); a PC tool writes it (tools/vmc_builder
`--online-config, or `python3 onfile.py card-spec ...` to make the SPEC from an enrolment). The file holds the device secret of the identity:
treat the card like a password (the VMC builder makes private files and refuses git work trees, as for the identity file).

: {"format": "multishine-online-setup", "version": 1, "server": "192.168.1.20" | "192.168.1.20:43150" (alias "server_ip"),
            "server_host": "lab.example.org" | "lab.example.org:43150" (optional: a NAME the console resolves by DNS at menu time; a numeric "server" wins when both are given;
            "server_port" 1..65535 is accepted next to either), "pin": "<64 hex: the server's Ed25519 public key>",
            "pin2": "<64 hex, optional>", "device_id": "<16 hex>", "device_secret": "<32 hex>", "code": "AB#1234", "name": "Alice",
            "unranked": false, "ranked": false, "find_ms": 0, "connect_ms": 0, "net_ms": 0}
"""
import json
import socket
import struct
import sys
import zlib

MAGIC = b"SDON"
VERSION = 1
REC_SIZE = 0xC0
EXT_OFF = 0x100                 # the optional host extension, relative to the record
EXT_SIZE = 0x3C
EXT_END = EXT_OFF + EXT_SIZE
EXT_MAGIC = b"SDHN"
HOST_MAX = 47                   # characters; the field is 48 bytes, NUL terminated
PAYLOAD_OFF = 0x1000
BLOCK = 0x2000
OK, DAMAGED, NEWER, INCOMPLETE = 0, 1, 2, 3
FILE_NAME = "MultishineOnline"
import os as _os
sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", "..", ".."))
import card_text as _ct  # noqa: E402  (offline/card_text.py: the one table of card file comment lines)
LINE1, LINE2 = _ct.pair("Online")
DEFAULT_PORT = 43150


class SetupError(Exception):
    pass


def _hex(spec, key, n, optional=False):
    v = spec.get(key)
    if v in (None, "") and optional:
        return bytes(n)
    try:
        b = bytes.fromhex(v)
    except (TypeError, ValueError):
        raise SetupError("%s must be %d hex characters" % (key, 2 * n))
    if len(b) != n:
        raise SetupError("%s must be %d hex characters" % (key, 2 * n))
    return b


def code_ok(c):
    marks = [i for i, ch in enumerate(c) if ch in "#@"] if isinstance(c, str) else []
    return (isinstance(c, str) and 3 <= len(c) <= 13 and len(marks) == 1 and 0 < marks[0] < len(c) - 1
            and all(ch.isascii() and ch.isalnum() or ch in "#@" for ch in c))      # '#' = Slippi universe, '@' = Multishine universe


def _text(s, n, what, space):
    if not isinstance(s, str) or not s or len(s) >= n or any(not (0x20 if space else 0x21) <= ord(c) <= 0x7E for c in s):
        raise SetupError("%s must be 1..%d printable ASCII characters%s" % (what, n - 1, "" if space else " without spaces"))
    return s.encode("ascii").ljust(n, b"\0")


def _range(spec, key, lo, hi):
    v = int(spec.get(key, 0) or 0)
    if v != 0 and not lo <= v <= hi:
        raise SetupError("%s must be 0 (default) or %d..%d" % (key, lo, hi))
    return v


def host_ok(h):
    """The host name rules of the console (on_cfgrec.c host_text_ok, od_glue.c od_host_ok): 1..47 characters of [A-Za-z0-9.-], labels 1..63, no empty label, no label
    starting or ending with a hyphen.  Letters, digits, dots and hyphens only: nothing that could carry a secret or a path."""
    if not isinstance(h, str) or not 1 <= len(h) <= HOST_MAX:
        return False
    for lab in h.split("."):
        if not 1 <= len(lab) <= 63 or lab[0] == "-" or lab[-1] == "-":
            return False
        if any(not (c.isascii() and (c.isalnum() or c == "-")) for c in lab):
            return False
    return True


def _port(port):
    p = int(port) if str(port).strip() else 0
    if not 0 <= p <= 65535:
        raise SetupError("server port")
    return 0 if p == DEFAULT_PORT else p


def parse_server(s):
    host, _, port = str(s).partition(":")
    try:
        ip = int.from_bytes(socket.inet_aton(host), "big")
    except OSError:
        raise SetupError('"server" must be an IPv4 address; give a name as "server_host"')
    if ip == 0:
        raise SetupError("server address 0.0.0.0")
    return ip, _port(port)


def parse_host(s):
    """"name" or "name:port" -> (name, port field).  A dotted quad is not a name (give it as "server")."""
    h, _, port = str(s).partition(":")
    h = h.lower()
    if not host_ok(h):
        raise SetupError("server_host must be 1..%d characters of letters, digits, dots and hyphens (labels 1..63, no hyphen at a label edge)" % HOST_MAX)
    last = h.rsplit(".", 1)[-1]
    if last.isdigit():
        raise SetupError("server_host %r ends in a number: give a numeric address as \"server\"" % h)
    return h, _port(port)


def server_of(spec):
    """(ip, host, port field) from a spec: the numeric address (key server / server_ip) and/or the name (server_host); at least one."""
    num, name = spec.get("server", spec.get("server_ip")), spec.get("server_host")
    if num in (None, "") and name in (None, ""):
        raise SetupError('give the server as "server": "A.B.C.D" and/or "server_host": "name"')
    ip, port, host = 0, 0, None
    if name not in (None, ""):
        host, port = parse_host(name)
    if num not in (None, ""):
        ip, p = parse_server(num)
        port = p or port
    if spec.get("server_port") not in (None, ""):
        port = _port(spec["server_port"])
    return ip, host, port


def backup_of(spec, primary_ip=0):
    """The optional backup server (key backup_server, a dotted IPv4, no port: the same port and keys as the primary) as an int, 0 = none ( C4)."""
    b = spec.get("backup_server")
    if b in (None, ""):
        return 0
    ip, port = parse_server(b)
    if port or ":" in str(b):
        raise SetupError('"backup_server" is an address only (it uses the primary\'s port)')
    if ip >> 24 in (0, 127) or ip >> 24 >= 224 or (primary_ip and ip == primary_ip):
        raise SetupError('"backup_server" must be a unicast address that differs from the primary')
    return ip


def backup_valid(rec):
    """What on_cfgrec.c onr_backup returns for a record: the backup address, or 0 when absent or not acceptable."""
    ip, = struct.unpack(">I", bytes(rec)[0xA8:0xAC])
    prim, = struct.unpack(">I", bytes(rec)[0x0C:0x10])
    return 0 if ip >> 24 in (0, 127) or ip >> 24 >= 224 or ip == prim else ip


def pack_ext(spec):
    """The 0x3C byte host extension of a spec, or None if the spec names no host."""
    host = server_of(spec)[1]
    if host is None:
        return None
    field = host.encode("ascii").ljust(48, b"\0")
    e = bytearray(EXT_SIZE)
    e[0:4] = EXT_MAGIC
    e[4] = 1
    e[5] = len(host)
    e[0x0C:0x3C] = field
    struct.pack_into(">I", e, 8, zlib.crc32(field) & 0xFFFFFFFF)
    return bytes(e)


def host_of(blk):
    """The valid host name of an extension (blk: the bytes from the record on, at least EXT_END), else None."""
    blk = bytes(blk)
    if len(blk) < EXT_END:
        return None
    e = blk[EXT_OFF:EXT_END]
    if e[0:4] != EXT_MAGIC or e[4] != 1 or e[6:8] != b"\0\0" or struct.unpack(">I", e[8:12])[0] != zlib.crc32(e[0x0C:]) & 0xFFFFFFFF:
        return None
    n = e[0x0C:].find(b"\0")
    if n < 1 or any(e[0x0C + n:]) or e[5] != n:
        return None
    try:
        h = e[0x0C:0x0C + n].decode("ascii")
    except UnicodeDecodeError:
        return None
    return h if host_ok(h) else None


# ---- identity slots (on_cfgrec.h) ----
SL_OFF = 0x140
SL_N = 3
SL_ENT = 0x50
SL_HDR = 0x0C
SL_SIZE = SL_HDR + SL_N * SL_ENT
SL_END = SL_OFF + SL_SIZE
SL_MAGIC = b"SDSL"
ENV_PROD, ENV_DEV, ENV_LAB = 0, 1, 2
ENV_NAMES = {"prod": ENV_PROD, "dev": ENV_DEV, "lab": ENV_LAB}


def slots_state(blk):
    """1 = a valid SDSL, 2 = a newer one, 0 = none or damaged (blk: the bytes from the record on)."""
    blk = bytes(blk)
    x = blk[SL_OFF:SL_END]
    if len(x) < SL_SIZE or x[0:4] != SL_MAGIC:
        return 0
    if x[4] > 1:
        return 2
    if x[4] != 1 or x[5] != SL_N or x[6:8] != b"\0\0":
        return 0
    return 1 if struct.unpack(">I", x[8:12])[0] == zlib.crc32(x[SL_HDR:]) & 0xFFFFFFFF else 0


def _key(k):
    return k if k and any(k) else None


def _legacy_home(blk, env, lab_build, bk):
    """(home environment, tag) of the old single identity (record +0x60..+0xA8)."""
    for k in bk:
        if _key(k) and (k == blk[0x20:0x40] or k == blk[0x40:0x60]):
            return ENV_PROD, k[:8]
    return (env if env != ENV_PROD else (ENV_LAB if lab_build else ENV_DEV)), blk[0x20:0x28]


def _tag_ok(tag, blk, env, bk):
    if not any(tag):
        return False
    if env == ENV_PROD:
        return any(_key(k) and k[:8] == tag for k in bk)
    return any(blk[0x20:0x40]) and blk[0x20:0x28] == tag


def slot_get(blk, env, lab_build=False, bk=(None, None)):
    """The 0x48 byte identity (device id 8, secret 16, code 24, name 24) of env, or None (a guest).  No write."""
    blk = bytes(blk)
    st = slots_state(blk)
    if st == 2 or not 0 <= env < SL_N:
        return None
    if st == 1:
        e = blk[SL_OFF + SL_HDR + env * SL_ENT:][:SL_ENT]
        tag, ident = e[:8], e[8:]
    else:
        home, tag = _legacy_home(blk, env, lab_build, bk)
        if home != env:
            return None
        ident = blk[0x60:0xA8]
    if not any(ident[:8]) or not any(ident[8:24]) or not _tag_ok(tag, blk, env, bk):
        return None
    return bytes(ident)


def slot_put(blk, env, ident, lab_build=False, bk=(None, None)):
    """New block bytes with ident (0x48 bytes; all-zero device id clears) in env's slot; None when refused (newer SDSL).  Migrates the old single identity first when there is no SDSL."""
    b = bytearray(blk)
    st = slots_state(b)
    if st == 2 or not 0 <= env < SL_N:
        return None
    if st != 1:
        b[SL_OFF:SL_END] = bytes(SL_SIZE)
        if any(b[0x60:0x68]) and any(b[0x68:0x78]):
            home, tag = _legacy_home(bytes(b), env, lab_build, bk)
            o = SL_OFF + SL_HDR + home * SL_ENT
            b[o:o + 8] = tag
            b[o + 8:o + SL_ENT] = b[0x60:0xA8]
    o = SL_OFF + SL_HDR + env * SL_ENT
    b[o:o + SL_ENT] = bytes(SL_ENT)
    if any(ident[:8]):
        k = _key(bk[0]) if env == ENV_PROD else bytes(b[0x20:0x40])
        b[o:o + 8] = (k or bytes(8))[:8]
        b[o + 8:o + SL_ENT] = bytes(ident)
    b[SL_OFF:SL_OFF + 12] = SL_MAGIC + bytes([1, SL_N, 0, 0]) + struct.pack(">I", zlib.crc32(bytes(b[SL_OFF + SL_HDR:SL_END])) & 0xFFFFFFFF)
    return bytes(b)


def ident_bytes(device_id, secret, code, name):
    """The 0x48 byte identity field group from hex ids and text."""
    return bytes.fromhex(device_id) + bytes.fromhex(secret) + _text(code, 24, "code", False) + _text(name, 24, "name", True)


def slots_view(blk):
    """Public view of a block's slots (no secrets): [{env, code, name, device, tag, secret_set}] for a valid SDSL, else None."""
    blk = bytes(blk)
    if slots_state(blk) != 1:
        return None
    out = []
    for i, n in enumerate(("prod", "dev", "lab")):
        e = blk[SL_OFF + SL_HDR + i * SL_ENT:][:SL_ENT]
        if not any(e[8:16]):
            out.append({"env": n, "linked": False})
            continue
        out.append({"env": n, "linked": True, "device": e[8:16].hex(), "code": e[0x20:0x38].split(b"\0")[0].decode("ascii", "replace"),
                    "name": e[0x38:0x50].split(b"\0")[0].decode("ascii", "replace"), "tag": e[:8].hex(), "secret_set": any(e[16:32])})
    return out


def pack(spec):
    """SPEC dict -> the 0xC0 byte record."""
    ip, host, port = server_of(spec)
    code = str(spec.get("code", "")).upper()
    if not code_ok(code):
        raise SetupError("code must be a connect code like AB#1234 (3..13 characters, one # or @)")
    pin = _hex(spec, "pin", 32)
    if not any(pin):
        raise SetupError("pin must not be all zero")
    modes = (1 if spec.get("unranked") else 0) | (2 if spec.get("ranked") else 0)
    r = bytearray(REC_SIZE)
    r[0:4] = MAGIC
    r[4] = VERSION
    struct.pack_into(">I", r, 0x0C, ip)
    struct.pack_into(">HH", r, 0x10, port, modes)
    struct.pack_into(">III", r, 0x14, _range(spec, "find_ms", 5000, 300000), _range(spec, "connect_ms", 5000, 120000), _range(spec, "net_ms", 5000, 120000))
    r[0x20:0x40] = pin
    r[0x40:0x60] = _hex(spec, "pin2", 32, True)
    r[0x60:0x68] = _hex(spec, "device_id", 8)
    r[0x68:0x78] = _hex(spec, "device_secret", 16)
    r[0x78:0x90] = _text(code, 24, "code", False)
    r[0x90:0xA8] = _text(str(spec.get("name", "")), 24, "name", True)
    struct.pack_into(">I", r, 0xA8, backup_of(spec, ip))
    struct.pack_into(">I", r, 8, zlib.crc32(bytes(r[0x0C:])) & 0xFFFFFFFF)
    return bytes(r)


def check(rec):
    """Verdict of a record exactly as on_cfgrec.c's onr_check (len(rec) == REC_SIZE: the host extension is not looked at) or onr_check2 (len(rec) >= EXT_END: counted)."""
    rec = bytes(rec)
    has_host = host_of(rec) is not None
    if len(rec) < REC_SIZE or rec[0:4] != MAGIC:
        return DAMAGED
    ver = rec[4]
    if ver == 0:
        return DAMAGED
    if ver > VERSION:
        return NEWER
    rec = rec[:REC_SIZE]
    if struct.unpack(">I", rec[8:12])[0] != zlib.crc32(rec[0x0C:]) & 0xFFFFFFFF:
        return DAMAGED
    if rec[5] or rec[6:8] != b"\0\0":
        return DAMAGED
    if (struct.unpack(">I", rec[0x0C:0x10])[0] == 0 and not has_host) or not any(rec[0x20:0x40]):
        return INCOMPLETE

    def text(b, space):
        n = b.find(b"\0")
        if n < 1 or any(c != 0 for c in b[n:]) or any(not (0x20 if space else 0x21) <= c <= 0x7E for c in b[:n]):
            return None
        return b[:n].decode("ascii")
    code, name = text(rec[0x78:0x90], False), text(rec[0x90:0xA8], True)
    if code is None or name is None or not code_ok(code):
        return INCOMPLETE
    f, c, n = struct.unpack(">III", rec[0x14:0x20])
    for v, lo, hi in ((f, 5000, 300000), (c, 5000, 120000), (n, 5000, 120000)):
        if v and not lo <= v <= hi:
            return INCOMPLETE
    return OK


def unpack(rec):
    """Public view of an OK record (no secret); pass the bytes from the record on (len >= EXT_END) to include the host extension."""
    v = check(rec)
    host = host_of(rec)
    if v != OK:
        raise SetupError({DAMAGED: "damaged", NEWER: "from a newer version", INCOMPLETE: "incomplete"}[v])
    rec = bytes(rec)
    ip, = struct.unpack(">I", rec[0x0C:0x10])
    port, modes = struct.unpack(">HH", rec[0x10:0x14])
    srv = "%s:%d" % (socket.inet_ntoa(struct.pack(">I", ip)), port or DEFAULT_PORT) if ip else "%s:%d" % (host, port or DEFAULT_PORT)
    return {"server": srv, "server_host": host, "pin": rec[0x20:0x40].hex(),
            "pin2": rec[0x40:0x60].hex() if any(rec[0x40:0x60]) else None, "device_id": rec[0x60:0x68].hex(), "secret_set": any(rec[0x68:0x78]),
            "code": rec[0x78:0x90].split(b"\0")[0].decode(), "name": rec[0x90:0xA8].split(b"\0")[0].decode(),
            "unranked": bool(modes & 1), "ranked": bool(modes & 2), "find_ms": struct.unpack(">I", rec[0x14:0x18])[0],
            "connect_ms": struct.unpack(">I", rec[0x18:0x1C])[0], "net_ms": struct.unpack(">I", rec[0x1C:0x20])[0],
            "backup_server": socket.inet_ntoa(rec[0xA8:0xAC]) if backup_valid(rec) else None}


def add_slot(payload, spec):
    """payload: the card block from the record on (len >= SL_END).  With "slot": "prod" | "dev" | "lab" in the spec the identity of the record is also stored in that slot of an SDSL
    (the old single identity stays as written, for older readers); without the key the payload is returned unchanged (no SDSL: such a card migrates when the console first links)."""
    slot = spec.get("slot")
    if slot in (None, ""):
        return bytes(payload)
    if slot not in ENV_NAMES:
        raise SetupError('"slot" must be prod, dev or lab')
    rec = bytes(payload)
    # the slot tag is the key that minted it: a prod slot takes the record's key 1 (the patched key of a prod record), dev / lab the record's key 1 as the console reads it
    put = slot_put(rec, ENV_NAMES[slot], rec[0x60:0xA8], False, (rec[0x20:0x40] if slot == "prod" else None, None))
    return put


def new_block(spec):
    """The card block of a spec (see add_slot for the optional "slot" key)."""
    b = bytearray(_new_block(spec))
    b[PAYLOAD_OFF:] = add_slot(bytes(b[PAYLOAD_OFF:]), spec)[:len(b) - PAYLOAD_OFF]
    return bytes(b)


def _new_block(spec):
    b = bytearray(BLOCK)
    b[0:len(LINE1)] = LINE1.encode()
    b[32:32 + len(LINE2)] = LINE2.encode()
    b[PAYLOAD_OFF:PAYLOAD_OFF + REC_SIZE] = pack(spec)
    ext = pack_ext(spec)
    if ext:
        b[PAYLOAD_OFF + EXT_OFF:PAYLOAD_OFF + EXT_END] = ext
    return bytes(b)


def spec_from_ids(ids, index, server, pin, ranked=False, unranked=False, **extra):
    """SPEC from the output of menus/od/od_provision.py (identities[index]) for the lab rig."""
    i = ids["identities"][index]
    s = {"format": "multishine-online-setup", "version": 1, "server": server, "pin": pin, "device_id": i["deviceId"], "device_secret": i["token"],
         "code": i["connectCode"], "name": i["displayName"], "ranked": ranked, "unranked": unranked}
    s.update(extra)
    return s


def spec_from_json(d):
    if not isinstance(d, dict) or d.get("format") != "multishine-online-setup" or d.get("version") != 1:
        raise SetupError('not a {"format": "multishine-online-setup", "version": 1} file')
    return d


if __name__ == "__main__":
    sys.stderr.write(__doc__)
