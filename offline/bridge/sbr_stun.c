/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sbr_stun: see sbr_stun.h.  C89, no libc, no heap. */
#include "sbr_stun.h"

#define COOKIE 0x2112A442u

static sbrs_u32 rd16(const sbrs_u8 *p) { return ((sbrs_u32)p[0] << 8) | p[1]; }
static sbrs_u32 rd32(const sbrs_u8 *p) { return ((sbrs_u32)p[0] << 24) | ((sbrs_u32)p[1] << 16) | ((sbrs_u32)p[2] << 8) | p[3]; }
static void wr32(sbrs_u8 *p, sbrs_u32 v) { p[0] = (sbrs_u8)(v >> 24); p[1] = (sbrs_u8)(v >> 16); p[2] = (sbrs_u8)(v >> 8); p[3] = (sbrs_u8)v; }

void sbr_stun_build_request(sbrs_u8 *out, const sbrs_u8 *tid)
{
    sbrs_u32 i;
    out[0] = 0x00; out[1] = 0x01; out[2] = 0x00; out[3] = 0x00;
    wr32(out + 4, COOKIE);
    for (i = 0; i < SBR_STUN_TID_LEN; i++) out[8 + i] = tid[i];
}

int sbr_stun_is_stun(const sbrs_u8 *d, sbrs_u32 len)
{
    if (len < SBR_STUN_REQ_LEN || (d[0] & 0xC0u) != 0 || rd32(d + 4) != COOKIE) return 0;
    return rd16(d + 2) <= len - SBR_STUN_REQ_LEN;
}

int sbr_stun_parse_response(const sbrs_u8 *req, const sbrs_u8 *resp, sbrs_u32 len, struct sbr_stun_addr *out)
{
    sbrs_u32 off, end, i, al, ty, xor_ok = 0, map_ok = 0, xip = 0, mip = 0, xport = 0, mport = 0;
    if (!sbr_stun_is_stun(resp, len) || rd16(resp) != 0x0101u) return 0;
    for (i = 0; i < SBR_STUN_TID_LEN; i++) if (resp[8 + i] != req[8 + i]) return 0;
    end = SBR_STUN_REQ_LEN + rd16(resp + 2);
    off = SBR_STUN_REQ_LEN;
    while (off + 4 <= end) {
        ty = rd16(resp + off); al = rd16(resp + off + 2); off += 4;
        if (al > end - off) return 0;                                  /* attribute runs past the message */
        if ((ty == 0x0020u || ty == 0x0001u) && al >= 8 && resp[off + 1] == 0x01) {   /* IPv4 only */
            if (ty == 0x0020u) { xor_ok = 1; xport = rd16(resp + off + 2) ^ (COOKIE >> 16); xip = rd32(resp + off + 4) ^ COOKIE; }
            else { map_ok = 1; mport = rd16(resp + off + 2); mip = rd32(resp + off + 4); }
        }
        off += (al + 3u) & ~3u;
    }
    if (xor_ok) { out->ip = xip; out->port = (sbrs_u16)xport; return 1; }
    if (map_ok) { out->ip = mip; out->port = (sbrs_u16)mport; return 1; }
    return 0;
}

int sbr_stun_nat_type(int have1, const struct sbr_stun_addr *a1, int have2, const struct sbr_stun_addr *a2)
{
    if (!have1 || !have2 || !a1 || !a2) return SBR_NAT_UNKNOWN;
    return (a1->ip == a2->ip && a1->port == a2->port) ? SBR_NAT_CONE : SBR_NAT_SYMMETRIC;
}

const char *sbr_stun_nat_name(int nat)
{
    return nat == SBR_NAT_CONE ? "cone" : nat == SBR_NAT_SYMMETRIC ? "symmetric" : "unknown";
}

/* appends to out at *n (capacity cap, keeps one byte for NUL); returns 0 on overflow */
static int put(char *out, sbrs_u32 cap, sbrs_u32 *n, const char *s)
{
    while (*s) { if (*n + 1 >= cap) return 0; out[(*n)++] = *s++; }
    return 1;
}
static int putu(char *out, sbrs_u32 cap, sbrs_u32 *n, sbrs_u32 v)
{
    char t[11]; int k = 0;
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) { if (*n + 1 >= cap) return 0; out[(*n)++] = t[--k]; }
    return 1;
}
static int putip(char *out, sbrs_u32 cap, sbrs_u32 *n, sbrs_u32 ip)
{
    return putu(out, cap, n, ip >> 24) && put(out, cap, n, ".") && putu(out, cap, n, (ip >> 16) & 255u) && put(out, cap, n, ".") &&
           putu(out, cap, n, (ip >> 8) & 255u) && put(out, cap, n, ".") && putu(out, cap, n, ip & 255u);
}

int sbr_stun_addr_str(sbrs_u32 ip, sbrs_u16 port, char *out, sbrs_u32 cap)
{
    sbrs_u32 n = 0;
    if (!cap) return -1;
    if (!putip(out, cap, &n, ip) || !put(out, cap, &n, ":") || !putu(out, cap, &n, port)) { out[0] = 0; return -1; }
    out[n] = 0;
    return (int)n;
}

int sbr_stun_cands_json(const struct sbr_stun_cand *c, sbrs_u32 n, char *out, sbrs_u32 cap)
{
    sbrs_u32 len = 0, i;
    const char *tn;
    if (!cap) return -1;
    out[0] = 0;
    if (n == 0) return 0;
    if (!put(out, cap, &len, "[")) goto bad;
    for (i = 0; i < n; i++) {
        tn = c[i].type == SBR_CAND_SRFLX ? "srflx" : c[i].type == SBR_CAND_OBSERVED ? "observed" : "host";
        if (!(i == 0 || put(out, cap, &len, ",")) || !put(out, cap, &len, "{\"type\":\"") || !put(out, cap, &len, tn) ||
            !put(out, cap, &len, "\",\"ip\":\"") || !putip(out, cap, &len, c[i].ip) || !put(out, cap, &len, "\",\"port\":") ||
            !putu(out, cap, &len, c[i].port) || !put(out, cap, &len, "}")) goto bad;
    }
    if (!put(out, cap, &len, "]")) goto bad;
    out[len] = 0;
    return (int)len;
bad:
    out[0] = 0;
    return -1;
}
