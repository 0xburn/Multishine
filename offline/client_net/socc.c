/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* socc: the social ops client of the console menus, see socc.h.  C89, no libc, no heap. */
#include "socc.h"

static mnc_u32 g32(const mnc_u8 *p) { return ((mnc_u32) p[0] << 24) | ((mnc_u32) p[1] << 16) | ((mnc_u32) p[2] << 8) | p[3]; }

void socc_init(socc *s, mnc *c)
{
    mnc_u8 *b = (mnc_u8 *) s;
    mnc_u32 i;
    for (i = 0; i < sizeof *s; i++) b[i] = 0;
    s->c = c; s->state = SOCC_IDLE; s->timeout_ms = 700u;
}

static void fail(socc *s, int err, int status) { s->state = SOCC_FAILED; s->err = err; s->status = status; }

/* ---- request building into c->req + MW2_HDR ---- */
typedef struct { mnc_u8 *p; mnc_u32 n, cap; int bad; } wb;
static void w8(wb *w, unsigned v) { if (w->n < w->cap) w->p[w->n++] = (mnc_u8) v; else w->bad = 1; }
static void w32(wb *w, mnc_u32 v) { w8(w, (unsigned) (v >> 24)); w8(w, (unsigned) (v >> 16) & 255u); w8(w, (unsigned) (v >> 8) & 255u); w8(w, (unsigned) v & 255u); }
static void wstr(wb *w, const char *s, unsigned max)
{
    unsigned n = 0, i;
    while (s && s[n]) n++;
    if (n > max || n == 0u) { w->bad = 1; return; }
    w8(w, n);
    for (i = 0; i < n; i++) { if ((mnc_u8) s[i] < 0x20u || (mnc_u8) s[i] > 0x7Eu) w->bad = 1; w8(w, (mnc_u8) s[i]); }
}
static void begin(socc *s, wb *w) { w->p = s->c->req + MW2_HDR; w->n = 0; w->cap = 80u; w->bad = 0; }
static int send(socc *s, mnc_u8 op, wb *w, mnc_u32 now)
{
    mnc *c = s->c;
    mnc_u32 n, i;
    if (s->state == SOCC_BUSY) return SOCC_E_BUSY;
    if (w->bad) { fail(s, SOCC_E_ARG, 0); return SOCC_E_ARG; }
    if (!mw2_ready(&c->w, now)) { fail(s, SOCC_E_NOSESSION, 0); return SOCC_E_NOSESSION; }
    n = mw2_seal(&c->w, c->req, op, w->n);
    if (n > sizeof s->pkt) { fail(s, SOCC_E_ARG, 0); return SOCC_E_ARG; }
    for (i = 0; i < n; i++) s->pkt[i] = c->req[i];
    s->pkt_len = n; s->ctr = c->w.ctr; s->op = op; s->sent_ms = now; s->tries = 1;
    s->state = SOCC_BUSY; s->err = SOCC_E_NONE; s->status = 0;
    s->st_sent++;
    (void) c->io.send(c->io.ctx, s->pkt, n);
    return 0;
}

