/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* codehist: see codehist.h.  Strict C89, soft-float safe (integers only), no libc symbols, fixed buffers, no globals. */
#include "codehist.h"

typedef unsigned char u8;
typedef unsigned int u32;

/* CH_AT (the hub images: on, ond, od, onb, onbc): a TAG@NNN code of the Multishine universe is a code too, '@' = full-width SJIS 0x8197.  Without it (online-menu, image 1, menu layout) the
   file is exactly what it was, so those images keep their ids. */
#ifdef CH_AT
#define CH_MARK(c) ((c) == '#' || (c) == '@')
#else
#define CH_MARK(c) ((c) == '#')
#endif

u32 ch_crc32(const u8 *p, u32 len)
{
    u32 c = 0xFFFFFFFFu, i;
    int k;
    for (i = 0; i < len; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
    }
    return ~c;
}

static u32 rd32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3]; }
static void wr32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_upper(int c) { return c >= 'A' && c <= 'Z'; }

/* one stored/typed character in ASCII: upper-cased; 0 when it is outside [A-Z0-9#] ([A-Z0-9#@] with CH_AT) */
static char ascii_char(int c)
{
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    return (char)((is_upper(c) || is_digit(c) || CH_MARK(c)) ? c : 0);
}

/* two Shift-JIS bytes (full-width # digits letters) -> ASCII upper case, 0 = not a code character */
static char sjis_char(unsigned int b0, unsigned int b1)
{
    if (b0 == 0x81u && b1 == 0x94u) return '#';
#ifdef CH_AT
    if (b0 == 0x81u && b1 == 0x97u) return '@';
#endif
    if (b0 == 0x82u) {
        if (b1 >= 0x4Fu && b1 <= 0x58u) return (char)('0' + (b1 - 0x4Fu));
        if (b1 >= 0x60u && b1 <= 0x79u) return (char)('A' + (b1 - 0x60u));
        if (b1 >= 0x81u && b1 <= 0x9Au) return (char)('A' + (b1 - 0x81u));
    }
    return 0;
}

static unsigned int slen(const char *s)
{
    unsigned int n = 0;
    while (n <= CH_LEN && s[n]) n++;
    return n;
}

int ch_valid(const char *s)
{
    unsigned int i, n = slen(s), hash = 0, at = 0;
    if (n < 1 || n > CH_LEN) return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (!(is_upper(c) || is_digit(c) || CH_MARK(c))) return 0;
        if (CH_MARK(c)) { hash++; at = i; }
    }
    return hash == 1 && at >= 1 && at + 1 < n;
}

unsigned int ch_from_melee(const u8 *b, u32 len, char *out)
{
    u32 i = 0, n = 0;
    out[0] = 0;
    while (i < len && b[i]) {
        char c;
        if (b[i] < 0x80u) {
            c = ascii_char(b[i]);
            i += 1;
        } else {
            if (i + 1 >= len) { out[0] = 0; return 0; }
            c = sjis_char(b[i], b[i + 1]);
            i += 2;
        }
        if (!c || n >= CH_LEN) { out[0] = 0; return 0; }
        out[n++] = c;
        out[n] = 0;
    }
    if (!ch_valid(out)) { out[0] = 0; return 0; }
    return n;
}

void ch_to_melee(const char *code, u8 *out)
{
    u32 i;
    for (i = 0; i < 24; i++) out[i] = 0;
    for (i = 0; i < CH_LEN && code[i]; i++) {
        char c = code[i];
        if (c == '#') { out[3 * i] = 0x81; out[3 * i + 1] = 0x94; }
#ifdef CH_AT
        else if (c == '@') { out[3 * i] = 0x81; out[3 * i + 1] = 0x97; }
#endif
        else if (is_digit(c)) { out[3 * i] = 0x82; out[3 * i + 1] = (u8)(0x4F + (c - '0')); }
        else { out[3 * i] = 0x82; out[3 * i + 1] = (u8)(0x60 + (c - 'A')); }
    }
}

static int same(const char *a, const char *b)
{
    u32 i;
    for (i = 0; i <= CH_LEN; i++) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
    }
    return 1;
}

static void copy_code(char *d, const char *s)
{
    u32 i;
    for (i = 0; i < CH_LEN && s[i]; i++) d[i] = s[i];
    for (; i <= CH_LEN; i++) d[i] = 0;
}

