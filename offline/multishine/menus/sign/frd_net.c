/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * frd_net: the network of the Friends screen and of the Accounts row "Friend requests after a fight" (sign overlay).
 * The model raises a request (frd.req / sg.fight_req), this answers it with client_net/socc.c on the resident hub client's wire v2 session (the mnc the Update row and the
 * Game Night browser use): HELLO when there is no ready session, then the op; one request at a time; a forgotten session gets one fresh handshake.  The list is
 * fetched as the requests page (FRIEND_LIST kind 1) then the friends 8 at a time (kind 0, ext: sorted online first then last seen, with the Slippi fields), then, when the
 * server announces SOCC_CAP_RECENT, the recent players 16 at a time (kind 4; step 2).  A failed recent page is silent: no recent rows.
 * A challenge accepted (frd.play): the hand-off block of the hub client is armed (gn_state MNC_GN_DIRECT, the code) and the menu leaves for Slippi's online character
 * screen like the Multishine rows (console only, frdn_go_css); the lock-in searches the code in Direct mode (on_backend.c od_find).
 * Pure apart from frdn_go_css (-DGNB_MELEE): host-tested through tests/frdn_test.c against the real server.
 */
#include "frd_net.h"

static struct {
    unsigned char phase, what, rehello, step, fight_what;   /* phase: 0 idle, 1 HELLO running, 2 op running; step: LIST 0 requests, 3 invites, 1 friends, 2 recent players */
    unsigned char noinv;                                    /* the server refused FRIEND_LIST kind 5 (an older server): not asked again */
    struct socc_inv inv[SOCC_INV_PAGE];
    unsigned char nogame;                                   /* the server refused the friends page with the game bit (an older server): asked without it from now on, no Watch */
    unsigned char roff;                                     /* recent players fetched so far (the next page offset) */
    socc S;
    struct socc_friend buf[SOCC_RQ_PAGE];
    struct socc_prof prof;
    unsigned char have_prof;
} N;

void frdn_reset(void) { unsigned char *b = (unsigned char *)&N; unsigned i; for (i = 0; i < sizeof N; i++) b[i] = 0; }

int frdn_busy(void) { return N.phase != 0; }

static void scopy(char *d, const char *s, unsigned max) { unsigned i = 0; while (s[i] && i < max) { d[i] = s[i]; i++; } d[i] = 0; }

static unsigned why_of(const socc *s)
{
    if (s->err == SOCC_E_SERVER) {
        switch (s->status) {
        case SOCC_ST_UNKNOWN_OP: case SOCC_ST_NOT_SUPPORTED: return FRD_W_NOSOC;
        case SOCC_ST_AUTH_FAILED: case SOCC_ST_ACCOUNT_NEEDED: return FRD_W_SIGNIN;
        case SOCC_ST_NOT_FOUND: return FRD_W_NOTFOUND;
        case SOCC_ST_INVITE_GONE: return FRD_W_GONE;
        case SOCC_ST_INVITE_PENDING: return FRD_W_PENDING;
        case SOCC_ST_RATE_LIMITED: return FRD_W_RATE;
        case SOCC_ST_LIMIT_REACHED: return FRD_W_FULL;
        default: return FRD_W_BAD;
        }
    }
    return FRD_W_UNREACH;
}

/* the texts the model offers as messages: the player's own list when it has one, else the console's defaults (mnc lists, MNC_N x MNC_TXT) */
void frdn_texts(struct frd *f, const mnc *c)
{
    const char (*t)[MNC_TXT] = c->own[0][0] ? c->own : c->defaults;
    char tmp[FRD_NTXT][FRD_TXT];
    unsigned i;
    for (i = 0; i < FRD_NTXT && i < MNC_N; i++) scopy(tmp[i], t[i], FRD_TXT - 1u);
    frd_set_texts(f, (const char (*)[FRD_TXT])tmp, FRD_NTXT);
}

