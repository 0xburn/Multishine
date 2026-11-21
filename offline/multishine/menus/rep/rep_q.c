/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* rep_q: the RESIDENT half of the Slippi game-report client: the queue, the counters and the HELLO clock live in `struct rep_keep`,
   which survives the report overlay (ms/rep.ovl is loaded only while there is something to send, in a menu scene).  A bridge game ends in a match scene where no overlay
   exists, so the enqueue is plain resident code: no TCP, no TLS, no secrets (the uid and play key are read from the card at send time).  Strict C89, no libc, no float. */
#include "rep_keep.h"

static void zero(void *p, rep_u32 n) { rep_u8 *b = (rep_u8 *)p; rep_u32 i; for (i = 0; i < n; i++) b[i] = 0; }
static rep_u32 be32(const rep_u8 *p) { return ((rep_u32)p[0] << 24) | ((rep_u32)p[1] << 16) | ((rep_u32)p[2] << 8) | (rep_u32)p[3]; }
static int plain_tok(const char *s, rep_u32 max)
{
    rep_u32 i;
    if (!s || !s[0]) return 0;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        if (i >= max) return 0;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == ':' || c == '-')) return 0;
    }
    return 1;
}
static void copy_tok(char *d, const char *s, rep_u32 max) { rep_u32 i; for (i = 0; s[i] && i < max; i++) d[i] = s[i]; d[i] = 0; }

void rep_keep_init(struct rep_keep *k, rep_u32 (*ms)(void), int (*server)(void))
{
    zero(k, sizeof *k);
    k->ms = ms; k->server = server;
    rep_clock_init(&k->clk);
    k->cur = -1;
}

int rep_enabled(const struct rep_keep *k) { return k->server != 0 && k->server() != 0; }

int rep_pending(const struct rep_keep *k)
{
    int i;
    for (i = 0; i < REP_QUEUE; i++) if (k->q[i].used) return 1;
    return 0;
}

/* ---------------------------------------------------------------- damage: float32 BE -> thousandths */
rep_u32 rep_damage_milli(const rep_u8 *f)
{
    rep_u32 bits = ((rep_u32)f[0] << 24) | ((rep_u32)f[1] << 16) | ((rep_u32)f[2] << 8) | (rep_u32)f[3];
    rep_u32 e = (bits >> 23) & 0xFFu, m = bits & 0x7FFFFFu, x;
    int s;
    if (bits & 0x80000000u) return 0;              /* negative: not a damage value */
    if (e == 0xFFu) return 0;                      /* NaN / infinity */
    if (e == 0) return 0;                          /* zero and denormals */
    m |= 0x800000u;                                /* 24 bits, value = m * 2^(e - 150) */
    if (e > 147u) return 999999u;                  /* >= 2^21 percent: absurd, clamp */
    /* thousandths = m * 1000 * 2^(e-150) = (m * 125) >> (147 - e)   (m * 125 < 2^31) */
    s = (int)(147u - e);
    x = m * 125u;
    if (s >= 32) return 0;
    if (s > 0) x = (x + (1u << (s - 1))) >> s;
    return x > 999999u ? 999999u : x;
}
int rep_enqueue(struct rep_keep *r, const rep_u8 *rgb, const char *match_id, const char *opp_uid, int my_idx, int opp_idx)
{
    struct rep_rec *q = 0;
    int i, np = 0;
    rep_u32 now = r->ms ? r->ms() : 0u;
    if (!rep_enabled(r)) { r->st.off++; return REP_Q_OFF; }
    if (rgb[REP_RGB_MODE] != 1u && !(REP_REPORT_DIRECT && rgb[REP_RGB_MODE] == 2u)) { r->st.mode_refused++; return REP_Q_MODE; }
    if (!plain_tok(match_id, REP_MID_MAX) || !plain_tok(opp_uid, REP_ID_MAX) || my_idx < 0 || my_idx > 3 || opp_idx < 0 || opp_idx > 3 || my_idx == opp_idx) { r->st.bad++; return REP_Q_BAD; }
    for (i = 0; i < REP_QUEUE; i++) if (!r->q[i].used) { q = &r->q[i]; break; }
    if (!q) {                                               /* full: drop the oldest that is not in flight */
        int best = -1;
        for (i = 0; i < REP_QUEUE; i++) {
            if (i == r->cur) continue;
            if (best < 0 || (int)(r->q[best].t_enq - r->q[i].t_enq) > 0) best = i;
        }
        if (best < 0) { r->st.bad++; return REP_Q_BAD; }
        q = &r->q[best];
        r->st.full_dropped++;
    }
    zero(q, sizeof *q);
    q->used = 1; q->mode = rgb[REP_RGB_MODE]; q->t_enq = now;
    q->frames = be32(rgb + REP_RGB_FRAMES); q->game = be32(rgb + REP_RGB_GAME); q->tiebreak = be32(rgb + REP_RGB_TIEBREAK);
    q->winner = (signed char)rgb[REP_RGB_WINNER]; q->endm = rgb[REP_RGB_ENDM]; q->lras = (signed char)rgb[REP_RGB_LRAS];
    q->stage = (rep_u16)(((rep_u16)rgb[REP_RGB_INFO + REP_INFO_STAGE] << 8) | rgb[REP_RGB_INFO + REP_INFO_STAGE + 1]);
    copy_tok(q->match_id, match_id, REP_MID_MAX); copy_tok(q->opp_uid, opp_uid, REP_ID_MAX);
    for (i = 0; i < 4; i++) {
        const rep_u8 *pr = rgb + REP_RGB_PLAYERS + 9 * i, *pb = rgb + REP_RGB_INFO + REP_INFO_PLAYER0 + REP_INFO_PLAYER_STEP * i;
        struct rep_player *p;
        if (i != my_idx && i != opp_idx) continue;
        p = &q->pl[np++];
        p->who = (rep_u8)(i == my_idx ? 0 : 1);
        p->slot_type = pr[0]; p->stocks = pr[1]; p->damage_milli = rep_damage_milli(pr + 2);
        p->char_id = pb[0]; p->start_stocks = pb[2]; p->color_id = pb[3];
    }
    q->np = (rep_u8)np;
    r->st.enq++;
    return REP_Q_OK;
}


/* ---------------------------------------------------------------- the HELLO clock */
void rep_clock_init(struct rep_clock *c) { c->srv_time = c->stamp_ms = c->last_seen = 0; c->have = 0; }
void rep_clock_note(struct rep_clock *c, int have_hello, rep_u32 srv_time, rep_u32 now_ms)
{
    if (!have_hello || srv_time < 1700000000u) return;         /* before Nov 2023 = not a real time */
    if (c->have && srv_time == c->last_seen) return;           /* the same HELLO reply as last frame */
    c->srv_time = srv_time; c->last_seen = srv_time; c->stamp_ms = now_ms; c->have = 1;
}
rep_u32 rep_clock_unix(const struct rep_clock *c, rep_u32 now_ms)
{
    if (!c->have) return 0;
    return c->srv_time + (rep_u32)(now_ms - c->stamp_ms) / 1000u;
}
