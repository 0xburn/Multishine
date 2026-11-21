/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sdx509: minimal bounded X.509 reader and chain checker.  See sdx509.h.  Strict C89. */
#include "sdx509.h"
#include "sdcrypto.h"

#define T_BOOL 0x01
#define T_INT 0x02
#define T_BITS 0x03
#define T_OCTS 0x04
#define T_NULL 0x05
#define T_OID 0x06
#define T_UTC 0x17
#define T_GEN 0x18
#define T_SEQ 0x30

static const sdx_u8 OID_RSA[9] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01 };
static const sdx_u8 OID_EC[7] = { 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01 };
static const sdx_u8 OID_P256[8] = { 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07 };
static const sdx_u8 OID_P384[5] = { 0x2b, 0x81, 0x04, 0x00, 0x22 };
static const sdx_u8 OID_RSA_SHA256[9] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b };
static const sdx_u8 OID_RSA_SHA384[9] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0c };
static const sdx_u8 OID_ECDSA_SHA256[8] = { 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02 };
static const sdx_u8 OID_ECDSA_SHA384[8] = { 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x03 };
static const sdx_u8 OID_BC[3] = { 0x55, 0x1d, 0x13 };
static const sdx_u8 OID_KU[3] = { 0x55, 0x1d, 0x0f };
static const sdx_u8 OID_EKU[3] = { 0x55, 0x1d, 0x25 };
static const sdx_u8 OID_SAN[3] = { 0x55, 0x1d, 0x11 };
static const sdx_u8 OID_SKI[3] = { 0x55, 0x1d, 0x0e };
static const sdx_u8 OID_AKI[3] = { 0x55, 0x1d, 0x23 };
static const sdx_u8 OID_CRLDP[3] = { 0x55, 0x1d, 0x1f };
static const sdx_u8 OID_POLICIES[3] = { 0x55, 0x1d, 0x20 };
static const sdx_u8 OID_AIA[8] = { 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x01 };
static const sdx_u8 OID_SERVER_AUTH[8] = { 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01 };
static const sdx_u8 OID_ANY_EKU[4] = { 0x55, 0x1d, 0x25, 0x00 };