static int issue_frd(struct frd *f, unsigned now)
{
    socc *s = &N.S;
    switch (N.what) {
    case FRD_REQ_LIST:
        if (N.step == 0u) return socc_flist(s, now, 1, 0, N.buf, SOCC_RQ_PAGE);
        if (N.step == 2u) return socc_flist(s, now, 4, N.roff, N.buf, SOCC_RQ_PAGE);
        if (N.step == 3u) return socc_ilist(s, now, N.inv, SOCC_INV_PAGE);
        return socc_flist_x(s, now, 0, f->lpage, N.buf, SOCC_FL_PAGE, N.nogame ? 0u : 1u);       /* ext bits 0 + 1: the friend's game state */
    case FRD_REQ_INVITES: return socc_ilist(s, now, N.inv, SOCC_INV_PAGE);
    case FRD_REQ_INVRESP: return socc_invite_respond_l(s, now, (mnc_u32)f->arg_id, f->arg_yes);
    case FRD_REQ_ADD: return socc_friend_add(s, now, f->arg_code);
    case FRD_REQ_ANSWER: return socc_friend_answer(s, now, f->arg_code, f->arg_yes);
    case FRD_REQ_REMOVE: return socc_friend_remove(s, now, f->arg_code);
    case FRD_REQ_BLOCK: return socc_block(s, now, f->arg_code);
    case FRD_REQ_RMREC: return socc_recent_remove(s, now, f->arg_code);
    case FRD_REQ_INVITE: return socc_invite_send(s, now, f->arg_code);
    case FRD_REQ_STATUS: return socc_invite_status(s, now, (mnc_u32)f->inv_id);
    case FRD_REQ_MSG: return f->arg_txt < FRD_NTXT && f->txt[f->arg_txt][0] ? socc_msg_send(s, now, f->arg_code, f->txt[f->arg_txt]) : SOCC_E_ARG;
    default: return SOCC_E_ARG;
    }
}

static unsigned io_caps(const mnc *c) { return c ? (unsigned)c->srv_caps : 0u; }

static int inv_refused(const socc *s)                                        /* the server refused kind 5 itself (an older server answers BAD_REQUEST): not asked again; a rate limit or a missing session says nothing about it */
{
    return s->err == SOCC_E_SERVER && s->status != SOCC_ST_AUTH_FAILED && s->status != SOCC_ST_ACCOUNT_NEEDED && s->status != SOCC_ST_RATE_LIMITED;
}

static void invites_done(struct frd *f)
{
    socc *s = &N.S;
    struct frd_in v[FRD_IN];
    unsigned i;
    for (i = 0; i < s->n && i < FRD_IN && i < SOCC_INV_PAGE; i++) {
        v[i].type = N.inv[i].type; v[i].id = N.inv[i].id; v[i].age = N.inv[i].age; v[i].left = N.inv[i].left;
        scopy(v[i].code, N.inv[i].code, 8); scopy(v[i].name, N.inv[i].name, 12);
    }
    frd_net_invites(f, i, v);
}

