/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Stage prep screen model (gnb_prep.h).  Pure C89, no libc, no heap. */
#include "gnb_prep.h"

/* buttons (the values of gnb.h GNB_B_*; this file stays independent of gnb.h) */
#define PB_LEFT 0x0001u
#define PB_RIGHT 0x0002u
#define PB_DOWN 0x0004u
#define PB_B 0x0200u
#define PB_UP 0x0008u
#define PB_A 0x0100u
#define PB_START 0x1000u

static unsigned pp_slen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static void pp_cpy(char *d, const char *s, unsigned max) { unsigned i = 0; while (s[i] && i < max) { d[i] = s[i]; i++; } d[i] = 0; }
static void pp_cat(char *d, const char *s, unsigned max) { unsigned n = pp_slen(d); pp_cpy(d + n, s, max > n ? max - n : 0u); }
static void pp_catu(char *d, unsigned v, unsigned max)
{
    char t[4];
    unsigned k = 0;
    if (v >= 100u) t[k++] = (char)('0' + v / 100u % 10u);
    if (v >= 10u) t[k++] = (char)('0' + v / 10u % 10u);
    t[k++] = (char)('0' + v % 10u);
    t[k] = 0;
    pp_cat(d, t, max);
}
static void pp_zero(void *q, unsigned n) { unsigned char *b = (unsigned char *)q; while (n--) *b++ = 0; }

void gnp_clear(struct gnp *p)
{
    unsigned char res[9], seen;
    unsigned i;
    for (i = 0; i < 9u; i++) res[i] = p->res[i];
    seen = p->sc_seen;
    pp_zero(p, sizeof *p);
    for (i = 0; i < 9u; i++) p->res[i] = res[i];
    p->sc_seen = seen;
    p->chosen = 0xFFu; p->secs = 0xFFu;
    p->ch[0] = p->ch[1] = 0xFFu; p->tier[0] = p->tier[1] = 0xFFu; p->pend = 0xFFu; p->prevc = p->cos = 0xFFu;
}

const char *gnp_stage_name(unsigned id)
{
    switch (id) {                                          /* external stage ids (the Vs stage ids of the room wire) */
    case 0x02u: return "Fountain of Dreams";
    case 0x03u: return "Pokemon Stadium";
    case 0x08u: return "Yoshi's Story";
    case 0x1Fu: return "Battlefield";
    case 0x1Cu: return "Dream Land";
    case 0x20u: return "Final Destination";
    default: return "Another stage";
    }
}

/* Slippi's stage rows: the strike row of five is Fountain, Dream Land, Yoshi's, Final Destination, Battlefield; the counterpick row of six is Fountain, Dream Land,
   Yoshi's, Battlefield, Final Destination, Pokemon.  The server lists them in its own order; the screen shows them in Slippi's and an action carries the server's index (srv[]). */
unsigned gnp_rank(unsigned n, unsigned id)
{
    static const unsigned char STR[5] = { 0x02u, 0x1Cu, 0x08u, 0x20u, 0x1Fu };
    static const unsigned char CP[6] = { 0x02u, 0x1Cu, 0x08u, 0x1Fu, 0x20u, 0x03u };
    unsigned i, m = n > 5u ? 6u : 5u;
    const unsigned char *t = n > 5u ? CP : STR;
    for (i = 0; i < m; i++) if (t[i] == id) return i;
    return 100u + id;
}
static void pp_sort(struct gnp *p)
{
    unsigned i, j;
    for (i = 0; i < p->n; i++) p->srv[i] = (unsigned char)i;
    for (i = 1; i < p->n; i++)
        for (j = i; j > 0 && gnp_rank(p->n, p->id[j]) < gnp_rank(p->n, p->id[j - 1u]); j--) {
            unsigned char t = p->id[j]; p->id[j] = p->id[j - 1u]; p->id[j - 1u] = t;
            t = p->state[j]; p->state[j] = p->state[j - 1u]; p->state[j - 1u] = t;
            t = p->srv[j]; p->srv[j] = p->srv[j - 1u]; p->srv[j - 1u] = t;
        }
}

static int pp_free(const struct gnp *p, unsigned i) { return i < p->n && p->state[i] == GNP_S_FREE; }
static void pp_fix(struct gnp *p)
{
    unsigned i;
    if (pp_free(p, p->cur)) return;
    for (i = 0; i < p->n; i++) if (pp_free(p, i)) { p->cur = (unsigned char)i; return; }
    if (p->cur >= p->n) p->cur = 0;
}

