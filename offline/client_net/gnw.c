/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* gnw: the "Watch Last Game" client, see gnw.h.  C89, no libc, no heap, never blocks. */
#include "gnw.h"

static mnc_u32 g32(const mnc_u8 *p) { return ((mnc_u32) p[0] << 24) | ((mnc_u32) p[1] << 16) | ((mnc_u32) p[2] << 8) | p[3]; }
static void p32(mnc_u8 *p, mnc_u32 v) { p[0] = (mnc_u8) (v >> 24); p[1] = (mnc_u8) (v >> 16); p[2] = (mnc_u8) (v >> 8); p[3] = (mnc_u8) v; }

void gnw_init(gnw *g)
{
    mnc_u8 *b = (mnc_u8 *) g;
    mnc_u32 i;
    for (i = 0; i < sizeof *g; i++) b[i] = 0;
}

const char *gnw_why_text(unsigned why)
{
    switch (why) {
    case GNW_W_NOSESSION: return "Not connected to the server";
    case GNW_W_TIMEOUT: return "The server did not answer";
    case GNW_W_NOGAME: return "No game to watch right now";
    case GNW_W_NOSUPPORT: return "Watching is not available";
    case GNW_W_FULL: return "This game has enough watchers";
    case GNW_W_RATE: return "Try again in a moment";
    case GNW_W_BAD: return "Could not start watching. Try again.";
    case GNW_W_DESC: return "This game can't be watched";
    case GNW_W_STADIUM: return "Stadium games can't be watched yet";
    case GNW_W_SILENT: return "The stream stopped";
    case GNW_W_SYNC: return "Lost sync with the game";
    default: return "Can't watch this game";
    }
}

static void fail(gnw *g, unsigned why) { if (g->state != GNW_FAIL) { g->state = GNW_FAIL; g->why = (mnc_u8) why; } }

static void send_req(gnw *g, mw2 *w, const mnc_io *io, mnc_u8 op, const mnc_u8 *pl, mnc_u32 plen, mnc_u32 now, int keep)
{
    mnc_u32 i, n;
    for (i = 0; i < plen; i++) g->pkt[MW2_HDR + i] = pl[i];
    n = mw2_seal(w, g->pkt, op, plen);
    if (keep) { g->pkt_len = n; g->ctr = w->ctr; g->op = op; g->sent_ms = now; g->tries = 1; }
    (void) io->send(io->ctx, g->pkt, n);
}

static int begin_ex(gnw *g, mw2 *w, const mnc_io *io, mnc_u32 now, unsigned live)
{
    static const mnc_u8 pl_last[2] = { GNW_VER, 1u };      /* GN_SPEC: version, action A_LAST (the room's kept last game) */
    static const mnc_u8 pl_live[2] = { GNW_VER, 2u };      /* ... action A_LIVE (the game that is still being played) */
    const mnc_u8 *pl = live ? pl_live : pl_last;
    gnw_init(g);
    if (!w || !mw2_ready(w, now)) { fail(g, GNW_W_NOSESSION); return GNW_W_NOSESSION; }
    g->state = GNW_SPEC;
    g->last_new_ms = now;
    send_req(g, w, io, GNW_OP_SPEC, pl, 2u, now, 1);
    return 0;
}

int gnw_begin_friend(gnw *g, mw2 *w, const mnc_io *io, mnc_u32 now, const char *code)
{
    mnc_u8 pl[2 + 1 + 8];
    mnc_u32 n = 0, i;
    gnw_init(g);
    while (code && code[n] && n < 8u) n++;
    if (n == 0u) { fail(g, GNW_W_NOSESSION); return GNW_W_NOSESSION; }
    pl[0] = GNW_VER; pl[1] = 4u; pl[2] = (mnc_u8) n;                       /* GN_SPEC action A_FRIEND: the friend's connect code, p9 */
    for (i = 0; i < n; i++) pl[3 + i] = (mnc_u8) code[i];
    if (!w || !mw2_ready(w, now)) { fail(g, GNW_W_NOSESSION); return GNW_W_NOSESSION; }
    g->state = GNW_SPEC;
    g->last_new_ms = now;
    send_req(g, w, io, GNW_OP_SPEC, pl, 3u + n, now, 1);
    return 0;
}
int gnw_begin(gnw *g, mw2 *w, const mnc_io *io, mnc_u32 now) { return begin_ex(g, w, io, now, 0u); }
int gnw_begin_live(gnw *g, mw2 *w, const mnc_io *io, mnc_u32 now) { return begin_ex(g, w, io, now, 1u); }