static int eq(const sdx_u8 *a, sdx_u32 al, const sdx_u8 *b, sdx_u32 bl)
{
    sdx_u32 i;
    if (al != bl) return 0;
    for (i = 0; i < al; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* One TLV at *p (bounded by end).  Strict DER: single-byte tag, definite minimal length of at most two length bytes.  *tlv_start/ *tlv_len cover the
   whole element (header included).  Advances *p past it. */
static int tlv(const sdx_u8 **p, const sdx_u8 *end, sdx_u32 *tag, const sdx_u8 **v, sdx_u32 *vl, const sdx_u8 **start)
{
    const sdx_u8 *q = *p;
    sdx_u32 len;
    if (end - q < 2) return 0;
    if ((q[0] & 0x1f) == 0x1f) return 0;
    *tag = q[0];
    if (start) *start = q;
    if (q[1] < 0x80) { len = q[1]; q += 2; }
    else if (q[1] == 0x81) { if (end - q < 3 || q[2] < 0x80) return 0; len = q[2]; q += 3; }
    else if (q[1] == 0x82) { if (end - q < 4) return 0; len = ((sdx_u32)q[2] << 8) | q[3]; if (len < 0x100) return 0; q += 4; }
    else return 0;
    if (len > (sdx_u32)(end - q)) return 0;
    *v = q; *vl = len; *p = q + len;
    return 1;
}
/* expect a specific tag */
static int tlv_t(const sdx_u8 **p, const sdx_u8 *end, sdx_u32 want, const sdx_u8 **v, sdx_u32 *vl, const sdx_u8 **start)
{
    sdx_u32 tag;
    if (!tlv(p, end, &tag, v, vl, start) || tag != want) return 0;
    return 1;
}

/* ---- time ---- */
sdx_u32 sdx_unix_time(sdx_u32 y, sdx_u32 mo, sdx_u32 d, sdx_u32 h, sdx_u32 mi, sdx_u32 s)
{
    sdx_u32 era, yoe, doy, doe, days;
    if (y < 1970) return 0;
    if (y > 2105) return 0xffffffffu;
    if (mo <= 2) y--;                                           /* days_from_civil (H. Hinnant), unsigned because y >= 1969 */
    era = y / 400; yoe = y - era * 400;
    doy = (153u * (mo > 2 ? mo - 3 : mo + 9) + 2) / 5 + d - 1;
    doe = yoe * 365u + yoe / 4 - yoe / 100 + doy;
    days = era * 146097u + doe - 719468u;
    return days * 86400u + h * 3600u + mi * 60u + s;
}
static int two(const sdx_u8 *p, sdx_u32 *out)
{
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return 0;
    *out = (sdx_u32)((p[0] - '0') * 10 + (p[1] - '0'));
    return 1;
}
static int parse_time(const sdx_u8 **p, const sdx_u8 *end, sdx_u32 *out)
{
    sdx_u32 tag, vl, y, y2, mo, d, h, mi, s;
    const sdx_u8 *v;
    static const sdx_u8 dim[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (!tlv(p, end, &tag, &v, &vl, 0)) return 0;
    if (tag == T_UTC && vl == 13) {
        if (!two(v, &y2) || v[12] != 'Z') return 0;
        y = y2 >= 50 ? 1900 + y2 : 2000 + y2;
        v += 2;
    } else if (tag == T_GEN && vl == 15) {
        sdx_u32 a, b;
        if (!two(v, &a) || !two(v + 2, &b) || v[14] != 'Z') return 0;
        y = a * 100 + b;
        v += 4;
    } else return 0;
    if (!two(v, &mo) || !two(v + 2, &d) || !two(v + 4, &h) || !two(v + 6, &mi) || !two(v + 8, &s)) return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > dim[mo - 1] || h > 23 || mi > 59 || s > 59) return 0;
    *out = sdx_unix_time(y, mo, d, h, mi, s);
    return 1;
}

/* ---- keys ---- */
static int parse_spki(struct sdx_cert *c, const sdx_u8 *v, sdx_u32 vl)
{
    const sdx_u8 *p = v, *end = v + vl, *av, *oid, *par, *bits, *q, *qe, *iv;
    sdx_u32 al, ol, pl, bl, ql, il;
    const sdx_u8 *alg_end;
    sdx_u32 tag;
    c->key_type = SDX_KEY_NONE;
    if (!tlv_t(&p, end, T_SEQ, &av, &al, 0)) return 0;
    alg_end = av + al;
    q = av;
    if (!tlv_t(&q, alg_end, T_OID, &oid, &ol, 0)) return 0;
    if (!tlv_t(&p, end, T_BITS, &bits, &bl, 0) || p != end || bl < 2 || bits[0] != 0) return 0;
    bits++; bl--;
    if (eq(oid, ol, OID_RSA, 9)) {
        if (!tlv_t(&q, alg_end, T_NULL, &par, &pl, 0) || pl != 0 || q != alg_end) return 0;
        q = bits; qe = bits + bl;
        if (!tlv_t(&q, qe, T_SEQ, &iv, &ql, 0) || q != qe) return 0;
        q = iv; qe = iv + ql;
        if (!tlv_t(&q, qe, T_INT, &iv, &il, 0) || il < 2) return 0;
        if (iv[0] & 0x80) return 0;
        if (iv[0] == 0) { if (!(iv[1] & 0x80)) return 0; iv++; il--; }
        c->key = iv; c->key_len = il;
        if (!tlv_t(&q, qe, T_INT, &iv, &il, 0) || il < 1 || il > 5 || (iv[0] & 0x80) || q != qe) return 0;
        if (iv[0] == 0 && il > 1) { if (!(iv[1] & 0x80)) return 0; iv++; il--; }
        c->key_e = iv; c->key_e_len = il;
        if (c->key_len < 256 || c->key_len > SDPK_RSA_MAX_BYTES) { c->key_type = SDX_KEY_NONE; return 1; }   /* too small or too large: unusable, not malformed */
        c->key_type = SDX_KEY_RSA;
        return 1;
    }
    if (eq(oid, ol, OID_EC, 7)) {
        if (!tlv(&q, alg_end, &tag, &par, &pl, 0) || tag != T_OID || q != alg_end) return 0;
        if (eq(par, pl, OID_P256, 8) && bl == 65 && bits[0] == 4) c->key_type = SDX_KEY_EC256;
        else if (eq(par, pl, OID_P384, 5) && bl == 97 && bits[0] == 4) c->key_type = SDX_KEY_EC384;
        else return 1;                                          /* other curve: key unusable */
        c->key = bits; c->key_len = bl; c->key_e = 0; c->key_e_len = 0;
        return 1;
    }
    return 1;                                                   /* some other key type: parsed, unusable */
}

static int parse_sigalg(const sdx_u8 *v, sdx_u32 vl, sdx_u8 *alg)
{
    const sdx_u8 *p = v, *end = v + vl, *oid, *par;
    sdx_u32 ol, pl;
    *alg = SDX_SIG_NONE;
    if (!tlv_t(&p, end, T_OID, &oid, &ol, 0)) return 0;
    if (eq(oid, ol, OID_RSA_SHA256, 9) || eq(oid, ol, OID_RSA_SHA384, 9)) {
        if (!tlv_t(&p, end, T_NULL, &par, &pl, 0) || pl != 0 || p != end) return 0;
        *alg = eq(oid, ol, OID_RSA_SHA256, 9) ? SDX_SIG_RSA_SHA256 : SDX_SIG_RSA_SHA384;
    } else if (eq(oid, ol, OID_ECDSA_SHA256, 8) || eq(oid, ol, OID_ECDSA_SHA384, 8)) {
        if (p != end) return 0;
        *alg = eq(oid, ol, OID_ECDSA_SHA256, 8) ? SDX_SIG_ECDSA_SHA256 : SDX_SIG_ECDSA_SHA384;
    }
    return 1;                                                   /* unknown algorithm: parsed, unusable (alg stays NONE) */
}

/* ---- extensions ---- */
static int parse_exts(struct sdx_cert *c, const sdx_u8 *v, sdx_u32 vl)
{
    const sdx_u8 *p = v, *end = v + vl, *ev, *oid, *val, *q, *qe;
    sdx_u32 el, ol, vll, tag, critical, ql;
    const sdx_u8 *seen_end;
    sdx_u32 seen = 0;
    (void)seen_end;
    while (p < end) {
        const sdx_u8 *e;
        if (!tlv_t(&p, end, T_SEQ, &ev, &el, 0)) return SDX_E_PARSE;
        e = ev; qe = ev + el;
        if (!tlv_t(&e, qe, T_OID, &oid, &ol, 0)) return SDX_E_PARSE;
        critical = 0;
        if (e < qe && *e == T_BOOL) {
            if (!tlv_t(&e, qe, T_BOOL, &q, &ql, 0) || ql != 1 || q[0] != 0xff) return SDX_E_PARSE;
            critical = 1;
        }
        if (!tlv_t(&e, qe, T_OCTS, &val, &vll, 0) || e != qe) return SDX_E_PARSE;
        if (eq(oid, ol, OID_BC, 3)) {
            if (seen & 1) return SDX_E_PARSE;
            seen |= 1;
            q = val; qe = val + vll;
            { const sdx_u8 *s, *se, *x, *in; sdx_u32 xl;
              if (!tlv_t(&q, qe, T_SEQ, &in, &ql, 0) || q != qe) return SDX_E_PARSE;
              s = in; se = in + ql;
              c->has_bc = 1; c->is_ca = 0; c->pathlen = -1;
              if (s < se && *s == T_BOOL) { if (!tlv_t(&s, se, T_BOOL, &x, &xl, 0) || xl != 1 || (x[0] != 0xff)) return SDX_E_PARSE; c->is_ca = 1; }
              if (s < se && *s == T_INT) {
                  if (!tlv_t(&s, se, T_INT, &x, &xl, 0) || xl != 1 || (x[0] & 0x80)) return SDX_E_PARSE;      /* path lengths above 127 are not meaningful here */
                  c->pathlen = x[0];
              }
              if (s != se) return SDX_E_PARSE; }
        } else if (eq(oid, ol, OID_KU, 3)) {
            if (seen & 2) return SDX_E_PARSE;
            seen |= 2;
            q = val; qe = val + vll;
            { const sdx_u8 *in;
              if (!tlv_t(&q, qe, T_BITS, &in, &ql, 0) || q != qe || ql < 2 || in[0] > 7) return SDX_E_PARSE;
              c->has_ku = 1; c->ku_sign = (in[1] & 0x80) ? 1 : 0; c->ku_cert = (in[1] & 0x04) ? 1 : 0; }
        } else if (eq(oid, ol, OID_EKU, 3)) {
            if (seen & 4) return SDX_E_PARSE;
            seen |= 4;
            q = val; qe = val + vll;
            { const sdx_u8 *in;
              if (!tlv_t(&q, qe, T_SEQ, &in, &ql, 0) || q != qe) return SDX_E_PARSE;
              c->has_eku = 1; q = in; qe = in + ql;
              while (q < qe) {
                  const sdx_u8 *o; sdx_u32 oll;
                  if (!tlv_t(&q, qe, T_OID, &o, &oll, 0)) return SDX_E_PARSE;
                  if (eq(o, oll, OID_SERVER_AUTH, 8) || eq(o, oll, OID_ANY_EKU, 4)) c->eku_server = 1;
              } }
        } else if (eq(oid, ol, OID_SAN, 3)) {
            if (seen & 8) return SDX_E_PARSE;
            seen |= 8;
            q = val; qe = val + vll;
            { const sdx_u8 *in;
              if (!tlv_t(&q, qe, T_SEQ, &in, &ql, 0) || q != qe) return SDX_E_PARSE;
              c->san = in; c->san_len = ql; }
        } else if (eq(oid, ol, OID_SKI, 3) || eq(oid, ol, OID_AKI, 3) || eq(oid, ol, OID_CRLDP, 3) || eq(oid, ol, OID_POLICIES, 3) || eq(oid, ol, OID_AIA, 8)) {
            /* informational for us: ignored even when marked critical */
        } else if (critical) {
            (void)tag;
            return SDX_E_CRITICAL;
        }
    }
    return 0;
}

int sdx_parse(struct sdx_cert *c, const sdx_u8 *der, sdx_u32 len)
{
    const sdx_u8 *p, *end, *cv, *tv, *te, *v, *sv, *start;
    sdx_u32 cl, tl, vl, sl;
    sdx_u8 *z = (sdx_u8 *)c;
    sdx_u32 i, tag;
    const sdx_u8 *alg_in, *alg_out;
    sdx_u32 al_in, al_out;
    int r;
    for (i = 0; i < sizeof(*c); i++) z[i] = 0;
    c->pathlen = -1;
    if (len > SDX_MAX_CERT) return SDX_E_PARSE;
    c->der = der; c->der_len = len;
    p = der; end = der + len;
    if (!tlv_t(&p, end, T_SEQ, &cv, &cl, 0) || p != end) return SDX_E_PARSE;
    p = cv; end = cv + cl;
    if (!tlv_t(&p, end, T_SEQ, &tv, &tl, &start)) return SDX_E_PARSE;
    c->tbs = start; c->tbs_len = (sdx_u32)(p - start);
    if (!tlv_t(&p, end, T_SEQ, &alg_out, &al_out, 0)) return SDX_E_PARSE;
    if (!tlv_t(&p, end, T_BITS, &sv, &sl, 0) || p != end || sl < 2 || sv[0] != 0) return SDX_E_PARSE;
    c->sig = sv + 1; c->sig_len = sl - 1;
    if (!parse_sigalg(alg_out, al_out, &c->sig_alg)) return SDX_E_PARSE;

    p = tv; te = tv + tl;
    /* version [0] EXPLICIT INTEGER 2 (v3) */
    if (te - p < 5 || p[0] != 0xa0 || p[1] != 3 || p[2] != T_INT || p[3] != 1) return SDX_E_PARSE;
    if (p[4] != 2) return SDX_E_UNSUPPORTED;
    p += 5;
    if (!tlv_t(&p, te, T_INT, &v, &vl, 0) || vl == 0 || vl > 21) return SDX_E_PARSE;
    if (!tlv_t(&p, te, T_SEQ, &alg_in, &al_in, 0)) return SDX_E_PARSE;
    if (!eq(alg_in, al_in, alg_out, al_out)) return SDX_E_PARSE;          /* signature algorithm fields must agree (RFC 5280 4.1.1.2) */
    if (!tlv_t(&p, te, T_SEQ, &v, &vl, &start)) return SDX_E_PARSE;
    c->issuer = start; c->issuer_len = (sdx_u32)(p - start);
    if (!tlv_t(&p, te, T_SEQ, &v, &vl, 0)) return SDX_E_PARSE;
    {
        const sdx_u8 *q = v, *qe = v + vl;
        if (!parse_time(&q, qe, &c->not_before) || !parse_time(&q, qe, &c->not_after) || q != qe) return SDX_E_PARSE;
    }
    if (!tlv_t(&p, te, T_SEQ, &v, &vl, &start)) return SDX_E_PARSE;
    c->subject = start; c->subject_len = (sdx_u32)(p - start);
    if (!tlv_t(&p, te, T_SEQ, &v, &vl, 0)) return SDX_E_PARSE;
    if (!parse_spki(c, v, vl)) return SDX_E_PARSE;
    /* issuerUniqueID [1], subjectUniqueID [2] are skipped when present; extensions [3] */
    while (p < te) {
        if (!tlv(&p, te, &tag, &v, &vl, 0)) return SDX_E_PARSE;
        if (tag == 0xa3) {
            const sdx_u8 *q = v, *in;
            sdx_u32 il;
            if (p != te) return SDX_E_PARSE;
            if (!tlv_t(&q, v + vl, T_SEQ, &in, &il, 0) || q != v + vl) return SDX_E_PARSE;
            r = parse_exts(c, in, il);
            if (r) return r;
        } else if (tag != 0x81 && tag != 0x82) return SDX_E_PARSE;
    }
    return 0;
}

/* ---- host names ---- */
static sdx_u8 lower(sdx_u8 c) { return (c >= 'A' && c <= 'Z') ? (sdx_u8)(c + 32) : c; }
static int host_ok(const char *h, sdx_u32 *len)
{
    sdx_u32 n = 0;
    if (!h) return 0;
    while (h[n]) {
        sdx_u8 c = (sdx_u8)h[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_')) return 0;
        if (++n > 253) return 0;
    }
    if (n == 0 || h[0] == '.' || h[n - 1] == '.') return 0;
    *len = n;
    return 1;
}
static int name_eq(const sdx_u8 *pat, sdx_u32 pl, const char *host, sdx_u32 hl)
{
    sdx_u32 i, at = 0;
    if (pl == 0) return 0;
    for (i = 0; i < pl; i++) if (pat[i] == 0 || pat[i] >= 0x80 || (pat[i] == '*' && i != 0)) return 0;
    if (pat[0] == '*') {
        sdx_u32 dots = 0;
        if (pl < 4 || pat[1] != '.') return 0;
        for (i = 2; i < pl; i++) if (pat[i] == '.') dots++;
        if (dots == 0) return 0;                                  /* "*.com" and the like */
        while (at < hl && host[at] != '.') at++;
        if (at == 0 || at >= hl) return 0;                        /* the wildcard stands for exactly one non-empty label */
        pat++; pl--;                                              /* compare ".rest" with the host's ".rest" */
        host += at; hl -= at;
    }
    if (pl != hl) return 0;
    for (i = 0; i < pl; i++) if (lower(pat[i]) != lower((sdx_u8)host[i])) return 0;
    return 1;
}
int sdx_host_match(const struct sdx_cert *c, const char *host)
{
    const sdx_u8 *p = c->san, *end = c->san + c->san_len, *v;
    sdx_u32 tag, vl, hl;
    if (!c->san || !host_ok(host, &hl)) return 0;
    while (p < end) {
        if (!tlv(&p, end, &tag, &v, &vl, 0)) return 0;
        if (tag == 0x82 && name_eq(v, vl, host, hl)) return 1;
    }
    return 0;
}

/* ---- anchors and chain ---- */
void sdx_anchor_from_cert(struct sdx_anchor *a, const struct sdx_cert *c)
{
    a->dn = c->subject; a->dn_len = c->subject_len;
    a->key_type = c->key_type; a->key = c->key; a->key_len = c->key_len; a->key_e = c->key_e; a->key_e_len = c->key_e_len;
}

static int cert_policy(const struct sdx_chain *ch, const struct sdx_cert *c, sdx_u32 i)
{
    if (!(ch->flags & SDX_F_NO_TIME)) {
        if (ch->now == 0) return SDX_E_NOTIME;
        if (ch->now < c->not_before || ch->now > c->not_after) return SDX_E_EXPIRED;
    }
    if (i == 0) {
        if (!sdx_host_match(c, ch->host)) return SDX_E_HOST;
        if (c->has_eku && !c->eku_server) return SDX_E_CA;
        if (c->has_ku && !c->ku_sign) return SDX_E_CA;
    } else {
        if (!c->has_bc || !c->is_ca) return SDX_E_CA;
        if (c->has_ku && !c->ku_cert) return SDX_E_CA;
        if (c->has_eku && !c->eku_server) return SDX_E_CA;
        if (c->pathlen >= 0 && (int)(i - 1) > c->pathlen) return SDX_E_CA;
    }
    return 0;
}

int sdx_chain_begin(struct sdx_chain *ch, const struct sdx_anchor *anchors, sdx_u32 nanchors, const char *host, sdx_u32 now, sdx_u32 flags,
                    const sdx_u8 *const *ders, const sdx_u32 *lens, sdx_u32 count)
{
    sdx_u32 i, a;
    int r;
    sdx_u8 *z = (sdx_u8 *)ch;
    for (i = 0; i < sizeof(*ch); i++) z[i] = 0;
    ch->anchors = anchors; ch->nanchors = nanchors; ch->host = host; ch->now = now; ch->flags = flags;
    if (count == 0) return SDX_E_PARSE;
    for (i = 0; i < count && i < SDX_MAX_CHAIN; i++) {
        struct sdx_cert *c = &ch->c[i];
        r = sdx_parse(c, ders[i], lens[i]);
        if (r) return r;
        r = cert_policy(ch, c, i);
        if (r) return r;
        for (a = 0; a < nanchors; a++) {
            if (eq(anchors[a].dn, anchors[a].dn_len, c->issuer, c->issuer_len)) { ch->root = &anchors[a]; ch->n = i + 1; return 0; }
        }
        if (i + 1 >= count || i + 1 >= SDX_MAX_CHAIN) return SDX_E_UNTRUSTED;
        {   /* the next certificate must be this one's issuer */
            struct sdx_cert nx;
            if (sdx_parse(&nx, ders[i + 1], lens[i + 1]) != 0) return SDX_E_PARSE;
            if (!eq(nx.subject, nx.subject_len, c->issuer, c->issuer_len)) return SDX_E_UNTRUSTED;
        }
    }
    return SDX_E_UNTRUSTED;
}

int sdx_chain_step(struct sdx_chain *ch, struct sdpk_ws *ws)
{
    const struct sdx_cert *c;
    sdx_u8 key_type, hash_id;
    const sdx_u8 *key, *key_e;
    sdx_u32 key_len, key_e_len, hl;
    sdx_u8 digest[48];
    int ok;
    if (ch->step >= ch->n) return SDX_E_UNSUPPORTED;
    c = &ch->c[ch->step];
    if (ch->step + 1 < ch->n) {
        const struct sdx_cert *is = &ch->c[ch->step + 1];
        key_type = is->key_type; key = is->key; key_len = is->key_len; key_e = is->key_e; key_e_len = is->key_e_len;
    } else {
        key_type = ch->root->key_type; key = ch->root->key; key_len = ch->root->key_len; key_e = ch->root->key_e; key_e_len = ch->root->key_e_len;
    }
    if (c->sig_alg == SDX_SIG_NONE || key_type == SDX_KEY_NONE) return SDX_E_UNSUPPORTED;
    hash_id = (c->sig_alg == SDX_SIG_RSA_SHA256 || c->sig_alg == SDX_SIG_ECDSA_SHA256) ? SDPK_HASH_SHA256 : SDPK_HASH_SHA384;
    if (hash_id == SDPK_HASH_SHA256) { sdc_sha256(c->tbs, c->tbs_len, digest); hl = 32; }
    else { sdpk_sha384(c->tbs, c->tbs_len, digest); hl = 48; }
    if (c->sig_alg == SDX_SIG_RSA_SHA256 || c->sig_alg == SDX_SIG_RSA_SHA384) {
        if (key_type != SDX_KEY_RSA) return SDX_E_UNSUPPORTED;
        ok = sdpk_rsa_pkcs1_verify(ws, key, key_len, key_e, key_e_len, hash_id, digest, c->sig, c->sig_len);
    } else {
        if (key_type != SDX_KEY_EC256 && key_type != SDX_KEY_EC384) return SDX_E_UNSUPPORTED;
        ok = sdpk_ecdsa_verify(ws, key_type == SDX_KEY_EC256 ? SDPK_CURVE_P256 : SDPK_CURVE_P384, key, key_len, digest, hl, c->sig, c->sig_len);
    }
    if (!ok) return SDX_E_SIG;
    ch->step++;
    return ch->step == ch->n ? 1 : 0;
}
