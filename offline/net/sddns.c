/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sddns: see sddns.h.  Freestanding C89: no libc calls, no heap, no float, struct copies are field-wise. */
#include "sddns.h"

#ifndef SDDNS_NO_SDNET
#include "sdnet.h"
#endif

/* ---- byte helpers ---- */
static sddns_u32 rd16(const sddns_u8 *p) { return ((sddns_u32)p[0] << 8) | p[1]; }
static sddns_u32 rd32(const sddns_u8 *p)
{
    return ((sddns_u32)p[0] << 24) | ((sddns_u32)p[1] << 16) | ((sddns_u32)p[2] << 8) | p[3];
}
static void wr16(sddns_u8 *p, sddns_u32 v) { p[0] = (sddns_u8)(v >> 8); p[1] = (sddns_u8)v; }

static int due(sddns_u32 now, sddns_u32 deadline) { return (int)(now - deadline) >= 0; }

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void zero(void *vp, sddns_u32 n)
{
    sddns_u8 *p = (sddns_u8 *)vp;
    while (n--) {
        *p++ = 0;
    }
}

/* ---- host names ---- */
int sddns_parse_dotted(const char *s, sddns_u32 *ip)
{
    sddns_u32 v = 0, part, i, digits;

    if (!s) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        part = 0;
        digits = 0;
        while (*s >= '0' && *s <= '9') {
            part = part * 10 + (sddns_u32)(*s - '0');
            digits++;
            if (digits > 3 || part > 255) {
                return 0;
            }
            if (digits == 2 && part < 10) {   /* leading zero: "01" */
                return 0;
            }
            s++;
        }
        if (digits == 0) {
            return 0;
        }
        v = (v << 8) | part;
        if (i < 3) {
            if (*s != '.') {
                return 0;
            }
            s++;
        }
    }
    if (*s) {
        return 0;
    }
    if (ip) {
        *ip = v;
    }
    return 1;
}

int sddns_normalize_host(const char *host, char *out)
{
    int n = 0, label = 0, i;
    char c;

    if (!host) {
        return -1;
    }
    for (i = 0; host[i]; i++) {
        c = lower(host[i]);
        if (c == '.') {
            if (label == 0) {
                return -1;               /* empty label (also a leading dot, "..") */
            }
            if (out[n - 1] == '-') {
                return -1;
            }
            if (host[i + 1] == 0) {
                break;                   /* one trailing dot is dropped */
            }
            label = 0;
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            if (c == '-' && label == 0) {
                return -1;
            }
            label++;
            if (label > 63) {
                return -1;
            }
        } else {
            return -1;
        }
        if (n >= SDDNS_HOST_MAX) {
            return -1;
        }
        out[n++] = c;
    }
    if (n == 0 || label == 0 || out[n - 1] == '-') {
        return -1;
    }
    out[n] = 0;
    return n;
}

int sddns_encode_name(const char *host, sddns_u8 *out, sddns_u32 cap)
{
    sddns_u32 n = 1, lab = 0, len, i;
    char c;

    if (!host || !*host || cap < 2) {
        return -1;
    }
    for (i = 0;; i++) {
        c = host[i];
        if (c == '.' || c == 0) {
            len = n - lab - 1;
            if (len == 0 || len > 63) {
                return -1;
            }
            out[lab] = (sddns_u8)len;
            if (c == 0) {
                break;
            }
            if (n >= cap) {
                return -1;
            }
            lab = n;
            n++;
            continue;
        }
        if (n >= cap) {
            return -1;
        }
        out[n++] = (sddns_u8)c;
    }
    if (n >= cap || n + 1 > SDDNS_WIRE_NAME_MAX) {
        return -1;
    }
    out[n++] = 0;
    return (int)n;
}

int sddns_build_query(const sddns_u8 *qwire, sddns_u32 qlen, sddns_u16 txid, sddns_u8 *out, sddns_u32 cap)
{
    sddns_u32 i, n = 12;

    if (qlen < 1 || qlen > SDDNS_WIRE_NAME_MAX || cap < 12 + qlen + 4) {
        return -1;
    }
    wr16(out, txid);
    wr16(out + 2, 0x0100);            /* RD */
    wr16(out + 4, 1);
    wr16(out + 6, 0);
    wr16(out + 8, 0);
    wr16(out + 10, 0);
    for (i = 0; i < qlen; i++) {
        out[n++] = qwire[i];
    }
    wr16(out + n, 1);                 /* A */
    wr16(out + n + 2, 1);             /* IN */
    return (int)(n + 4);
}

