/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sbr_codec: the pure pieces of the Slippi bridge that BOTH sides use: connect-code checks and Shift-JIS form, the strict bounded JSON reader and the reply / match parser.
   Linked into the engine (code checks only; gc-sections drops the rest) and into ms/rep.ovl (the WebSocket matchmaker client, sbr_ws.c) and the host tests.
   Split out of sbr.c on when the ENet matchmaker left the engine.  Project-owned C99, no libc, no heap. */
#include "sbr.h"


typedef sbr_u32 u32;
typedef sbr_s32 s32;
typedef sbr_u16 u16;
typedef sbr_u8 u8;

/* ------------------------------------------------------------------ small helpers (no libc) */
static void zero(void *p, u32 n) { volatile u8 *b = (volatile u8 *)p; while (n--) *b++ = 0; }   /* volatile: wipes are not optimised away */
static u32 slen(const char *s) { u32 n = 0; if (!s) return 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s, u32 cap) { u32 i = 0; if (!cap) return; if (s) for (; s[i] && i + 1 < cap; i++) d[i] = s[i]; d[i] = 0; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int contains(const char *h, const char *n)
{
    u32 i, j, hn = slen(h), nn = slen(n);
    for (i = 0; i + nn <= hn; i++) {
        for (j = 0; j < nn && h[i + j] == n[j]; j++) {}
        if (j == nn) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ codes */
int sbr_code_valid(const char *c)              /* 1-7 of A-Z/0-9, '#', 1+ digits, 8 characters at most (Slippi's entry) */
{
    u32 i = 0, pre = 0, post = 0;
    if (!c) return 0;
    for (; c[i] && c[i] != '#'; i++) {
        if (!((c[i] >= 'A' && c[i] <= 'Z') || (c[i] >= '0' && c[i] <= '9'))) return 0;
        pre++;
    }
    if (c[i] != '#' || pre == 0) return 0;
    for (i++; c[i]; i++) {
        if (!(c[i] >= '0' && c[i] <= '9')) return 0;
        post++;
    }
    return post >= 1 && pre + 1 + post <= 8;
}

static u16 to_sjis(char c)
{
    if (c >= '0' && c <= '9') return (u16)(0x824F + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (u16)(0x8260 + (c - 'A'));
    if (c == '#') return 0x8194;
    return 0;
}

u32 sbr_code_to_sjis(const char *code, u8 out[18])
{
    u32 i, n = 0;
    zero(out, 18);
    for (i = 0; code && code[i] && n + 2 <= 18; i++) {
        u16 v = to_sjis(code[i]);
        if (!v) break;
        out[n++] = (u8)(v >> 8);
        out[n++] = (u8)v;
    }
    return n;                                          /* the stock client cuts the 18-byte buffer at the first 0 byte */
}


/* ------------------------------------------------------------------ JSON reader (strict, bounded) */
struct jr { const u8 *p, *e; int bad; };
static void ws(struct jr *r) { while (r->p < r->e && (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r')) r->p++; }
static int peek(struct jr *r) { ws(r); return r->p < r->e ? *r->p : -1; }
static int expect(struct jr *r, char c) { if (peek(r) != (u8)c) { r->bad = 1; return 0; } r->p++; return 1; }
static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* string into out (cap, may be 0 = skip); non-ASCII and escapes above 0x7E become '?' (Melee's font draws ASCII only) */
static int jstr(struct jr *r, char *out, u32 cap)
{
    u32 n = 0;
    if (!expect(r, '"')) return -1;
    while (r->p < r->e) {
        u8 c = *r->p++;
        int o;
        if (c == '"') { if (cap) out[n < cap ? n : cap - 1] = 0; return 0; }
        if (c < 0x20) break;
        if (c == '\\') {
            if (r->p >= r->e) break;
            c = *r->p++;
            switch (c) {
            case '"': o = '"'; break; case '\\': o = '\\'; break; case '/': o = '/'; break;
            case 'b': o = '\b'; break; case 'f': o = '\f'; break; case 'n': o = '\n'; break;
            case 'r': o = '\r'; break; case 't': o = '\t'; break;
            case 'u': {
                int k, v = 0;
                if (r->e - r->p < 4) { r->bad = 1; return -1; }
                for (k = 0; k < 4; k++) { int h = hexv(r->p[k]); if (h < 0) { r->bad = 1; return -1; } v = v * 16 + h; }
                r->p += 4;
                o = v >= 0x20 && v < 0x7F ? v : '?';
                break;
            }
            default: r->bad = 1; return -1;
            }
        } else o = c >= 0x80 ? '?' : c;
        if (cap && n + 1 < cap) out[n++] = (char)o;
    }
    r->bad = 1;
    return -1;
}

static int jnum(struct jr *r, s32 *iv, int *is_int)    /* JSON number; *iv only for an integer that fits */
{
    const u8 *s;
    s32 v = 0;
    int neg = 0, digits = 0, intg = 1, ovf = 0;
    ws(r);
    s = r->p;
    if (r->p < r->e && *r->p == '-') { neg = 1; r->p++; }
    if (r->p < r->e && *r->p == '0') { r->p++; digits = 1; }
    else while (r->p < r->e && *r->p >= '0' && *r->p <= '9') {
        if (v > 214748363) ovf = 1; else v = v * 10 + (*r->p - '0');
        r->p++; digits++;
    }
    if (!digits) { r->bad = 1; return -1; }
    if (r->p < r->e && *r->p == '.') {
        intg = 0; r->p++;
        if (!(r->p < r->e && *r->p >= '0' && *r->p <= '9')) { r->bad = 1; return -1; }
        while (r->p < r->e && *r->p >= '0' && *r->p <= '9') r->p++;
    }
    if (r->p < r->e && (*r->p == 'e' || *r->p == 'E')) {
        intg = 0; r->p++;
        if (r->p < r->e && (*r->p == '+' || *r->p == '-')) r->p++;
        if (!(r->p < r->e && *r->p >= '0' && *r->p <= '9')) { r->bad = 1; return -1; }
        while (r->p < r->e && *r->p >= '0' && *r->p <= '9') r->p++;
    }
    (void)s;
    if (iv) *iv = neg ? -v : v;
    if (is_int) *is_int = intg && !ovf;
    return 0;
}

static int jlit(struct jr *r, const char *w)
{
    u32 i, n = slen(w);
    ws(r);
    if ((u32)(r->e - r->p) < n) { r->bad = 1; return -1; }
    for (i = 0; i < n; i++) if (r->p[i] != (u8)w[i]) { r->bad = 1; return -1; }
    r->p += n;
    return 0;
}

static int jbool(struct jr *r, s32 *out)
{
    int c = peek(r);
    if (c == 't') { if (jlit(r, "true") < 0) return -1; *out = 1; return 0; }
    if (c == 'f') { if (jlit(r, "false") < 0) return -1; *out = 0; return 0; }
    r->bad = 1;
    return -1;
}

static int jskip(struct jr *r, int depth)
{
    int c = peek(r);
    if (depth > SBR_JSON_MAX_DEPTH) { r->bad = 1; return -1; }
    if (c == '"') return jstr(r, 0, 0);
    if (c == '{' || c == '[') {
        int close = c == '{' ? '}' : ']';
        r->p++;
        if (peek(r) == close) { r->p++; return 0; }
        for (;;) {
            if (c == '{') { if (jstr(r, 0, 0) < 0 || !expect(r, ':')) return -1; }
            if (jskip(r, depth + 1) < 0) return -1;
            if (peek(r) == ',') { r->p++; continue; }
            if (peek(r) == close) { r->p++; return 0; }
            r->bad = 1;
            return -1;
        }
    }
    if (c == 't') return jlit(r, "true");
    if (c == 'f') return jlit(r, "false");
    if (c == 'n') return jlit(r, "null");
    return jnum(r, 0, 0);
}

static int jkey(struct jr *r, char *k, u32 cap) { if (jstr(r, k, cap) < 0 || !expect(r, ':')) return -1; return 0; }

static int parse_player(struct jr *r, struct sbr_player *P)
{
    char k[24];
    zero(P, sizeof *P);
    if (!expect(r, '{')) return -1;
    if (peek(r) == '}') { r->p++; return 0; }
    for (;;) {
        if (jkey(r, k, sizeof k) < 0) return -1;
        if (seq(k, "displayName")) { if (jstr(r, P->name, sizeof P->name) < 0) return -1; }
        else if (seq(k, "uid") && peek(r) == '"') { if (jstr(r, P->uid, sizeof P->uid) < 0) return -1; }      /* a non-string uid is skipped below, as before */
        else if (seq(k, "connectCode")) { if (jstr(r, P->code, sizeof P->code) < 0) return -1; }
        else if (seq(k, "ipAddress")) { if (jstr(r, P->ip, sizeof P->ip) < 0) return -1; }
        else if (seq(k, "ipAddressLan")) { if (jstr(r, P->lan, sizeof P->lan) < 0) return -1; P->has_lan = 1; }
        else if (seq(k, "port")) { s32 v; int ii; if (jnum(r, &v, &ii) < 0 || !ii) { r->bad = 1; return -1; } P->port = v; }
        else if (seq(k, "isLocalPlayer")) { if (jbool(r, &P->local) < 0) return -1; }
        else if (seq(k, "isBot")) { if (jbool(r, &P->bot) < 0) return -1; }
        else if (jskip(r, 3) < 0) return -1;          /* uid, chatMessages, rank, unknown keys */
        if (peek(r) == ',') { r->p++; continue; }
        if (peek(r) == '}') { r->p++; return 0; }
        r->bad = 1;
        return -1;
    }
}

int sbr_parse_reply(const u8 *js, u32 len, struct sbr_reply *R)
{
    struct jr r;
    char k[24];
    zero(R, sizeof *R);
    if (!js || !len) return -1;
    r.p = js; r.e = js + len; r.bad = 0;
    if (!expect(&r, '{')) return -1;
    if (peek(&r) == '}') { r.p++; goto end; }
    for (;;) {
        if (jkey(&r, k, sizeof k) < 0) return -1;
        if (seq(k, "type")) { if (jstr(&r, R->type, sizeof R->type) < 0) return -1; }
        else if (seq(k, "error")) { if (jstr(&r, R->error, sizeof R->error) < 0) return -1; }
        else if (seq(k, "latestVersion")) { if (jstr(&r, R->latest, sizeof R->latest) < 0) return -1; }
        else if (seq(k, "ticketId") && peek(&r) == '"') { if (jstr(&r, R->ticket, sizeof R->ticket) < 0) return -1; }          /* WebSocket matchmaker (sbr_ws.c) */
        else if (seq(k, "token") && peek(&r) == '"') { if (jstr(&r, R->token, sizeof R->token) < 0) return -1; }
        else if (seq(k, "secondsInQueue") && (peek(&r) == '-' || (peek(&r) >= '0' && peek(&r) <= '9'))) { s32 v = 0; int ii; if (jnum(&r, &v, &ii) < 0) return -1; R->queue_s = v > 0 ? (u32)v : 0u; }   /* float: whole seconds */
        else if (seq(k, "matchId")) { if (jstr(&r, R->match_id, sizeof R->match_id) < 0) return -1; }
        else if (seq(k, "isHost")) { if (jbool(&r, &R->is_host) < 0) return -1; }
        else if (seq(k, "items")) { s32 v; int ii; if (jnum(&r, &v, &ii) < 0 || !ii || v < 0) { r.bad = 1; return -1; } R->items = (u32)v; }
        else if (seq(k, "stages")) {
            if (!expect(&r, '[')) return -1;
            if (peek(&r) == ']') r.p++;
            else for (;;) {
                s32 v; int ii;
                if (jnum(&r, &v, &ii) < 0 || !ii || v < 0 || v > 0xFFFF) { r.bad = 1; return -1; }
                if (R->nstages < SBR_STAGES_MAX) R->stages[R->nstages++] = (u16)v;
                if (peek(&r) == ',') { r.p++; continue; }
                if (!expect(&r, ']')) return -1;
                break;
            }
        } else if (seq(k, "players")) {
            if (!expect(&r, '[')) return -1;
            if (peek(&r) == ']') r.p++;
            else for (;;) {
                struct sbr_player tmp;
                if (parse_player(&r, R->nplayers < SBR_PLAYERS_MAX ? &R->pl[R->nplayers] : &tmp) < 0) return -1;
                if (R->nplayers < SBR_PLAYERS_MAX) R->nplayers++;
                R->players_seen++;
                if (peek(&r) == ',') { r.p++; continue; }
                if (!expect(&r, ']')) return -1;
                break;
            }
        } else if (jskip(&r, 1) < 0) return -1;
        if (peek(&r) == ',') { r.p++; continue; }
        if (!expect(&r, '}')) return -1;
        break;
    }
end:
    if (peek(&r) != -1) return -1;                     /* nothing after the object */
    return r.bad ? -1 : 0;
}

static int parse_addr(const char *s, u32 *ip, u16 *port)   /* "a.b.c.d:port" */
{
    u32 v = 0, k, n, i = 0;
    for (k = 0; k < 4; k++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        n = 0;
        while (s[i] >= '0' && s[i] <= '9') { n = n * 10u + (u32)(s[i] - '0'); if (n > 255u) return -1; i++; }
        v = (v << 8) | n;
        if (s[i] != (k < 3 ? '.' : ':')) return -1;
        i++;
    }
    n = 0;
    if (s[i] < '0' || s[i] > '9') return -1;
    while (s[i] >= '0' && s[i] <= '9') { n = n * 10u + (u32)(s[i] - '0'); if (n > 65535u) return -1; i++; }
    if (s[i] || n == 0) return -1;
    *ip = v; *port = (u16)n;
    return 0;
}
static u32 host_part(const char *s, char *out, u32 cap)   /* the text before ':' (the stock client compares strings) */
{
    u32 i;
    for (i = 0; s[i] && s[i] != ':' && i + 1 < cap; i++) out[i] = s[i];
    out[i] = 0;
    return i;
}


/* Used by the WebSocket matchmaker client (sbr_ws.c, in ms/rep.ovl): validate a match body and fill the match record.
   0 = ok, else SBR_E_RANKED / SBR_E_PROTO with *text set (the caller shows it through the R3 notice path). */
int sbr_assign_parse(const struct sbr_reply *R, u16 local_port, u32 mseq, struct sbr_match *M, const char **text)
{
    const struct sbr_player *loc = 0, *rem = 0;
    char lext[24], rext[24];
    u32 i, nloc = 0, ip = 0;
    u16 port = 0;
    int lan = 0;
    if (contains(R->match_id, "mode.ranked")) { *text = "Ranked match refused: this client never plays Ranked"; return SBR_E_RANKED; }   /* R6 */
    if (R->players_seen != 2 || R->nplayers != 2) { *text = "Matchmaking sent an unexpected match."; return SBR_E_PROTO; }
    for (i = 0; i < 2; i++) if (R->pl[i].local) { loc = &R->pl[i]; nloc++; }
    if (nloc != 1 || loc->port < 1 || loc->port > 2) { *text = "Matchmaking sent an unexpected match."; return SBR_E_PROTO; }
    for (i = 0; i < 2; i++) if (&R->pl[i] != loc && R->pl[i].port - 1 != loc->port - 1) rem = &R->pl[i];
    if (!rem || rem->port < 1 || rem->port > 2) { *text = "Matchmaking sent an unexpected match."; return SBR_E_PROTO; }
    /* stock rule (M:582-609): external address unless it has the same IP as our own external one, then the LAN one */
    host_part(loc->ip[0] ? loc->ip : "1.1.1.1:123", lext, sizeof lext);
    host_part(rem->ip[0] ? rem->ip : "1.1.1.1:123", rext, sizeof rext);
    if (!seq(lext, rext) || !rem->has_lan || !rem->lan[0]) {
        if (parse_addr(rem->ip, &ip, &port) < 0) { *text = "Matchmaking sent a bad opponent address."; return SBR_E_PROTO; }
    } else {
        if (parse_addr(rem->lan, &ip, &port) < 0) { *text = "Matchmaking sent a bad opponent address."; return SBR_E_PROTO; }
        lan = 1;
    }
    zero(M, sizeof *M);
    M->used_lan = (u32)lan;
    M->seq = mseq;
    M->is_host = R->is_host ? 1u : 0u;
    M->local_idx = (u32)(loc->port - 1);
    M->opp_ip = ip;
    M->opp_port = port;
    M->local_port = local_port;
    M->opp_is_bot = rem->bot ? 1u : 0u;
    M->items = R->items;
    M->nstages = R->nstages;
    for (i = 0; i < R->nstages; i++) M->stages[i] = R->stages[i];
    if (!M->nstages) {                                 /* stock default pool for two players (M:625-640) */
        static const u16 pool[6] = { 0x3, 0x8, 0x1C, 0x1F, 0x20, 0x2 };
        for (i = 0; i < 6; i++) M->stages[i] = pool[i];
        M->nstages = 6;
    }
    scpy(M->opp_code, rem->code, sizeof M->opp_code);
    scpy(M->opp_name, rem->name, sizeof M->opp_name);
    scpy(M->match_id, R->match_id, sizeof M->match_id);
    scpy(M->opp_uid, rem->uid, sizeof M->opp_uid);
    return 0;
}

