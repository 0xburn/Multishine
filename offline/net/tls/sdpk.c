/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sdpk: RSA and ECDSA signature verification on a generic Montgomery multiplier, plus SHA-384/512.  See sdpk.h.  Strict C89.
   Written from RFC 8017 (RSASSA-PKCS1-v1_5, RSASSA-PSS), FIPS 186-4 (ECDSA), FIPS 180-4 (SHA-512), SEC 1 (point format); the curve
   and hash constants come from gen_curves.py.  Verification only: public inputs, variable time. */
#include "sdpk.h"
#include "sdcrypto.h"

__extension__ typedef unsigned long long sdpk_u64;
#include "sdpk_curves.inc"

#ifdef SDPK_COUNT
unsigned long sdpk_count_mm[130];
#endif

#define KMAX 128
#define STRIDE 132

/* ---------------------------------------------------------------- limb helpers */
struct mod { sdpk_u32 k, n0; sdpk_u32 *m, *r2; };

static void l_zero(sdpk_u32 *a, sdpk_u32 k) { sdpk_u32 i; for (i = 0; i < k; i++) a[i] = 0; }
static void l_copy(sdpk_u32 *d, const sdpk_u32 *s, sdpk_u32 k) { sdpk_u32 i; for (i = 0; i < k; i++) d[i] = s[i]; }
static int l_cmp(const sdpk_u32 *a, const sdpk_u32 *b, sdpk_u32 k)
{
    sdpk_u32 i;
    for (i = k; i > 0; i--) { if (a[i - 1] != b[i - 1]) return a[i - 1] > b[i - 1] ? 1 : -1; }
    return 0;
}
static int l_is_zero(const sdpk_u32 *a, sdpk_u32 k) { sdpk_u32 i; for (i = 0; i < k; i++) if (a[i]) return 0; return 1; }
static sdpk_u32 l_add(sdpk_u32 *r, const sdpk_u32 *a, const sdpk_u32 *b, sdpk_u32 k)
{
    sdpk_u64 c = 0; sdpk_u32 i;
    for (i = 0; i < k; i++) { c += (sdpk_u64)a[i] + b[i]; r[i] = (sdpk_u32)c; c >>= 32; }
    return (sdpk_u32)c;
}
static sdpk_u32 l_sub(sdpk_u32 *r, const sdpk_u32 *a, const sdpk_u32 *b, sdpk_u32 k)
{
    sdpk_u32 bw = 0, i;
    for (i = 0; i < k; i++) {
        sdpk_u32 x = a[i], y = b[i], d = x - y - bw;
        bw = (x < y || (x == y && bw)) ? 1u : 0u;
        r[i] = d;
    }
    return bw;
}
/* big-endian bytes (len <= 4k) to k little-endian limbs */
static void be_to_l(sdpk_u32 *l, sdpk_u32 k, const sdpk_u8 *b, sdpk_u32 len)
{
    sdpk_u32 i;
    l_zero(l, k);
    for (i = 0; i < len; i++) l[(len - 1 - i) >> 2] |= (sdpk_u32)b[i] << (8 * ((len - 1 - i) & 3));
}
static void l_to_be(sdpk_u8 *b, sdpk_u32 len, const sdpk_u32 *l)
{
    sdpk_u32 i;
    for (i = 0; i < len; i++) b[len - 1 - i] = (sdpk_u8)(l[i >> 2] >> (8 * (i & 3)));
}