/* ---- reply parsing ---- */
/* Expands the (possibly compressed) name at pos into lower-case wire form; *next = first byte after the name in the stream. */
static int name_expand(const sddns_u8 *p, sddns_u32 len, sddns_u32 pos, sddns_u8 *out, sddns_u32 *outlen, sddns_u32 *next)
{
    sddns_u32 n = 0, jumps = 0, target, l, i;
    int jumped = 0;

    for (;;) {
        sddns_u32 c;

        if (pos >= len) {
            return -1;
        }
        c = p[pos];
        if (c == 0) {
            if (n + 1 > SDDNS_WIRE_NAME_MAX) {
                return -1;
            }
            out[n++] = 0;
            if (!jumped) {
                *next = pos + 1;
            }
            *outlen = n;
            return 0;
        }
        if ((c & 0xC0u) == 0xC0u) {
            if (pos + 1 >= len) {
                return -1;
            }
            target = ((c & 0x3Fu) << 8) | p[pos + 1];
            if (!jumped) {
                *next = pos + 2;
                jumped = 1;
            }
            if (target >= pos || ++jumps > SDDNS_MAX_JUMPS) {   /* only backward: no loop is possible, the count bounds the work */
                return -1;
            }
            pos = target;
            continue;
        }
        if (c & 0xC0u) {              /* 01 and 10 label types are reserved */
            return -1;
        }
        l = c;
        if (pos + 1 + l > len || n + 1 + l + 1 > SDDNS_WIRE_NAME_MAX) {
            return -1;
        }
        out[n++] = (sddns_u8)l;
        for (i = 0; i < l; i++) {
            out[n++] = (sddns_u8)lower((char)p[pos + 1 + i]);
        }
        pos += 1 + l;
    }
}

