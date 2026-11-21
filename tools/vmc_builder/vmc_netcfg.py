# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
"""DEV/LAB net-config section inside the card file MultishineBridge (offline/bridge/sbr_netcfg.h).

28 bytes at file offset 0x1100 of the one-block file: "SBNC" | u8 version 1 | u8 mask (1 mm_ip, 2 status, 4 dns) | u16 length 28 | u32 mm_ip |
u32 status_ip | u32 dns_ip | u16 status_port | u16 0 | u32 CRC32 over the first 24 bytes.  Only IPv4 addresses; the validation is the console's
(sbr_nc_ip_ok): never 0.0.0.0, loopback, multicast, link-local, class E / broadcast.
Since the console IGNORES the status address / port fields and mask bit 2 (the status server is gone); they are still encoded here so that older cards
and command lines keep working.
Only a dev/lab image (built with -DSBR_NETCFG=1) reads it; a release image ignores the bytes."""
import ipaddress
import struct
import zlib

OFF = 0x1100
LEN = 28
MM, STATUS, DNS = 1, 2, 4
MAGIC = b"SBNC"


class NetCfgError(Exception):
    pass


def check_ip(text, what, private_only=False):
    try:
        ip = ipaddress.IPv4Address(text)
    except ValueError:
        raise NetCfgError("%s: %r is not a dotted IPv4 address" % (what, text))
    v = int(ip)
    a, b = v >> 24, (v >> 16) & 255
    if v == 0 or a == 0 or a == 127 or a >= 224 or (a == 169 and b == 254):
        raise NetCfgError("%s: %s is not allowed (0.0.0.0, 'this network', loopback, multicast, class E / broadcast and link-local are refused)" % (what, text))
    if private_only and not (a == 10 or (a == 172 and 16 <= b <= 31) or (a == 192 and b == 168)):
        raise NetCfgError("%s: %s is not a private address (10/8, 172.16/12, 192.168/16): the status server must be on your LAN" % (what, text))
    return v


def encode(cfg):
    """cfg = {'mm_ip': 'A.B.C.D'|None, 'status_ip': .., 'status_port': int|None, 'dns_ip': ..} -> 28 bytes (validated)."""
    mask, mm, sip, dns, port = 0, 0, 0, 0, 0
    if cfg.get("mm_ip"):
        mask |= MM
        mm = check_ip(cfg["mm_ip"], "matchmaker address (mm_ip)")
    if cfg.get("status_ip"):
        mask |= STATUS
        sip = check_ip(cfg["status_ip"], "status server address", True)
        port = int(cfg.get("status_port") or 0)
        if not 0 <= port <= 65535:
            raise NetCfgError("status port must be 1..65535 (or omitted to keep the baked port)")
    elif cfg.get("status_port"):
        raise NetCfgError("--status-port needs --status-ip (the port belongs to the status address)")
    if cfg.get("dns_ip"):
        mask |= DNS
        dns = check_ip(cfg["dns_ip"], "DNS server address (dns_ip)")
    if not mask:
        raise NetCfgError("nothing to store: give --mm-ip, --status-ip [--status-port] or --dns-ip (or --clear to remove the section)")
    b = bytearray(MAGIC + bytes([1, mask]) + struct.pack(">H", LEN) + struct.pack(">III", mm, sip, dns) + struct.pack(">HH", port, 0))
    b += struct.pack(">I", zlib.crc32(bytes(b)))
    assert len(b) == LEN
    return bytes(b)


def decode(raw):
    """-> (state, cfg dict).  state: 'absent' | 'active' | 'damaged' | 'invalid' | 'newer' (the console's verdicts)."""
    raw = bytes(raw[:LEN])
    if raw[:4] != MAGIC:
        return "absent", {}
    if raw[4] > 1:
        return "newer", {}
    if raw[4] != 1 or struct.unpack(">H", raw[6:8])[0] != LEN or zlib.crc32(raw[:24]) != struct.unpack(">I", raw[24:28])[0]:
        return "damaged", {}
    mask = raw[5]
    mm, sip, dns = struct.unpack(">III", raw[8:20])
    port = struct.unpack(">H", raw[20:22])[0]
    cfg = {}
    try:
        if not mask or mask & ~7 or raw[22] or raw[23]:
            raise NetCfgError("mask/pad")
        if mask & MM:
            cfg["mm_ip"] = str(ipaddress.IPv4Address(mm)); check_ip(cfg["mm_ip"], "mm_ip")
        if mask & STATUS:
            cfg["status_ip"] = str(ipaddress.IPv4Address(sip)); check_ip(cfg["status_ip"], "status_ip", True)
            cfg["status_port"] = port or None
        if mask & DNS:
            cfg["dns_ip"] = str(ipaddress.IPv4Address(dns)); check_ip(cfg["dns_ip"], "dns_ip")
    except NetCfgError:
        return "invalid", {}
    return "active", cfg


def merge(old, new):
    """Fields given now replace the stored ones; the others stay."""
    out = dict(old)
    for k, v in new.items():
        if v is not None:
            out[k] = v
    if "status_ip" not in out:
        out.pop("status_port", None)
    return out