/* ---------------------------------------------------------------- Montgomery arithmetic */
/* r = a*b/R mod m, a,b < m.  t: k+2 scratch words.  r may alias a or b. */
static void mm(const struct mod *md, sdpk_u32 *r, const sdpk_u32 *a, const sdpk_u32 *b, sdpk_u32 *t)
{
    sdpk_u32 k = md->k, i, j, c, mq;
    const sdpk_u32 *m = md->m;
    sdpk_u64 s;
#ifdef SDPK_COUNT
    sdpk_count_mm[k]++;
#endif
    for (j = 0; j < k + 2; j++) t[j] = 0;
    for (i = 0; i < k; i++) {
        sdpk_u32 bi = b[i];
        c = 0;
        for (j = 0; j < k; j++) { s = (sdpk_u64)t[j] + (sdpk_u64)a[j] * bi + c; t[j] = (sdpk_u32)s; c = (sdpk_u32)(s >> 32); }
        s = (sdpk_u64)t[k] + c; t[k] = (sdpk_u32)s; t[k + 1] = (sdpk_u32)(s >> 32);
        mq = t[0] * md->n0;
        s = (sdpk_u64)t[0] + (sdpk_u64)mq * m[0]; c = (sdpk_u32)(s >> 32);
        for (j = 1; j < k; j++) { s = (sdpk_u64)t[j] + (sdpk_u64)mq * m[j] + c; t[j - 1] = (sdpk_u32)s; c = (sdpk_u32)(s >> 32); }
        s = (sdpk_u64)t[k] + c; t[k - 1] = (sdpk_u32)s; t[k] = t[k + 1] + (sdpk_u32)(s >> 32);
    }
    if (t[k] || l_cmp(t, m, k) >= 0) l_sub(r, t, m, k); else l_copy(r, t, k);
}
static void m_add(const struct mod *md, sdpk_u32 *r, const sdpk_u32 *a, const sdpk_u32 *b)
{
    sdpk_u32 c = l_add(r, a, b, md->k);
    if (c || l_cmp(r, md->m, md->k) >= 0) l_sub(r, r, md->m, md->k);
}
static void m_sub(const struct mod *md, sdpk_u32 *r, const sdpk_u32 *a, const sdpk_u32 *b)
{
    if (l_sub(r, a, b, md->k)) l_add(r, r, md->m, md->k);
}

/* mod->m must be odd.  Returns 0 (unusable) when it is not.  r2 = R^2 mod m with R = 2^(32k).  b32, t: k and k+2 scratch words (clobbered).
   No long division and no 64k-step doubling loop: 2^(bits-1) is doubled up to R mod m (at most 32 steps), another 32 doublings give 2^32 in
   Montgomery form, and R^2 = mont(2^(32k)) comes from log2(k) Montgomery squarings. */
static void m_dbl(const struct mod *md, sdpk_u32 *a, sdpk_u32 times)
{
    while (times--) m_add(md, a, a, a);
}
static int mod_init(struct mod *md, sdpk_u32 k, sdpk_u32 *m, sdpk_u32 *r2, sdpk_u32 *b32, sdpk_u32 *t)
{
    sdpk_u32 x, inv, i, bits, j;
    int top;
    md->k = k; md->m = m; md->r2 = r2;
    if (!(m[0] & 1) || m[k - 1] == 0) return 0;
    x = m[0]; inv = x;                                  /* x*x = 1 mod 8: 3 bits right, each step doubles them */
    for (i = 0; i < 4; i++) inv *= 2u - x * inv;
    md->n0 = 0u - inv;
    bits = 32 * k;
    for (j = m[k - 1]; !(j & 0x80000000u); j <<= 1) bits--;
    l_zero(b32, k);
    b32[(bits - 1) >> 5] = 1u << ((bits - 1) & 31);
    m_dbl(md, b32, 32 * k - bits + 1);                  /* b32 = R mod m = Montgomery form of 1 */
    m_dbl(md, b32, 32);                                 /* b32 = Montgomery form of 2^32 */
    l_copy(r2, b32, k);
    for (top = 31; top > 0 && !((k >> top) & 1); top--) { }
    for (top--; top >= 0; top--) {                      /* r2 = Montgomery form of 2^(32*k) = R^2 mod m */
        mm(md, r2, r2, r2, t);
        if ((k >> top) & 1) mm(md, r2, r2, b32, t);
    }
    return 1;
}

