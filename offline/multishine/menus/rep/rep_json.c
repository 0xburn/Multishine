/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* rep_json: the GraphQL answer scanner and the HELLO clock of the report client.
   One bounded pass over the answer.  Tags: 1 root object, 2 "data", 3 "reportOnlineGame", 8 "errors", 20 "success", 21 "uploadUrl".  Reads: errors (any
   element, or a non-null non-array value), data.reportOnlineGame.success (true / false), data.reportOnlineGame.uploadUrl (a non-null string).
   Malformed input, trailing bytes or nesting deeper than J_MAXD -> -1.  No heap, no recursion beyond J_MAXD. */
#include "rep.h"

#define J_MAXD 8
struct jp { const rep_u8 *p; rep_u32 n, i; };
struct jres { int err_present, success, upload; };

static rep_u32 slen(const char *s) { rep_u32 n = 0; while (s[n]) n++; return n; }
static void ws(struct jp *j) { while (j->i < j->n && (j->p[j->i] == ' ' || j->p[j->i] == '\t' || j->p[j->i] == '\r' || j->p[j->i] == '\n')) j->i++; }
static int keyis(const struct jp *j, rep_u32 s, rep_u32 l, const char *k)
{
    rep_u32 i;
    if (slen(k) != l) return 0;
    for (i = 0; i < l; i++) if (j->p[s + i] != (rep_u8)k[i]) return 0;
    return 1;
}
static int jstr(struct jp *j, rep_u32 *s, rep_u32 *len)        /* at an opening quote; leaves i after the closing quote; s / len = raw contents */
{
    rep_u32 st;
    if (j->i >= j->n || j->p[j->i] != '"') return 0;
    st = ++j->i;
    while (j->i < j->n) {
        rep_u8 c = j->p[j->i];
        if (c == '\\') { j->i += 2; continue; }
        if (c == '"') { if (j->i > j->n) return 0; *s = st; *len = j->i - st; j->i++; return 1; }
        if (c < 0x20) return 0;
        j->i++;
    }
    return 0;
}
static int lit(struct jp *j, const char *w)
{
    rep_u32 l = slen(w), i;
    if (j->i + l > j->n) return 0;
    for (i = 0; i < l; i++) if (j->p[j->i + i] != (rep_u8)w[i]) return 0;
    j->i += l;
    return 1;
}
static int jvalue(struct jp *j, int depth, int tag, struct jres *r)
{
    rep_u8 c;
    ws(j);
    if (j->i >= j->n || depth > J_MAXD) return 0;
    c = j->p[j->i];
    if (c == '{') {
        j->i++; ws(j);
        if (tag == 8) r->err_present = 1;                      /* "errors": {...} is an error too */
        if (j->i < j->n && j->p[j->i] == '}') { j->i++; return 1; }
        for (;;) {
            rep_u32 ks, kl;
            int ct = 0;
            ws(j);
            if (!jstr(j, &ks, &kl)) return 0;
            ws(j);
            if (j->i >= j->n || j->p[j->i] != ':') return 0;
            j->i++;
            if (tag == 1) ct = keyis(j, ks, kl, "data") ? 2 : keyis(j, ks, kl, "errors") ? 8 : 0;
            else if (tag == 2) ct = keyis(j, ks, kl, "reportOnlineGame") ? 3 : 0;
            else if (tag == 3) ct = keyis(j, ks, kl, "success") ? 20 : keyis(j, ks, kl, "uploadUrl") ? 21 : 0;
            if (!jvalue(j, depth + 1, ct, r)) return 0;
            ws(j);
            if (j->i >= j->n) return 0;
            if (j->p[j->i] == ',') { j->i++; continue; }
            if (j->p[j->i] == '}') { j->i++; return 1; }
            return 0;
        }
    }
    if (c == '[') {
        j->i++; ws(j);
        if (j->i < j->n && j->p[j->i] == ']') { j->i++; return 1; }
        if (tag == 8) r->err_present = 1;
        for (;;) {
            if (!jvalue(j, depth + 1, 0, r)) return 0;
            ws(j);
            if (j->i >= j->n) return 0;
            if (j->p[j->i] == ',') { j->i++; continue; }
            if (j->p[j->i] == ']') { j->i++; return 1; }
            return 0;
        }
    }
    if (c == '"') {
        rep_u32 s, l;
        if (!jstr(j, &s, &l)) return 0;
        if (tag == 21) r->upload = 1;
        if (tag == 8) r->err_present = 1;
        return 1;
    }
    if (c == 't') { if (!lit(j, "true")) return 0; if (tag == 20) r->success = 1; if (tag == 8) r->err_present = 1; return 1; }
    if (c == 'f') { if (!lit(j, "false")) return 0; if (tag == 20) r->success = 0; if (tag == 8) r->err_present = 1; return 1; }
    if (c == 'n') return lit(j, "null");
    if (c == '-' || (c >= '0' && c <= '9')) {
        while (j->i < j->n && ((j->p[j->i] >= '0' && j->p[j->i] <= '9') || j->p[j->i] == '-' || j->p[j->i] == '+' || j->p[j->i] == '.' || j->p[j->i] == 'e' || j->p[j->i] == 'E')) j->i++;
        if (tag == 8) r->err_present = 1;
        return 1;
    }
    return 0;
}

int rep_parse_answer(const rep_u8 *p, rep_u32 n, int *upload)
{
    struct jp j;
    struct jres r;
    j.p = p; j.n = n; j.i = 0;
    r.err_present = 0; r.success = 0; r.upload = 0;
    if (upload) *upload = 0;
    ws(&j);
    if (j.i >= j.n || j.p[j.i] != '{') return -1;
    if (!jvalue(&j, 0, 1, &r)) return -1;
    ws(&j);
    if (j.i != j.n) return -1;
    if (r.err_present) return -1;
    if (upload) *upload = r.upload;
    return r.success;
}
