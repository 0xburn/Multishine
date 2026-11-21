/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* mw2: wire v2 session layer.  See mw2.h.  Strict C89, no libc, no heap. */
#include "mw2.h"

static void zero(void *p, mw2_u32 n) { mw2_u8 *b = (mw2_u8 *)p; while (n--) *b++ = 0; }
static void copy(void *d, const void *s, mw2_u32 n) { mw2_u8 *a = (mw2_u8 *)d; const mw2_u8 *b = (const mw2_u8 *)s; while (n--) *a++ = *b++; }
static void put32(mw2_u8 *p, mw2_u32 v) { p[0] = (mw2_u8)(v >> 24); p[1] = (mw2_u8)(v >> 16); p[2] = (mw2_u8)(v >> 8); p[3] = (mw2_u8)v; }
static mw2_u32 get32(const mw2_u8 *p) { return ((mw2_u32)p[0] << 24) | ((mw2_u32)p[1] << 16) | ((mw2_u32)p[2] << 8) | p[3]; }
static mw2_u32 elapsed(mw2_u32 now, mw2_u32 then) { mw2_u32 d = (now - then) & 0xFFFFFFFFul; return (d & 0x80000000ul) ? 0ul : d; }

void mw2_init(mw2 *m) { zero(m, sizeof *m); }
void mw2_wipe(mw2 *m) { zero(m, sizeof *m); }

void mw2_set_pins(mw2 *m, const mw2_u8 *keys, int n)
{
    int i;
    if (n < 0) n = 0;
    if (n > 2) n = 2;
    for (i = 0; i < n; i++) copy(m->pin[i], keys + 32 * i, 32);
    m->npin = n;
    mw2_drop(m);
}

void mw2_seed(mw2 *m, const mw2_u8 *data, mw2_u32 n)
{
    mw2_u8 t[32];
    while (n) {
        mw2_u32 k = n > 256 ? 256 : n;
        mw2_hmac(m->pool, data, k, t);
        copy(m->pool, t, 32);
        data += k; n -= k;
    }
}

void mw2_drop(mw2 *m)
{
    m->state = MW2_NONE;
    zero(m->kc, 32); zero(m->ks, 32);
}

void mw2_use(mw2 *m, const mw2_u8 *dev, const mw2_u8 *secret)
{
    int same;
    mw2_u32 i;
    if (dev && secret) {
        same = m->have_dev;
        for (i = 0; i < 8; i++) if (m->dev[i] != dev[i]) same = 0;
        for (i = 0; i < 16; i++) if (m->secret[i] != secret[i]) same = 0;
        if (!same) { copy(m->dev, dev, 8); copy(m->secret, secret, 16); m->have_dev = 1; mw2_drop(m); }
    } else if (m->have_dev) {
        zero(m->dev, 8); zero(m->secret, 16); m->have_dev = 0; mw2_drop(m);
    }
}

int mw2_ready(const mw2 *m, mw2_u32 now)
{
    if (m->state != MW2_READY) return 0;
    if (m->ctr >= 0xFFFF0000ul) return 0;
    return elapsed(now, m->last_ok) + 10000ul < m->ttl_ms;
}

static void skey(const mw2 *m, mw2_u8 key[32])
{
    zero(key, 32);
    copy(key, m->secret, 16);
}

const mw2_u8 *mw2_hs1(mw2 *m, mw2_u32 now)
{
    mw2_u8 msg[16], t[32], k[32];
    (void)now;
    m->pool_ctr++;
    msg[0] = 'c'; msg[1] = 'n'; put32(msg + 2, m->pool_ctr);
    mw2_hmac(m->pool, msg, 6, t);
    copy(m->cn, t, 16);
    zero(m->hs1, sizeof m->hs1);
    m->hs1[0] = 0x4D; m->hs1[1] = 2; m->hs1[2] = MW2_OP_HS; m->hs1[3] = (mw2_u8)(m->have_dev ? 1 : 0);
    copy(m->hs1 + 4 + 8, m->cn, 16);
    if (m->have_dev) {
        mw2_u8 tm[7 + 8 + 16];
        copy(m->hs1 + 4, m->dev, 8);
        copy(tm, "ms2 hs1", 7); copy(tm + 7, m->dev, 8); copy(tm + 15, m->cn, 16);
        skey(m, k);
        mw2_hmac(k, tm, sizeof tm, t);
        copy(m->hs1 + 4 + 8 + 16, t, 16);
        zero(k, 32);
    }
    m->dev_sess = m->have_dev;
    m->state = MW2_HS_SENT;
    return m->hs1;
}

/* the signed text of HS2 */
static mw2_u32 hs2_text(const mw2 *m, mw2_u8 *o, mw2_u32 sid, const mw2_u8 *sn, mw2_u32 ttl_s)
{
    mw2_u32 n = 0;
    copy(o, "ms2-hs2", 7); n = 7;
    o[n++] = (mw2_u8)(m->dev_sess ? 1 : 0);
    if (m->dev_sess) copy(o + n, m->dev, 8); else zero(o + n, 8);
    n += 8;
    copy(o + n, m->cn, 16); n += 16;
    put32(o + n, sid); n += 4;
    copy(o + n, sn, 16); n += 16;
    o[n++] = (mw2_u8)(ttl_s >> 8); o[n++] = (mw2_u8)ttl_s;
    return n;
}