int socc_tick(socc *s, mnc_u32 now, unsigned state)
{
    wb w;
    begin(s, &w);
    w8(&w, 1u); w8(&w, state); w8(&w, 0u); w8(&w, 7u);            /* sver 1 | state | flags | ext bit 0 = the unread message count, bit 1 = the toast block, bit 2 = the number of friends online (an older server ignores it) */
    return send(s, SOCC_OP_PRESENCE, &w, now);
}
int socc_invite_respond(socc *s, mnc_u32 now, mnc_u32 id, int accept)
{
    wb w;
    begin(s, &w);
    w32(&w, id); w8(&w, accept ? 1u : 0u);
    return send(s, SOCC_OP_INVRESP, &w, now);
}
int socc_fight_friend(socc *s, mnc_u32 now, const mnc_u8 match_id[8])
{
    wb w;
    unsigned i;
    begin(s, &w);
    for (i = 0; i < 8u; i++) w8(&w, match_id[i]);
    return send(s, SOCC_OP_FIGHT_FRIEND, &w, now);
}
int socc_fight_check(socc *s, mnc_u32 now, const mnc_u8 match_id[8])
{
    wb w;
    unsigned i;
    begin(s, &w);
    for (i = 0; i < 8u; i++) w8(&w, match_id[i]);
    w8(&w, 1u);                                                     /* ask only: 0 = the prompt may show, 2 already friends, 5 do not show (an older server refuses the byte: no prompt) */
    return send(s, SOCC_OP_FIGHT_FRIEND, &w, now);
}
static int code_op(socc *s, mnc_u32 now, mnc_u8 op, const char *code, int extra, unsigned v)
{
    wb w;
    begin(s, &w);
    wstr(&w, code, 8);
    if (extra) w8(&w, v);
    return send(s, op, &w, now);
}
int socc_friend_answer(socc *s, mnc_u32 now, const char *code, int accept) { return code_op(s, now, SOCC_OP_FANSWER, code, 1, accept ? 1u : 0u); }
int socc_msg_fetch(socc *s, mnc_u32 now, mnc_u32 after, int show_only)
{
    wb w;
    begin(s, &w);
    w32(&w, after);
    if (show_only) w8(&w, 1u);                                      /* optional flags byte: bit 0 = only show (an older server refuses it with BAD_REQUEST: the caller treats a failure as no messages) */
    s->nm = 0;
    return send(s, SOCC_OP_MSGFETCH, &w, now);
}
#ifndef SOCC_TICK_ONLY                                              /* the Friends screen's ops (sign overlay); the hub overlay's menu tick needs none of them */
int socc_flist(socc *s, mnc_u32 now, unsigned kind, unsigned offset, struct socc_friend *out, unsigned cap) { return socc_flist_x(s, now, kind, offset, out, cap, 1u); }
int socc_flist_x(socc *s, mnc_u32 now, unsigned kind, unsigned offset, struct socc_friend *out, unsigned cap, unsigned game)
{
    wb w;
    if (kind > 4u || !out || cap == 0u) { fail(s, SOCC_E_ARG, 0); return SOCC_E_ARG; }
    begin(s, &w);
    w8(&w, kind); w8(&w, offset);
    s->fgame = (mnc_u8) (kind == 0u && game ? 1u : 0u);              /* the reply is parsed with the game tail only when it was asked for */
    if (kind == 0u) w8(&w, s->fgame ? 1u | SOCC_FL_GAME : 1u);      /* FL_EXT (+ FL_GAME): sorted (online first, then last seen), extended entries, 8 per page; a friend in a joinable Game Night room carries a room tail (always part of the current protocol) */
    s->fl = out; s->fl_cap = (mnc_u8) (cap > 255u ? 255u : cap); s->kind = (mnc_u8) kind;
    return send(s, SOCC_OP_FLIST, &w, now);
}
int socc_ilist(socc *s, mnc_u32 now, struct socc_inv *out, unsigned cap)
{
    wb w;
    if (!out || cap == 0u) { fail(s, SOCC_E_ARG, 0); return SOCC_E_ARG; }
    begin(s, &w);
    w8(&w, 5u); w8(&w, 0u);
    s->il = out; s->fl_cap = (mnc_u8) (cap > SOCC_INV_PAGE ? SOCC_INV_PAGE : cap); s->kind = 5u; s->fgame = 0u;
    return send(s, SOCC_OP_FLIST, &w, now);
}
int socc_invite_respond_l(socc *s, mnc_u32 now, mnc_u32 id, int accept)
{
    wb w;
    begin(s, &w);
    w32(&w, id); w8(&w, accept ? 1u : 0u); w8(&w, 1u);
    return send(s, SOCC_OP_INVRESP, &w, now);
}
int socc_friend_add(socc *s, mnc_u32 now, const char *code) { return code_op(s, now, SOCC_OP_FADD, code, 0, 0); }
int socc_friend_remove(socc *s, mnc_u32 now, const char *code) { return code_op(s, now, SOCC_OP_FREMOVE, code, 0, 0); }
int socc_recent_remove(socc *s, mnc_u32 now, const char *code) { return code_op(s, now, SOCC_OP_FREMOVE, code, 1, 1u); }
int socc_block(socc *s, mnc_u32 now, const char *code) { return code_op(s, now, SOCC_OP_BLOCK, code, 1, 0u); }
int socc_invite_send(socc *s, mnc_u32 now, const char *code)
{
    wb w;
    begin(s, &w);
    wstr(&w, code, 8); w8(&w, 2u); w8(&w, 1u);                      /* mode Direct (Mano A Mano) | INV_SEND_TOAST */
    return send(s, SOCC_OP_INVSEND, &w, now);
}
int socc_invite_status(socc *s, mnc_u32 now, mnc_u32 id)
{
    wb w;
    begin(s, &w);
    w32(&w, id);
    return send(s, SOCC_OP_INVSTATUS, &w, now);
}
int socc_msg_send(socc *s, mnc_u32 now, const char *code, const char *text)
{
    wb w;
    begin(s, &w);
    wstr(&w, code, 8); wstr(&w, text, 60);
    return send(s, SOCC_OP_MSGSEND, &w, now);
}
#endif
int socc_rank_me(socc *s, mnc_u32 now)
{
    wb w;
    begin(s, &w);
    s->rk.have = 0;
    return send(s, SOCC_OP_RANKED_STATUS, &w, now);
}
int socc_prof_get(socc *s, mnc_u32 now)
{
    wb w;
    begin(s, &w);
    w8(&w, 7u);                                                     /* SPG_EXT | SPG_EXT_ACCT | SPG_EXT_RG7: the fight byte follows the four, then region (0..6, NAC included) and watch (an older server answers five) */
    return send(s, SOCC_OP_PROFGET, &w, now);
}
int socc_prof_set_fight(socc *s, mnc_u32 now, const struct socc_prof *cur, int on)
{
    wb w;
    begin(s, &w);
    w8(&w, cur->privacy); w8(&w, cur->policy); w8(&w, cur->flags & 7u); w8(&w, cur->msg_policy); w8(&w, on ? 1u : 0u);
    return send(s, SOCC_OP_PROFSET, &w, now);
}

