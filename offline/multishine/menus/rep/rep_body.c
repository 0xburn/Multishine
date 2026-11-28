/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* rep_body: the reportOnlineGame request bytes, the GraphQL answer parser, the HELLO clock (9).
   The JSON follows the reference client's serialisation (slippi-rust-extensions game-reporter types.rs / queue.rs, an earlier revision): same query text,
   same field names and order.  Differences, all on purpose: damageDone has at most three decimals (integer math from the engine's float32; the console
   has no float code), and every string is a plain token ([A-Za-z0-9._:-]) checked before it is written, so nothing needs escaping. */
#include "rep.h"

static rep_u32 slen(const char *s) { rep_u32 n = 0; while (s[n]) n++; return n; }

/* ---------------------------------------------------------------- output helpers */
struct ob { rep_u8 *p; rep_u32 cap, n; int bad; };
static void o_raw(struct ob *o, const char *s)
{
    rep_u32 n = slen(s), i;
    if (o->n + n > o->cap) { o->bad = 1; return; }
    for (i = 0; i < n; i++) o->p[o->n + i] = (rep_u8)s[i];
    o->n += n;
}
static void o_uint(struct ob *o, rep_u32 v)
{
    char t[12];
    int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    if (o->n + (rep_u32)n > o->cap) { o->bad = 1; return; }
    while (n) o->p[o->n++] = (rep_u8)t[--n];
}
static void o_int(struct ob *o, int v)
{
    if (v < 0) { o_raw(o, "-"); o_uint(o, (rep_u32)(-v)); } else o_uint(o, (rep_u32)v);
}
static int plain(const char *s, rep_u32 max)
{
    rep_u32 i;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        if (i >= max) return 0;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' || c == '-')) return 0;
    }
    return 1;
}
static void o_strn(struct ob *o, const char *s, rep_u32 max)
{
    if (!plain(s, max)) { o->bad = 1; return; }
    o_raw(o, "\""); o_raw(o, s); o_raw(o, "\"");
}
static void o_str(struct ob *o, const char *s) { o_strn(o, s, REP_ID_MAX); }

static void o_milli(struct ob *o, rep_u32 milli)
{
    rep_u32 ip = milli / 1000u, fp = milli % 1000u;
    o_uint(o, ip);
    o_raw(o, ".");
    if (fp == 0) { o_raw(o, "0"); return; }
    o_uint(o, fp / 100u);
    if (fp % 100u == 0) return;
    o_uint(o, (fp / 10u) % 10u);
    if (fp % 10u == 0) return;
    o_uint(o, fp % 10u);
}

/* ---------------------------------------------------------------- the request */
#define HEAD_RES 512u
rep_u32 rep_build_request(rep_u8 *out, rep_u32 cap, const struct rep_rec *rec, const char *uid, const char *key, const char *iso_hash,
                          rep_u32 build, const char *platform)
{
    struct ob o, h;
    rep_u8 head[HEAD_RES];
    rep_u32 i, blen, hlen;
    int first = 1;

    if (cap <= HEAD_RES + 64u) return 0;
    o.p = out + HEAD_RES; o.cap = cap - HEAD_RES; o.n = 0; o.bad = 0;
    (void)uid; (void)iso_hash;                                    /* LOCAL FORK: no Slippi identity leaves the console; the key says who we are */
    o_raw(&o, "{\"version\":1,\"matchId\":"); o_strn(&o, rec->match_id, REP_MID_MAX);
    o_raw(&o, ",\"mode\":"); o_uint(&o, rec->mode);
    o_raw(&o, ",\"opponentCode\":"); o_str(&o, rec->opp_uid);      /* opp_uid holds the connect code with '-' for '#' (br_glue.c d_bd) */
    o_raw(&o, ",\"players\":[");
    for (i = 0; i < rec->np && i < 4u; i++) {
        const struct rep_player *p = &rec->pl[i];
        if (p->who > 1) continue;
        if (!first) o_raw(&o, ",");
        first = 0;
        o_raw(&o, "{\"self\":"); o_raw(&o, p->who == 0 ? "true" : "false");
        o_raw(&o, ",\"damageDone\":"); o_milli(&o, p->damage_milli);
        o_raw(&o, ",\"stocksRemaining\":"); o_uint(&o, p->stocks);
        o_raw(&o, ",\"characterId\":"); o_uint(&o, p->char_id);
        o_raw(&o, ",\"colorId\":"); o_uint(&o, p->color_id);
        o_raw(&o, ",\"startingStocks\":"); o_uint(&o, p->start_stocks);
        o_raw(&o, "}");
    }
    o_raw(&o, "],\"gameDurationFrames\":"); o_uint(&o, rec->frames);
    o_raw(&o, ",\"gameIndex\":"); o_uint(&o, rec->game);
    o_raw(&o, ",\"tiebreakIndex\":"); o_uint(&o, rec->tiebreak);
    o_raw(&o, ",\"winnerIndex\":"); o_int(&o, rec->winner);
    o_raw(&o, ",\"gameEndMethod\":"); o_uint(&o, rec->endm);
    o_raw(&o, ",\"lrasInitiator\":"); o_int(&o, rec->lras);
    o_raw(&o, ",\"stageId\":"); o_uint(&o, rec->stage);
    o_raw(&o, "}");
    if (o.bad) return 0;
    blen = o.n;
    if (!plain(platform ? platform : "", 16)) return 0;

    h.p = head; h.cap = HEAD_RES; h.n = 0; h.bad = 0;
    o_raw(&h, "POST " REP_PATH " HTTP/1.1\r\nHost: " REP_HOST "\r\nUser-Agent: " REP_CLIENT_NAME " (b: "); o_uint(&h, build);
    o_raw(&h, ") (v: " REP_SLIPPI_VERSION ") (o: "); o_raw(&h, platform ? platform : "console");
    o_raw(&h, ")\r\nX-App-Id: multishine\r\nAuthorization: Bearer ");
    if (!plain(key, REP_KEY_MAX) || slen(key) != REP_KEY_MAX) return 0;
    o_raw(&h, key); o_raw(&h, "\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: keep-alive\r\nContent-Type: application/json\r\nContent-Length: ");
    o_uint(&h, blen); o_raw(&h, "\r\n\r\n");
    if (h.bad) return 0;
    hlen = h.n;
    for (i = 0; i < blen; i++) out[hlen + i] = out[HEAD_RES + i];     /* forward copy to a lower address: safe in place */
    for (i = 0; i < hlen; i++) out[i] = head[i];
    { volatile rep_u8 *z = head; for (i = 0; i < HEAD_RES; i++) z[i] = 0; }      /* the key was in here */
    return hlen + blen;
}

