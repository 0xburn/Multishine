/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* s8_json: see s8_json.h.  Project-owned; written for the lab lobby protocol of the private lobby (our own server). */
#include "s8_json.h"

/* ------------------------------------------------------------------ writer */
void s8_jw_init(struct s8_jw *w, char *buf, s8_u32 cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = 0;
    if (cap) buf[0] = 0;
}

static void put(struct s8_jw *w, char c)
{
    if (w->len + 1 >= w->cap) { w->overflow = 1; return; }
    w->buf[w->len++] = c;
    w->buf[w->len] = 0;
}

void s8_jw_raw(struct s8_jw *w, const char *s)
{
    while (*s) put(w, *s++);
}

void s8_jw_str(struct s8_jw *w, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    put(w, '"');
    for (; *s; s++) {
        s8_u8 c = (s8_u8)*s;
        if (c == '"' || c == '\\') { put(w, '\\'); put(w, (char)c); }
        else if (c < 0x20 || c >= 0x7F) {      /* only ASCII is ever sent; anything else is escaped */
            put(w, '\\'); put(w, 'u'); put(w, '0'); put(w, '0');
            put(w, hex[c >> 4]); put(w, hex[c & 15]);
        } else put(w, (char)c);
    }
    put(w, '"');
}

void s8_jw_uint(struct s8_jw *w, s8_u32 v)
{
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 11);
    while (n) put(w, t[--n]);
}

/* ------------------------------------------------------------------ reader */
struct st { const s8_u8 *p; s8_u32 len, pos; };

enum { T_NONE, T_STR, T_INT, T_BOOL };
struct target { int kind; char *s; s8_u32 cap; s8_s32 *i; };
enum { O_ROOT, O_PLAYER, O_OTHER };

