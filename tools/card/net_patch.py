#!/usr/bin/env python3
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""net_patch: build, check and sign a NETWORK PATCH, the OTA kind net-N.mscf (MSCF ABI 3).

A network patch changes ONLY an allow-listed set of fields of two card files the updater never touches otherwise: the online record "MultishineOnline" (server address, port,
host name, pinned keys 1 and 2, backup server) and the SBNC section of "MultishineBridge" (mm_ip, dns_ip, mask).  It cannot carry anything else: the device id, device secret,
connect code and name are not in the format at all (offline/multishine/menus/sign/sgu_net.h has the byte layout, sgu_net.c is the console's applier).

  net_patch.py build --seq N --out net.gci [--server IP] [--port P] [--host NAME|none] [--pin HEX] [--pin2 HEX|none] [--backup IP|none]
                     [--mm-ip IP|none] [--dns-ip IP|none] [--mask M] (--dev | --key PATH)      build and sign (the file is the .gci that dev_ota.py publish --kind net takes)
  net_patch.py info FILE                                   check a patch (container, payload, field rules) and print the fields
Library: build_payload(fields, seq), parse_payload(bytes, seq) -> fields, build_gci(payload, seq), guard(gci_or_body) -> (ok, message).  Stdlib only (signing: mscf_sign)."""
import argparse, os, socket, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, "offline", "cardloader"))
sys.path.insert(0, os.path.join(REPO, "offline", "provision"))

NET_ABI = 3
MAGIC = b"MSNP"
BLOCK, HDR_OFF, GCI_HDR = 0x2000, 0x1000, 0x40
MAX_PAYLOAD = 0x200
FILE_NAME = "MultishineNet"
# tag -> (name, kind, size)   kinds: ip (4, big endian), u16, host (0..47), key (32), mask (1)
TAGS = {0x01: ("server", "ip", 4), 0x02: ("port", "u16", 2), 0x03: ("host", "host", None), 0x04: ("pin", "key", 32), 0x05: ("pin2", "key", 32),
        0x06: ("backup", "ip", 4), 0x10: ("mm_ip", "ip", 4), 0x11: ("dns_ip", "ip", 4), 0x12: ("mask", "mask", 1)}
BY_NAME = {v[0]: (k,) + v[1:] for k, v in TAGS.items()}
ALLOWED = tuple(v[0] for v in TAGS.values())


class PatchError(Exception):
    pass


def _ip(v):
    if isinstance(v, int):
        return v
    if v in (None, "", "none", "0.0.0.0"):
        return 0
    try:
        return int.from_bytes(socket.inet_aton(str(v)), "big")
    except OSError:
        raise PatchError("not an IPv4 address: %r" % (v,))


def _host_ok(h):
    if not 1 <= len(h) <= 47:
        return False
    for lab in h.split("."):
        if not 1 <= len(lab) <= 63 or lab[0] == "-" or lab[-1] == "-" or any(not (c.isascii() and (c.isalnum() or c == "-")) for c in lab):
            return False
    return True


def _unicast(ip):
    return (ip >> 24) not in (0, 127) and (ip >> 24) < 224


def _nc_ok(ip):      # sbr_nc_ip_ok(ip, 0)
    return ip != 0 and (ip >> 24) not in (0, 127) and (ip >> 24) < 224 and (ip >> 16) != 0xA9FE


def check_fields(f):
    """The value rules, the same as the console's applier.  f: {name: value} with ints / bytes / str already normalised.  Raises PatchError."""
    if not f:
        raise PatchError("a patch must change at least one field")
    for k in f:
        if k not in ALLOWED:
            raise PatchError("field %r is not on the allow list %s" % (k, ALLOWED))
    if f.get("server") and not _unicast(f["server"]):
        raise PatchError("server address is not a unicast address")
    if "port" in f and not 0 <= f["port"] <= 65535:
        raise PatchError("port")
    if f.get("host") and not _host_ok(f["host"]):
        raise PatchError("host name must be 1..47 characters of letters, digits, dots, hyphens (labels 1..63, no hyphen at a label edge)")
    if "pin" in f and (len(f["pin"]) != 32 or not any(f["pin"])):
        raise PatchError("pin must be 32 bytes, not all zero")
    if "pin2" in f and len(f["pin2"]) != 32:
        raise PatchError("pin2 must be 32 bytes (all zero = none)")
    if f.get("backup"):
        b = f["backup"]
        if not _unicast(b) or b == f.get("server"):
            raise PatchError("backup server must be a unicast address that differs from the primary")
    for k in ("mm_ip", "dns_ip"):
        if f.get(k) and not _nc_ok(f[k]):
            raise PatchError("%s is not an acceptable address (loopback, link-local, multicast...)" % k)
    if "mask" in f and f["mask"] & ~5:
        raise PatchError("mask may only use bits 1 (mm_ip) and 4 (dns_ip)")
    return f


def build_payload(f, seq):
    f = check_fields(dict(f))
    body = b""
    for name in ALLOWED:
        if name not in f:
            continue
        tag, kind, _ = BY_NAME[name]
        v = f[name]
        raw = {"ip": lambda: struct.pack(">I", v), "u16": lambda: struct.pack(">H", v), "host": lambda: v.encode("ascii"), "key": lambda: bytes(v), "mask": lambda: bytes([v])}[kind]()
        body += bytes([tag, len(raw)]) + raw
    n = len(f)
    total = 12 + len(body) + 4
    if total > MAX_PAYLOAD:
        raise PatchError("payload too long")
    p = MAGIC + bytes([1, n]) + struct.pack(">HI", total, seq) + body
    return p + struct.pack(">I", zlib.crc32(p) & 0xFFFFFFFF)


def parse_payload(b, seq):
    """strict parser (the console's rules): -> {name: value}.  Raises PatchError on an unknown tag, a repeated tag, a wrong length, a CRC or seq mismatch, a bad value."""
    if len(b) < 16 or b[:4] != MAGIC or b[4] != 1:
        raise PatchError("not a network patch payload")
    n, total, s = b[5], struct.unpack(">H", b[6:8])[0], struct.unpack(">I", b[8:12])[0]
    if total < 16 or total > len(b) or total > MAX_PAYLOAD:
        raise PatchError("bad payload length")
    if struct.unpack(">I", b[total - 4:total])[0] != zlib.crc32(b[:total - 4]) & 0xFFFFFFFF:
        raise PatchError("payload CRC")
    if s != seq:
        raise PatchError("payload seq %d != container build_seq %d" % (s, seq))
    f, i = {}, 12
    for _ in range(n):
        if i + 2 > total - 4:
            raise PatchError("truncated field")
        t, l = b[i], b[i + 1]
        v = b[i + 2:i + 2 + l]
        i += 2 + l
        if i > total - 4:
            raise PatchError("truncated field")
        if t not in TAGS:
            raise PatchError("unknown field tag 0x%02X" % t)
        name, kind, size = TAGS[t]
        if name in f:
            raise PatchError("field %s twice" % name)
        if kind != "host" and l != size:
            raise PatchError("field %s has length %d" % (name, l))
        if kind == "ip":
            f[name] = struct.unpack(">I", v)[0]
        elif kind == "u16":
            f[name] = struct.unpack(">H", v)[0]
        elif kind == "host":
            if l > 47:
                raise PatchError("host too long")
            f[name] = v.decode("ascii", "strict")
        elif kind == "key":
            f[name] = bytes(v)
        else:
            f[name] = v[0]
    if i != total - 4 or any(b[total:]):
        raise PatchError("trailing bytes")
    return check_fields(f)


def build_gci(payload, seq):
    """-> the UNSIGNED .gci (0x40 directory entry + header block + one payload block); sign it with mscf_sign.py (or build --dev/--key)."""
    import multishine_provision as P
    import mscf as M
    if not 1 <= seq <= 0xFFFFFFFF:
        raise PatchError("seq out of range")
    pay = payload + bytes(BLOCK - len(payload))
    h = bytearray(0x80)
    struct.pack_into(">IHHIIIIIIIII", h, 0, M.MAGIC, M.VERSION, NET_ABI, seq, 0, 0, BLOCK, len(payload), zlib.crc32(payload) & 0xFFFFFFFF, 0, 0, 0)
    struct.pack_into(">I", h, 0x2C, zlib.crc32(bytes(h[:0x2C])) & 0xFFFFFFFF)
    blk = bytearray(BLOCK)
    blk[0:32] = b"Multishine".ljust(32, b"\0")
    blk[32:64] = b"Network settings".ljust(32, b"\0")
    P.put_icon(blk, FILE_NAME)
    blk[HDR_OFF:HDR_OFF + 0x80] = h
    data = bytes(blk) + pay
    return P.make_dir_entry(FILE_NAME, 0, len(data) // BLOCK, P.PERM_PUBLIC, None) + data


def guard(src):
    """src: a .gci (with the 0x40 directory entry) or a served file (without).  -> (ok, message).  The container must be exactly header block + one payload block, ABI 3, no segments or
    patches, and the payload must parse under the allow list.  Run by dev_ota.py publish --kind net and by the update catalog."""
    import mscf as M
    b = bytes(src)
    if len(b) == GCI_HDR + 2 * BLOCK:
        b = b[GCI_HDR:]
    if len(b) != 2 * BLOCK:
        return False, "REFUSED: a network patch is exactly 2 blocks (got %d bytes)" % len(b)
    h = b[HDR_OFF:HDR_OFF + 0x80]
    magic, ver, abi, seq, flags, base, size, ilen, icrc, nseg, npatch, entry = struct.unpack_from(">IHHIIIIIIIII", h, 0)
    if magic != M.MAGIC or ver != M.VERSION or abi != NET_ABI or flags or nseg or npatch:
        return False, "REFUSED: not a plain ABI 3 container (version %d abi %d flags 0x%X segments %d patches %d)" % (ver, abi, flags, nseg, npatch)
    if not 0 < ilen <= MAX_PAYLOAD or zlib.crc32(b[BLOCK:BLOCK + ilen]) & 0xFFFFFFFF != icrc:
        return False, "REFUSED: payload length or CRC"
    try:
        f = parse_payload(b[BLOCK:BLOCK + ilen], seq)
    except PatchError as e:
        return False, "REFUSED: %s" % e
    if any(b[BLOCK + ilen:]):
        return False, "REFUSED: bytes after the payload"
    return True, "ok: network patch %d changes only %s" % (seq, ", ".join(sorted(f)))


def describe(b, seq):
    f = parse_payload(b, seq)
    out = []
    for k in ALLOWED:
        if k in f:
            v = f[k]
            out.append("%s=%s" % (k, socket.inet_ntoa(struct.pack(">I", v)) if BY_NAME[k][1] == "ip" else v.hex() if isinstance(v, bytes) else v))
    return " ".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("--seq", type=int, required=True); b.add_argument("--out", required=True)
    b.add_argument("--server"); b.add_argument("--port", type=int); b.add_argument("--host"); b.add_argument("--pin"); b.add_argument("--pin2"); b.add_argument("--backup")
    b.add_argument("--mm-ip"); b.add_argument("--dns-ip"); b.add_argument("--mask", type=lambda x: int(x, 0))
    g = b.add_mutually_exclusive_group(required=True)
    g.add_argument("--dev", action="store_true"); g.add_argument("--key")
    i = sp.add_parser("info"); i.add_argument("file")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "info":
            ok, msg = guard(open(a.file, "rb").read())
            print(msg, file=sys.stdout if ok else sys.stderr)
            if ok:
                raw = open(a.file, "rb").read()
                raw = raw[GCI_HDR:] if len(raw) == GCI_HDR + 2 * BLOCK else raw
                seq = struct.unpack_from(">I", raw, HDR_OFF + 8)[0]
                print(describe(raw[BLOCK:BLOCK + struct.unpack_from(">I", raw, HDR_OFF + 0x18)[0]], seq))
            return 0 if ok else 1
        f = {}
        if a.server is not None: f["server"] = _ip(a.server)
        if a.port is not None: f["port"] = a.port
        if a.host is not None: f["host"] = "" if a.host.lower() == "none" else a.host.lower()
        if a.pin is not None: f["pin"] = bytes.fromhex(a.pin)
        if a.pin2 is not None: f["pin2"] = bytes(32) if a.pin2.lower() == "none" else bytes.fromhex(a.pin2)
        if a.backup is not None: f["backup"] = _ip(a.backup)
        if a.mm_ip is not None: f["mm_ip"] = _ip(a.mm_ip)
        if a.dns_ip is not None: f["dns_ip"] = _ip(a.dns_ip)
        if a.mask is not None: f["mask"] = a.mask
        import mscf_sign as S
        gci = build_gci(build_payload(f, a.seq), a.seq)
        seed = S.DEV_SEED if a.dev else S.load_seed(a.key)
        out = S.sign(S.mark_dev(gci) if a.dev else gci, seed, a.seq)
        ok, msg = guard(out)
        if not ok:
            raise PatchError(msg)
        open(a.out, "wb").write(out)
        print("%s: %s  (%s)%s" % (a.out, msg, describe(build_payload(f, a.seq), a.seq), "  DEV KEY" if a.dev else ""))
        return 0
    except (PatchError, ValueError) as e:
        print("net_patch: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