static void done_frd(struct frd *f)
{
    socc *s = &N.S;
    unsigned i;
    switch (N.what) {
    case FRD_REQ_LIST:
        if (N.step == 0u) {
            struct frd_rq rq[FRD_RQ];
            for (i = 0; i < s->n && i < FRD_RQ; i++) { scopy(rq[i].code, N.buf[i].code, 8); scopy(rq[i].name, N.buf[i].name, 12); }
            frd_net_requests(f, s->total, i, rq);
            N.step = (unsigned char)(!N.noinv && (io_caps(s->c) & SOCC_CAP_TOASTS) ? 3u : 1u);        /* the Invites next (a server with toasts), then the friends */
            N.phase = 3;                                                     /* the friends page next (same request) */
            return;
        } else if (N.step == 3u) {
            invites_done(f);
            N.step = 1;
            N.phase = 3;                                                     /* the friends page next (same request) */
            return;
        } else if (N.step == 2u) {
            struct frd_rc rc[SOCC_RQ_PAGE];
            for (i = 0; i < s->n && i < SOCC_RQ_PAGE; i++) { scopy(rc[i].code, N.buf[i].code, 8); scopy(rc[i].name, N.buf[i].name, 12); }
            frd_net_recent(f, s->total, N.roff, i, rc);
            N.roff = (unsigned char)(N.roff + i);
            if (i && N.roff < s->total && N.roff < FRD_RC) { N.phase = 3; return; }       /* the next recent page (same request) */
        } else {
            struct frd_ent e[SOCC_FL_PAGE];
            for (i = 0; i < s->n && i < SOCC_FL_PAGE; i++) {
                scopy(e[i].code, N.buf[i].code, 8); scopy(e[i].name, N.buf[i].name, 12);
                e[i].presence = N.buf[i].presence; e[i].flags = N.buf[i].flags; e[i].seen = N.buf[i].seen;
                scopy(e[i].scode, N.buf[i].scode, 8); scopy(e[i].sname, N.buf[i].sname, 15);
                scopy(e[i].room, N.buf[i].room, 4); e[i].rflags = N.buf[i].rflags; e[i].gmode = N.buf[i].gmode; e[i].gflags = N.buf[i].gflags;       /* a friend in a Game Night room we may join (flags bit 2) */
            }
            frd_net_friends(f, s->total, f->lpage, i, e);
            if (f->req == FRD_REQ_NONE && (io_caps(s->c) & SOCC_CAP_RECENT)) {          /* the last friends page: the recent players next */
                N.step = 2; N.roff = 0; N.phase = 3;
                return;
            }
        }
        break;
    case FRD_REQ_INVITES: invites_done(f); break;
    case FRD_REQ_INVRESP: frd_net_invresp(f, s->peer); break;
    case FRD_REQ_INVITE: frd_net_invite(f, s->inv_id); break;
    case FRD_REQ_STATUS: frd_net_status(f, s->result, s->peer); break;
    case FRD_REQ_ADD: frd_net_done(f, FRD_REQ_ADD, s->result); break;
    default: frd_net_done(f, N.what, 0); break;
    }
    N.phase = 0;
}

static int session_ready(const struct frdn_io *io, unsigned now) { return io->net->have_hello && mw2_ready(&io->net->w, now); }

/* one frame: the Friends screen (f, may be 0) and the Online page's two settings (fight / pol / fight_req of struct sg: fight 0 unknown, 1 off, 2 on, 3 not available;
   pol 0 unknown, 1 Friends, 2 Anyone With My Code, 3 Nobody, 4 not available; req 1 get, 2 set the fight byte, 3 set the policy) */