int gnw_decline(gnw *g, mw2 *w, const mnc_io *io, mnc_u32 now)
{
    static const mnc_u8 pl[2] = { GNW_VER, 3u };                                /* GN_SPEC action A_DECLINE: the server applies its missed-call-up rule */
    if (!w || !mw2_ready(w, now)) return -1;
    g->called = 0;                                                              /* until the next RANGE reply says otherwise */
    send_req(g, w, io, GNW_OP_SPEC, pl, 2u, now, 0);
    return 0;
}
void gnw_leave(gnw *g) { g->leave_req = 1; }

/* ---- parsers: every length, count and flag checked ---- */
int gnw_parse_feed(const mnc_u8 *b, mnc_u32 n, mnc_u32 *set_id, mnc_u8 *game, mnc_u8 *digest, mnc_u32 *first, mnc_u8 *count, mnc_u8 *flags, const mnc_u8 **pads,
                   mnc_u8 *nchk, mnc_u32 *chk_f, mnc_u32 *chk_c)
{
    mnc_u32 i, c, k;
    if (n < 16u + 1u || b[0] != GNW_VER) return -1;
    *set_id = g32(b + 1); *game = b[5];
    for (i = 0; i < 4u; i++) digest[i] = b[6 + i];
    *first = g32(b + 10); *count = b[14]; *flags = b[15];
    if (*count > GNW_FEED_TAGS || (*flags & ~7u) || *first < 1u) return -1;
    c = 16u + (mnc_u32) *count * 16u;
    if (c + 1u > n) return -1;
    *pads = b + 16;
    *nchk = b[c];
    if (*nchk > GNW_FEED_CHK || c + 1u + (mnc_u32) *nchk * 8u != n) return -1;
    for (k = 0; k < *nchk; k++) { chk_f[k] = g32(b + c + 1u + k * 8u); chk_c[k] = g32(b + c + 5u + k * 8u); }
    return 0;
}

static int parse_spec(gnw *g, const mnc_u8 *b, mnc_u32 n)
{
    if (n != 17u || b[0] != GNW_VER) return -1;
    g->set_id = g32(b + 1); g->game = b[5]; g->frames = g32(b + 6); g->flags = b[10]; g->delay10 = b[11]; g->cap = b[12];
    g->digest[0] = b[13]; g->digest[1] = b[14]; g->digest[2] = b[15]; g->digest[3] = b[16];
    g->live = (mnc_u8) ((g->flags & GNW_SF_LIVE) ? 1u : 0u);
    g->rt_ask = (mnc_u8) ((g->flags & GNW_SF_RTYPE) ? 1u : 0u);        /* the game came from a Game Night room: ask HEAD for the room-type block (an older server never sets the bit, then the request stays the old one) */
    return (g->frames >= (g->live ? 0u : 1u)) && g->frames <= 36000u ? 0 : -1;
}

static int pstr12(const mnc_u8 *b, mnc_u32 n, mnc_u32 *i, char *out)
{
    mnc_u32 len, k;
    if (*i >= n) return -1;
    len = b[*i];
    if (len > 12u || *i + 1u + len > n) return -1;
    for (k = 0; k < len; k++) {
        if (b[*i + 1u + k] < 0x20u || b[*i + 1u + k] > 0x7Eu) return -1;
        out[k] = (char) b[*i + 1u + k];
    }
    out[len] = 0;
    *i += 1u + len;
    return 0;
}

