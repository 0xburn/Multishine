/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* on_cfgrec: see on_cfgrec.h.  Strict C89, no libc, no heap, no globals. */
#include "on_cfgrec.h"

extern unsigned int ch_crc32(const unsigned char *p, unsigned int len);    /* codehist.c: zlib CRC-32 */

static unsigned int rd32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}
static unsigned int rd16(const unsigned char *p) { return ((unsigned int)p[0] << 8) | (unsigned int)p[1]; }

static int nonzero(const unsigned char *p, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) if (p[i]) return 1;
    return 0;
}

static int same(const unsigned char *a, const unsigned char *b, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}
/* copy n bytes, or clear them when src is NULL */
void onr_cpz(void *dst, const unsigned char *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    unsigned int i;
    for (i = 0; i < n; i++) d[i] = src ? src[i] : 0;
}

static int key_is(const unsigned char *k, const unsigned char *p, unsigned int n)       /* k is a real (non-NULL, non-zero) 32 byte key and its first n bytes equal p */
{
    return k && nonzero(k, 32) && same(k, p, n);
}

/* NUL terminated inside 24 bytes, 1..23 printable ASCII characters (0x21..0x7E for a code, 0x20..0x7E for a name), bytes after the NUL are zero */
static int text_ok(const unsigned char *p, int allow_space)
{
    unsigned int i, n = 0;
    while (n < 24 && p[n]) n++;
    if (n == 0 || n >= 24) return 0;
    for (i = 0; i < n; i++) if (p[i] < (allow_space ? 0x20 : 0x21) || p[i] > 0x7E) return 0;
    for (i = n; i < 24; i++) if (p[i]) return 0;
    return 1;
}

static int code_ok(const unsigned char *p)
{
    unsigned int i, n = 0, hash = 0, at = 0;
    if (!text_ok(p, 0)) return 0;
    while (p[n]) n++;
    if (n < 3 || n > 13) return 0;
    for (i = 0; i < n; i++) {
        if (p[i] == '#' || p[i] == '@') { hash++; at = i; }
        else if (!((p[i] >= 'A' && p[i] <= 'Z') || (p[i] >= 'a' && p[i] <= 'z') || (p[i] >= '0' && p[i] <= '9'))) return 0;
    }
    return hash == 1 && at != 0 && at != n - 1;
}

static int in_range(unsigned int v, unsigned int lo, unsigned int hi) { return v == 0 || (v >= lo && v <= hi); }

/* the host name rules (the same as od_glue.c od_host_ok and onfile.py host_ok): 1..47 characters of [A-Za-z0-9.-], labels 1..63, no leading/trailing dot or hyphen at a label edge */
static int host_text_ok(const unsigned char *p)
{
    unsigned int i, n = 0, lab = 0;
    unsigned char prev = '.';
    while (n < 48 && p[n]) n++;
    if (n == 0 || n >= 48) return 0;
    for (i = n; i < 48; i++) if (p[i]) return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = p[i];
        if (c == '.') {
            if (prev == '.' || prev == '-' || lab == 0) return 0;
            lab = 0;
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            if (c == '-' && prev == '.') return 0;
            if (++lab > 63u) return 0;
        } else return 0;
        prev = c;
    }
    return prev != '.' && prev != '-';
}

int onr_host(const unsigned char *r, char *out)
{
    const unsigned char *x = r + ONR_EXT_OFF;
    unsigned int i, n = 0;
    for (i = 0; i < 48; i++) out[i] = 0;
    if (rd32(x) != ONR_EXT_MAGIC || x[4] != 1 || x[6] || x[7]) return 0;
    if (rd32(x + 8) != ch_crc32(x + 0x0C, 48)) return 0;
    if (!host_text_ok(x + 0x0C)) return 0;
    while (x[0x0C + n]) n++;
    if (x[5] != n) return 0;
    for (i = 0; i < 48; i++) out[i] = (char)x[0x0C + i];
    return 1;
}