int socc_prof_set_acct(socc *s, mnc_u32 now, const struct socc_prof *cur, int fight, unsigned region, unsigned watch)
{
    wb w;
    begin(s, &w);
    w8(&w, cur->privacy); w8(&w, cur->policy); w8(&w, cur->flags & 7u); w8(&w, cur->msg_policy); w8(&w, fight ? 1u : 0u);
    w8(&w, (mnc_u8)region); w8(&w, watch ? 1u : 0u);                /* the 6th and 7th bytes: region (0..6, 0xFF none) and watch (0 nobody, 1 friends) */
    w8(&w, SOCC_SPS_RG7);                                           /* the 8th byte: this console knows NAC, so 0xFF really clears (a pre-NAC console's 0xFF keeps a NAC it could not show).  Deploy the server first */
    return send(s, SOCC_OP_PROFSET, &w, now);
}

/* ---- parsers ---- */
typedef struct { const mnc_u8 *b; mnc_u32 n, i; } rd;
static int pstr(rd *r, char *out, unsigned max)
{
    unsigned k, len;
    if (r->i >= r->n) return -1;
    len = r->b[r->i];
    if (len > max || r->i + 1u + len > r->n) return -1;
    for (k = 0; k < len; k++) {
        mnc_u8 ch = r->b[r->i + 1u + k];
        if (ch < 0x20u || ch > 0x7Eu) return -1;
        out[k] = (char) ch;
    }
    out[len] = 0;
    r->i += 1u + len;
    return 0;
}
static int take(rd *r, unsigned n) { if (r->i + n > r->n) return -1; r->i += n; return 0; }

int socc_parse_peer(const mnc_u8 *b, mnc_u32 n, char peer[9])
{
    rd r;
    r.b = b; r.n = n; r.i = 0;
    if (pstr(&r, peer, 8) || r.i != n) return -1;
    return 0;
}

int socc_parse_msgs(const mnc_u8 *b, mnc_u32 n, mnc_u8 *count, struct socc_msg *out)
{
    rd r;
    unsigned k, cnt;
    if (n < 1u || n > 400u) return -1;
    cnt = b[0];
    if (cnt > SOCC_MSG_PAGE) return -1;
    r.b = b; r.n = n; r.i = 1;
    for (k = 0; k < cnt; k++) {
        struct socc_msg *m = &out[k];
        if (take(&r, 4)) return -1;
        m->id = g32(b + r.i - 4u);
        if (pstr(&r, m->from, 8) || pstr(&r, m->name, 12) || take(&r, 2)) return -1;
        m->age = (mnc_u16) ((b[r.i - 2u] << 8) | b[r.i - 1u]);
        if (pstr(&r, m->text, 60) || !m->from[0] || !m->text[0] || m->id == 0u) return -1;
    }
    if (r.i != n) return -1;
    *count = (mnc_u8) cnt;
    return 0;
}