/* the Stadium rule (SPECTATE_DESIGN 6): a Stadium game is watchable only as the frozen variant (a room with the Frozen policy: the uploader records altStageMode 1 in both selections), because the transformation path of an unfrozen one differs by console (F1 N1f/N1g);
   until the consoles apply the room's Frozen policy (P17 setup block), a Stadium game of a spectatable room was played unfrozen: refuse it */
static int stadium_bad(const gnw *g)
{
    mnc_u32 stage, alt;
#ifdef GNW_STADIUM_OK                              /* experiment: watch Stadium games, unfrozen too */
    (void)g; (void)stage; (void)alt;
    return 0;
#endif
    if (g->desc_flags & GNW_DF_WORDS) {
        stage = g->words[1] & 0xFFFFu; alt = (g->words[1] >> 16) & 0xFFu;
        return stage == 3u && alt != 1u;
    }
    if (g->desc_flags & GNW_DF_SEL) {
        /* a real game's Stadium is watchable, normal (transforming) and Frozen: a normal Stadium game replays exactly in Dolphin (play-then-watch, 6,387 frames,
           399 checksum compares, 0 mismatches); the server's room policy decides what is spectatable, not the client */
        return 0;
    }
    return 1;
}

static int parse_head(gnw *g, const mnc_u8 *b, mnc_u32 n)
{
    mnc_u32 i = 14u, k, w0;
    mnc_u8 fl;
    if (n < 14u + 32u + 3u || b[0] != GNW_VER) return -1;
    if (g32(b + 1) != g->set_id || b[5] != g->game) return -1;
    if (g->live) { if (g32(b + 6) > 36000u) return -1; if (g32(b + 6) > g->frames) g->frames = g32(b + 6); }       /* live: the head has grown since the SPEC reply */
    else if (g32(b + 6) != g->frames) return -1;
    for (k = 0; k < 4u; k++) if (b[10 + k] != g->digest[k]) return -1;
    if (n - 14u > GNW_DESC_MAX + GNW_RT_LEN) return -1;
    w0 = i;
    for (k = 0; k < 8u; k++) g->words[k] = g32(b + w0 + 4u * k);
    i = w0 + 32u;
    fl = b[i];
    g->desc_flags = fl;
    i += 3u;                                                    /* flags, port0, port1 */
    if ((fl & ~3u) || !(fl & 3u) || ((fl & GNW_DF_WORDS) && g->words[5] != GNW_DESC_MAGIC)) return -1;
    if (pstr12(b, n, &i, g->names[0]) || pstr12(b, n, &i, g->names[1])) return -1;
    if (fl & GNW_DF_SEL) {
        for (k = 0; k < 2u; k++) {
            mnc_u32 len, j;
            if (i >= n) return -1;
            len = b[i++];
            if (len != GNW_SEL_LEN || i + len > n) return -1;
            for (j = 0; j < len; j++) g->sel[k][j] = b[i + j];
            g->sel_n[k] = (mnc_u8) len;
            i += len;
        }
    }
    if (i - 14u > GNW_DESC_MAX) return -1;
    g->desc_len = i - 14u;
    for (k = 0; k < g->desc_len; k++) g->desc[k] = b[14u + k];
    if (i == n) return 0;
    if (g->rt_ask && n - i == GNW_RT_LEN && b[i] == 1u) {              /* the room-type block (ms_spec.pack_rtype): ver 1 | room type | game index | patch flags | items hint */
        g->rt_have = 1u; g->rt_type = b[i + 1u]; g->rt_game = b[i + 2u]; g->rt_patches = b[i + 3u]; g->rt_items = b[i + 4u];
        return 0;
    }
    return -1;
}

static unsigned map_status(unsigned st)
{
    switch (st) {
    case 16: return GNW_W_NOGAME;       /* NOT_FOUND */
    case 18: return GNW_W_NOSUPPORT;    /* NOT_SUPPORTED */
    case 17: return GNW_W_FULL;         /* SERVER_FULL */
    case 3: return GNW_W_RATE;          /* RATE_LIMITED (the caller retries) */
    default: return GNW_W_BAD;
    }
}