void ch_init(struct ch_hist *h)
{
    u32 i, k;
    h->seq = 0;
    h->n = 0;
    for (i = 0; i < CH_MAX; i++) for (k = 0; k <= CH_LEN; k++) h->code[i][k] = 0;
}

static int find(const struct ch_hist *h, const char *code)
{
    u32 i;
    for (i = 0; i < h->n; i++) if (same(h->code[i], code)) return (int)i;
    return -1;
}

int ch_add(struct ch_hist *h, const char *code)
{
    int at;
    u32 i;
    char tmp[CH_LEN + 1];
    if (!ch_valid(code)) return -1;
    copy_code(tmp, code);
    at = find(h, tmp);
    if (at == 0) return 0;
    if (at < 0) {
        at = (int)(h->n < CH_MAX ? h->n : CH_MAX - 1);      /* a new entry takes the free slot or replaces the oldest */
        if (h->n < CH_MAX) h->n++;
    }
    for (i = (u32)at; i > 0; i--) copy_code(h->code[i], h->code[i - 1]);
    copy_code(h->code[0], tmp);
    return 1;
}

#ifdef CH_AT
char ch_sep(const char *code)
{
    u32 i;
    if (!ch_valid(code)) return 0;
    for (i = 0; code[i]; i++) if (code[i] == '#' || code[i] == '@') return code[i];
    return 0;
}

static u32 ns_count(const struct ch_hist *h, char sep)
{
    u32 i, n = 0;
    for (i = 0; i < h->n; i++) if (ch_sep(h->code[i]) == sep) n++;
    return n;
}

/* index of the oldest entry of namespace sep, -1 when none */
static int ns_oldest(const struct ch_hist *h, char sep)
{
    int i;
    for (i = (int)h->n - 1; i >= 0; i--) if (ch_sep(h->code[i]) == sep) return i;
    return -1;
}

static void drop_at(struct ch_hist *h, int at)
{
    u32 i;
    for (i = (u32)at; i + 1 < h->n; i++) copy_code(h->code[i], h->code[i + 1]);
    for (i = 0; i <= CH_LEN; i++) h->code[h->n - 1][i] = 0;
    h->n--;
}

int ch_add_ns(struct ch_hist *h, const char *code, char sep)
{
    char tmp[CH_LEN + 1];
    int at;
    if (!sep || ch_sep(code) != sep) return -1;
    copy_code(tmp, code);
    if (find(h, tmp) < 0) {                              /* a new entry: make room within the namespace rules */
        if (ns_count(h, sep) >= CH_NS_MAX) drop_at(h, ns_oldest(h, sep));
        else if (h->n >= CH_MAX) {
            at = ns_oldest(h, (char)(sep == '#' ? '@' : '#'));
            drop_at(h, at >= 0 ? at : (int)h->n - 1);
        }
    }
    return ch_add(h, tmp);
}

void ch_filter(struct ch_hist *dst, const struct ch_hist *src, char sep)
{
    u32 i;
    ch_init(dst);
    dst->seq = src->seq;
    for (i = 0; i < src->n; i++)
        if (ch_sep(src->code[i]) == sep) copy_code(dst->code[dst->n++], src->code[i]);
}
#endif

u32 ch_merge(struct ch_hist *dst, const struct ch_hist *src)
{
    u32 i, added = 0;
    for (i = 0; i < src->n && dst->n < CH_MAX; i++) {
        if (find(dst, src->code[i]) < 0) {
            copy_code(dst->code[dst->n++], src->code[i]);
            added++;
        }
    }
    return added;
}

int ch_check(const u8 *rec)
{
    u32 n, i, k;
    struct ch_hist seen;
    if (rd32(rec) != CH_MAGIC) return CH_DAMAGED;
    if (rec[4] == 0) return CH_DAMAGED;
    if (rec[4] > CH_VERSION) return CH_NEWER;
    n = rec[5];
    if (n > CH_MAX) return CH_DAMAGED;
    if (rd32(rec + 8) != ch_crc32(rec + 0x0C, CH_REC_SIZE - 0x0C)) return CH_DAMAGED;
    ch_init(&seen);
    for (i = 0; i < CH_MAX; i++) {
        const u8 *e = rec + 0x10 + CH_LEN * i;
        if (i >= n) {
            for (k = 0; k < CH_LEN; k++) if (e[k]) return CH_DAMAGED;
            continue;
        }
        for (k = 0; k < CH_LEN; k++) seen.code[i][k] = (char)e[k];
        seen.code[i][CH_LEN] = 0;
        if (!ch_valid(seen.code[i])) return CH_DAMAGED;
        for (k = 0; k < i; k++) if (same(seen.code[k], seen.code[i])) return CH_DAMAGED;
    }
    return CH_OK;
}