int socc_parse_tick(const mnc_u8 *b, mnc_u32 n, struct socc_tick *t)
{
    rd r;
    unsigned k;
    mnc_u8 has;
    r.b = b; r.n = n; r.i = 0;
    if (n < 8u || n > 200u) return -1;
    t->invite = g32(b); t->requests = b[4]; t->msgs = b[5];
    has = b[6];
    if (has & ~3u) return -1;
    r.i = 7;
    t->has = has; t->id = 0; t->from[0] = 0; t->name[0] = 0; t->flags = 0; t->mode = 0; t->ms_left = 0; t->go[0] = 0; t->nn = 0;
    if (has & 1u) {
        if (take(&r, 4)) return -1;
        t->id = g32(b + 7);
        if (pstr(&r, t->from, 8) || pstr(&r, t->name, 12) || take(&r, 4)) return -1;
        t->flags = b[r.i - 4u]; t->mode = b[r.i - 3u]; t->ms_left = (mnc_u16) ((b[r.i - 2u] << 8) | b[r.i - 1u]);
        if ((t->flags & ~15u) || t->ms_left > 20000u || !t->from[0] || t->id == 0u) return -1;
    }
    if ((has & 2u) && (pstr(&r, t->go, 8) || !t->go[0])) return -1;
    if (r.i >= n) return -1;
    t->nn = b[r.i++];
    if (t->nn > SOCC_NOTICES) return -1;
    for (k = 0; k < t->nn; k++) {
        struct socc_notice *o = &t->n[k];
        if (take(&r, 1)) return -1;
        o->kind = b[r.i - 1u];
        if ((o->kind < SOCC_N_REQUEST || o->kind > SOCC_N_ROOM_JOIN) || pstr(&r, o->code, 8) || pstr(&r, o->name, 12) || take(&r, 2) || !o->code[0]) return -1;
        o->age = (mnc_u16) ((b[r.i - 2u] << 8) | b[r.i - 1u]);
    }
    t->online = 0; t->online_ok = 0;
    if (r.i + 1u == n) { t->online = b[r.i++]; t->online_ok = 1; }          /* the optional last byte (ext bit 2) */
    return r.i == n ? 0 : -1;
}

#ifndef SOCC_TICK_ONLY
int socc_parse_flist_g(const mnc_u8 *b, mnc_u32 n, int ext, int game, mnc_u8 *total, mnc_u8 *count, struct socc_friend *out, unsigned cap)
{
    rd r;
    unsigned k, cnt;
    if (n < 2u || n > 500u) return -1;
    cnt = b[1];
    if (cnt > (ext ? SOCC_FL_PAGE : SOCC_RQ_PAGE) || cnt > cap) return -1;
    r.b = b; r.n = n; r.i = 2;
    for (k = 0; k < cnt; k++) {
        struct socc_friend *e = &out[k];
        if (pstr(&r, e->code, 8) || pstr(&r, e->name, 12) || take(&r, 2) || !e->code[0]) return -1;
        e->presence = b[r.i - 2u]; e->flags = b[r.i - 1u];
        if (e->presence > 4u) return -1;
        e->seen = 255u; e->scode[0] = 0; e->sname[0] = 0; e->room[0] = 0; e->rflags = 0; e->gmode = 0; e->gflags = 0;
        if (ext) {
            if (take(&r, 1)) return -1;
            e->seen = b[r.i - 1u];
            if (pstr(&r, e->scode, 8) || pstr(&r, e->sname, 15)) return -1;
            if (e->flags & 4u) {                                    /* flag 4: the friend is in a Game Night room this console may join: room flags u8 (bit 0 passcode asked, bit 1 room has one) | code p4 */
                unsigned j;
                if (take(&r, 1)) return -1;
                e->rflags = b[r.i - 1u];
                if (e->rflags > 3u || pstr(&r, e->room, 4)) return -1;
                for (j = 0; j < 4u; j++) if (e->room[j] < '0' || e->room[j] > '9') return -1;
                if (e->room[4]) return -1;
            }
            if (game && (e->flags & SOCC_FF_GAME)) {                /* flag 32: mode u8 | game flags u8 (bit 0 watchable); a game tail only follows an entry that the request asked it for */
                if (take(&r, 2)) return -1;
                e->gmode = b[r.i - 2u]; e->gflags = b[r.i - 1u];
                if (e->gmode > 6u || (e->gflags & ~1u)) return -1;
            }
        } else if (e->flags & 4u) return -1;
    }
    if (r.i != n) return -1;
    *total = b[0]; *count = (mnc_u8) cnt;
    return 0;
}


