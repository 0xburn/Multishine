/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sgx: see sgx.h.  Strict C89. */
#include "sgx.h"

static const char ALPHA[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static const char INFO[10] = { 'm', 's', '-', 'c', 'r', 'e', 'd', '-', 'v', '1' };
static const char FPINFO[13] = { 'm', 's', '-', 'c', 'r', 'e', 'd', '-', 'f', 'p', '-', 'v', '1' };

void sgx_pub(sdc_u8 pub[32], const sdc_u8 priv[32])
{
    sdc_u8 base[32];
    unsigned i;
    for (i = 0; i < 32u; i++) base[i] = 0;
    base[0] = 9;
    sdc_x25519(pub, priv, base);
}

int sgx_code_ok(const char *code)
{
    unsigned i, j;
    for (i = 0; i < SGX_CODE_LEN; i++) {
        if (!code[i]) return 0;
        for (j = 0; ALPHA[j] && ALPHA[j] != code[i]; j++) {}
        if (!ALPHA[j]) return 0;
    }
    return code[SGX_CODE_LEN] == 0;
}

void sgx_fingerprint(const sdc_u8 pub[32], char out[SGX_FP_LEN])
{
    sdc_u8 m[13 + 32], h[32];
    sdc_u32 n, a, b;
    unsigned i;
    for (i = 0; i < 13u; i++) m[i] = (sdc_u8)FPINFO[i];
    for (i = 0; i < 32u; i++) m[13 + i] = pub[i];
    sdc_sha256(m, sizeof m, h);
    n = ((sdc_u32)h[0] << 24 | (sdc_u32)h[1] << 16 | (sdc_u32)h[2] << 8 | h[3]) % 100000000u;
    a = n / 10000u; b = n % 10000u;
    for (i = 0; i < 4u; i++) { out[3u - i] = (char)('0' + a % 10u); a /= 10u; out[8u - i] = (char)('0' + b % 10u); b /= 10u; }
    out[4] = ' '; out[9] = 0;
}

unsigned sgx_url(char *out, unsigned cap, const char *host, const char *code, const sdc_u8 pub[32])
{
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    unsigned n = 0, i;
    const char *s;
    if (!sgx_code_ok(code)) return 0;
    for (i = 0; host[i]; i++) {}
    if (i == 0 || i > 40u || cap < SGX_URL_MAX) return 0;
    for (s = "https://"; *s; s++) out[n++] = *s;
    for (s = host; *s; s++) out[n++] = *s;
    for (s = "/send?c="; *s; s++) out[n++] = *s;
    for (s = code; *s; s++) out[n++] = *s;
    out[n++] = '#'; out[n++] = 'k'; out[n++] = '=';
    for (i = 0; i + 2u < 32u; i += 3u) {
        sdc_u32 v = (sdc_u32)pub[i] << 16 | (sdc_u32)pub[i + 1] << 8 | pub[i + 2];
        out[n++] = B64[v >> 18 & 63u]; out[n++] = B64[v >> 12 & 63u]; out[n++] = B64[v >> 6 & 63u]; out[n++] = B64[v & 63u];
    }
    {
        sdc_u32 v = (sdc_u32)pub[30] << 16 | (sdc_u32)pub[31] << 8;
        out[n++] = B64[v >> 18 & 63u]; out[n++] = B64[v >> 12 & 63u]; out[n++] = B64[v >> 6 & 63u];
    }
    out[n] = 0;
    return n;
}

int sgx_open(const char *code, const sdc_u8 priv[32], const sdc_u8 pub[32], sdc_u8 *blob, sdc_u32 n, sdc_u32 *off)
{
    sdc_u8 shared[32], prk[32], key[32], zero[32], nonce[12], tag[16], info[10 + 8 + 32 + 32 + 1], aad[18];
    sdc_u32 m, i, acc;
    int ok;
    if (n < SGX_BLOB_HDR + 1u || n > SGX_BLOB_MAX || blob[0] != 1u || !sgx_code_ok(code)) return SGX_E_FORMAT;
    m = n - SGX_BLOB_HDR;
    sdc_x25519(shared, priv, blob + 1);
    for (acc = 0, i = 0; i < 32u; i++) acc |= shared[i];
    if (!acc) { sdc_wipe(shared, sizeof shared); return SGX_E_AUTH; }
    for (i = 0; i < 32u; i++) zero[i] = 0;
    for (i = 0; i < 12u; i++) nonce[i] = 0;
    sdc_hmac(zero, 32, shared, 32, prk);
    for (i = 0; i < 10u; i++) { info[i] = (sdc_u8)INFO[i]; aad[i] = (sdc_u8)INFO[i]; }
    for (i = 0; i < 8u; i++) { info[10 + i] = (sdc_u8)code[i]; aad[10 + i] = (sdc_u8)code[i]; }
    for (i = 0; i < 32u; i++) { info[18 + i] = pub[i]; info[50 + i] = blob[1 + i]; }
    info[82] = 1;
    sdc_hmac(prk, 32, info, sizeof info, key);
    for (i = 0; i < 16u; i++) tag[i] = blob[33 + i];
    for (i = 0; i < m; i++) blob[33 + i] = blob[49 + i];         /* ciphertext first, then the tag (the layout sdc_aead_open takes) */
    for (i = 0; i < 16u; i++) blob[33 + m + i] = tag[i];
    ok = sdc_aead_open(key, nonce, aad, sizeof aad, blob + 33, m, blob + 33);
    sdc_wipe(shared, sizeof shared); sdc_wipe(prk, sizeof prk); sdc_wipe(key, sizeof key);
    if (!ok) return SGX_E_AUTH;
    *off = 33;
    return (int)m;
}

/* ---- the plaintext: {"uid":"..","playKey":"..","displayName":"..","connectCode":".."}  exactly these four, each once, any order, no spaces ---- */
struct jr { const sdc_u8 *p; sdc_u32 n, i; };
static int jch(struct jr *r, sdc_u8 c) { if (r->i < r->n && r->p[r->i] == c) { r->i++; return 1; } return 0; }
static int hex4(struct jr *r, sdc_u32 *v)
{
    unsigned k;
    *v = 0;
    for (k = 0; k < 4u; k++) {
        sdc_u8 c;
        if (r->i >= r->n) return 0;
        c = r->p[r->i++];
        *v <<= 4;
        if (c >= '0' && c <= '9') *v |= (sdc_u32)(c - '0');
        else if (c >= 'a' && c <= 'f') *v |= (sdc_u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') *v |= (sdc_u32)(c - 'A' + 10);
        else return 0;
    }
    return 1;
}
/* one JSON string into d (cap bytes with the NUL): control characters, bad UTF-8, surrogate escapes and over-long values are refused */
static int jstr(struct jr *r, char *d, unsigned cap)
{
    unsigned n = 0, more = 0;
    if (!jch(r, '"')) return 0;
    for (;;) {
        sdc_u8 c;
        sdc_u32 u;
        if (r->i >= r->n) return 0;
        c = r->p[r->i++];
        if (c == '"' && !more) break;
        if (more) { if ((c & 0xC0u) != 0x80u) return 0; more--; if (n + 1u >= cap) return 0; d[n++] = (char)c; continue; }
        if (c < 0x20u || c == 0x7Fu) return 0;
        if (c == '\\') {
            if (r->i >= r->n) return 0;
            c = r->p[r->i++];
            if (c == '"' || c == '\\' || c == '/') { if (n + 1u >= cap) return 0; d[n++] = (char)c; continue; }
            if (c != 'u' || !hex4(r, &u) || u < 0x20u || u == 0x7Fu || (u >= 0xD800u && u < 0xE000u)) return 0;
            if (u < 0x80u) { if (n + 1u >= cap) return 0; d[n++] = (char)u; }
            else if (u < 0x800u) { if (n + 2u >= cap) return 0; d[n++] = (char)(0xC0u | u >> 6); d[n++] = (char)(0x80u | (u & 63u)); }
            else { if (n + 3u >= cap) return 0; d[n++] = (char)(0xE0u | u >> 12); d[n++] = (char)(0x80u | (u >> 6 & 63u)); d[n++] = (char)(0x80u | (u & 63u)); }
            continue;
        }
        if (c >= 0x80u) {
            if (c < 0xC2u || c > 0xF4u) return 0;
            more = c >= 0xF0u ? 3u : c >= 0xE0u ? 2u : 1u;
        }
        if (n + 1u >= cap) return 0;
        d[n++] = (char)c;
    }
    if (more || n == 0u) return 0;
    d[n] = 0;
    return 1;
}
static int cc_ok(const char *s)      /* ^[^\s#]{1,8}#[0-9]{1,4}$ */
{
    unsigned chars = 0, i = 0, d = 0;
    while (s[i] && s[i] != '#') {
        unsigned char c = (unsigned char)s[i];
        if (c <= ' ') return 0;
        if ((c & 0xC0u) != 0x80u) chars++;
        i++;
    }
    if (s[i] != '#' || chars < 1u || chars > 8u) return 0;
    i++;
    while (s[i] >= '0' && s[i] <= '9') { d++; i++; }
    return s[i] == 0 && d >= 1u && d <= 4u;
}
int sgx_parse(const sdc_u8 *plain, sdc_u32 n, struct sgx_ident *o)
{
    struct jr r;
    unsigned seen = 0, k, j;
    static const char *const NAME[4] = { "uid", "playKey", "displayName", "connectCode" };
    char *dst[4];
    unsigned cap[4];
    dst[0] = o->uid; cap[0] = SGX_UID_LEN; dst[1] = o->key; cap[1] = SGX_KEY_LEN; dst[2] = o->name; cap[2] = SGX_NAME_LEN; dst[3] = o->code; cap[3] = SGX_CC_LEN;
    sdc_wipe(o, sizeof *o);
    r.p = plain; r.n = n; r.i = 0;
    if (!jch(&r, '{')) goto bad;
    for (j = 0; j < 4u; j++) {
        if (j && !jch(&r, ',')) goto bad;
        if (!jch(&r, '"')) goto bad;
        for (k = 0; k < 4u; k++) {
            unsigned l = 0;
            while (NAME[k][l] && r.i + l < r.n && r.p[r.i + l] == (sdc_u8)NAME[k][l]) l++;
            if (!NAME[k][l] && r.i + l + 1u < r.n && r.p[r.i + l] == '"') { r.i += l + 1u; break; }
        }
        if (k == 4u || (seen >> k & 1u) || !jch(&r, ':') || !jstr(&r, dst[k], cap[k])) goto bad;
        seen |= 1u << k;
    }
    if (!jch(&r, '}') || r.i != r.n || seen != 15u || !cc_ok(o->code)) goto bad;
    return 0;
bad:
    sdc_wipe(o, sizeof *o);
    return SGX_E_JSON;
}

sdc_u32 sgx_crc32(const sdc_u8 *p, sdc_u32 n, sdc_u32 crc)
{
    sdc_u32 i, b;
    crc = ~crc;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (b = 0; b < 8u; b++) crc = crc & 1u ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

static void put_field(sdc_u8 *d, const char *s, unsigned cap)
{
    unsigned i = 0;
    while (s[i] && i < cap) { d[i] = (sdc_u8)s[i]; i++; }
    while (i < cap) d[i++] = 0;
}
void sgx_pack(const struct sgx_ident *id, sdc_u8 out[SGX_REC_LEN])
{
    sdc_u32 c;
    out[0] = 'S'; out[1] = 'D'; out[2] = 'I'; out[3] = 'D'; out[4] = 1; out[5] = 0; out[6] = 0; out[7] = 0;
    put_field(out + 8, id->uid, SGX_UID_LEN);
    put_field(out + 72, id->key, SGX_KEY_LEN);
    put_field(out + 200, id->code, SGX_CC_LEN);
    put_field(out + 232, id->name, SGX_NAME_LEN);
    c = sgx_crc32(out, SGX_REC_LEN - 4u, 0);
    out[296] = (sdc_u8)(c >> 24); out[297] = (sdc_u8)(c >> 16); out[298] = (sdc_u8)(c >> 8); out[299] = (sdc_u8)c;
}
static int get_field(char *d, const sdc_u8 *s, unsigned cap)
{
    unsigned i = 0;
    while (i < cap && s[i]) { d[i] = (char)s[i]; i++; }
    if (i == cap) return -1;
    d[i] = 0;
    for (; i < cap; i++) if (s[i]) return -1;
    return 0;
}
int sgx_unpack(const sdc_u8 *p, sdc_u32 n, struct sgx_ident *id)
{
    sdc_u32 c;
    if (n < SGX_REC_LEN || p[0] != 'S' || p[1] != 'D' || p[2] != 'I' || p[3] != 'D' || p[4] != 1) return -1;
    c = (sdc_u32)p[296] << 24 | (sdc_u32)p[297] << 16 | (sdc_u32)p[298] << 8 | p[299];
    if (c != sgx_crc32(p, SGX_REC_LEN - 4u, 0)) return -1;
    if (get_field(id->uid, p + 8, SGX_UID_LEN) || get_field(id->key, p + 72, SGX_KEY_LEN) || get_field(id->code, p + 200, SGX_CC_LEN) || get_field(id->name, p + 232, SGX_NAME_LEN)) return -1;
    return 0;
}

void sgx_show(char *dst, unsigned cap, const char *s)
{
    unsigned n = 0;
    while (*s && n + 1u < cap) {
        unsigned char c = (unsigned char)*s++;
        dst[n++] = (char)((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '#' || c == '-' || c == '.' || c == ',' || c == ':' || c == '\'' || c == ' ' || c == '@' ? c : '.');
    }
    dst[n] = 0;
}