static unsigned pp_need(const struct gnp *p)
{
    if (p->step != GNP_STRIKE) return 1u;
    if (p->remaining) return p->remaining > 1u ? 2u : 1u;
    return (p->kind == 0u && p->game <= 1u && p->struck_n == 1u) ? 2u : 1u;                    /* a room's game 1: one, two, one strike: the second player's first strike asks for two (the view carries no count) */
}
static void pp_unmark(struct gnp *p) { unsigned i; for (i = 0; i < GNP_MAX; i++) p->sel[i] = 0; p->nsel = 0; p->confirm = 0; p->btn = 0; }

static void pp_settle(struct gnp *p, unsigned step_code)
{
    unsigned i;
    if (step_code != p->prev || !p->mine) { pp_unmark(p); p->pend = 0xFFu; }
    else for (i = 0; i < p->n; i++) if (p->sel[i] && p->state[i] != GNP_S_FREE) { p->sel[i] = 0; if (p->nsel) p->nsel--; }
    p->need = (unsigned char)pp_need(p);
    if (p->step == GNP_CHAR) { p->confirm = (unsigned char)p->mine; p->nsel = p->confirm; if (!p->confirm) p->btn = 0; }       /* the character step: OK / Change are up for the player to act */
    else if (p->nsel < p->need) p->confirm = 0;
    /* a new view: the busy guard ends, the clock restarts, a changed step is a change of the whole screen */
    p->busy = 0; p->frames = 0; p->dirty = 1;
    if (step_code != p->prev) { p->cur = 0; p->prev = (unsigned char)step_code; if (p->mine && (p->step == GNP_STRIKE || p->step == GNP_BAN || p->step == GNP_PICK)) p->sfx = 2; }
    pp_fix(p);
    if (p->pend != 0xFFu && p->nsel == 0u) {                                      /* the second strike of a confirmed pair: the view that shows the first struck is back (its mark is gone) */
        if (p->mine && p->step == GNP_STRIKE && p->pend < p->n && p->state[p->pend] == GNP_S_FREE) { p->act = GNP_A_STRIKE; p->val = p->srv[p->pend]; p->busy = 1; p->dirty = 1; }
        p->pend = 0xFFu;
    }
}

/* the order the stages were struck / banned in, as this console saw them (a view carries the states, not their order): a stage that is new in this view gets the next number (the ones new in one
   view, e.g. the opponent's two strikes, in list order); a screen that was not up, or a new game, starts over */
static void pp_track(struct gnp *p, unsigned was_open, unsigned old_game)
{
    unsigned i;
    if (!was_open || old_game != p->game) { pp_zero(p->order, sizeof p->order); p->nord = 0; }
    for (i = 0; i < GNP_MAX; i++) if (p->order[i] && (i >= p->n || (p->state[i] != GNP_S_STRUCK && p->state[i] != GNP_S_BANNED))) p->order[i] = 0;
    p->nord = 0;
    for (i = 0; i < p->n; i++) if (p->order[i] && p->order[i] > p->nord) p->nord = p->order[i];
    for (i = 0; i < p->n; i++) if (!p->order[i] && (p->state[i] == GNP_S_STRUCK || p->state[i] == GNP_S_BANNED)) p->order[i] = ++p->nord;
}

/* the pips: how each game went, as this console saw the set go (a view carries only the two scores): a new game's score that grew by one fixes the result of the game before it; a set joined late
   fills wins first, then losses */
static void pp_results(struct gnp *p)
{
    unsigned total = (unsigned)p->sc_me + p->sc_opp, i, w = 0, l = 0;
    if (p->best_of > 9u || total > 9u) return;
    for (i = 0; i < total; i++) { if (p->res[i] == 1u) w++; else if (p->res[i] == 2u) l++; }
    if (w == p->sc_me && l == p->sc_opp && p->sc_seen <= total) { p->sc_seen = (unsigned char)total; return; }
    if (total == (unsigned)p->sc_seen + 1u && p->res[total - 1u] == 0u) {
        unsigned w0 = 0;
        for (i = 0; i + 1u < total; i++) if (p->res[i] == 1u) w0++;
        if (w0 <= p->sc_me && w0 + 1u >= p->sc_me) { p->res[total - 1u] = (unsigned char)(p->sc_me > w0 ? 1u : 2u); p->sc_seen = (unsigned char)total; return; }
    }
    for (i = 0; i < 9u; i++) p->res[i] = 0;
    for (i = 0; i < total; i++) p->res[i] = (unsigned char)(i < p->sc_me ? 1u : 2u);
    p->sc_seen = (unsigned char)total;
}