static int onr_check_core(const unsigned char *r, int host_ok)
{
    unsigned int ver;
    if (rd32(r) != ONR_MAGIC) return ONR_DAMAGED;
    ver = r[4];
    if (ver == 0) return ONR_DAMAGED;
    if (ver > ONR_VERSION) return ONR_NEWER;                       /* a newer layout is never read as ours (and never written by this code) */
    if (rd32(r + 8) != ch_crc32(r + 0x0C, ONR_SIZE - 0x0C)) return ONR_DAMAGED;
    if (r[5] || rd16(r + 6)) return ONR_DAMAGED;                   /* reserved bytes of version 1 are zero */
    if (rd32(r + 0x0C) == 0 && !host_ok) return ONR_INCOMPLETE;    /* no server address (and no valid host name) */
    if (!nonzero(r + 0x20, 32)) return ONR_INCOMPLETE;             /* no pinned key: nothing could be verified */
    if (!code_ok(r + 0x78) || !text_ok(r + 0x90, 1)) return ONR_INCOMPLETE;
    if (!in_range(rd32(r + 0x14), 5000u, 300000u) || !in_range(rd32(r + 0x18), 5000u, 120000u) || !in_range(rd32(r + 0x1C), 5000u, 120000u)) return ONR_INCOMPLETE;
    return ONR_OK;
}

int onr_check(const unsigned char *r) { return onr_check_core(r, 0); }
int onr_check2(const unsigned char *r)
{
    char h[48];
    return onr_check_core(r, onr_host(r, h));
}

#ifdef SBR_NETCFG                       /* a release build has no lab parameter at all: the decoders compile exactly as before */
#define LABP , int lab
#define LABA(x) , (x)
#define LABV lab
#else
#define LABP
#define LABA(x)
#define LABV 0
#endif
/* lab: 1 = the lab/dev "Server: Lab" choice: the server lock is not applied, the card record's address, name and pinned key are used and its identity
   is kept.  Only -DSBR_NETCFG builds have the public _lab entry points below; onr_decode / onr_decode_host (all other builds) always pass 0. */
static void decode_host(struct od_cfg *d, const unsigned char *r LABP)
{
    if ((d->flags & OD_CFGF_SRVLOCK) && !LABV) return;              /* the patched server name stays */
    if (onr_host(r, d->host)) d->flags |= OD_CFGF_HOST;
    else d->flags &= ~OD_CFGF_HOST;
}
void onr_decode_host(struct od_cfg *d, const unsigned char *r) { decode_host(d, r LABA(0)); }

/* the card's first pinned key equals one of the patched (non-zero) ones */
static int pin_matches(const struct od_cfg *d, const unsigned char *r)
{
    unsigned int k;
    for (k = 0; k < 2; k++)                                      /* the card's pin 1 (+0x20) or pin 2 (+0x40, written by a link on the patched server) */
        if (key_is(d->pin[k], r + 0x20, 32) || key_is(d->pin[k], r + 0x40, 32)) return 1;
    return 0;
}

/* dev, tok (24 bytes from dt; NULL = a guest: all zero) and code, name (48 bytes from cn): in the config they are one contiguous 0x48 byte group, in the order of the record's +0x60..+0xA8 */
#define OFS(f) __builtin_offsetof(struct od_cfg, f)
typedef char od_ident_contiguous[(OFS(tok) == OFS(dev) + 8 && OFS(code) == OFS(dev) + 24 && OFS(name) == OFS(dev) + 48) ? 1 : -1];
static void put_ident(struct od_cfg *d, const unsigned char *dt, const unsigned char *cn)
{
    unsigned char *g = (unsigned char *)d + OFS(dev);
    unsigned int i;
    onr_cpz(g, dt, 24);
    onr_cpz(g + 24, cn, 48);
    for (i = 24; i < 48; i++) if (g[i] >= 'a' && g[i] <= 'z') g[i] = (unsigned char)(g[i] - 32);      /* the server's codes are upper case */
}

static void decode_rec(struct od_cfg *d, const unsigned char *r LABP)
{
    unsigned int i, modes = rd16(r + 0x12), lock = LABV ? 0u : (d->flags & OD_CFGF_SRVLOCK), ident = !lock || pin_matches(d, r);
    d->flags = (d->flags & ~OD_CFGF_CARD_MASK) | (lock ? (d->flags & OD_CFGF_HOST) : 0u) | OD_CFGF_ID | ((modes & ONR_MODE_UNRANKED) ? OD_CFGF_UNRANKED : 0u) |
               ((modes & (ONR_MODE_RANKED | ONR_MODE_UNRANKED)) ? OD_CFGF_RANKED : 0u);   /*: Grandmaster follows the server (it refuses Ranked itself when it does not offer it); the cards' Ranked bit predates production Ranked */
    if (!lock) {
        d->server_ip = rd32(r + 0x0C);
        d->server_port = rd16(r + 0x10);
        for (i = 0; i < 32; i++) { d->pin[0][i] = r[0x20 + i]; d->pin[1][i] = r[0x40 + i]; }
    }
    d->find_ms = rd32(r + 0x14);
    d->connect_ms = rd32(r + 0x18);
    d->net_ms = rd32(r + 0x1C);
    put_ident(d, ident ? r + 0x60 : 0, r + 0x78);                          /* all zero device id and secret = a guest (od_glue.c od_guest) */
}