/* ---------------------------------------------------------------- hashes (SHA-256 from sdcrypto, SHA-384/512 here) */
static sdpk_u64 rd64(const sdpk_u8 *p) { sdpk_u64 v = 0; int i; for (i = 0; i < 8; i++) v = (v << 8) | p[i]; return v; }
#define ROR(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
static void sha512_block(sdpk_u64 h[8], const sdpk_u8 *p)
{
    sdpk_u64 w[80], a[8], t1, t2;
    int i, j;
    for (i = 0; i < 16; i++) w[i] = rd64(p + 8 * i);
    for (i = 16; i < 80; i++) {
        sdpk_u64 s0 = ROR(w[i - 15], 1) ^ ROR(w[i - 15], 8) ^ (w[i - 15] >> 7);
        sdpk_u64 s1 = ROR(w[i - 2], 19) ^ ROR(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (j = 0; j < 8; j++) a[j] = h[j];
    for (i = 0; i < 80; i++) {
        sdpk_u64 k = ((sdpk_u64)SHA512_K[2 * i] << 32) | SHA512_K[2 * i + 1];
        t1 = a[7] + (ROR(a[4], 14) ^ ROR(a[4], 18) ^ ROR(a[4], 41)) + ((a[4] & a[5]) ^ (~a[4] & a[6])) + k + w[i];
        t2 = (ROR(a[0], 28) ^ ROR(a[0], 34) ^ ROR(a[0], 39)) + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
        for (j = 7; j > 0; j--) a[j] = a[j - 1];
        a[4] += t1;
        a[0] = t1 + t2;
    }
    for (j = 0; j < 8; j++) h[j] += a[j];
}
static void sha512_blk(struct sdpk_sha512 *c, const sdpk_u8 *p)
{
    sdpk_u64 h[8]; int j;
    for (j = 0; j < 8; j++) h[j] = ((sdpk_u64)c->h[2 * j] << 32) | c->h[2 * j + 1];
    sha512_block(h, p);
    for (j = 0; j < 8; j++) { c->h[2 * j] = (sdpk_u32)(h[j] >> 32); c->h[2 * j + 1] = (sdpk_u32)h[j]; }
}
static void sha_init(struct sdpk_sha512 *c, const sdpk_u32 *iv, sdpk_u32 outlen)
{
    sdpk_u32 i;
    for (i = 0; i < 16; i++) c->h[i] = iv[i];
    c->n = 0; c->fill = 0; c->outlen = outlen;
}
void sdpk_sha384_init(struct sdpk_sha512 *c) { sha_init(c, SHA384_H, 48); }
void sdpk_sha512_init(struct sdpk_sha512 *c) { sha_init(c, SHA512_H, 64); }
void sdpk_sha512_update(struct sdpk_sha512 *c, const sdpk_u8 *p, sdpk_u32 n)
{
    while (n) {
        sdpk_u32 t = 128 - c->fill, i;
        if (t > n) t = n;
        for (i = 0; i < t; i++) c->buf[c->fill + i] = p[i];
        c->fill += t; p += t; n -= t; c->n += t;
        if (c->fill == 128) { sha512_blk(c, c->buf); c->fill = 0; }
    }
}
void sdpk_sha512_final(struct sdpk_sha512 *c, sdpk_u8 *out)
{
    sdpk_u32 i, bits_hi = c->n >> 29, bits_lo = c->n << 3;
    c->buf[c->fill++] = 0x80;
    if (c->fill > 112) { while (c->fill < 128) c->buf[c->fill++] = 0; sha512_blk(c, c->buf); c->fill = 0; }
    while (c->fill < 120) c->buf[c->fill++] = 0;
    for (i = 0; i < 4; i++) c->buf[120 + i] = (sdpk_u8)(bits_hi >> (24 - 8 * i));
    for (i = 0; i < 4; i++) c->buf[124 + i] = (sdpk_u8)(bits_lo >> (24 - 8 * i));
    sha512_blk(c, c->buf);
    for (i = 0; i < c->outlen; i++) out[i] = (sdpk_u8)(c->h[i >> 2] >> (24 - 8 * (i & 3)));
}
void sdpk_sha384(const sdpk_u8 *p, sdpk_u32 n, sdpk_u8 out[48])
{
    struct sdpk_sha512 c;
    sdpk_sha384_init(&c); sdpk_sha512_update(&c, p, n); sdpk_sha512_final(&c, out);
}

static sdpk_u32 hlen_of(int hash) { return hash == SDPK_HASH_SHA256 ? 32u : hash == SDPK_HASH_SHA384 ? 48u : 0u; }
/* hash of up to three parts */
static void hash3(int hash, sdpk_u8 *out, const sdpk_u8 *a, sdpk_u32 al, const sdpk_u8 *b, sdpk_u32 bl, const sdpk_u8 *c, sdpk_u32 cl)
{
    if (hash == SDPK_HASH_SHA256) {
        struct sdc_sha s;
        sdc_sha_init(&s); sdc_sha_update(&s, a, al); sdc_sha_update(&s, b, bl); sdc_sha_update(&s, c, cl); sdc_sha_final(&s, out);
    } else {
        struct sdpk_sha512 s;
        sdpk_sha384_init(&s); sdpk_sha512_update(&s, a, al); sdpk_sha512_update(&s, b, bl); sdpk_sha512_update(&s, c, cl); sdpk_sha512_final(&s, out);
    }
}

/* ---------------------------------------------------------------- RSA */
/* em = sig^e mod n as nlen big-endian bytes.  Returns 0 on bad input. */
static int rsa_public(struct sdpk_ws *ws, const sdpk_u8 *n, sdpk_u32 nlen, const sdpk_u8 *e, sdpk_u32 elen, const sdpk_u8 *sig, sdpk_u32 siglen, sdpk_u8 *em)
{
    struct mod md;
    sdpk_u32 k, *nl = ws->w, *r2 = ws->w + STRIDE, *x = ws->w + 2 * STRIDE, *acc = ws->w + 3 * STRIDE, *t = ws->w + 4 * STRIDE, *one = ws->w + 5 * STRIDE;
    sdpk_u32 ev = 0, i;
    int bit;
    if (nlen < 128 || nlen > SDPK_RSA_MAX_BYTES || n[0] == 0 || !(n[nlen - 1] & 1)) return 0;
    if (siglen != nlen) return 0;
    if (elen == 0 || elen > 4 || e[0] == 0) return 0;
    for (i = 0; i < elen; i++) ev = (ev << 8) | e[i];
    if (ev < 3 || !(ev & 1)) return 0;
    k = (nlen + 3) / 4;
    be_to_l(nl, k, n, nlen);
    if (!mod_init(&md, k, nl, r2, x, t)) return 0;
    be_to_l(x, k, sig, siglen);
    if (l_cmp(x, nl, k) >= 0) return 0;
    mm(&md, x, x, r2, t);                               /* x = s*R */
    l_copy(acc, x, k);
    for (bit = 31; bit >= 0 && !((ev >> bit) & 1); bit--) { }      /* top set bit; the loop below starts under it */
    for (bit--; bit >= 0; bit--) {
        mm(&md, acc, acc, acc, t);
        if ((ev >> bit) & 1) mm(&md, acc, acc, x, t);
    }
    l_zero(one, k); one[0] = 1;
    mm(&md, acc, acc, one, t);
    l_to_be(em, nlen, acc);
    return 1;
}

int sdpk_rsa_pkcs1_verify(struct sdpk_ws *ws, const sdpk_u8 *n, sdpk_u32 nlen, const sdpk_u8 *e, sdpk_u32 elen, int hash, const sdpk_u8 *h,
                          const sdpk_u8 *sig, sdpk_u32 siglen)
{
    static const sdpk_u8 DI256[19] = { 0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20 };
    static const sdpk_u8 DI384[19] = { 0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30 };
    sdpk_u8 em[SDPK_RSA_MAX_BYTES];
    sdpk_u32 hl = hlen_of(hash), tl, ps, i, bad = 0;
    const sdpk_u8 *di = hash == SDPK_HASH_SHA256 ? DI256 : DI384;
    if (!hl || !rsa_public(ws, n, nlen, e, elen, sig, siglen, em)) return 0;
    tl = 19 + hl;
    if (nlen < tl + 11) return 0;
    ps = nlen - tl - 3;
    bad |= em[0]; bad |= em[1] ^ 1u;
    for (i = 0; i < ps; i++) bad |= em[2 + i] ^ 0xffu;
    bad |= em[2 + ps];
    for (i = 0; i < 19; i++) bad |= em[3 + ps + i] ^ di[i];
    for (i = 0; i < hl; i++) bad |= em[3 + ps + 19 + i] ^ h[i];
    return bad == 0;
}

int sdpk_rsa_pss_verify(struct sdpk_ws *ws, const sdpk_u8 *n, sdpk_u32 nlen, const sdpk_u8 *e, sdpk_u32 elen, int hash, const sdpk_u8 *h,
                        const sdpk_u8 *sig, sdpk_u32 siglen)
{
    sdpk_u8 emfull[SDPK_RSA_MAX_BYTES], hp[48], zeros[8];
    const sdpk_u8 *em;
    sdpk_u32 hl = hlen_of(hash), modbits, embits, emlen, dblen, i, off, top, bad = 0, ctr;
    if (!hl || !rsa_public(ws, n, nlen, e, elen, sig, siglen, emfull)) return 0;
    modbits = nlen * 8;
    for (top = n[0]; !(top & 0x80); top <<= 1) modbits--;
    embits = modbits - 1;
    emlen = (embits + 7) / 8;
    em = emfull + (nlen - emlen);                        /* when modbits-1 is a multiple of 8 the leading byte of the result must be zero */
    if (emlen < nlen && emfull[0] != 0) return 0;
    if (emlen < hl + hl + 2) return 0;
    if (em[emlen - 1] != 0xbc) return 0;
    dblen = emlen - hl - 1;
    if (8 * emlen != embits && (em[0] >> (8 - (8 * emlen - embits)))) return 0;     /* the unused top bits of maskedDB must be zero */
    /* MGF1 over H (em + dblen), XOR into a copy of maskedDB */
    {
        sdpk_u8 dbuf[SDPK_RSA_MAX_BYTES], dg[48], cb[4];
        const sdpk_u8 *seed = em + dblen;
        for (i = 0; i < dblen; i++) dbuf[i] = em[i];
        for (ctr = 0, off = 0; off < dblen; ctr++) {
            cb[0] = (sdpk_u8)(ctr >> 24); cb[1] = (sdpk_u8)(ctr >> 16); cb[2] = (sdpk_u8)(ctr >> 8); cb[3] = (sdpk_u8)ctr;
            hash3(hash, dg, seed, hl, cb, 4, 0, 0);
            for (i = 0; i < hl && off + i < dblen; i++) dbuf[off + i] ^= dg[i];
            off += hl;
        }
        if (8 * emlen != embits) dbuf[0] &= (sdpk_u8)(0xffu >> (8 * emlen - embits));
        for (i = 0; i < dblen - hl - 1; i++) bad |= dbuf[i];
        bad |= dbuf[dblen - hl - 1] ^ 1u;
        for (i = 0; i < 8; i++) zeros[i] = 0;
        hash3(hash, hp, zeros, 8, h, hl, dbuf + dblen - hl, hl);
        for (i = 0; i < hl; i++) bad |= hp[i] ^ seed[i];
    }
    return bad == 0;
}

/* ---------------------------------------------------------------- ECDSA */
struct curve {
    sdpk_u32 k, sz;
    const sdpk_u8 *p, *n, *b, *gx, *gy;
};
static const struct curve C256 = { 8, 32, P256_P, P256_N, P256_B, P256_GX, P256_GY };
static const struct curve C384 = { 12, 48, P384_P, P384_N, P384_B, P384_GX, P384_GY };

struct pt { sdpk_u32 x[12], y[12], z[12]; int inf; };

/* Jacobian doubling for a = -3 (dbl-2001-b) */
static void pt_dbl(const struct mod *f, struct pt *r, const struct pt *p, sdpk_u32 *t)
{
    sdpk_u32 k = f->k, delta[12], gamma[12], beta[12], alpha[12], u[12], v[12];
    if (p->inf || l_is_zero(p->y, k)) { r->inf = 1; return; }
    mm(f, delta, p->z, p->z, t);
    mm(f, gamma, p->y, p->y, t);
    mm(f, beta, p->x, gamma, t);
    m_sub(f, u, p->x, delta);
    m_add(f, v, p->x, delta);
    mm(f, alpha, u, v, t);
    m_add(f, u, alpha, alpha); m_add(f, alpha, u, alpha);     /* alpha = 3*(x-delta)(x+delta) */
    m_add(f, u, p->y, p->z);
    mm(f, u, u, u, t);
    m_sub(f, u, u, gamma); m_sub(f, u, u, delta);             /* new z = (y+z)^2 - gamma - delta */
    mm(f, v, alpha, alpha, t);
    m_add(f, delta, beta, beta); m_add(f, delta, delta, delta); m_add(f, delta, delta, delta);   /* 8*beta */
    m_sub(f, v, v, delta);                                    /* new x */
    m_add(f, delta, beta, beta); m_add(f, delta, delta, delta);                                  /* 4*beta */
    m_sub(f, delta, delta, v);
    mm(f, delta, alpha, delta, t);
    mm(f, gamma, gamma, gamma, t);
    m_add(f, gamma, gamma, gamma); m_add(f, gamma, gamma, gamma); m_add(f, gamma, gamma, gamma);   /* 8*gamma^2 */
    m_sub(f, delta, delta, gamma);                            /* new y */
    l_copy(r->x, v, k); l_copy(r->y, delta, k); l_copy(r->z, u, k); r->inf = 0;
}

/* general Jacobian addition (add-2007-bl shape), handling infinity, equal and opposite points */
static void pt_add(const struct mod *f, struct pt *r, const struct pt *p, const struct pt *q, sdpk_u32 *t)
{
    sdpk_u32 k = f->k, z1z1[12], z2z2[12], u1[12], u2[12], s1[12], s2[12], h[12], rr[12], hh[12], hhh[12], v[12], x3[12], y3[12], z3[12];
    if (p->inf) { *r = *q; return; }
    if (q->inf) { *r = *p; return; }
    mm(f, z1z1, p->z, p->z, t);
    mm(f, z2z2, q->z, q->z, t);
    mm(f, u1, p->x, z2z2, t);
    mm(f, u2, q->x, z1z1, t);
    mm(f, s1, q->z, z2z2, t); mm(f, s1, s1, p->y, t);
    mm(f, s2, p->z, z1z1, t); mm(f, s2, s2, q->y, t);
    if (l_cmp(u1, u2, k) == 0) {
        if (l_cmp(s1, s2, k) == 0) { pt_dbl(f, r, p, t); return; }
        r->inf = 1; return;
    }
    m_sub(f, h, u2, u1);
    m_sub(f, rr, s2, s1);
    mm(f, hh, h, h, t);
    mm(f, hhh, hh, h, t);
    mm(f, v, u1, hh, t);
    mm(f, x3, rr, rr, t);
    m_sub(f, x3, x3, hhh); m_sub(f, x3, x3, v); m_sub(f, x3, x3, v);
    m_sub(f, y3, v, x3);
    mm(f, y3, rr, y3, t);
    mm(f, s1, s1, hhh, t);
    m_sub(f, y3, y3, s1);
    mm(f, z3, p->z, q->z, t);
    mm(f, z3, z3, h, t);
    l_copy(r->x, x3, k); l_copy(r->y, y3, k); l_copy(r->z, z3, k); r->inf = 0;
}

/* DER INTEGER (positive, minimal) to fixed-size big-endian */
static int der_uint(const sdpk_u8 **pp, const sdpk_u8 *end, sdpk_u8 *out, sdpk_u32 sz)
{
    const sdpk_u8 *p = *pp;
    sdpk_u32 len, i;
    if (end - p < 2 || p[0] != 0x02) return 0;
    len = p[1];
    if (len == 0 || len & 0x80 || (sdpk_u32)(end - p) < 2 + len) return 0;
    p += 2;
    if (p[0] & 0x80) return 0;                            /* negative */
    if (len > 1 && p[0] == 0 && !(p[1] & 0x80)) return 0;   /* not minimal */
    if (p[0] == 0) { p++; len--; }
    if (len > sz) return 0;
    for (i = 0; i < sz; i++) out[i] = 0;
    for (i = 0; i < len; i++) out[sz - len + i] = p[i];
    *pp = p + len;
    return 1;
}

int sdpk_ecdsa_verify(struct sdpk_ws *ws, int curve, const sdpk_u8 *pub, sdpk_u32 publen, const sdpk_u8 *h, sdpk_u32 hlen, const sdpk_u8 *sig, sdpk_u32 siglen)
{
    const struct curve *c = curve == SDPK_CURVE_P256 ? &C256 : curve == SDPK_CURVE_P384 ? &C384 : 0;
    struct mod fp, fn;
    struct pt tbl[4], acc;
    sdpk_u32 k, sz, *pl, *p2, *nl, *n2, *t, i;
    sdpk_u32 rl[12], sl[12], el[12], u1[12], u2[12], sinv[12], one[12], tmp[12], px[12], py[12], lhs[12], rhs[12], z2[12];
    sdpk_u8 rb[48], sb[48], eb[48], u1b[48], u2b[48], nm2[48];
    const sdpk_u8 *p, *end;
    int bit, started;

    if (!c || publen != 1 + 2 * c->sz || pub[0] != 4) return 0;
    k = c->k; sz = c->sz;
    pl = ws->w; p2 = ws->w + STRIDE; nl = ws->w + 2 * STRIDE; n2 = ws->w + 3 * STRIDE; t = ws->w + 4 * STRIDE;
    be_to_l(pl, k, c->p, sz); be_to_l(nl, k, c->n, sz);
    if (!mod_init(&fp, k, pl, p2, ws->w + 5 * STRIDE, t) || !mod_init(&fn, k, nl, n2, ws->w + 5 * STRIDE, t)) return 0;

    /* signature: SEQUENCE { INTEGER r, INTEGER s } */
    if (siglen < 8 || sig[0] != 0x30) return 0;
    p = sig + 2; end = sig + siglen;
    if (sig[1] & 0x80) {
        if (sig[1] != 0x81) return 0;
        p = sig + 3;
        if (siglen < 3 || sig[2] < 0x80 || (sdpk_u32)(end - p) != sig[2]) return 0;
    } else if ((sdpk_u32)(end - p) != sig[1]) return 0;
    if (!der_uint(&p, end, rb, sz) || !der_uint(&p, end, sb, sz) || p != end) return 0;
    be_to_l(rl, k, rb, sz); be_to_l(sl, k, sb, sz);
    if (l_is_zero(rl, k) || l_is_zero(sl, k) || l_cmp(rl, nl, k) >= 0 || l_cmp(sl, nl, k) >= 0) return 0;

    /* digest to integer mod n */
    for (i = 0; i < sz; i++) eb[i] = 0;
    if (hlen >= sz) { for (i = 0; i < sz; i++) eb[i] = h[i]; } else { for (i = 0; i < hlen; i++) eb[sz - hlen + i] = h[i]; }
    be_to_l(el, k, eb, sz);
    if (l_cmp(el, nl, k) >= 0) l_sub(el, el, nl, k);

    /* public key on the curve: y^2 = x^3 - 3x + b, coordinates < p */
    be_to_l(px, k, pub + 1, sz); be_to_l(py, k, pub + 1 + sz, sz);
    if (l_cmp(px, pl, k) >= 0 || l_cmp(py, pl, k) >= 0) return 0;
    mm(&fp, px, px, p2, t); mm(&fp, py, py, p2, t);          /* to Montgomery form */
    mm(&fp, lhs, py, py, t);
    mm(&fp, rhs, px, px, t); mm(&fp, rhs, rhs, px, t);
    m_sub(&fp, rhs, rhs, px); m_sub(&fp, rhs, rhs, px); m_sub(&fp, rhs, rhs, px);
    be_to_l(tmp, k, c->b, sz); mm(&fp, tmp, tmp, p2, t);
    m_add(&fp, rhs, rhs, tmp);
    if (l_cmp(lhs, rhs, k) != 0) return 0;

    /* sinv = s^(n-2) mod n; u1 = e*sinv, u2 = r*sinv */
    for (i = 0; i < sz; i++) nm2[i] = c->n[i];
    { sdpk_u32 j = sz; sdpk_u32 sub = 2; while (j > 0 && sub) { sdpk_u32 v = nm2[j - 1]; nm2[j - 1] = (sdpk_u8)(v - sub); sub = v < sub ? 1u : 0u; j--; } }
    l_zero(one, k); one[0] = 1;
    mm(&fn, tmp, sl, n2, t);                                  /* s in Montgomery form */
    for (i = 0, started = 0; i < sz * 8; i++) {
        bit = (nm2[i >> 3] >> (7 - (i & 7))) & 1;
        if (started) mm(&fn, sinv, sinv, sinv, t);
        if (bit) { if (!started) { l_copy(sinv, tmp, k); started = 1; } else mm(&fn, sinv, sinv, tmp, t); }
    }
    mm(&fn, sinv, sinv, one, t);                              /* back to normal form */
    mm(&fn, u1, el, n2, t); mm(&fn, u1, u1, sinv, t);
    mm(&fn, u2, rl, n2, t); mm(&fn, u2, u2, sinv, t);
    l_to_be(u1b, sz, u1); l_to_be(u2b, sz, u2);

    /* R = u1*G + u2*Q by Shamir's trick */
    tbl[0].inf = 1;
    be_to_l(tbl[1].x, k, c->gx, sz); mm(&fp, tbl[1].x, tbl[1].x, p2, t);
    be_to_l(tbl[1].y, k, c->gy, sz); mm(&fp, tbl[1].y, tbl[1].y, p2, t);
    mm(&fp, tbl[1].z, one, p2, t); tbl[1].inf = 0;
    l_copy(tbl[2].x, px, k); l_copy(tbl[2].y, py, k); l_copy(tbl[2].z, tbl[1].z, k); tbl[2].inf = 0;
    pt_add(&fp, &tbl[3], &tbl[1], &tbl[2], t);
    acc.inf = 1;
    for (i = 0; i < sz * 8; i++) {
        sdpk_u32 sel = (sdpk_u32)(((u1b[i >> 3] >> (7 - (i & 7))) & 1) | (((u2b[i >> 3] >> (7 - (i & 7))) & 1) << 1));
        pt_dbl(&fp, &acc, &acc, t);
        if (sel) pt_add(&fp, &acc, &acc, &tbl[sel], t);
    }
    if (acc.inf) return 0;
    /* x(R) mod n == r  <=>  X == r*Z^2 (mod p), also with r+n when that is below p */
    mm(&fp, z2, acc.z, acc.z, t);
    mm(&fp, tmp, rl, p2, t);
    mm(&fp, tmp, tmp, z2, t);
    if (l_cmp(tmp, acc.x, k) == 0) return 1;
    if (l_add(u1, rl, nl, k) == 0 && l_cmp(u1, pl, k) < 0) {
        mm(&fp, tmp, u1, p2, t);
        mm(&fp, tmp, tmp, z2, t);
        if (l_cmp(tmp, acc.x, k) == 0) return 1;
    }
    return 0;
}