void gnp_from_room(struct gnp *p, const struct gnp_t6 *t, unsigned side, unsigned phase, unsigned called, unsigned turn, unsigned stage, const char *name0, const char *name1)
{
    unsigned i, nst, was_open = p->open, old_game = p->game;
    unsigned char cur_keep = p->cur;
    p->open = 0;
    if (!t || !t->have || side > 1u || phase != 1u || !called || t->best_of < 2u || t->n > GNP_MAX) { gnp_clear(p); return; }
    p->kind = 0;
    p->best_of = t->best_of; p->game = t->game;
    p->sc_me = t->sc[side]; p->sc_opp = t->sc[side ^ 1u];
    pp_results(p);
    p->me_first = p->game <= 1u ? (unsigned char)(side == 0u) : (unsigned char)(p->game >= 2u && p->res[p->game - 2u] == 1u);      /* game 1: seat 0 strikes first; later: the previous winner's character comes first */
    pp_cpy(p->me, side ? name1 : name0, GNP_NAME - 1u); pp_cpy(p->opp, side ? name0 : name1, GNP_NAME - 1u);
    if (!p->me[0]) pp_cpy(p->me, "You", GNP_NAME - 1u);
    if (!p->opp[0]) pp_cpy(p->opp, "Opponent", GNP_NAME - 1u);
    nst = t->n;
    p->n = (unsigned char)nst; p->chosen = 0xFFu; p->struck_n = 0; p->remaining = 0;
    for (i = 0; i < nst; i++) {
        p->id[i] = t->st[i];
        p->state[i] = (unsigned char)((t->avail >> i) & 1u ? GNP_S_FREE : (t->struck >> i) & 1u ? GNP_S_STRUCK : GNP_S_OUT);
        if ((t->struck >> i) & 1u) p->struck_n++;
    }
    pp_sort(p);
    pp_track(p, was_open, old_game);
    p->secs = t->secs;
    if (turn == GNP_TURN_WAIT) { p->step = GNP_WAIT; p->mine = 0; p->open = 1; }
    else if (turn == GNP_TURN_GO && stage != 0xFFu && nst) {                /* the game's stage is fixed: the one row that is not crossed out */
        p->step = GNP_PLAY; p->mine = 0; p->open = 1;
        for (i = 0; i < nst; i++) if (p->id[i] == stage) { p->chosen = (unsigned char)i; p->state[i] = GNP_S_CHOSEN; }
    }
    else if (t->step == GNP_RV_STRIKE || t->step == GNP_RV_BAN || t->step == GNP_RV_PICK_STAGE) {
        p->step = (unsigned char)(t->step == GNP_RV_STRIKE ? GNP_STRIKE : t->step == GNP_RV_BAN ? GNP_BAN : GNP_PICK);
        p->mine = (unsigned char)(t->owner == side);
        p->open = 1;
    }
    else if (t->step == GNP_RV_PICK_CHAR && t->game >= 2u && nst) {         /* a later game's character step: the fighter of last game with OK / Change on this screen (game 1's characters are picked in the lobby before the strikes) */
        p->step = GNP_CHAR; p->mine = (unsigned char)(t->owner == side); p->open = 1;
        for (i = 0; stage != 0xFFu && i < nst; i++) if (p->id[i] == stage) { p->chosen = (unsigned char)i; p->state[i] = GNP_S_CHOSEN; }
    }
    else { p->step = GNP_NONE; p->mine = 0; p->open = 0; }                  /* a game 1 character step (the lobby's own pick grid) or no step */
    if (p->open) {
        if (!was_open) p->cur = 0;
        else p->cur = cur_keep;
        pp_settle(p, p->step == GNP_WAIT ? 200u : p->step == GNP_PLAY ? 201u : t->step);
        if (p->step != GNP_STRIKE && p->step != GNP_BAN && p->step != GNP_PICK && p->step != GNP_CHAR) p->secs = 0xFFu;
    } else { p->prev = 0; p->busy = 0; p->dirty = 1; }
}