void onr_decode(struct od_cfg *d, const unsigned char *r) { decode_rec(d, r LABA(0)); }

/* ---- the guest setup of a server-locked build on a card that has no MultishineOnline (fresh seeded card): the patched server, the patched key, no identity ---- */
#define ONR_GUEST_CODE "GUEST@0"           /* placeholders the record format needs (code_ok / text_ok); the guest's real code comes with its token, the device id stays all zero */
#define ONR_GUEST_NAME "Guest"
static void put_str24(unsigned char *p, const char *s)
{
    unsigned int i = 0;
    for (; i < 23u && s[i]; i++) p[i] = (unsigned char)s[i];
    for (; i < 24u; i++) p[i] = 0;
}
void onr_guest(struct od_cfg *d)
{
    d->flags = (d->flags & ~OD_CFGF_CARD_MASK) | (d->flags & OD_CFGF_HOST) | OD_CFGF_ID | OD_CFGF_UNRANKED | OD_CFGF_RANKED;
    d->find_ms = d->connect_ms = d->net_ms = 0;
    onr_cpz(d->dev, 0, 24);                                         /* dev + tok: all zero = a guest (od_glue.c od_guest) */
    put_str24((unsigned char *)d->code, ONR_GUEST_CODE);
    put_str24((unsigned char *)d->name, ONR_GUEST_NAME);
}

void onr_make_guest(unsigned char *r, const struct od_cfg *b)
{
    unsigned int i, n = 0, crc;
    unsigned char *x = r + ONR_EXT_OFF;
    onr_cpz(r, 0, ONR_EXT_END);
    onr_cpz(r, (const unsigned char *)"SDON\1", 5);                  /* magic, version 1; byte 5 and 6..7 stay zero */
    for (i = 0; i < 4; i++) r[0x0C + i] = (unsigned char)(b->server_ip >> (24 - 8 * i));
    r[0x10] = (unsigned char)(b->server_port >> 8); r[0x11] = (unsigned char)b->server_port;
    r[0x13] = (unsigned char)(ONR_MODE_UNRANKED | ONR_MODE_RANKED);
    onr_cpz(r + 0x20, b->pin[0], 32);
    onr_cpz(r + 0x40, b->pin[1], 32);
    put_str24(r + 0x78, ONR_GUEST_CODE);
    put_str24(r + 0x90, ONR_GUEST_NAME);
    crc = ch_crc32(r + 0x0C, ONR_SIZE - 0x0C);
    for (i = 0; i < 4; i++) r[8 + i] = (unsigned char)(crc >> (24 - 8 * i));
    if (!b->server_ip && (b->flags & OD_CFGF_HOST)) {               /* a name-only build: the host extension carries the name */
        while (n < 47u && b->host[n]) n++;
        onr_cpz(x, (const unsigned char *)"SDHN\1", 5);
        x[5] = (unsigned char)n;
        onr_cpz(x + 0x0C, (const unsigned char *)b->host, n);
        crc = ch_crc32(x + 0x0C, 48);
        for (i = 0; i < 4; i++) x[8 + i] = (unsigned char)(crc >> (24 - 8 * i));
    }
}
#ifdef SBR_NETCFG
void onr_decode_lab(struct od_cfg *d, const unsigned char *r, int lab) { decode_rec(d, r, lab); }
void onr_decode_host_lab(struct od_cfg *d, const unsigned char *r, int lab) { decode_host(d, r, lab); }
#endif

/* ---- identity slots (on_cfgrec.h) ---- */
int onr_slots_state(const unsigned char *r)
{
    const unsigned char *x = r + ONR_SL_OFF;
    if (rd32(x) != ONR_SL_MAGIC) return 0;
    if (x[4] > 1) return 2;                                           /* a newer layout is never read and never written */
    if (x[4] != 1 || x[5] != ONR_SL_N || x[6] || x[7]) return 0;
    if (rd32(x + 8) != ch_crc32(x + ONR_SL_HDR, ONR_SL_N * ONR_SL_ENT)) return 0;
    return 1;
}