int socc_parse_ilist(const mnc_u8 *b, mnc_u32 n, mnc_u8 *total, mnc_u8 *count, struct socc_inv *out, unsigned cap)
{
    rd r;
    unsigned k, cnt, j;
    if (n < 2u || n > 500u) return -1;
    cnt = b[1];
    if (cnt > SOCC_INV_PAGE || cnt > cap) return -1;
    r.b = b; r.n = n; r.i = 2;
    for (k = 0; k < cnt; k++) {
        struct socc_inv *e = &out[k];
        if (take(&r, 5)) return -1;
        e->type = b[r.i - 5u];
        e->id = ((mnc_u32) b[r.i - 4u] << 24) | ((mnc_u32) b[r.i - 3u] << 16) | ((mnc_u32) b[r.i - 2u] << 8) | (mnc_u32) b[r.i - 1u];
        if ((e->type != 1u && e->type != 2u) || pstr(&r, e->code, 8) || pstr(&r, e->name, 12) || take(&r, 4) || !e->code[0]) return -1;
        e->age = (mnc_u16) ((b[r.i - 4u] << 8) | b[r.i - 3u]); e->left = (mnc_u16) ((b[r.i - 2u] << 8) | b[r.i - 1u]);
        if (e->type == 2u) { for (j = 0; j < 4u; j++) if (e->code[j] < '0' || e->code[j] > '9') return -1; if (e->code[4]) return -1; }
    }
    if (r.i != n) return -1;
    *total = b[0]; *count = (mnc_u8) cnt;
    return 0;
}
#endif
#ifndef SOCC_TICK_ONLY
int socc_parse_flist(const mnc_u8 *b, mnc_u32 n, int ext, mnc_u8 *total, mnc_u8 *count, struct socc_friend *out, unsigned cap) { return socc_parse_flist_g(b, n, ext, 0, total, count, out, cap); }
#endif
int socc_parse_rank(const mnc_u8 *b, mnc_u32 n, struct socc_rank *o)
{
    mnc_u32 i, k;
    if (n < 13u || b[12] > 12u) return -1;
    i = 13u + b[12];                                                   /* the old body: shown i32 | rd u16 | games u16 | wins u16 | placement_left u8 | flag u8 | tier p-string */
    if (i > n || (n != i && n != i + 9u)) return -1;
    for (k = 0; k < b[12]; k++) if (b[13u + k] < 0x20u || b[13u + k] > 0x7Eu) return -1;
    o->have = 0;
    if (n == i) return 0;                                              /* a server from before the tail */
    o->games = (mnc_u16) ((b[6] << 8) | b[7]); o->pl_left = b[10];
    o->shown10 = (int) g32(b + i);
    o->rank = (mnc_u16) ((b[i + 4u] << 8) | b[i + 5u]); o->tier = b[i + 6u]; o->total = (mnc_u16) ((b[i + 7u] << 8) | b[i + 8u]);
    if (o->tier > 9u && o->tier != SOCC_TIER_GM && o->tier != SOCC_TIER_PLACEMENT) return -1;
    if ((o->tier == SOCC_TIER_PLACEMENT) != (o->pl_left > 0u)) return -1;
    o->have = 1;
    return 0;
}
static int on_reply(socc *s, const mnc_u8 *b, mnc_u32 n)
{
    switch (s->op) {
    case SOCC_OP_PRESENCE: return socc_parse_tick(b, n, &s->tick);
    case SOCC_OP_RANKED_STATUS: return socc_parse_rank(b, n, &s->rk);
    case SOCC_OP_INVRESP: return socc_parse_peer(b, n, s->peer);
    case SOCC_OP_MSGFETCH: return socc_parse_msgs(b, n, &s->nm, s->msg);
    case SOCC_OP_FIGHT_FRIEND: case SOCC_OP_FADD:
        if (n != 1u || b[0] > 5u) return -1;
        s->result = b[0];
        return 0;
#ifndef SOCC_TICK_ONLY
    case SOCC_OP_FLIST:
        if (s->kind == 5u) return socc_parse_ilist(b, n, &s->total, &s->n, s->il, s->fl_cap);
        return socc_parse_flist_g(b, n, s->kind == 0u, s->fgame, &s->total, &s->n, s->fl, s->fl_cap);
    case SOCC_OP_INVSEND:
        if (n != 5u) return -1;
        s->inv_id = g32(b); s->hold = b[4];
        return 0;
    case SOCC_OP_INVSTATUS: {
        rd r;
        if (n < 3u || b[0] > 2u) return -1;
        s->result = b[0]; s->secs = b[1];
        r.b = b; r.n = n; r.i = 2;
        if (pstr(&r, s->peer, 8) || r.i != n) return -1;
        return 0;
    }
#endif
    case SOCC_OP_PROFGET: case SOCC_OP_PROFSET:
        if (n != 4u && n != 5u && n != 7u) return -1;
        if (b[0] > 2u || b[1] > 2u || (b[2] & ~7u) || b[3] > 2u || (n >= 5u && b[4] > 1u)) return -1;
        if (n == 7u && ((b[5] >= SOCC_REGION_N && b[5] != 0xFFu) || b[6] > 1u)) return -1;
        s->prof.have_acct = (mnc_u8) (n == 7u); s->prof.region = n == 7u ? b[5] : 0xFFu; s->prof.watch = n == 7u ? b[6] : 1u;
        s->prof.privacy = b[0]; s->prof.policy = b[1]; s->prof.flags = b[2]; s->prof.msg_policy = b[3];
        s->prof.have_fight = (mnc_u8) (n >= 5u); s->prof.fight = n >= 5u ? b[4] : 1u;
        return 0;
    default:                                                         /* FRIEND_ANSWER, FRIEND_REMOVE, BLOCK, MSG_SEND: empty */
        return n == 0u ? 0 : -1;
    }
}