void gnp_from_gm(struct gnp *p, const struct gnp_gm *s, const char *me, const char *opp)
{
    unsigned i, was_open = p->open, ms, old_game = p->game;
    unsigned char cur_keep = p->cur;
    p->open = 0;
    if (!s || !s->have || s->n > GNP_MAX || (s->my_slot != 1u && s->my_slot != 2u) || s->best_of < 2u) { gnp_clear(p); return; }
    ms = s->my_slot - 1u;
    p->kind = 1;
    p->tier[0] = s->tier[ms]; p->tier[1] = s->tier[ms ^ 1u];                 /* the header's rank icons: this console's player first */
    
    p->best_of = s->best_of; p->game = s->game;
    p->sc_me = s->score[ms]; p->sc_opp = s->score[ms ^ 1u];
    pp_results(p);
    p->me_first = p->game <= 1u ? (unsigned char)(s->p0 == s->my_slot) : (unsigned char)(s->prev_winner == s->my_slot);
    pp_cpy(p->me, me && me[0] ? me : "You", GNP_NAME - 1u); pp_cpy(p->opp, opp && opp[0] ? opp : "Opponent", GNP_NAME - 1u);
    p->n = s->n; p->chosen = 0xFFu; p->struck_n = 0; p->remaining = s->remaining;
    for (i = 0; i < s->n; i++) {
        p->id[i] = s->id[i];
        switch (s->state[i]) {
        case 0u: p->state[i] = (unsigned char)((s->avail >> i) & 1u ? GNP_S_FREE : GNP_S_OUT); break;
        case 1u: p->state[i] = GNP_S_STRUCK; p->struck_n++; break;
        case 2u: p->state[i] = GNP_S_BANNED; break;
        case 3u: p->state[i] = GNP_S_DAVE; break;
        default: p->state[i] = GNP_S_OUT; break;
        }
    }
    pp_sort(p);
    pp_track(p, was_open, old_game);
    p->secs = s->secs;
    if (s->phase == 1u && s->stage != 0xFFu && s->n) {
        p->step = GNP_PLAY; p->mine = 0; p->open = 1;
        for (i = 0; i < s->n; i++) if (p->id[i] == s->stage) { p->chosen = (unsigned char)i; p->state[i] = GNP_S_CHOSEN; }
    } else if (s->phase == 0u && s->step == 4u && s->game >= 2u) {          /* a later game's character step (see gnp_from_room) */
        p->step = GNP_CHAR; p->mine = (unsigned char)(s->owner == s->my_slot); p->open = 1;
    } else if (s->phase == 0u && (s->step == 1u || s->step == 2u || s->step == 3u)) {
        p->step = (unsigned char)(s->step == 1u ? GNP_STRIKE : s->step == 2u ? GNP_BAN : GNP_PICK);
        p->mine = (unsigned char)(s->owner == s->my_slot);
        p->open = 1;
    } else { p->step = GNP_NONE; p->mine = 0; }
    if (p->open) {
        p->cur = was_open ? cur_keep : 0;
        pp_settle(p, p->step == GNP_PLAY ? 201u : s->step);
        if (p->step == GNP_PLAY) p->secs = 0xFFu;
    } else { p->prev = 0; p->busy = 0; p->dirty = 1; }
}

static void pp_move(struct gnp *p, int dir)
{
    unsigned i, c = p->cur, n = p->n;
    if (!n) return;
    for (i = 0; i < n; i++) {
        c = dir > 0 ? (c + 1u < n ? c + 1u : 0u) : (c ? c - 1u : n - 1u);
        if (pp_free(p, c)) { if (c != p->cur) { p->cur = (unsigned char)c; p->sfx = 3; p->dirty = 1; } return; }
    }
}

/* A marks the row under the cursor (a second A takes the mark back); with all the marks the step asks for made, OK / Redo take the prompt's place: A on OK sends them, Redo or B take the marks back (Slippi's
    HandleStageInputs). A two-strike confirm sends the first strike now and the second when the view that shows the first struck is back (pend). */