static unsigned char *A_rgn, *A_wch;                                         /* the Online page's Region and Who can watch my games (struct sg rgn / wch; 0 = the caller has none) */
static void settings_na(unsigned char *fight, unsigned char *pol)
{
    if (fight) *fight = 3;
    if (pol) *pol = 4;
    if (A_rgn) *A_rgn = 9;                                                    /* SG_RGN_NA */
    if (A_wch) *A_wch = 3;
}
void frdn_frame(struct frd *f, unsigned char *fight, unsigned char *fight_req, unsigned char *pol, unsigned char *rgn, unsigned char *wch, const struct frdn_io *io, unsigned now)
{
    mnc *c = io->net;
    socc *s = &N.S;
    A_rgn = rgn; A_wch = wch;
    if (f && f->abort) {                                                     /* the screen dropped the request (B, or its 12 s limit): stop what runs; a late reply no longer matches anything */
        f->abort = 0;
        if (N.phase == 2) socc_abort(s);
        else if (N.phase == 1 && c && c->state == MNC_BUSY) { c->state = MNC_IDLE; c->task = 0; c->inflight = 0; c->err = MNC_E_NONE; }   /* (mnc_abort inline: mnc.c is the resident's, the overlay may not import it) */
        N.phase = 0; N.what = 0; N.step = 0;
    }
    if (N.phase == 0) {
        unsigned what = 0;
        if (f && f->req != FRD_REQ_NONE) { what = f->req; f->req = FRD_REQ_NONE; N.fight_what = 0; }
        else if (fight_req && *fight_req) { what = 100u + *fight_req; if (what == 105u) what = 104u; *fight_req = 0; }
        if (!what) return;
        N.what = (unsigned char)what; N.step = 0; N.rehello = 0;
        if (!c || !io->link_up || !io->link_up() || !io->hello || !io->poll || c->state == MNC_BUSY) goto unreach;
        if (!session_ready(io, now)) { if (io->hello(now) != 0) goto unreach; N.phase = 1; return; }
        N.phase = 3;
    }
    if (N.phase == 1) {
        (void)io->poll(now);
        if (c->state == MNC_DONE) N.phase = 3;
        else if (c->state == MNC_FAILED) goto unreach;
        else return;
    }
    if (N.phase == 3) {                                                      /* issue */
        int r;
        if (!c->w.dev_sess) { if (N.what >= 100u) { settings_na(fight, pol); N.phase = 0; return; } if (f) frd_net_fail(f, N.what, FRD_W_SIGNIN); N.phase = 0; return; }
        if (!(c->srv_caps & SOCC_CAP_SOCIAL)) { if (N.what >= 100u) { settings_na(fight, pol); N.phase = 0; return; } if (f) frd_net_fail(f, N.what, FRD_W_NOSOC); N.phase = 0; return; }
        if (f && N.what == FRD_REQ_MSG && !(c->srv_caps & SOCC_CAP_MESSAGES)) { frd_net_fail(f, N.what, FRD_W_NOMSG); N.phase = 0; return; }
        socc_init(s, c);
        if (f && N.what == FRD_REQ_LIST && N.step == 2u && N.roff == 0u) frd_net_recent(f, 0, 0, 0, 0);       /* a refresh: the old recent rows go while the new ones come */
        if (N.what == 101u) r = socc_prof_get(s, now);
        else if (N.what == 102u && N.have_prof) r = socc_prof_set_fight(s, now, &N.prof, fight && *fight == 2u);   /* the other four bytes exactly as read: only the 5th changes */
        else if (N.what == 103u && N.have_prof && pol && *pol >= 1u && *pol <= 3u) {   /* the policy byte (2nd); the others exactly as read */
            struct socc_prof p = N.prof;
            p.policy = (mnc_u8)(*pol - 1u);
            r = socc_prof_set_fight(s, now, &p, p.fight != 0);
        }
        else if (N.what == 104u && N.have_prof && A_rgn && A_wch && *A_rgn >= 1u && *A_rgn <= 8u && *A_wch >= 1u && *A_wch <= 2u) {   /* Region / watch: the 7-byte set, the other values as last read */
            r = socc_prof_set_acct(s, now, &N.prof, N.prof.fight != 0, *A_rgn == 8u ? 0xFFu : (unsigned)(*A_rgn - 1u), (unsigned)(*A_wch - 1u));
        }
        else if (N.what == 102u || N.what == 103u || N.what == 104u) { N.what = 101u; r = socc_prof_get(s, now); }  /* never write settings that were not read first */
        else if (f) r = issue_frd(f, now);
        else r = SOCC_E_ARG;
        if (r) goto fail;
        N.phase = 2;
        return;
    }
    if (N.phase == 2) {
        int st = socc_poll(s, now);
        if (st == SOCC_BUSY) return;
        if (st == SOCC_DONE) {
            if (N.what >= 100u) {
                N.prof = s->prof; N.have_prof = 1;
                if (fight) *fight = s->prof.fight ? 2u : 1u;
                if (pol) *pol = s->prof.policy <= 2u ? (unsigned char)(s->prof.policy + 1u) : 4u;
                if (N.what == 101u || N.what == 104u) {                         /* the 5-byte answers of the fight / policy sets carry no account values: left alone */
                    if (A_rgn) *A_rgn = s->prof.have_acct ? (s->prof.region == 0xFFu ? 8u : s->prof.region < SOCC_REGION_N ? (unsigned char)(s->prof.region + 1u) : 9u) : 9u;     /* 1..7 regions (7 NAC), 8 not set, 9 not available */
                    if (A_wch) *A_wch = s->prof.have_acct ? (s->prof.watch ? 2u : 1u) : 3u;
                }
                N.phase = 0; return;
            }
            if (f) done_frd(f); else N.phase = 0;
            return;
        }
        if (s->err == SOCC_E_SERVER && N.what == FRD_REQ_LIST && N.step == 1u && !N.nogame && s->status != SOCC_ST_AUTH_FAILED && s->status != SOCC_ST_ACCOUNT_NEEDED
            && s->status != SOCC_ST_NOT_SUPPORTED && s->status != SOCC_ST_UNKNOWN_OP && s->status != SOCC_ST_RATE_LIMITED) {
            N.nogame = 1; N.phase = 3; return;                                   /* an older server does not know ext bit 1 (it answers BAD_REQUEST): the same page again without it, once */
        }
        if (s->err == SOCC_E_NOSESSION && !N.rehello && io->hello(now) == 0) { N.rehello = 1; N.phase = 1; return; }   /* the server forgot the session: one fresh handshake */
        if (s->err == SOCC_E_TIMEOUT && !N.rehello && N.what < 100u) {       /* silence for 5 sends: a session the server dropped without a word (a restart, a changed address) looks the same; forget it, handshake once, ask once more */
            mw2_drop(&c->w);
            if (io->hello(now) == 0) { N.rehello = 1; N.phase = 1; return; }
        }
        if (N.what == FRD_REQ_LIST && N.step == 3u) {                            /* the Invites are an extra: a failed page leaves them out and the friends follow (an older server: not asked again) */
            if (inv_refused(s)) N.noinv = 1;
            frd_net_invites(f, 0, 0);
            N.step = 1; N.phase = 3; return;
        }
        goto fail;
    }
    return;
unreach:
    N.phase = 0;
    if (N.what == FRD_REQ_LIST && N.step == 2u) return;                      /* (same: the recent players are an extra) */
    if (N.what == FRD_REQ_INVITES) return;                                   /* (the Invites alone, every 10 s: a failed ask is silent) */
    if (N.what >= 100u) { settings_na(fight, pol); return; }
    if (f) frd_net_fail(f, N.what, FRD_W_UNREACH);
    return;
fail:
    N.phase = 0;
    if (N.what == FRD_REQ_LIST && N.step == 2u) return;                      /* the recent players are an extra: a failed page just leaves them out */
    if (N.what == FRD_REQ_INVITES && inv_refused(s)) { N.noinv = 1; if (f) frd_net_invites(f, 0, 0); N.phase = 0; return; }       /* an older server: no Invites list */
    if (N.what >= 100u) { settings_na(fight, pol); return; }
    if (f) frd_net_fail(f, N.what, why_of(s));
}

#ifdef GNB_MELEE
/* ---- console: the hand-off and the way to the character screen (ml_glue.c go_mode, Unranked: no code keyboard; the lock-in's B4 searches the armed code) ---- */
#define FR_H(a) (*(volatile unsigned short *)(a))
#define FR_B(a) (*(volatile unsigned char *)(a))
void frdn_go_css(mnc *c, const char *code)
{
    if (!c || c->gn_magic != MNC_GN_MAGIC) return;
    scopy(c->gn_room, code, sizeof c->gn_room - 1u);
    c->gn_state = MNC_GN_DIRECT;                                            /* the lock-in's B4 searches this code; the scene change itself is made by the resident Main Menu after this overlay has closed
                                                                               (the caller armed it with env opt 70: ml_glue.c g_slip_back == 3).  Leaving from inside the Event overlay left the closed Event screen frozen on screen. */
}
#endif