int socc_poll(socc *s, mnc_u32 now)
{
    mnc *c = s->c;
    int i;
    if (s->state != SOCC_BUSY) return s->state;
    for (i = 0; i < MNC_RX_BUDGET; i++) {
        mnc_u8 st = 0, op;
        mw2_u32 bl = 0;
        int n = c->io.recv(c->io.ctx, c->rx, MNC_MAX_PACKET), r;
        if (n <= 0) break;
        if (n < (int) MW2_HDR || n > MNC_MAX_PACKET) { s->st_bad++; continue; }
        op = (mnc_u8) (c->rx[2] & 0x7Fu);
        if (op != s->op || g32(c->rx + 8) != s->ctr) { s->st_bad++; continue; }
        r = mw2_open(&c->w, now, c->rx, (mnc_u32) n, op, s->ctr, &st, &bl);
        if (r == 2) { fail(s, SOCC_E_NOSESSION, 0); return s->state; }
        if (r != 1) { s->st_bad++; continue; }
        if (st != 0) { fail(s, SOCC_E_SERVER, st); return s->state; }
        if (on_reply(s, c->rx + MW2_HDR, (mnc_u32) bl)) { s->st_bad++; fail(s, SOCC_E_BAD_REPLY, 0); return s->state; }
        s->state = SOCC_DONE;
        return s->state;
    }
    if (now - s->sent_ms >= s->timeout_ms) {
        if (s->tries >= SOCC_TRIES) { fail(s, SOCC_E_TIMEOUT, 0); return s->state; }
        s->tries++; s->sent_ms = now; s->st_resent++;
        (void) c->io.send(c->io.ctx, s->pkt, s->pkt_len);         /* the same counter: the server's reply cache answers a repeat */
    }
    return s->state;
}

void socc_abort(socc *s) { s->state = SOCC_IDLE; }