static void store_feed(gnw *g, mnc_u32 first, unsigned count, const mnc_u8 *pads, mnc_u32 now)
{
    unsigned i, j;
    for (i = 0; i < count; i++) {
        mnc_s32 t = (mnc_s32) (first + i);
        mnc_u32 slot;
        if (g->live && t >= 1 && (mnc_u32) t > g->frames && (mnc_u32) t <= 36000u) g->frames = (mnc_u32) t;      /* live: the feed can overtake the RANGE reply that announces it */
        if (t < g->floor || t >= g->floor + (mnc_s32) GNW_RING || t < 1 || (mnc_u32) t > g->frames) { g->st_dup++; continue; }
        slot = (mnc_u32) t & (GNW_RING - 1u);
        if (g->rtag[slot] == t) { g->st_dup++; continue; }
        for (j = 0; j < 16u; j++) g->pad[slot][j] = pads[i * 16u + j];
        g->rtag[slot] = t;
    }
    while (g->have < g->frames && g->rtag[(g->have + 1u) & (GNW_RING - 1u)] == (mnc_s32) (g->have + 1u)) g->have++;
    if (g->have != g->last_have) { g->last_have = g->have; g->last_new_ms = now; }
}

int gnw_rx(gnw *g, mw2 *w, const mnc_u8 *d, mnc_u32 n, mnc_u32 now)
{
    mnc_u8 st = 0, op;
    mw2_u32 blen = 0;
    mnc_u32 ctr;
    int rc;
    if (g->state == GNW_OFF || n < MW2_HDR || d[0] != 0x6D || d[1] != 2u) return 0;
    op = d[2];
    ctr = g32(d + 8);
    if (op == (mnc_u8) (GNW_OP_FEED | 0x80u)) {                              /* the pushed feed: its own counter, any good one */
        mnc_u32 set_id, first, cf[GNW_FEED_CHK], cc[GNW_FEED_CHK], k;
        mnc_u8 game, dg[4], count, fl, nchk;
        const mnc_u8 *pads;
        rc = mw2_open(w, now, d, n, GNW_OP_FEED, ctr, &st, &blen);
        if (rc != 1 || st != 0u) { g->st_bad++; return 1; }
        if (gnw_parse_feed(d + MW2_HDR, (mnc_u32) blen, &set_id, &game, dg, &first, &count, &fl, &pads, &nchk, cf, cc) != 0) { g->st_bad++; return 1; }
        if (g->state < GNW_READY || set_id != g->set_id || game != g->game || dg[0] != g->digest[0] || dg[1] != g->digest[1] || dg[2] != g->digest[2] || dg[3] != g->digest[3]) { g->st_bad++; return 1; }
        g->st_feed++;
        store_feed(g, first, count, pads, now);
        for (k = 0; k < nchk; k++)
            if (!(cf[k] & 15u)) { mnc_u32 s = (cf[k] >> 4) & (GNW_CHK_N - 1u); g->chk_f[s] = cf[k]; g->chk_c[s] = cc[k]; }
        return 1;
    }
    if (op == (mnc_u8) (GNW_OP_RANGE | 0x80u)) {                             /* the RANGE answer carries nothing we need (the feed does); a refusal ends the watch */
        rc = mw2_open(w, now, d, n, GNW_OP_RANGE, ctr, &st, &blen);
        if (rc == 2) fail(g, GNW_W_NOSESSION);
        else if (rc == 1 && st != 0u && st != 3u) fail(g, map_status(st));
        else if (rc == 1 && st == 0u && blen == 7u && d[MW2_HDR] == GNW_VER) {      /* ver | n | flags | visible head u32 (live) or the frame count */
            mnc_u32 fr = g32(d + MW2_HDR + 3);
            g->called = (mnc_u8) ((d[MW2_HDR + 2] & GNW_RF_CALLED) ? 1u : 0u);
            if (g->live) {
                if (fr > g->frames && fr <= 36000u) g->frames = fr;
                if (!(d[MW2_HDR + 2] & GNW_RF_LIVE)) g->live = 0;                  /* the game is over: frames is final, the log ends */
            }
        }
        return 1;
    }
    if ((op == (mnc_u8) (GNW_OP_SPEC | 0x80u) && g->state == GNW_SPEC) || (op == (mnc_u8) (GNW_OP_HEAD | 0x80u) && g->state == GNW_HEAD)) {
        if (ctr != g->ctr) return 1;                                         /* a stale answer */
        rc = mw2_open(w, now, d, n, g->op, g->ctr, &st, &blen);
        if (rc == 2) { fail(g, GNW_W_NOSESSION); return 1; }
        if (rc != 1) return 1;
        if (st != 0u) {
            unsigned why = map_status(st);
            if (why == GNW_W_RATE) { g->sent_ms = now - GNW_RESEND_MS + 300u; return 1; }       /* try again soon with the same datagram */
            fail(g, why);
            return 1;
        }
        if (g->state == GNW_SPEC) {
            if (parse_spec(g, d + MW2_HDR, (mnc_u32) blen) != 0) { fail(g, GNW_W_BAD); return 1; }
            g->state = GNW_HEAD; g->pkt_len = 0;                             /* tick sends the HEAD request */
        } else {
            if (parse_head(g, d + MW2_HDR, (mnc_u32) blen) != 0) { fail(g, GNW_W_DESC); return 1; }
            if (stadium_bad(g)) { fail(g, GNW_W_STADIUM); return 1; }
            g->state = GNW_READY; g->floor = 1; g->last_new_ms = now; g->range_ms = now - GNW_RANGE_EVERY_MS;
        }
        return 1;
    }
    if (op == (mnc_u8) (GNW_OP_SPEC | 0x80u) || op == (mnc_u8) (GNW_OP_HEAD | 0x80u)) return 1;       /* the answer to a decline (or a late duplicate): ours, nothing to do */
    return 0;
}