void ch_decode(struct ch_hist *h, const u8 *rec)
{
    u32 i, k, n = rec[5];
    ch_init(h);
    if (n > CH_MAX) n = CH_MAX;
    h->seq = rd32(rec + 0x0C);
    h->n = n;
    for (i = 0; i < n; i++) {
        for (k = 0; k < CH_LEN; k++) h->code[i][k] = (char)rec[0x10 + CH_LEN * i + k];
        h->code[i][CH_LEN] = 0;
    }
}

void ch_encode(u8 *rec, const struct ch_hist *h)
{
    u32 i, k, n = h->n > CH_MAX ? CH_MAX : h->n;
    wr32(rec, CH_MAGIC);
    rec[4] = CH_VERSION;
    rec[5] = (u8)n;
    rec[6] = 0;
    rec[7] = 0;
    wr32(rec + 0x0C, h->seq);
    for (i = 0; i < CH_MAX; i++)
        for (k = 0; k < CH_LEN; k++) rec[0x10 + CH_LEN * i + k] = (u8)(i < n ? h->code[i][k] : 0);
    wr32(rec + 8, ch_crc32(rec + 0x0C, CH_REC_SIZE - 0x0C));
}

void ch_new_block(u8 *block, const char *line1, const char *line2, const struct ch_hist *h)
{
    u32 i;
    for (i = 0; i < CH_BLOCK; i++) block[i] = 0;
    for (i = 0; i + 1 < CH_COMMENT_LEN && line1[i]; i++) block[i] = (u8)line1[i];
    for (i = 0; i + 1 < CH_COMMENT_LEN && line2[i]; i++) block[CH_COMMENT_LEN + i] = (u8)line2[i];
    ch_encode(block + CH_PAYLOAD_OFF, h);
}

/* does the typed text (first n characters) match the stored code?  A code shorter than the text never matches. */
static int matches(const u8 *input, u32 n, const char *code)
{
    u32 i, len = slen(code);
    if (n > len) return 0;
    for (i = 0; i < n; i++) {
        char c = sjis_char(input[3 * i], input[3 * i + 1]);
        if (!c && input[3 * i] < 0x80u && !input[3 * i + 1]) c = ascii_char(input[3 * i]);
        if (!c || c != code[i]) return 0;
    }
    return 1;
}

int ch_answer(const struct ch_hist *h, const u8 *req, u8 *rsp)
{
    const u8 *input = req + 1;
    u32 n = req[25], initial = rd32(req + 26), dir = req[30], cur, count, i;
    int hit = 0;
    for (i = 0; i < 30; i++) rsp[i] = 0;
    if (n > CH_LEN) n = CH_LEN;
    count = (req[31] == CH_MODE_DIRECT) ? h->n : 0;    /* Slippi keeps a second list for Teams: this image has none */
    cur = initial;
    if (dir == CH_SCROLL_OLDER) cur++;
    else if (dir == CH_SCROLL_NEWER) cur = cur > 0 ? cur - 1 : cur;
    else if (dir == CH_SCROLL_RESET) cur = 0;
    while (cur < count) {                                /* unsigned: going newer from 0 wraps and ends the loop, as in Slippi */
        if (matches(input, n, h->code[cur])) { hit = 1; break; }
        cur = (dir == CH_SCROLL_NEWER) ? cur - 1 : cur + 1;
    }
    if (!hit && initial < count && matches(input, n, h->code[initial])) { cur = initial; hit = 1; }
    if (!hit) {                                          /* no suggestion: the typed characters come back, the rest zero */
        for (i = 0; i < 3 * n; i++) rsp[1 + i] = input[i];
        rsp[25] = (u8)n;
        wr32(rsp + 26, initial);
        return 0;
    }
    rsp[0] = 1;
    ch_to_melee(h->code[cur], rsp + 1);
    rsp[25] = (u8)slen(h->code[cur]);
    wr32(rsp + 26, cur);
    return 1;
}