static int wire_eq(const sddns_u8 *a, sddns_u32 al, const sddns_u8 *b, sddns_u32 bl)
{
    sddns_u32 i;

    if (al != bl) {
        return 0;
    }
    for (i = 0; i < al; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static void wire_cpy(sddns_u8 *dst, const sddns_u8 *src, sddns_u32 n)
{
    sddns_u32 i;
    for (i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

int sddns_parse_reply(const sddns_u8 *p, sddns_u32 len, sddns_u16 txid, const sddns_u8 *qwire, sddns_u32 qlen,
                      sddns_u32 *ip, sddns_u32 *ttl)
{
    sddns_u8 nm[SDDNS_WIRE_NAME_MAX + 1], cur[SDDNS_WIRE_NAME_MAX + 1];
    sddns_u32 flags, an, pos, nmlen, curlen, i, next, type, cls, rdlen, rdata, raw, tmin = 0xFFFFFFFFu, chain = 0, addr, t;

    if (!p || !qwire || len < 12 || len > SDDNS_MAX_PACKET) {
        return SDDNS_E_BAD;
    }
    if (rd16(p) != (sddns_u32)txid) {
        return SDDNS_E_BAD;
    }
    flags = rd16(p + 2);
    if (!(flags & 0x8000u) || (flags & 0x7800u) || (flags & 0x0200u)) {   /* QR, OPCODE 0, TC clear */
        return SDDNS_E_BAD;
    }
    if (rd16(p + 4) != 1) {
        return SDDNS_E_BAD;
    }
    an = rd16(p + 6);
    if (an > SDDNS_MAX_ANSWERS) {
        return SDDNS_E_BAD;
    }
    if (name_expand(p, len, 12, nm, &nmlen, &next) || next + 4 > len) {
        return SDDNS_E_BAD;
    }
    if (!wire_eq(nm, nmlen, qwire, qlen) || rd16(p + next) != 1 || rd16(p + next + 2) != 1) {
        return SDDNS_E_BAD;
    }
    pos = next + 4;
    if ((flags & 15u) == 3u) {
        return SDDNS_E_NXDOMAIN;
    }
    if (flags & 15u) {
        return SDDNS_E_SERVFAIL;
    }
    wire_cpy(cur, qwire, qlen);
    curlen = qlen;
    for (i = 0; i < an; i++) {
        if (name_expand(p, len, pos, nm, &nmlen, &next) || next + 10 > len) {
            return SDDNS_E_BAD;
        }
        type = rd16(p + next);
        cls = rd16(p + next + 2);
        raw = rd32(p + next + 4);
        rdlen = rd16(p + next + 8);
        rdata = next + 10;
        if (rdata + rdlen > len) {
            return SDDNS_E_BAD;
        }
        pos = rdata + rdlen;
        if (cls != 1 || !wire_eq(nm, nmlen, cur, curlen)) {
            continue;                 /* another name or class: skipped, never trusted */
        }
        t = (raw & 0x80000000u) ? 0 : raw;
        if (type == 1) {
            if (rdlen != 4) {
                return SDDNS_E_BAD;
            }
            addr = rd32(p + rdata);
            if (addr == 0 || addr >= 0xE0000000u) {
                return SDDNS_E_BAD;
            }
            if (t < tmin) {
                tmin = t;
            }
            if (tmin < SDDNS_TTL_MIN) {
                tmin = SDDNS_TTL_MIN;
            }
            if (tmin > SDDNS_TTL_MAX) {
                tmin = SDDNS_TTL_MAX;
            }
            if (ip) {
                *ip = addr;
            }
            if (ttl) {
                *ttl = tmin;
            }
            return SDDNS_E_NONE;
        }
        if (type == 5) {
            if (rdlen < 1 || ++chain > SDDNS_MAX_CNAME) {
                return SDDNS_E_BAD;
            }
            if (name_expand(p, len, rdata, nm, &nmlen, &next) || next != rdata + rdlen) {
                return SDDNS_E_BAD;
            }
            wire_cpy(cur, nm, nmlen);
            curlen = nmlen;
            if (t < tmin) {
                tmin = t;
            }
        }
    }
    return SDDNS_E_NODATA;
}

/* ---- state ---- */
static sddns_u32 next_rand(sddns *d, sddns_u32 now)
{
    sddns_u32 x = d->rng ^ (now * 2654435761u);

    if (!x) {
        x = 0x2545f491u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    d->rng = x ? x : 0x2545f491u;
    return d->rng;
}

void sddns_init(sddns *d, const sddns_io *io, sddns_u32 seed)
{
    zero(d, (sddns_u32)sizeof(*d));
    d->io.send = io->send;
    d->io.recv = io->recv;
    d->io.ctx = io->ctx;
    d->port = SDDNS_PORT;
    d->base_ms = SDDNS_BASE_MS;
    d->rng = seed ? seed : 0x9e3779b9u;
    d->state = SDDNS_IDLE;
}

void sddns_set_servers(sddns *d, sddns_u32 primary, sddns_u32 secondary)
{
    if (primary == 0) {
        primary = secondary;
        secondary = 0;
    }
    if (secondary == primary) {
        secondary = 0;
    }
    d->server[0] = primary;
    d->server[1] = secondary;
}

void sddns_cache_flush(sddns *d)
{
    int i;
    for (i = 0; i < SDDNS_CACHE; i++) {
        d->cache[i].used = 0;
    }
}

static sddns_cache_entry *cache_find(sddns *d, const char *name, sddns_u32 now)
{
    int i;

    for (i = 0; i < SDDNS_CACHE; i++) {
        sddns_cache_entry *e = &d->cache[i];
        if (e->used && streq(e->name, name)) {
            if ((sddns_u32)(now - e->stamp) < e->ttl_ms) {
                return e;
            }
            e->used = 0;              /* expired (or the clock went backwards) */
        }
    }
    return 0;
}

static void cache_put(sddns *d, const char *name, sddns_u32 ip, sddns_u32 ttl_s, sddns_u32 now)
{
    int i, pick = 0;
    sddns_cache_entry *e;
    sddns_u32 best = 0xFFFFFFFFu;

    e = cache_find(d, name, now);
    if (!e) {
        for (i = 0; i < SDDNS_CACHE; i++) {
            if (!d->cache[i].used) {
                pick = i;
                best = 0;
                break;
            }
            if (d->cache[i].age < best) {
                best = d->cache[i].age;
                pick = i;
            }
        }
        e = &d->cache[pick];
    }
    for (i = 0; name[i]; i++) {
        e->name[i] = name[i];
    }
    e->name[i] = 0;
    e->ip = ip;
    e->stamp = now;
    e->ttl_ms = ttl_s * 1000u;
    e->age = ++d->age_clock;
    e->used = 1;
}

int sddns_cache_lookup(sddns *d, const char *host, sddns_u32 now, sddns_u32 *ip)
{
    char nm[SDDNS_HOST_MAX + 1];
    sddns_cache_entry *e;

    if (sddns_normalize_host(host, nm) < 0) {
        return 0;
    }
    e = cache_find(d, nm, now);
    if (!e) {
        return 0;
    }
    if (ip) {
        *ip = e->ip;
    }
    return 1;
}

static void finish(sddns *d, int err)
{
    d->err = err;
    d->state = err ? SDDNS_FAILED : SDDNS_DONE;
}

static int last_label_numeric(const char *s)
{
    const char *last = s, *p;

    for (p = s; *p; p++) {
        if (*p == '.') {
            last = p + 1;
        }
    }
    if (!*last) {
        return 0;
    }
    for (p = last; *p; p++) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    return 1;
}

int sddns_start(sddns *d, const char *host, sddns_u32 now)
{
    int n, q;
    sddns_cache_entry *e;
    sddns_u32 ip;

    if (d->state == SDDNS_BUSY) {
        return SDDNS_E_BUSY;
    }
    d->err = SDDNS_E_NONE;
    d->state = SDDNS_IDLE;
    d->from_cache = 0;
    d->result_ip = 0;
    d->result_ttl = 0;
    if (!host) {
        return SDDNS_E_ARG;
    }
    if (sddns_parse_dotted(host, &ip)) {                 /* literal: no query, no cache, no server needed */
        d->name[0] = 0;
        d->result_ip = ip;
        d->state = SDDNS_DONE;
        return 0;
    }
    n = sddns_normalize_host(host, d->name);
    if (n < 0 || last_label_numeric(d->name)) {
        d->name[0] = 0;
        return SDDNS_E_ARG;
    }
    e = cache_find(d, d->name, now);
    if (e) {
        e->age = ++d->age_clock;
        d->result_ip = e->ip;
        d->result_ttl = (e->ttl_ms - (sddns_u32)(now - e->stamp)) / 1000u;
        d->from_cache = 1;
        d->state = SDDNS_DONE;
        return 0;
    }
    if (d->server[0] == 0) {
        finish(d, SDDNS_E_NOSERVER);
        return 0;
    }
    q = sddns_encode_name(d->name, d->qwire, (sddns_u32)sizeof(d->qwire));
    if (q < 0) {
        d->name[0] = 0;
        return SDDNS_E_ARG;
    }
    d->qwire_len = (sddns_u32)q;
    d->txid = (sddns_u16)(next_rand(d, now) >> 8);
    d->tries = 0;
    d->sent_mask = 0;
    d->bad_seen = 0;
    d->servfail_seen = 0;
    d->deadline = now;                                   /* first poll sends */
    d->state = SDDNS_BUSY;
    return 0;
}

void sddns_abort(sddns *d)
{
    d->state = SDDNS_IDLE;
    d->err = SDDNS_E_NONE;
}

static int nservers(const sddns *d) { return d->server[1] ? 2 : 1; }

static void handle_datagram(sddns *d, const sddns_u8 *buf, sddns_u32 n, sddns_u32 ip, sddns_u16 port, sddns_u32 now)
{
    int i, ok = 0, r;
    sddns_u32 rip = 0, rttl = 0;

    d->st_rx++;
    if (d->state != SDDNS_BUSY || port != d->port) {
        d->st_ignored++;
        return;
    }
    for (i = 0; i < nservers(d); i++) {
        if (d->server[i] == ip && (d->sent_mask & (1u << i))) {
            ok = 1;
        }
    }
    if (!ok) {
        d->st_ignored++;
        return;
    }
    r = sddns_parse_reply(buf, n, d->txid, d->qwire, d->qwire_len, &rip, &rttl);
    if (r == SDDNS_E_NONE) {
        d->result_ip = rip;
        d->result_ttl = rttl;
        cache_put(d, d->name, rip, rttl, now);
        finish(d, SDDNS_E_NONE);
    } else if (r == SDDNS_E_NXDOMAIN || r == SDDNS_E_NODATA) {
        finish(d, r);
    } else if (r == SDDNS_E_SERVFAIL) {
        for (i = 0; i < nservers(d); i++) {
            if (d->server[i] == ip) {
                d->servfail_seen = (sddns_u8)(d->servfail_seen | (1u << i));
            }
        }
        if (d->servfail_seen == (nservers(d) == 2 ? 3 : 1)) {   /* every server said no */
            finish(d, SDDNS_E_SERVFAIL);
        } else {
            d->deadline = now;                                  /* try the other server now */
        }
    } else {
        d->st_bad++;
        d->bad_seen = 1;
    }
}

int sddns_poll(sddns *d, sddns_u32 now)
{
    sddns_u8 buf[SDDNS_MAX_PACKET + 1];     /* one spare byte: a datagram the socket had to truncate is seen as oversized */
    sddns_u32 ip, n, tmo;
    sddns_u16 port;
    int i, r, idx;

    for (i = 0; i < SDDNS_RX_BUDGET; i++) {
        ip = 0;
        port = 0;
        r = d->io.recv ? d->io.recv(d->io.ctx, buf, (sddns_u32)sizeof(buf), &ip, &port) : 0;
        if (r <= 0) {
            break;
        }
        n = (sddns_u32)r;
        if (n > SDDNS_MAX_PACKET) {                      /* oversized, or a lying callback */
            d->st_rx++;
            d->st_ignored++;
            continue;
        }
        handle_datagram(d, buf, n, ip, port, now);
    }
    if (d->state != SDDNS_BUSY || !due(now, d->deadline)) {
        return d->state;
    }
    if (d->tries >= SDDNS_TRIES) {
        finish(d, d->servfail_seen ? SDDNS_E_SERVFAIL : d->bad_seen ? SDDNS_E_BAD : SDDNS_E_TIMEOUT);
        return d->state;
    }
    n = (sddns_u32)sddns_build_query(d->qwire, d->qwire_len, d->txid, buf, (sddns_u32)sizeof(buf));
    idx = (int)(d->tries % (sddns_u32)nservers(d));
    d->sent_mask = (sddns_u8)(d->sent_mask | (1u << idx));
    if (d->io.send) {
        (void)d->io.send(d->io.ctx, d->server[idx], d->port, buf, n);   /* a failed send is a lost datagram: the timer decides */
    }
    d->st_sent++;
    tmo = d->base_ms << d->tries;
    d->sent_ms = now;
    d->deadline = now + tmo;
    d->tries++;
    return d->state;
}

/* ---- sdnet binding ---- */
#ifndef SDDNS_NO_SDNET
static int sn_send(void *ctx, sddns_u32 ip, sddns_u16 port, const sddns_u8 *p, sddns_u32 len)
{
    struct sddns_sdnet_ctx *c = (struct sddns_sdnet_ctx *)ctx;
    return sdnet_sendto(c->net, c->sock, ip, port, p, (sdnet_u16)len) < 0 ? -1 : 0;
}

static int sn_recv(void *ctx, sddns_u8 *buf, sddns_u32 cap, sddns_u32 *ip, sddns_u16 *port)
{
    struct sddns_sdnet_ctx *c = (struct sddns_sdnet_ctx *)ctx;
    sdnet_u32 sip = 0;
    sdnet_u16 sport = 0;
    int r = sdnet_recvfrom(c->net, c->sock, buf, (sdnet_u16)cap, &sip, &sport);

    if (r <= 0) {
        return 0;
    }
    *ip = sip;
    *port = sport;
    return r;
}

int sddns_sdnet_bind(sddns_io *io, struct sddns_sdnet_ctx *ctx, struct sdnet *net, sddns_u16 src_port)
{
    int sock = sdnet_bind(net, src_port, 0, 0);

    if (sock < 0) {
        return sock;
    }
    ctx->net = net;
    ctx->sock = sock;
    io->send = sn_send;
    io->recv = sn_recv;
    io->ctx = ctx;
    return sock;
}
#endif