int gnw_tick(gnw *g, mw2 *w, const mnc_io *io, mnc_u32 now)
{
    if (g->state == GNW_OFF || g->state == GNW_FAIL || g->state == GNW_DONE) return g->state;
    if (!mw2_ready(w, now)) { fail(g, GNW_W_NOSESSION); return g->state; }
    if (g->state == GNW_SPEC) {
        if (now - g->sent_ms >= GNW_RESEND_MS) {
            if (g->tries >= GNW_TRIES) { fail(g, GNW_W_TIMEOUT); return g->state; }
            g->tries++; g->sent_ms = now;
            (void) io->send(io->ctx, g->pkt, g->pkt_len);
        }
    } else if (g->state == GNW_HEAD) {
        static const mnc_u8 pl[1] = { GNW_VER };
        static const mnc_u8 pl_rt[2] = { GNW_VER, GNW_HEAD_EXT_RTYPE };
        if (g->pkt_len == 0u) { if (g->rt_ask) send_req(g, w, io, GNW_OP_HEAD, pl_rt, 2u, now, 1); else send_req(g, w, io, GNW_OP_HEAD, pl, 1u, now, 1); }
        else if (now - g->sent_ms >= GNW_RESEND_MS) {
            if (g->tries >= GNW_TRIES) { fail(g, GNW_W_TIMEOUT); return g->state; }
            g->tries++; g->sent_ms = now;
            (void) io->send(io->ctx, g->pkt, g->pkt_len);
        }
    } else {                                                                 /* READY / RUN: the progress report + request for what the ring can take */
        mnc_s32 room = g->floor + (mnc_s32) GNW_RING - 1 - (mnc_s32) g->have;
        mnc_u32 want = room < 0 ? 0u : (mnc_u32) room;
        mnc_u8 pl[6];
        if (want > GNW_RANGE_MAX) want = GNW_RANGE_MAX;
        if (!g->live && g->frames - g->have < want) want = g->frames - g->have;        /* live: ask for what the ring can take, the server gives what exists up to its head */
        if (g->live) {
            g->lag = (mnc_s32) g->frames + (mnc_s32) g->delay10 * 6 - (g->last_frame > 0 ? g->last_frame : 0);
            if (g->last_frame > 0 && g->lag > g->lag_max) g->lag_max = g->lag;
        }
        if ((want && now - g->range_ms >= GNW_RANGE_EVERY_MS) || now - g->range_ms >= 1000u) {      /* with nothing to ask for, a 1 s keepalive keeps the subscription */
            pl[0] = GNW_VER; p32(pl + 1, g->have); pl[5] = (mnc_u8) want;
            g->range_ms = now;
            g->st_range++;
            send_req(g, w, io, GNW_OP_RANGE, pl, 6u, now, 0);
        }
        if (g->live) { if (now - g->last_new_ms > GNW_SILENT_LIVE_MS) fail(g, GNW_W_SILENT); }
        else if (g->state == GNW_RUN && g->have < g->frames && now - g->last_new_ms > GNW_SILENT_MS) fail(g, GNW_W_SILENT);
    }
    return g->state;
}

