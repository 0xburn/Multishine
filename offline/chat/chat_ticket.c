/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* chat_ticket: see chat_ticket.h.  Original work, no third-party code.  A small recursive-descent JSON reader that only
   understands what it needs: the top-level object, its "players" array, and in each player "port" and "chatMessages". */
#include "chat_ticket.h"

struct rd {
    const cc_u8 *p, *end;
    int err;
};

static void ws(struct rd *r)
{
    while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r')) r->p++;
}

static int peek(struct rd *r)
{
    ws(r);
    return r->p < r->end ? *r->p : -1;
}

static int eat(struct rd *r, int c)
{
    if (peek(r) != c) { r->err = 1; return 0; }
    r->p++;
    return 1;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* read a string; up to cap bytes of its unescaped value go to out (if out != 0), *outlen = bytes stored,
   *truncated = 1 if the value was longer.  \uXXXX below 0x80 becomes that byte, anything else one 0x80 marker byte
   (cc_sanitize drops it).  Returns 1 on success. */
static int str(struct rd *r, cc_u8 *out, cc_u32 cap, cc_u32 *outlen, int *truncated)
{
    cc_u32 n = 0;
    if (outlen) *outlen = 0;
    if (truncated) *truncated = 0;
    if (!eat(r, '"')) return 0;
    while (r->p < r->end) {
        int c = *r->p++;
        int v = -1;
        if (c == '"') {
            if (outlen) *outlen = n;
            return 1;
        }
        if (c < 0x20) break;              /* raw control character inside a string: invalid JSON */
        if (c == '\\') {
            if (r->p >= r->end) break;
            c = *r->p++;
            switch (c) {
            case '"': v = '"'; break;
            case '\\': v = '\\'; break;
            case '/': v = '/'; break;
            case 'b': v = 8; break;
            case 'f': v = 12; break;
            case 'n': v = 10; break;
            case 'r': v = 13; break;
            case 't': v = 9; break;
            case 'u': {
                int k, u = 0;
                if (r->end - r->p < 4) { r->err = 1; return 0; }
                for (k = 0; k < 4; k++) {
                    int h = hexval(r->p[k]);
                    if (h < 0) { r->err = 1; return 0; }
                    u = u * 16 + h;
                }
                r->p += 4;
                v = u < 0x80 ? u : 0x80;
                break;
            }
            default: r->err = 1; return 0;
            }
        } else {
            v = c;
        }
        if (out) {
            if (n < cap) out[n++] = (cc_u8)v;
            else if (truncated) *truncated = 1;
        }
    }
    r->err = 1;
    return 0;
}

static int skip_value(struct rd *r, int depth);

static int skip_container(struct rd *r, int depth, int open, int close, int keyed)
{
    if (depth > CT_MAX_DEPTH) { r->err = 1; return 0; }
    if (!eat(r, open)) return 0;
    if (peek(r) == close) { r->p++; return 1; }
    for (;;) {
        if (keyed) {
            if (!str(r, 0, 0, 0, 0) || !eat(r, ':')) return 0;
        }
        if (!skip_value(r, depth + 1)) return 0;
        if (peek(r) == ',') { r->p++; continue; }
        return eat(r, close);
    }
}

static int skip_value(struct rd *r, int depth)
{
    int c = peek(r);
    if (c == '"') return str(r, 0, 0, 0, 0);
    if (c == '{') return skip_container(r, depth, '{', '}', 1);
    if (c == '[') return skip_container(r, depth, '[', ']', 0);
    if (c == 't' || c == 'f' || c == 'n') {   /* true, false, null: exactly */
        static const char *const lit[3] = {"true", "false", "null"};
        int i;
        for (i = 0; i < 3; i++) {
            cc_u32 k = 0;
            while (lit[i][k] && r->p + k < r->end && r->p[k] == (cc_u8)lit[i][k]) k++;
            if (!lit[i][k]) {
                r->p += k;
                return 1;
            }
        }
        r->err = 1;
        return 0;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {   /* number: sign, digits, dot, exponent (not checked further) */
        const cc_u8 *s = r->p;
        while (r->p < r->end) {
            int d = *r->p;
            if ((d >= '0' && d <= '9') || d == '-' || d == '+' || d == '.' || d == 'e' || d == 'E') r->p++;
            else break;
        }
        if (r->p == s) { r->err = 1; return 0; }
        return 1;
    }
    r->err = 1;
    return 0;
}

static int key_is(const cc_u8 *k, cc_u32 n, const char *want)
{
    cc_u32 i = 0;
    for (; i < n; i++)
        if (!want[i] || want[i] != (char)k[i]) return 0;
    return want[i] == 0;
}

/* small non-negative integer (port); anything else -> -1 (value skipped) */
static int read_port(struct rd *r, int depth)
{
    int v = 0, digits = 0;
    if (peek(r) < '0' || peek(r) > '9') return skip_value(r, depth) ? -1 : -2;
    while (r->p < r->end && *r->p >= '0' && *r->p <= '9') {
        if (v < 1000) v = v * 10 + (*r->p - '0');
        r->p++;
        digits++;
    }
    if (r->p < r->end && (*r->p == '.' || *r->p == 'e' || *r->p == 'E')) {   /* 1.5, 1e3: not a port */
        while (r->p < r->end && ((*r->p >= '0' && *r->p <= '9') || *r->p == '.' || *r->p == 'e' || *r->p == 'E' ||
                                 *r->p == '+' || *r->p == '-'))
            r->p++;
        return -1;
    }
    return digits ? v : -1;
}

/* chatMessages value: an array of exactly 16 strings -> texts (sanitised) and 1; anything else skipped -> 0 */
static int read_messages(struct rd *r, int depth, char texts[CP_N][CC_TXT], struct ct_result *res)
{
    int count = 0, all_strings = 1;
    if (peek(r) != '[') return skip_value(r, depth) ? 0 : -1;
    r->p++;
    if (peek(r) == ']') { r->p++; return 0; }
    for (;;) {
        if (peek(r) == '"') {
            cc_u8 scratch[CT_SCRATCH];
            cc_u32 n = 0;
            int cut = 0;
            if (!str(r, scratch, sizeof scratch, &n, &cut)) return -1;
            if (count < CP_N) {
                int k = cc_sanitize(scratch, n, texts[count]);
                if (k == 0) res->strings_empty++;
                else if ((cc_u32)k < n || cut) res->strings_cut++;
            }
        } else {
            all_strings = 0;
            if (!skip_value(r, depth + 1)) return -1;
        }
        count++;
        if (peek(r) == ',') { r->p++; continue; }
        if (!eat(r, ']')) return -1;
        break;
    }
    return (all_strings && count == CP_N) ? 1 : 0;
}

static int read_player(struct rd *r, int depth, struct cc_strings *s, struct ct_result *res)
{
    char texts[CP_N][CC_TXT];
    int port = -1, msgs = 0, i, k;
    if (depth > CT_MAX_DEPTH || !eat(r, '{')) return 0;
    if (peek(r) == '}') { r->p++; return 1; }
    for (;;) {
        cc_u8 key[24];
        cc_u32 kn = 0;
        if (!str(r, key, sizeof key, &kn, 0) || !eat(r, ':')) return 0;
        if (key_is(key, kn, "port")) {
            port = read_port(r, depth + 1);
            if (port == -2) return 0;
        } else if (key_is(key, kn, "chatMessages")) {
            msgs = read_messages(r, depth + 1, texts, res);
            if (msgs < 0) return 0;
            if (msgs == 0) res->arrays_unusable++;
        } else if (!skip_value(r, depth + 1)) {
            return 0;
        }
        if (peek(r) == ',') { r->p++; continue; }
        if (!eat(r, '}')) return 0;
        break;
    }
    res->players_seen++;
    if (port < 1 || port > 4) return 1;   /* no usable slot: ignore the entry */
    if (msgs == 1) {
        for (i = 0; i < CP_N; i++)
            for (k = 0; k < CC_TXT; k++) s->text[port - 1][i][k] = texts[i][k];
        s->have[port - 1] = 1;
        res->arrays_ok++;
    } else {
        s->have[port - 1] = 0;
    }
    return 1;
}

int ct_read_players(const cc_u8 *js, cc_u32 len, struct cc_strings *s, struct ct_result *res)
{
    struct rd r;
    struct ct_result dummy;
    if (!res) res = &dummy;
    {
        cc_u8 *z = (cc_u8 *)res;
        cc_u32 i;
        for (i = 0; i < sizeof *res; i++) z[i] = 0;
    }
    cc_strings_clear(s);
    r.p = js;
    r.end = js + len;
    r.err = 0;
    if (!eat(&r, '{')) return -1;
    if (peek(&r) == '}') return 0;
    for (;;) {
        cc_u8 key[24];
        cc_u32 kn = 0;
        if (!str(&r, key, sizeof key, &kn, 0) || !eat(&r, ':')) return -1;
        if (key_is(key, kn, "players") && peek(&r) == '[') {
            r.p++;
            if (peek(&r) == ']') {
                r.p++;
            } else {
                for (;;) {
                    if (peek(&r) == '{') {
                        if (!read_player(&r, 2, s, res)) return -1;
                    } else if (!skip_value(&r, 2)) {
                        return -1;
                    }
                    if (peek(&r) == ',') { r.p++; continue; }
                    if (!eat(&r, ']')) return -1;
                    break;
                }
            }
        } else if (!skip_value(&r, 1)) {
            return -1;
        }
        if (peek(&r) == ',') { r.p++; continue; }
        if (!eat(&r, '}')) return -1;
        break;
    }
    if (peek(&r) != -1) return -1;            /* nothing but white space after the object */
    return r.err ? -1 : 0;
}

int ct_write_array(char *out, cc_u32 cap, cc_u32 len, const char texts[CP_N][CC_TXT])
{
    static const char head[] = ",\"chatMessages\":[";
    cc_u32 n = len, i, k;
    for (k = 0; head[k]; k++) {
        if (n + 1 >= cap) goto fail;
        out[n++] = head[k];
    }
    for (i = 0; i < CP_N; i++) {
        if (n + 2 >= cap) goto fail;
        if (i) out[n++] = ',';
        out[n++] = '"';
        for (k = 0; k < CC_TXT - 1 && texts[i][k]; k++) {
            if (!cc_drawable((cc_u8)texts[i][k]) || n + 1 >= cap) goto fail;   /* never needs an escape */
            out[n++] = texts[i][k];
        }
        if (k == 0 || texts[i][k] || n + 1 >= cap) goto fail;              /* empty, or longer than 25 */
        out[n++] = '"';
    }
    if (n + 1 >= cap) goto fail;
    out[n++] = ']';
    out[n] = 0;
    return (int)n;
fail:
    if (len < cap) out[len] = 0;
    return -1;
}