static void pp_commit(struct gnp *p)
{
    unsigned i, k = 0, row[2];
    for (i = 0; i < p->n && k < 2u; i++) if (p->sel[i]) row[k++] = i;
    if (!k) return;
    p->act = (unsigned char)(p->step == GNP_STRIKE ? GNP_A_STRIKE : p->step == GNP_BAN ? GNP_A_BAN : GNP_A_PICK);
    p->val = p->srv[row[0]];
    if (k > 1u) { p->pend = (unsigned char)row[1]; p->sel[row[1]] = 0; p->nsel--; }
    p->busy = 1; p->confirm = 0; p->sfx = 2; p->dirty = 1;
}

void gnp_frame(struct gnp *p, unsigned edge, int up, int down)
{
    if (!p->open) return;
    if (p->secs != 0xFFu && p->frames < 0xFFFFu && ++p->frames % GNP_FPS == 0u) p->dirty = 1;          /* the clock ticks */
    if (!p->mine || (p->step != GNP_STRIKE && p->step != GNP_BAN && p->step != GNP_PICK && p->step != GNP_CHAR)) return;
    if (p->busy) {                                                           /* one action per step: the answer or the next view frees it */
        if (edge & (PB_A | PB_START)) p->sfx = 4;
        return;
    }
    if (p->step == GNP_CHAR) {                                              /* OK keeps last game's fighter, Change (or B) opens the grid */
        if (edge & (PB_LEFT | PB_RIGHT)) { p->btn ^= 1u; p->sfx = 3; p->dirty = 1; }
        else if ((edge & PB_B) || ((edge & (PB_A | PB_START)) && (p->btn == 1u || p->prevc == 0xFFu))) { p->grid = 1; p->sfx = 2; p->dirty = 1; }
        else if (edge & (PB_A | PB_START)) { p->act = GNP_A_CHAR; p->val = p->prevc; p->busy = 1; p->sfx = 2; p->dirty = 1; }
        return;
    }
    if (p->confirm) {
        if (edge & (PB_LEFT | PB_RIGHT)) { p->btn ^= 1u; p->sfx = 3; p->dirty = 1; }
        else if ((edge & PB_B) || ((edge & (PB_A | PB_START)) && p->btn == 1u)) { pp_unmark(p); p->sfx = 1; p->dirty = 1; }
        else if (edge & (PB_A | PB_START)) pp_commit(p);
        return;
    }
    if (up || (edge & (PB_UP | PB_LEFT))) pp_move(p, -1);
    else if (down || (edge & (PB_DOWN | PB_RIGHT))) pp_move(p, 1);
    if (edge & PB_B) { if (p->nsel) { pp_unmark(p); p->sfx = 1; p->dirty = 1; } }
    else if (edge & (PB_A | PB_START)) {
        if (p->sel[p->cur]) { p->sel[p->cur] = 0; p->nsel--; p->sfx = 1; p->dirty = 1; }
        else if (!pp_free(p, p->cur) || p->nsel >= p->need) p->sfx = 4;
        else {
            p->sel[p->cur] = 1; p->nsel++; p->sfx = 2; p->dirty = 1;
            if (p->nsel >= p->need) { p->confirm = 1; p->btn = 0; }
        }
    }
}

unsigned gnp_clock_state(const struct gnp *p)
{
    unsigned s = gnp_secs(p);
    if (s == 0xFFu || !p->open || !p->mine || (p->step != GNP_STRIKE && p->step != GNP_BAN && p->step != GNP_PICK && p->step != GNP_CHAR)) return 0u;
    return s <= GNP_PANIC_SECS ? 2u : s <= GNP_WARN_SECS ? 1u : 0u;
}

unsigned gnp_secs(const struct gnp *p)
{
    unsigned s;
    if (p->secs == 0xFFu) return 0xFFu;
    s = (unsigned)p->secs;
    { unsigned used = (unsigned)p->frames / GNP_FPS; s = used >= s ? 0u : s - used; }
    return s;
}

void gnp_clock(const struct gnp *p, char *out)
{
    unsigned s = gnp_secs(p);
    out[0] = 0;
    if (s == 0xFFu || !p->open) return;
    pp_catu(out, s / 60u, 6u); pp_cat(out, ":", 6u);
    if (s % 60u < 10u) pp_cat(out, "0", 6u);
    pp_catu(out, s % 60u, 6u);
}