static void derive(mw2 *m)
{
    mw2_u8 msg[8 + 8 + 16 + 16 + 4], prk[32], k[32], c;
    mw2_u32 n = 0;
    if (!m->dev_sess) { zero(m->kc, 32); zero(m->ks, 32); return; }
    copy(msg, "ms2 prk", 7); n = 7;
    copy(msg + n, m->dev, 8); n += 8;
    copy(msg + n, m->cn, 16); n += 16;
    copy(msg + n, m->sn, 16); n += 16;
    put32(msg + n, m->sid); n += 4;
    skey(m, k);
    mw2_hmac(k, msg, n, prk);
    c = 'c'; mw2_hmac(prk, &c, 1, m->kc);
    c = 's'; mw2_hmac(prk, &c, 1, m->ks);
    zero(k, 32); zero(prk, 32);
}

int mw2_hs2(mw2 *m, mw2_u32 now, const mw2_u8 *p, mw2_u32 n)
{
    mw2_u8 text[7 + 1 + 8 + 16 + 4 + 16 + 2];
    mw2_u32 sid, ttl, tl;
    int i;
    if (m->state != MW2_HS_SENT || n < 4 || p[0] != 0x6D || p[1] != 2 || p[2] != (mw2_u8)(MW2_OP_HS | 0x80)) return 0;
    if (p[3] != 0) return n == 4 ? -(int)p[3] : 0;
    if (n != MW2_HS2_LEN) return 0;
    sid = get32(p + 4); ttl = ((mw2_u32)p[4 + 4 + 16] << 8) | p[4 + 4 + 16 + 1];
    if (sid == 0 || ttl < 30) return 0;
    tl = hs2_text(m, text, sid, p + 8, ttl);
    for (i = 0; i < m->npin; i++) {
        if (p[8 + 16 + 2] != m->pin[i][0]) continue;              /* key hint */
        if (mw2_ed25519_verify(m->pin[i], text, tl, p + 27)) break;
    }
    if (i >= m->npin) { m->st_bad++; return 0; }
    m->pin_used = i;
    m->sid = sid; copy(m->sn, p + 8, 16); m->ttl_ms = ttl * 1000ul; m->ctr = 0; m->last_ok = now;
    derive(m);
    mw2_seed(m, p + 8, 16);
    m->state = MW2_READY;
    return 1;
}

mw2_u32 mw2_seal(mw2 *m, mw2_u8 *p, mw2_u8 op, mw2_u32 plen)
{
    mw2_u32 n = MW2_HDR + plen;
    mw2_u8 t[32];
    m->ctr++;
    p[0] = 0x4D; p[1] = 2; p[2] = op; p[3] = 0;
    put32(p + 4, m->sid); put32(p + 8, m->ctr);
    if (m->dev_sess) {
        mw2_hmac(m->kc, p, n, t);
        copy(p + n, t, MW2_MAC);
        n += MW2_MAC;
    }
    return n;
}

int mw2_open(mw2 *m, mw2_u32 now, const mw2_u8 *p, mw2_u32 n, mw2_u8 op, mw2_u32 ctr, mw2_u8 *status, mw2_u32 *blen)
{
    mw2_u8 t[32];
    if (m->state != MW2_READY || n < MW2_HDR || p[0] != 0x6D || p[1] != 2 || p[2] != (mw2_u8)(op | 0x80)) return 0;
    if (get32(p + 4) != m->sid || get32(p + 8) != ctr) return 0;
    if (n == MW2_HDR) return p[3] == MW2_ST_SESSION_UNKNOWN ? 2 : 0;
    if (!m->dev_sess && op == MW2_OP_UPD_FETCHU && p[3] == 0) {         /* the one unsigned reply: public file bytes, authenticated end to end by the caller's whole-file hash */
        *status = 0; *blen = n - MW2_HDR;
        return 3;
    }
    if (m->dev_sess) {
        if (n < MW2_HDR + MW2_MAC) return 0;
        mw2_hmac(m->ks, p, n - MW2_MAC, t);
        if (!mw2_ct_equal(t, p + n - MW2_MAC, MW2_MAC)) { m->st_bad++; return 0; }
        *blen = n - MW2_HDR - MW2_MAC;
    } else {
        mw2_u8 msg[7 + 32 + MW2_MAX_PACKET];
        mw2_u32 k;
        if (n < MW2_HDR + MW2_SIG) return 0;
        k = n - MW2_SIG;
        if (7 + 32 + k > sizeof msg) return 0;
        copy(msg, "ms2-rsp", 7); copy(msg + 7, m->cn, 16); copy(msg + 23, m->sn, 16); copy(msg + 39, p, k);
        if (!mw2_ed25519_verify(m->pin[m->pin_used], msg, 39 + k, p + k)) { m->st_bad++; return 0; }
        *blen = k - MW2_HDR;
    }
    *status = p[3];
    m->last_ok = now;
    return 1;
}