static int peek(struct st *s) { return s->pos < s->len ? s->p[s->pos] : -1; }
static void ws(struct st *s)
{
    while (s->pos < s->len) {
        s8_u8 c = s->p[s->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        s->pos++;
    }
}
static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* at '"'; copies (truncated, NUL-terminated) into dst if dst != 0 */
static int str(struct st *s, char *dst, s8_u32 cap)
{
    s8_u32 n = 0;
    if (peek(s) != '"') return -1;
    s->pos++;
    for (;;) {
        int c = peek(s), o;
        if (c < 0) return -1;
        s->pos++;
        if (c == '"') break;
        if (c < 0x20) return -1;
        if (c == '\\') {
            int e = peek(s);
            if (e < 0) return -1;
            s->pos++;
            switch (e) {
            case '"': case '\\': case '/': o = e; break;
            case 'b': o = 8; break;
            case 'f': o = 12; break;
            case 'n': o = 10; break;
            case 'r': o = 13; break;
            case 't': o = 9; break;
            case 'u': {
                int k, v = 0;
                for (k = 0; k < 4; k++) {
                    int h = hexv(peek(s));
                    if (h < 0) return -1;
                    v = v * 16 + h;
                    s->pos++;
                }
                o = v < 0x80 ? v : '?';
                break;
            }
            default: return -1;
            }
        } else {
            o = c < 0x80 ? c : '?';                /* the console only shows ASCII */
        }
        if (dst && n + 1 < cap) dst[n++] = (char)o;
    }
    if (dst && cap) dst[n] = 0;
    return 0;
}

static int num(struct st *s, s8_s32 *out)
{
    s8_s32 v = 0;
    int neg = 0, digits = 0;
    if (peek(s) == '-') { neg = 1; s->pos++; }
    while (peek(s) >= '0' && peek(s) <= '9') {
        if (v < 100000000) v = v * 10 + (peek(s) - '0');
        s->pos++;
        digits++;
    }
    if (!digits) return -1;
    if (peek(s) == '.') { s->pos++; if (!(peek(s) >= '0' && peek(s) <= '9')) return -1; while (peek(s) >= '0' && peek(s) <= '9') s->pos++; }
    if (peek(s) == 'e' || peek(s) == 'E') {
        s->pos++;
        if (peek(s) == '+' || peek(s) == '-') s->pos++;
        if (!(peek(s) >= '0' && peek(s) <= '9')) return -1;
        while (peek(s) >= '0' && peek(s) <= '9') s->pos++;
    }
    if (out) *out = neg ? -v : v;
    return 0;
}

static int lit(struct st *s, const char *w)
{
    while (*w) {
        if (peek(s) != *w) return -1;
        s->pos++;
        w++;
    }
    return 0;
}

static int value(struct st *s, int depth, struct target *t, int okind, struct s8_reply *r, int arr_players);

static int keyeq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static int object(struct st *s, int depth, int okind, struct s8_reply *r)
{
    struct s8_player *pl = 0;
    if (depth > S8_JSON_MAX_DEPTH) return -1;
    if (okind == O_PLAYER) {
        if (r->nplayers >= S8_PLAYERS_MAX) okind = O_OTHER;
        else { pl = &r->pl[r->nplayers++]; pl->local = -1; pl->port = -1; }
    }
    s->pos++;                                   /* '{' */
    ws(s);
    if (peek(s) == '}') { s->pos++; return 0; }
    for (;;) {
        char key[24];
        struct target t;
        int players = 0;
        ws(s);
        if (str(s, key, sizeof key) < 0) return -1;
        ws(s);
        if (peek(s) != ':') return -1;
        s->pos++;
        t.kind = T_NONE; t.s = 0; t.cap = 0; t.i = 0;
        if (okind == O_ROOT) {
            if (keyeq(key, "type")) { t.kind = T_STR; t.s = r->type; t.cap = sizeof r->type; }
            else if (keyeq(key, "errorCode")) { t.kind = T_STR; t.s = r->error_code; t.cap = sizeof r->error_code; }
            else if (keyeq(key, "error")) { t.kind = T_STR; t.s = r->error; t.cap = sizeof r->error; }
            else if (keyeq(key, "matchId")) { t.kind = T_STR; t.s = r->match_id; t.cap = sizeof r->match_id; }
            else if (keyeq(key, "serverName")) { t.kind = T_STR; t.s = r->server_name; t.cap = sizeof r->server_name; }
            else if (keyeq(key, "isAssigned")) { t.kind = T_BOOL; t.i = &r->is_assigned; }
            else if (keyeq(key, "isHost")) { t.kind = T_BOOL; t.i = &r->is_host; }
            else if (keyeq(key, "queued")) { t.kind = T_BOOL; t.i = &r->queued; }
            else if (keyeq(key, "players")) players = 1;
        } else if (okind == O_PLAYER) {
            if (keyeq(key, "displayName")) { t.kind = T_STR; t.s = pl->name; t.cap = sizeof pl->name; }
            else if (keyeq(key, "connectCode")) { t.kind = T_STR; t.s = pl->code; t.cap = sizeof pl->code; }
            else if (keyeq(key, "ipAddress")) { t.kind = T_STR; t.s = pl->ip; t.cap = sizeof pl->ip; }
            else if (keyeq(key, "port")) { t.kind = T_INT; t.i = &pl->port; }
            else if (keyeq(key, "isLocalPlayer")) { t.kind = T_BOOL; t.i = &pl->local; }
        }
        ws(s);
        if (value(s, depth, &t, O_OTHER, r, players) < 0) return -1;
        ws(s);
        if (peek(s) == ',') { s->pos++; continue; }
        if (peek(s) == '}') { s->pos++; return 0; }
        return -1;
    }
}

static int array(struct st *s, int depth, struct s8_reply *r, int players)
{
    struct target none;
    if (depth > S8_JSON_MAX_DEPTH) return -1;
    none.kind = T_NONE; none.s = 0; none.cap = 0; none.i = 0;
    s->pos++;                                   /* '[' */
    ws(s);
    if (peek(s) == ']') { s->pos++; return 0; }
    for (;;) {
        ws(s);
        if (value(s, depth, &none, players ? O_PLAYER : O_OTHER, r, 0) < 0) return -1;
        ws(s);
        if (peek(s) == ',') { s->pos++; continue; }
        if (peek(s) == ']') { s->pos++; return 0; }
        return -1;
    }
}

static int value(struct st *s, int depth, struct target *t, int okind, struct s8_reply *r, int arr_players)
{
    int c = peek(s);
    if (c == '{') return object(s, depth + 1, okind, r);
    if (c == '[') return array(s, depth + 1, r, arr_players);
    if (c == '"') return str(s, t->kind == T_STR ? t->s : 0, t->cap);
    if (c == 't') { if (lit(s, "true") < 0) return -1; if (t->kind == T_BOOL) *t->i = 1; return 0; }
    if (c == 'f') { if (lit(s, "false") < 0) return -1; if (t->kind == T_BOOL) *t->i = 0; return 0; }
    if (c == 'n') return lit(s, "null");
    if (c == '-' || (c >= '0' && c <= '9')) return num(s, t->kind == T_INT ? t->i : 0);
    return -1;
}

int s8_json_parse_reply(const s8_u8 *js, s8_u32 len, struct s8_reply *out)
{
    struct st s;
    s8_u32 i;
    s8_u8 *z = (s8_u8 *)out;
    for (i = 0; i < sizeof *out; i++) z[i] = 0;
    out->is_assigned = out->is_host = out->queued = -1;
    s.p = js; s.len = len; s.pos = 0;
    ws(&s);
    if (peek(&s) != '{') return -1;
    if (object(&s, 1, O_ROOT, out) < 0) return -1;
    ws(&s);
    return s.pos == s.len ? 0 : -1;
}