void gnp_title(const struct gnp *p, char *out)
{
    out[0] = 0;
    switch (p->step) {
    case GNP_STRIKE: pp_cpy(out, p->mine ? "Strike a stage" : "Your opponent strikes", 30u); break;
    case GNP_BAN: pp_cpy(out, p->mine ? "Ban a stage" : "Your opponent bans a stage", 30u); break;
    case GNP_PICK: pp_cpy(out, p->mine ? "Pick a stage" : "Your opponent picks a stage", 30u); break;
    case GNP_WAIT: pp_cpy(out, "Waiting for your opponent", 30u); break;
    case GNP_PLAY:
        pp_cpy(out, "Stage: ", 30u);
        if (p->chosen < p->n) pp_cat(out, gnp_stage_name(p->id[p->chosen]), 30u);
        break;
    default: break;
    }
}

void gnp_sub(const struct gnp *p, char *out)
{
    out[0] = 0;
    switch (p->step) {
    case GNP_STRIKE:
        if (p->kind == 0u && p->game == 1u) {                                   /* the room's game 1: one, two, one strike over five stages (four in all) */
            unsigned k = p->struck_n + 1u;
            if (k > 4u) k = 4u;
            pp_cpy(out, "Strike ", 30u); pp_catu(out, k, 30u); pp_cat(out, " of 4", 30u);
        } else if (p->remaining > 1u) { pp_catu(out, p->remaining, 30u); pp_cat(out, " strikes left", 30u); }
        else pp_cpy(out, "Last stage left is played", 30u);
        break;
    case GNP_BAN: pp_cpy(out, "The loser then picks", 38u); break;
    case GNP_PICK: pp_cpy(out, p->kind == 1u ? "Pick from the stages left" : "The loser picks", 38u); break;
    case GNP_WAIT: pp_cpy(out, "Both must be in the room", 38u); break;
    case GNP_PLAY: pp_cpy(out, "Starting the game", 30u); break;
    default: break;
    }
}

void gnp_score(const struct gnp *p, char *out)
{
    out[0] = 0;
    if (!p->open) return;
    pp_cpy(out, "Game ", 30u); pp_catu(out, p->game, 30u); pp_cat(out, " of ", 30u); pp_catu(out, p->best_of, 30u);
    pp_cat(out, "   ", 30u); pp_catu(out, p->sc_me, 30u); pp_cat(out, " - ", 30u); pp_catu(out, p->sc_opp, 30u);
}

unsigned gnp_row(const struct gnp *p, unsigned i, char *name, char *note)
{
    name[0] = 0; note[0] = 0;
    if (i >= p->n || !p->open || p->step == GNP_WAIT) return 0;
    pp_cpy(name, gnp_stage_name(p->id[i]), 24u);
    switch (p->state[i]) {
    case GNP_S_STRUCK: pp_cpy(note, "Struck", 12u); return 1;
    case GNP_S_BANNED: pp_cpy(note, "Banned", 12u); return 1;
    case GNP_S_DAVE: pp_cpy(note, "Not allowed", 12u); return 1;
    case GNP_S_OUT: return 1;
    case GNP_S_CHOSEN: pp_cpy(note, "Selected", 12u); return 2;
    default: return 0;
    }
}

/* ---- the GameSetup look ---- */
const char *gnp_label(unsigned label)
{
    switch (label) {
    case GNP_L_YOUR: return "YOUR CHAR";
    case GNP_L_OPP: return "OPP CHAR";
    case GNP_L_STRIKE1: return "STRIKE 1";
    case GNP_L_STRIKE23: return "STRIKE 2/3";
    case GNP_L_STRIKE4: return "STRIKE 4";
    case GNP_L_BAN: return "BAN";
    case GNP_L_STAGE: return "STAGE";
    default: return "";
    }
}

/* the k-th (0 based) stage struck, by the order seen: its id, 0xFF when there is none */
static unsigned pp_struck_id(const struct gnp *p, unsigned k)
{
    unsigned o, i, c = 0;
    for (o = 1u; o <= p->nord; o++)
        for (i = 0; i < p->n; i++)
            if (p->order[i] == o && p->state[i] == GNP_S_STRUCK) { if (c++ == k) return p->id[i]; }
    return 0xFFu;
}
static unsigned pp_banned_id(const struct gnp *p)
{
    unsigned i;
    for (i = 0; i < p->n; i++) if (p->state[i] == GNP_S_BANNED) return p->id[i];
    return 0xFFu;
}

