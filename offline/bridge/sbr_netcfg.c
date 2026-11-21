/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sbr_netcfg: see sbr_netcfg.h.  Empty translation unit unless -DSBR_NETCFG=1 (dev/lab builds). */
#include "sbr_netcfg.h"
#include "sbr_ident.h"

#ifdef SBR_NETCFG

typedef sbr_u32 u32;
typedef sbr_u8 u8;

#ifndef SBRC_DATA
#ifdef SBRC_HOST
#define SBRC_DATA
#else
#define SBRC_DATA __attribute__((section(".data")))
#endif
#endif

static struct sbr_nc g_nc SBRC_DATA;
static int g_state SBRC_DATA = SBR_NC_ST_PENDING;
static int g_real SBRC_DATA;                                         /* 1 = Real Slippi chosen: the card override is not used */

static u32 get32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

int sbr_nc_ip_ok(u32 ip, int private_only)
{
    u32 a = ip >> 24, b = (ip >> 16) & 255u;
    if (ip == 0 || a == 0 || a == 127u || a >= 224u) return 0;       /* unspecified, "this network", loopback, multicast, class E, broadcast */
    if (a == 169u && b == 254u) return 0;                            /* link-local */
    if (private_only) return a == 10u || (a == 172u && b >= 16u && b <= 31u) || (a == 192u && b == 168u);
    return 1;
}

int sbr_nc_decode(const u8 *in, struct sbr_nc *o)
{
    u32 mask;
    o->mm_ip = o->dns_ip = o->mask = 0;
    if (in[0] != 'S' || in[1] != 'B' || in[2] != 'N' || in[3] != 'C') return SBR_NC_ST_ABSENT;
    if (in[4] > SBR_NC_VER) return SBR_NC_ST_NEWER;                   /* a newer section is never guessed at */
    if (in[4] != SBR_NC_VER || (((u32)in[6] << 8) | in[7]) != SBR_NC_LEN) return SBR_NC_ST_DAMAGED;
    if (sbr_crc32(in, SBR_NC_LEN - 4u) != get32(in + SBR_NC_LEN - 4u)) return SBR_NC_ST_DAMAGED;
    mask = in[5];
    if (!mask || (mask & ~(SBR_NC_MM | SBR_NC_STATUS | SBR_NC_DNS)) || in[22] || in[23]) return SBR_NC_ST_INVALID;
    if (mask & SBR_NC_MM) o->mm_ip = get32(in + 8);
    if (mask & SBR_NC_DNS) o->dns_ip = get32(in + 16);
    o->mask = mask;
    if (((mask & SBR_NC_MM) && !sbr_nc_ip_ok(o->mm_ip, 0)) || ((mask & SBR_NC_DNS) && !sbr_nc_ip_ok(o->dns_ip, 0))) {
        o->mm_ip = o->dns_ip = o->mask = 0;
        return SBR_NC_ST_INVALID;
    }
    return SBR_NC_ST_ACTIVE;
}

void sbr_nc_encode(u8 *out, const struct sbr_nc *c)
{
    u32 i;
    for (i = 0; i < SBR_NC_LEN; i++) out[i] = 0;
    out[0] = 'S'; out[1] = 'B'; out[2] = 'N'; out[3] = 'C';
    out[4] = (u8)SBR_NC_VER;
    out[5] = (u8)c->mask;
    out[6] = 0; out[7] = (u8)SBR_NC_LEN;
    put32(out + 8, c->mm_ip);
    put32(out + 16, c->dns_ip);
    put32(out + 24, sbr_crc32(out, SBR_NC_LEN - 4u));
}

void sbr_nc_parse_block(const u8 *p)
{
    struct sbr_nc n;
    int s = sbr_nc_decode(p + SBR_NC_OFF, &n);
    g_nc = n;
    g_state = s;
}

void sbr_nc_no_file(void)
{
    g_nc.mm_ip = g_nc.dns_ip = g_nc.mask = 0;
    g_state = SBR_NC_ST_ABSENT;
}

void sbr_nc_reset(void)
{
    sbr_nc_no_file();
    g_state = SBR_NC_ST_PENDING;
}

int sbr_nc_state(void) { return g_state; }
const struct sbr_nc *sbr_nc_get(void) { return g_state == SBR_NC_ST_ACTIVE ? &g_nc : 0; }
const char *sbr_nc_words(void) { return g_state == SBR_NC_ST_ACTIVE && !g_real ? "Net cfg from card" : 0; }

void sbr_nc_set_real(int on) { g_real = on ? 1 : 0; }
int sbr_nc_real(void) { return g_real; }
u32 sbr_nc_card_mm_ip(void) { return g_state == SBR_NC_ST_ACTIVE && (g_nc.mask & SBR_NC_MM) ? g_nc.mm_ip : 0u; }
u32 sbr_nc_mm_ip(u32 baked) { return !g_real && g_state == SBR_NC_ST_ACTIVE && (g_nc.mask & SBR_NC_MM) ? g_nc.mm_ip : baked; }
u32 sbr_nc_dns_ip(u32 baked) { return !g_real && g_state == SBR_NC_ST_ACTIVE && (g_nc.mask & SBR_NC_DNS) ? g_nc.dns_ip : baked; }

#endif