int gnw_css(const gnw *g, mnc_u8 *msrb)
{
    mnc_u32 i = 0;
    const char *t;
    if (g->state == GNW_FAIL) {
        t = gnw_why_text(g->why);
        msrb[0] = 5;
        while (t[i] && i < 240u) { msrb[357 + i] = (mnc_u8) t[i]; i++; }
        msrb[357 + i] = 0;
        return 1;
    }
    if (g->state == GNW_SPEC || g->state == GNW_HEAD
        || (g->state == GNW_READY && (g->have < g->frames || g->live) && g->have < (g->live ? (mnc_u32) GNW_LEAD_LIVE : (mnc_u32) GNW_LEAD))) { msrb[0] = 2; return 1; }
    return 0;
}

int gnw_b0(gnw *g, const mnc_u8 *txb, mnc_u8 *rxb)
{
    mnc_s32 frame = (mnc_s32) g32(txb + 1), fin = (mnc_s32) g32(txb + 5), lat, head, t;
    mnc_u32 cks = g32(txb + 9), i, j, s;
    int res;
    if (frame < 1 || frame > 0x3FFFFFFF) frame = 1;
    if (frame == 1 && g->last_frame != 1) {                                   /* a new match: the frame-1 start rule: no READY stream from tag 1 = do not start */
        g->calls = 0; g->cmp_hi = 0; g->floor = 1;
        if (g->state != GNW_READY && g->state != GNW_RUN) fail(g, GNW_W_BAD);
    }
    g->last_frame = frame;
    g->calls++;
    if (g->state == GNW_READY) g->state = GNW_RUN;
    head = (mnc_s32) g->have;
    if (g->state == GNW_RUN && fin > 0 && fin > g->cmp_hi) {                  /* deferred checksum compare against the players' series (every 16th frame) */
        g->cmp_hi = fin;
        if (!(fin & 15)) {
            mnc_u32 sl = ((mnc_u32) fin >> 4) & (GNW_CHK_N - 1u);
            if (g->chk_f[sl] == (mnc_u32) fin) {
                g->st_cmp++;
                if (g->chk_c[sl] != cks) { g->st_cmp_bad++; if (!g->st_first_bad) g->st_first_bad = (mnc_u32) fin; fail(g, GNW_W_SYNC); }
            }
        }
    }
    res = rxb[0];
    if (g->leave_req && g->state == GNW_RUN) { g->state = GNW_DONE; res = 3; }      /* the player left (hold B) or accepted the call-up (hold Z): the match ends through the disconnect end */
    if (res != 3) {
        if (g->state != GNW_RUN) res = 3;
        else if (!g->live && head >= (mnc_s32) g->frames && frame > (mnc_s32) g->frames) { g->state = GNW_DONE; res = 3; }      /* the log is over and the game did not end by itself (a lost tail): leave through the disconnect end */
        else res = (frame > head || res == 2) ? 2 : (g->live && head - frame > GNW_CATCH_LAG) ? 4 : 1;      /* the engine's own skip (rb_depth window) is kept; a missing tag is a stall, never a prediction; far behind a live feed: ADVANCE */
    }
    if (res == 3) g->st_leave++;
    if (res == 2) g->st_stall++;
    if (res == 1 || res == 4) g->st_fed++;
    if (res == 4) g->st_adv++;
    rxb[0] = (mnc_u8) res;
    lat = frame < head ? frame : head;
    rxb[1] = 2;
    for (i = 2; i < 26u; i++) rxb[i] = 0;
    p32(rxb + 26, (mnc_u32) lat);
    p32(rxb + 30, (mnc_u32) lat);
    p32(rxb + 34, (mnc_u32) frame);
    p32(rxb + 38, (mnc_u32) lat);
    for (i = 42; i < 42u + 3u * 84u; i++) rxb[i] = 0;
    for (s = 0; s < 2u; s++)
        for (j = 0; j < 7u; j++) {
            t = lat - (mnc_s32) j;
            if (t >= 1 && g->rtag[(mnc_u32) t & (GNW_RING - 1u)] == t)
                for (i = 0; i < 8u; i++) rxb[42u + s * 84u + j * 12u + i] = g->pad[(mnc_u32) t & (GNW_RING - 1u)][s * 8u + i];
        }
    g->floor = frame - 8 > 1 ? frame - 8 : 1;                                 /* consumed: the ring may take newer tags there */
    return res;
}