unsigned gnp_timeline(const struct gnp *p, struct gnp_tl *tl)
{
    unsigned n = 0, sn = p->struck_n, now = p->step == GNP_STRIKE, f;
    unsigned char c0 = p->ch[0], c1 = p->ch[1];
    if (!p->open) return 0;
    if (p->game > 1u && p->chosen >= p->n) c0 = c1 = 0xFFu;                 /* a later game: the fighters are picked again after the stage, "?" until then */
    if (p->game <= 1u) {                                                    /* game 1: the fighters first, then the strikes (one, two, one over five stages) */
        for (f = 0; f < 2u; f++) {                                          /* the first player's fighter first (Slippi's steps 0 and 1 are the players by seat, labelled YOUR / OPP) */
            unsigned mine_e = f ? !p->me_first : p->me_first, c = mine_e ? c0 : c1;
            tl[n].label = mine_e ? GNP_L_YOUR : GNP_L_OPP; tl[n].n = 1; tl[n].id[0] = (unsigned char)c; tl[n].id[1] = 0xFFu; tl[n].state = c != 0xFFu ? 2u : 0u; n++;
        }
        tl[n].label = GNP_L_STRIKE1; tl[n].n = 1; tl[n].id[0] = pp_struck_id(p, 0u); tl[n].id[1] = 0xFFu; tl[n].state = (unsigned char)(sn >= 1u ? 2u : now && sn == 0u ? 1u : 0u); n++;
        tl[n].label = GNP_L_STRIKE23; tl[n].n = 2; tl[n].id[0] = pp_struck_id(p, 1u); tl[n].id[1] = pp_struck_id(p, 2u); tl[n].state = (unsigned char)(sn >= 3u ? 2u : now && sn >= 1u ? 1u : 0u); n++;
        tl[n].label = GNP_L_STRIKE4; tl[n].n = 1; tl[n].id[0] = pp_struck_id(p, 3u); tl[n].id[1] = 0xFFu; tl[n].state = (unsigned char)(sn >= 4u ? 2u : now && sn == 3u ? 1u : 0u); n++;
    } else {                                                                /* a later game: [BAN (Grandmaster: the winner's)], the loser's STAGE pick, then the two fighters (the order the steps come in) */
        if (p->kind == 1u) {
            unsigned b = pp_banned_id(p);
            tl[n].label = GNP_L_BAN; tl[n].n = 1; tl[n].id[0] = (unsigned char)b; tl[n].id[1] = 0xFFu; tl[n].state = (unsigned char)(b != 0xFFu ? 2u : p->step == GNP_BAN ? 1u : 0u); n++;
        }
        tl[n].label = GNP_L_STAGE; tl[n].n = 1; tl[n].id[0] = p->chosen < p->n ? p->id[p->chosen] : 0xFFu; tl[n].id[1] = 0xFFu;
        tl[n].state = (unsigned char)(p->chosen < p->n || p->step == GNP_CHAR ? 2u : p->step == GNP_PICK ? 1u : 0u); n++;
        for (f = 0; f < 2u; f++) {                                          /* the previous winner's fighter first, then the loser's */
            unsigned mine_e = f ? !p->me_first : p->me_first, c = mine_e ? c0 : c1;
            if (p->step == GNP_CHAR) {                                      /* the character steps: the winner's first; its fighter shows once it is done, the step now is lit, the other waits */
                unsigned first_now = p->mine == p->me_first, done = f == 0u ? !first_now : 0u, now_e = f == 0u ? first_now : !first_now;
                c = done ? c : 0xFFu;
                tl[n].state = (unsigned char)(done ? 2u : now_e ? 1u : 0u);
            } else tl[n].state = c != 0xFFu ? 2u : 0u;
            tl[n].label = mine_e ? GNP_L_YOUR : GNP_L_OPP; tl[n].n = 1; tl[n].id[0] = (unsigned char)c; tl[n].id[1] = 0xFFu; n++;
        }
    }
    return n;
}