/* the environment the old single identity belongs to; tag = the first 8 bytes of the key it was linked on */
static int legacy_home(const unsigned char *r, int env, int lab_build, const unsigned char *bk0, const unsigned char *bk1, unsigned char *tag)
{
    const unsigned char *k = key_is(bk0, r + 0x20, 32) || key_is(bk0, r + 0x40, 32) ? bk0 : key_is(bk1, r + 0x20, 32) || key_is(bk1, r + 0x40, 32) ? bk1 : 0;
    onr_cpz(tag, k ? k : r + 0x20, 8);
    if (k) return ONR_ENV_PROD;
    return env != ONR_ENV_PROD ? env : (lab_build ? ONR_ENV_LAB : ONR_ENV_DEV);
}

int onr_slot_get(const unsigned char *r, int env, int lab_build, const unsigned char *bk0, const unsigned char *bk1, const unsigned char **ident)
{
    unsigned char ltag[8];
    const unsigned char *tag, *id;
    int st = onr_slots_state(r);
    if (st == 2 || env < 0 || env >= ONR_SL_N) return 0;
    if (st == 1) {
        tag = r + ONR_SL_OFF + ONR_SL_HDR + (unsigned int)env * ONR_SL_ENT; id = tag + 8;
    } else {
        if (legacy_home(r, env, lab_build, bk0, bk1, ltag) != env) return 0;
        tag = ltag; id = r + 0x60;
    }
    if (!nonzero(id, 8) || !nonzero(id + 8, 16) || !nonzero(tag, 8)) return 0;     /* the guest placeholder: no device id or no secret; an empty tag */
    if (env == ONR_ENV_PROD ? !key_is(bk0, tag, 8) && !key_is(bk1, tag, 8) : !key_is(r + 0x20, tag, 8)) return 0;     /* the tag must be the key in force (prod: a patched key; dev / lab: the record's key 1) */
    *ident = id;
    return 1;
}

void onr_decode_slot(struct od_cfg *d, const unsigned char *r, int env, int lab_build, const unsigned char *bk0, const unsigned char *bk1)
{
    const unsigned char *id = 0;
    if (!onr_slot_get(r, env, lab_build, bk0, bk1, &id)) id = 0;
    put_ident(d, id, id ? id + 24 : 0);
}

int onr_slot_put(unsigned char *r, int env, int lab_build, const unsigned char *bk0, const unsigned char *bk1, const unsigned char *ident)
{
    unsigned char ltag[8], *x = r + ONR_SL_OFF, *e;
    unsigned int i, crc;
    int st = onr_slots_state(r);
    if (st == 2 || env < 0 || env >= ONR_SL_N) return 0;
    if (st != 1) {                                                    /* none or damaged: make it, carrying the old single identity into its environment */
        onr_cpz(x, 0, ONR_SL_SIZE);
        if (nonzero(r + 0x60, 8) && nonzero(r + 0x68, 16)) {
            i = (unsigned int)legacy_home(r, env, lab_build, bk0, bk1, ltag);
            e = x + ONR_SL_HDR + i * ONR_SL_ENT;
            onr_cpz(e, ltag, 8); onr_cpz(e + 8, r + 0x60, 0x48);
        }
    }
    e = x + ONR_SL_HDR + (unsigned int)env * ONR_SL_ENT;
    onr_cpz(e, 0, ONR_SL_ENT);
    if (nonzero(ident, 8)) {
        onr_cpz(e, env == ONR_ENV_PROD ? bk0 : r + 0x20, 8);
        onr_cpz(e + 8, ident, 0x48);
    }
    onr_cpz(x, (const unsigned char *)"SDSL\1\3", 6);                 /* magic, version 1, 3 slots (bytes 6, 7 are zero: a valid region has them so, a made one was cleared above) */
    crc = ch_crc32(x + ONR_SL_HDR, ONR_SL_N * ONR_SL_ENT);
    for (i = 0; i < 4; i++) x[8 + i] = (unsigned char)(crc >> (24 - 8 * i));
    return 1;
}

unsigned int onr_backup(const unsigned char *r)
{
    unsigned int ip = rd32(r + 0xA8), a = ip >> 24;
    if (a == 0 || a == 127u || a >= 224u || ip == rd32(r + 0x0C)) return 0;     /* none, "this host", loopback, multicast/broadcast/reserved, or the primary again */
    return ip;
}