int gnw_b3(gnw *g, mnc_u8 *msrb, int markers)
{
    mnc_u8 *mb = msrb + 598;
    if (g->state < GNW_READY || g->state > GNW_RUN) return -1;
    if (markers) {
    msrb[3] = 2;                                                              /* local player index 2: game port 2 is typed 3 in the block, remote i = i-th other port */
    msrb[4] = 0;                                                              /* ODB_ONLINE_PLAYER_INDEX: only the LRAS "pauser" at the end of a game */
    }
    if ((g->desc_flags & GNW_DF_WORDS) && !(g->desc_flags & GNW_DF_SEL)) {   /* descriptor-driven block (the demo recording): every field of the review's list comes from the words */
        const mnc_u32 *d = g->words;
        if (d[5] != GNW_DESC_MAGIC) return -1;
        mb[0x60] = (mnc_u8) d[0]; mb[0x63] = (mnc_u8) (d[0] >> 8); mb[0x84] = (mnc_u8) (d[0] >> 16); mb[0x87] = (mnc_u8) (d[0] >> 24);
        mb[0x67] = 0; mb[0x69] = 0; mb[0x67 + 0x24] = (mnc_u8) (d[3] >> 16); mb[0x69 + 0x24] = 0;
        mb[0x0E] = (mnc_u8) (d[1] >> 8); mb[0x0F] = (mnc_u8) d[1];
        msrb[961] = (mnc_u8) (d[1] >> 16);
        mb[2] = (mnc_u8) ((mb[2] & 0xF7u) | ((d[1] >> 24) & 0x08u));        /* only the pause-off bit (gameBitField3 bit 3): the rest of the byte (0x86 in the template) carries the HUD / score-UI display bits, wiping it hid the percent, stock and icon HUD of the replay (console diag 474) */
        p32(msrb + 5, d[2]);
        mb[0x62] = (mnc_u8) d[3]; mb[0x62 + 0x24] = (mnc_u8) (d[3] >> 8);
        p32(mb + 0x10, d[4]);
    }
    if (g->rt_have && g->rt_items) {                                          /* Wild And Crazy (piece H): the players' block has every item on at Very High (s7_core.c canon_block, S7R_ITEMS_ALL); the replay needs the same or the first item spawn diverges */
        mnc_u32 k;
        mb[0xB] = 4u;
        for (k = 0; k < 8u; k++) mb[0x23 + k] = k < 5u ? 0xFFu : 0u;                  /* 0xFFFFFFFFFF000000, as canon_block */
    }
    return 0;
}