void gnp_msg(const struct gnp *p, char *out)
{
    out[0] = 0;
    if (p->confirm) return;                                                 /* the OK / Redo buttons are up instead of the prompt */
    switch (p->step) {
    case GNP_STRIKE:
        if (p->mine) pp_cpy(out, pp_need(p) >= 2u ? "Select two stages you do not want to play" : "Select a stage you do not want to play", 43u);
        else pp_cpy(out, "Waiting for opponent to strike", 43u);
        break;
    case GNP_BAN: pp_cpy(out, p->mine ? "Select a stage to ban" : "Waiting for opponent to ban", 43u); break;
    case GNP_PICK: pp_cpy(out, p->mine ? "Select a stage to play" : "Waiting for opponent to choose stage", 43u); break;
    case GNP_WAIT: pp_cpy(out, "Waiting for opponent", 43u); break;
    case GNP_PLAY: pp_cpy(out, "Get ready!", 43u); break;
    case GNP_CHAR: if (!p->mine) pp_cpy(out, "Waiting for opponent to choose character", 43u); break;
    default: break;
    }
}

const char *gnp_btn_label(const struct gnp *p, unsigned i) { return i ? (p->step == GNP_CHAR ? "Change" : "Redo") : (p->step == GNP_CHAR && p->prevc == 0xFFu) ? "Select" : "OK"; }      /* (no fighter locked before: the first button opens the grid, "Select") */

/* the pips under the clock (Slippi's GameResult row): one per game of the set, won / lost for the games played, the game now lit */
unsigned gnp_pip(const struct gnp *p, unsigned i)
{
    if (i < (unsigned)p->sc_me + p->sc_opp && i < 9u && p->res[i]) return p->res[i];
    return i == (unsigned)p->sc_me + p->sc_opp ? 3u : 0u;
}

void gnp_sub_name(const struct gnp *p, unsigned who, char *out)
{
    out[0] = 0;
    if (!p->open) return;
    pp_cpy(out, p->code[who ? 1 : 0], GNP_CODE - 1u);
}

void gnp_codes(struct gnp *p, const char *me, const char *opp)
{
    unsigned w, i;
    char c[2][GNP_CODE];
    for (w = 0; w < 2u; w++) {
        const char *s = w ? opp : me;
        for (i = 0; s && s[i] && i < GNP_CODE - 1u; i++) c[w][i] = s[i] == '@' ? '#' : s[i];     /* the Slippi form of a Multishine code (TAG@n) */
        c[w][i] = 0;
    }
    for (w = 0; w < 2u; w++) { for (i = 0; i < GNP_CODE && p->code[w][i] == c[w][i]; i++) if (!c[w][i]) break; if (i < GNP_CODE && p->code[w][i] != c[w][i]) { pp_cpy(p->code[w], c[w], GNP_CODE - 1u); p->dirty = 1; } }
}

const char *gnp_foot(const struct gnp *p)
{
    if (p->mine && p->step == GNP_CHAR) return "A: Confirm  B: Change  Hold B: Leave";
    if (p->mine && (p->step == GNP_STRIKE || p->step == GNP_BAN || p->step == GNP_PICK)) {
        if (p->confirm) return "A: Confirm  B: Redo  Hold B: Leave";
        return p->nsel ? "A: Select  B: Clear  Hold B: Leave" : "A: Select  Hold B: Leave";
    }
    return "Hold B: Leave";
}

unsigned gnp_text(const struct gnp *p, char (*line)[56], unsigned max)
{
    unsigned n = 0, i;
    char a[40], b[40];
    if (!p->open || max < 4u) return 0;
    gnp_title(p, a); pp_cpy(line[n++], a, 55u);
    gnp_sub(p, a); pp_cpy(line[n++], a, 55u);
    gnp_score(p, a); gnp_clock(p, b);
    pp_cpy(line[n], a, 55u); if (b[0]) { pp_cat(line[n], "   ", 55u); pp_cat(line[n], b, 55u); }
    n++;
    for (i = 0; i < p->n && n < max; i++) {
        unsigned at = gnp_row(p, i, a, b);
        pp_cpy(line[n], (p->mine && i == p->cur && at == 0u) ? "- " : "  ", 55u);
        pp_cat(line[n], a, 55u);
        if (b[0]) { pp_cat(line[n], "  ", 55u); pp_cat(line[n], b, 55u); }
        n++;
    }
    if (n < max) pp_cpy(line[n++], gnp_foot(p), 55u);
    return n;
}
