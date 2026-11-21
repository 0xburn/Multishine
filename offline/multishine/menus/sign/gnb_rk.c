/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* A Grandmaster set on the Game Night browser.  Not a translation unit of its own: gnb.c includes it at its end (as gnb_prep.c), so it uses that
   file's statics and every build that lists gnb.c carries it.  Pure C89, no libc, no heap, no network, no Melee address.
 *
 * The ranked set has no room.  What the lobby needs to show it (the two players with their characters, the set score, the pick grid on this console's character step) is built from the Grandmaster
 * set view (RK_SET_STATE, struct gnp_gm) into a lobby record of the Sweaty kind; the stage prep screen (gnb_prep.h) takes the same set view through gnp_from_gm.  The glue (sg_entry.c) owns
 * the network and the hand-off block; this file owns the decisions that are pure: is the next game decided, must the console wait for the server to count the game that was just played,
 * is the set over.  The front end that draws the prep screen is behind gnp_from_gm / the drawer, so a new front end (the GameSetup port) replaces the drawing, not this.
 *
 * The next game starts when the view is READY (phase 1) for a game number above the last one this console started (g->rk.played).  A READY view for the game that was just played means the
 * server has not counted its result yet (both consoles must report): the screen waits.  After GNRK_REPLAY_FRAMES without a change the same game is started again: that is what a tie or a void
 * game looks like (the server replays it with the same stage and no new selection). */

static char rk_text[40];

void gnrk_start(struct gnb *g, unsigned played)
{
    zero(&g->rk, (unsigned)sizeof g->rk);
    g->rk.on = 1; g->rk.played = (unsigned char)played;
    g->live = 1;
}

/* the set is over: one line (static, the model keeps the pointer) */
static const char *rk_result(const struct gnp_gm *s)
{
    unsigned ms = s->my_slot - 1u;
    unsigned mine = s->score[ms], theirs = s->score[ms ^ 1u];
    unsigned won = s->result == s->my_slot;
    rk_text[0] = 0;
    if (!s->result) { cpy(rk_text, "The set is over", 38); return rk_text; }
    cat(rk_text, won ? "You won the set " : "You lost the set ", 38);
    catu(rk_text, mine, 38); cat(rk_text, " - ", 38); catu(rk_text, theirs, 38);
    return rk_text;
}

void gnrk_view(struct gnb *g, const struct gnp_gm *s, const unsigned char ch[4], const char *me, const char *opp)
{
    struct gnrk *k = &g->rk;
    struct gnb_lobby v;
    unsigned ms, i;
    if (!s || !s->have || (s->my_slot != 1u && s->my_slot != 2u)) return;
    ms = s->my_slot - 1u;
    k->go = 0; k->wait = 0; k->fin = (unsigned char)(s->phase == 2u);
    if (s->phase == 1u) {
        if (s->game > k->played) k->go = 1;
        else if (k->waited >= GNRK_REPLAY_FRAMES) k->go = 1;                  /* the same game again: a tie or a void game (the server replays it with the same stage) */
        else k->wait = 1;
    }
    if (!k->wait) k->waited = 0;
    k->game = s->game; k->stage = s->stage;
    k->ch = ch[ms * 2u]; k->cos = ch[ms * 2u + 1u];
    zero(&v, (unsigned)sizeof v);
    v.have = 1; v.phase = 1; v.flags = GNB_V_CALLED | GNB_V_ONSIDE; v.members = 2; v.side = (unsigned char)ms;
    v.side_on[0] = 1; v.side_on[1] = 1;
    v.side_char[ms] = ch[ms * 2u]; v.side_char[ms ^ 1u] = ch[(ms ^ 1u) * 2u];
    cpy(v.side_name[ms], me && me[0] ? me : "You", 12); cpy(v.side_name[ms ^ 1u], opp && opp[0] ? opp : "Opponent", 12);
    cpy(v.rname, "Grandmaster", 12);
    v.v3 = 1; v.cap = 2; v.stage = s->stage;
    v.v6 = 1; v.rtype = GNB_RT_SWEATY; v.best_of = s->best_of; v.game = s->game; v.score_w = s->score[0]; v.score_c = s->score[1];
    v.step = 0;
    if (s->phase == 0u) v.step = (unsigned char)(s->step == 1u ? 2u : s->step == 2u ? 3u : s->step == 3u ? 4u : s->step == 4u ? 5u : 0u);
    v.step_owner = (s->phase == 0u && s->step && s->owner >= 1u && s->owner <= 2u) ? (unsigned char)(s->owner - 1u) : 0xFFu;
    v.step_secs = s->secs; v.stage_avail = s->avail; v.stage_n = s->n > 8u ? 8u : s->n;
    for (i = 0; i < 8u; i++) v.stage_list[i] = s->id[i];
    v.forced = GNB_RT_NONE;
    v.turn = (v.step == GNB_STEP_PICK_CHAR && v.step_owner < 2u) ? v.step_owner : 0xFFu;
    v.secs = s->secs;
    gnb_net_view(g, &v);
    gnp_from_gm(&g->prep, s, me, opp);
    prep_chars(g);
    if (k->fin) {                                                           /* the result replaces the prep screen */
        g->prep.open = 0; g->prep.dirty = 1; g->pick_open = 0; g->pick_lock = 0;
        g->msg = rk_result(s);
        g->dirty = GNB_D_ALL;
    } else if (k->wait) {                                                   /* the game was played: the server counts it when both consoles have reported */
        g->prep.step = GNP_WAIT; g->prep.mine = 0; g->prep.open = 1; g->prep.secs = 0xFFu; g->prep.dirty = 1;
    }
    if (g->prep.open && !(g->prep.step == GNP_CHAR && g->pick_open)) { g->pick_open = 0; g->ros_open = 0; }
}

/* one frame: counts the wait for the server to count the game */
void gnrk_tick(struct gnb *g)
{
    if (g->rk.on && g->rk.wait && g->rk.waited < 0xFFFFu) g->rk.waited++;
}

/* the right-hand panel of the set screen (the lobby's rows for a room - queue, streak, crowns, type - mean nothing here): the title, the pick prompt, the game, the score, the two players */
static unsigned gnrk_panel(const struct gnb *g, unsigned line, char *out)
{
    const struct gnb_lobby *v = &g->lob;
    unsigned me = v->side < 2u ? v->side : 0u, mine = me ? v->score_c : v->score_w, theirs = me ? v->score_w : v->score_c;
    switch (line) {
    case 0: cpy(out, "Grandmaster", GNB_LINE); return 2;
    case 1: if (g->rk.fin || g->rk.wait) return 0; lobby_status(g, out); return 2;
    case 2: cpy(out, "Game ", GNB_LINE); catu(out, v->game, GNB_LINE); cat(out, " of ", GNB_LINE); catu(out, v->best_of, GNB_LINE); return 0;
    case 3: cpy(out, "Score: ", GNB_LINE); catu(out, mine, GNB_LINE); cat(out, " - ", GNB_LINE); catu(out, theirs, GNB_LINE); return 0;
    case 4: cpy(out, "You: ", GNB_LINE); cat(out, v->side_name[me], GNB_LINE); return 0;
    case 5: cpy(out, "Opponent: ", GNB_LINE); cat(out, v->side_name[me ^ 1u], GNB_LINE); return 0;
    default: return 0;
    }
}
