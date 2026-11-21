/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Menu toasts: the model (toast.h).  C89, no libc. */
#include "toast.h"

static int reached(unsigned long now, unsigned long at) { return ((now - at) & 0xFFFFFFFFul) < 0x80000000ul; }   /* now >= at, wrap safe (32-bit clock) */
static void zero(void *p, unsigned n) { unsigned char *b = (unsigned char *)p; while (n--) *b++ = 0; }
static void scopy(char *d, const char *s, unsigned max) { unsigned i = 0; if (s) while (s[i] && i < max) { d[i] = s[i]; i++; } d[i] = 0; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

void tst_init(struct tst *t) { zero(t, sizeof *t); }

static void show(struct tst *t, const struct tst_item *it, unsigned long now)
{
    t->cur = *it;
    if (t->cur.kind != TST_K_INVITE) {                                 /* a notice's time starts when it is shown, not when it was queued */
        unsigned long d = (it->kind == TST_K_DIRECT || it->kind == TST_K_ROOM) ? TST_NOTE_MS : it->kind == TST_K_MSG ? TST_MSG_MS : it->kind == TST_K_REQUEST ? TST_REQ_MS : it->kind == TST_K_FIGHT ? TST_FIGHT_MS : it->friends ? TST_FRIENDS_MS : TST_INFO_MS;
        t->cur.t0 = now; t->cur.until = now + d;
    }
    t->hold.on = 0; t->hold.fired = 0; t->hold.pm = 0;             /* (hold.prev stays: a Z already down when the toast comes up does not count) */
    t->sfx = TST_SFX_SHOW;
    t->dirty = 1;
    t->shown++;
}

static void push_front(struct tst *t, const struct tst_item *it)
{
    unsigned i;
    if (t->qn == TST_QN) t->qn--;                                       /* the oldest (last) goes */
    for (i = t->qn; i > 0; i--) t->q[i] = t->q[i - 1u];
    t->q[0] = *it;
    t->qn++;
}

static void next(struct tst *t, unsigned long now)
{
    unsigned i;
    zero(&t->cur, sizeof t->cur);
    t->dirty = 1;
    if (!t->qn) return;
    show(t, &t->q[0], now);
    for (i = 1; i < t->qn; i++) t->q[i - 1u] = t->q[i];
    t->qn--;
}

void tst_invite(struct tst *t, unsigned long now, unsigned long id, const char *code, const char *name, unsigned ms_left)
{
    struct tst_item it;
    if (ms_left > TST_INVITE_MS) ms_left = TST_INVITE_MS;
    if (t->cur.kind == TST_K_INVITE && t->cur.id == id) {              /* the same challenge again: the server's clock decides the end */
        if (!t->cur.busy) t->cur.until = now + ms_left;
        return;
    }
    if (ms_left == 0u) return;
    zero(&it, sizeof it);
    it.kind = TST_K_INVITE; it.id = id;
    scopy(it.code, code, 8); scopy(it.name, name && name[0] ? name : code, 12);
    it.t0 = now; it.until = now + ms_left;
    if (t->cur.busy) { push_front(t, &it); return; }                    /* never pull a toast whose answer is pending: the challenge waits right behind it */
    if (t->cur.kind != TST_K_NONE && t->cur.kind != TST_K_INVITE) push_front(t, &t->cur);   /* a notice waits; an older challenge is simply replaced */
    show(t, &it, now);
}

static void enqueue(struct tst *t, unsigned long now, const struct tst_item *it)
{
    unsigned i;
    for (i = 0; i < t->qn; i++)
        if (t->q[i].kind == it->kind && same(t->q[i].code, it->code)) return;    /* the same notice twice: once */
    if (t->cur.kind == it->kind && same(t->cur.code, it->code)) return;
    if (t->cur.kind == TST_K_NONE) { show(t, it, now); return; }
    if (t->qn == TST_QN) {                                              /* full: the oldest is dropped */
        for (i = 1; i < TST_QN; i++) t->q[i - 1u] = t->q[i];
        t->qn--;
    }
    t->q[t->qn++] = *it;
}

void tst_notice(struct tst *t, unsigned long now, unsigned kind, const char *code, const char *name)
{
    struct tst_item it;
    if (kind != TST_K_REQUEST && kind != TST_K_ACCEPTED) return;
    zero(&it, sizeof it);
    it.kind = (unsigned char)kind;
    scopy(it.code, code, 8); scopy(it.name, name && name[0] ? name : code, 12);
    enqueue(t, now, &it);
}

void tst_note(struct tst *t, unsigned long now, unsigned kind, const char *code, const char *name, int join)
{
    struct tst_item it;
    if (kind != TST_K_DIRECT && kind != TST_K_ROOM) return;
    if (!code || !code[0]) return;
    zero(&it, sizeof it);
    it.kind = (unsigned char)kind;
    it.friends = join ? 1u : 0u;                                       /* TST_K_ROOM: 1 = the website's join note */
    scopy(it.code, code, 8); scopy(it.name, name && name[0] ? name : "A friend", 12);
    enqueue(t, now, &it);
}

void tst_msg(struct tst *t, unsigned long now, const char *code, const char *name, const char *text)
{
    struct tst_item it;
    unsigned i;
    if (!text || !text[0]) return;
    zero(&it, sizeof it);
    it.kind = TST_K_MSG;
    scopy(it.code, code, 8); scopy(it.name, name && name[0] ? name : code, 12); scopy(it.text, text, TST_MSG_TXT - 1u);
    if (t->cur.kind == TST_K_NONE) { show(t, &it, now); return; }
    if (t->qn == TST_QN) {
        for (i = 1; i < TST_QN; i++) t->q[i - 1u] = t->q[i];
        t->qn--;
    }
    t->q[t->qn++] = it;
}

void tst_hold(struct tst *t, unsigned long dt)
{
    if (t->cur.kind == TST_K_MSG) t->cur.until += dt;
}

int tst_fight_allowed(int mode, int recorded, const char *opp_code, int guest)
{
    if (!recorded || guest || !opp_code || !opp_code[0]) return 0;
    if (mode != TST_FM_RANKED && mode != TST_FM_DIRECT && mode != TST_FM_ROOM) return 0;
    { unsigned i, dig = 0, at = 0; for (i = 0; opp_code[i]; i++) { if (opp_code[i] == '@') { at = 1; break; } if (opp_code[i] >= '0' && opp_code[i] <= '9') dig = 1; } if (at && dig) return 0; }   /* a Sandbag (anonymous code: a digit before '@', ms_core.anon_code_like) is never offered as a friend */
    if (opp_code[0] == 'B' && opp_code[1] == 'O' && opp_code[2] == 'T' && (opp_code[3] == '#' || opp_code[3] == '@')) return 0;   /* the lab seat (BOT#101) and bot tags */
    return 1;
}

int tst_fight(struct tst *t, unsigned long now, const unsigned char mid[8], const char *name)
{
    struct tst_item it;
    unsigned i, d = 0;
    for (i = 0; i < 8u; i++) d |= mid[i] ^ t->fight_done[i];
    if (t->fight_have && !d) return 0;
    for (i = 0; i < 8u; i++) t->fight_done[i] = mid[i];
    t->fight_have = 1;
    zero(&it, sizeof it);
    it.kind = TST_K_FIGHT;
    for (i = 0; i < 8u; i++) it.mid[i] = mid[i];
    scopy(it.name, name, 12);                                           /* "" = the text says "your opponent" */
    enqueue(t, now, &it);
    return 1;
}

void tst_frame(struct tst *t, unsigned long now, int z, int zh)
{
    struct tst_item *c = &t->cur;
    if (c->kind == TST_K_NONE) { frd_hold_note(&t->hold, zh); if (t->qn) next(t, now); return; }
    if (c->busy) { frd_hold_note(&t->hold, zh); return; }               /* the glue's answer decides */
    if (c->kind == TST_K_INVITE) {
        frd_hold_note(&t->hold, zh);
        if (z && !reached(now, c->until + TST_GRACE_MS)) {
            c->busy = 1; t->act = TST_A_INVITE; t->sfx = TST_SFX_OK; t->dirty = 1; t->accepted++;
            return;
        }
        if (reached(now, c->until + TST_GRACE_MS)) { t->expired++; next(t, now); }   /* expiry just dismisses */
        return;
    }
    if (c->kind == TST_K_REQUEST) {                                     /* a friend request needs a HOLD of Z (a tap only starts the line and cancels on release) */
        if (frd_hold_step(&t->hold, now, zh, FRD_HOLD_MS)) {
            c->busy = 1; t->act = TST_A_FRIEND; t->sfx = TST_SFX_OK; t->dirty = 1;
            return;
        }
        if (reached(now, c->until) && !t->hold.on) next(t, now);        /* its own time-out (not in the middle of a hold) */
        return;
    }
    frd_hold_note(&t->hold, zh);
    if (z && (c->kind == TST_K_DIRECT || c->kind == TST_K_ROOM)) {
        c->busy = 1; t->act = c->kind == TST_K_DIRECT ? TST_A_DIRECT : TST_A_ROOM; t->sfx = TST_SFX_OK; t->dirty = 1;
        return;
    }
    if (z && c->kind == TST_K_FIGHT) {
        c->busy = 1; t->act = TST_A_FIGHT; t->sfx = TST_SFX_OK; t->dirty = 1;
        return;
    }
    if (reached(now, c->until)) next(t, now);
}

void tst_done(struct tst *t, unsigned long now, int ok, const char *msg)
{
    struct tst_item it;
    t->act = TST_A_NONE;
    if (!msg) { next(t, now); return; }
    zero(&it, sizeof it);
    it.kind = TST_K_INFO; it.info = msg;
    scopy(it.code, t->cur.code, 8); scopy(it.name, t->cur.name, 12);
    zero(&t->cur, sizeof t->cur);
    show(t, &it, now);
    if (!ok) t->sfx = TST_SFX_DENY;
}

void tst_done_friends(struct tst *t, unsigned long now)
{
    struct tst_item it;
    t->act = TST_A_NONE;
    zero(&it, sizeof it);
    it.kind = TST_K_INFO; it.friends = 1;
    scopy(it.code, t->cur.code, 8); scopy(it.name, t->cur.name, 12);
    zero(&t->cur, sizeof t->cur);
    show(t, &it, now);
}

void tst_clear(struct tst *t)
{
    zero(&t->cur, sizeof t->cur);
    t->qn = 0; t->act = TST_A_NONE; t->dirty = 1;
    t->status[0] = 0; t->status_dirty = 1;
}

int tst_on(const struct tst *t) { return t->cur.kind != TST_K_NONE; }

void tst_status(struct tst *t, const char *text)
{
    if (!text) text = "";
    if (same(text, t->status)) return;
    scopy(t->status, text, TST_LINE);
    t->status_dirty = 1;
}
const char *tst_status_text(const struct tst *t) { return t->cur.kind != TST_K_NONE ? "" : t->status; }

static char *put(char *d, char *e, const char *s) { while (*s && d < e) *d++ = *s++; *d = 0; return d; }

int tst_text(const struct tst *t, unsigned long now, char *l0, char *l1)
{
    const struct tst_item *c = &t->cur;
    char *e0 = l0 + TST_LINE, *e1 = l1 + TST_LINE, *p;
    l0[0] = 0; l1[0] = 0;
    switch (c->kind) {
    case TST_K_INVITE: {
        unsigned long left;
        p = put(l0, e0, "Challenge: "); put(p, e0, c->name);            /*: line 0 the message (white), line 1 only the button (yellow); short lines for a half-width plate */
        put(l1, e1, c->busy ? "Starting..." : "Z: Play Mano a mano");
        if (c->busy || reached(now, c->until)) return 0;
        left = c->until - now;
        if (left > TST_INVITE_MS) left = TST_INVITE_MS;
        return (int)(left * 1000ul / TST_INVITE_MS);
    }
    case TST_K_REQUEST:
        p = put(l0, e0, "Request: "); put(p, e0, c->name);
        put(l1, e1, c->busy ? "Accepting..." : "Hold Z: Add friend");
        return (!c->busy && t->hold.on && t->hold.pm > 0u) ? (int)t->hold.pm : -1;       /* the line FILLS along the hold (the invite's shrinks) */
    case TST_K_ACCEPTED:
        p = put(l0, e0, "New friend: "); put(p, e0, c->name);
        return -1;
    case TST_K_FIGHT:
        p = put(l0, e0, "Add "); p = put(p, e0, c->name[0] ? c->name : "your opponent"); put(p, e0, "?");
        put(l1, e1, c->busy ? "Sending..." : "Z: Add friend");
        return -1;
    case TST_K_DIRECT:
        p = put(l0, e0, "Slippi Direct: "); put(p, e0, c->code);
        put(l1, e1, c->busy ? "Starting..." : "Z: Play");
        return -1;
    case TST_K_ROOM:
        if (c->friends) {
            p = put(l0, e0, c->name); p = put(p, e0, "'s Room "); put(p, e0, c->code);
            put(l1, e1, c->busy ? "Opening..." : "Z: Join");
        } else {
            p = put(l0, e0, c->name); p = put(p, e0, ": Room "); put(p, e0, c->code);
            put(l1, e1, c->busy ? "Opening..." : "Z: Join Game Night");
        }
        return -1;
    case TST_K_MSG:
        p = put(l0, e0, c->name); p = put(p, e0, ": "); put(p, e0, c->text);
        return -1;
    case TST_K_INFO:
        if (c->friends) { p = put(l0, e0, "New friend: "); put(p, e0, c->name[0] ? c->name : c->code); return -1; }
        put(l0, e0, c->info ? c->info : "");
        return -1;
    default:
        return -1;
    }
}
