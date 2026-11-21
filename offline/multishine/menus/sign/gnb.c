/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Game Night room browser, the model (gnb.h).  Strict C89, no libc, no heap, no Melee address, no network. */
#include "gnb.h"
#include "sg_ui.h"
#include "../ms_fmt.h"

/* ---- small string helpers (no libc) ---- */
static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static char *cpy(char *d, const char *s, unsigned max)       /* copies at most max characters, terminates, returns the end */
{
    unsigned i = 0;
    while (s[i] && i < max) { d[i] = s[i]; i++; }
    d[i] = 0;
    return d + i;
}
static char *cat(char *d, const char *s, unsigned max)       /* appends to d while the whole stays within max characters */
{
    unsigned n = slen(d);
    return cpy(d + n, s, max > n ? max - n : 0u);
}
static char *catu(char *d, unsigned v, unsigned max)
{
    char r[12];
    r[ms_fmt_u(r, v, 11u)] = 0;
    return cat(d, r, max);
}
static void clr(char *d) { d[0] = 0; }
static int drawable(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == ',' || c == '\'' || c == '-' || c == ':';
}
/* a server string reduced to the menu font: anything else becomes a space (a name never reaches the screen with a glyph the font cannot draw) */
static void safe_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i < max) { d[i] = drawable(s[i]) ? s[i] : ' '; i++; }
    d[i] = 0;
}

/* ---- example data (Mockups hub "Rooms Browser (Graphic)"): invented, codes XMPL.. cannot be real room codes (the server alphabet has no 0, 1, I, O) ---- */
static const struct gnb_room EX[8] = {      /* ..., region, stage mode, room type, minutes */
    { "XMPL01", "Friday Night", "Wobble Pilot", 0, 5, 8, GNB_F_SPECT, 0, 2, 0, GNB_RT_CHILL, 0 },
    { "XMPL02", "Wobble Club", "Pocket Fox", 0, 12, 16, GNB_F_SPECT | GNB_F_RUN, 0, 0, 0, GNB_RT_SWEATY, 0 },
    { "XMPL03", "Low Tiers", "Lylat Lou", 0, 3, 8, 0, 0, 1, 0, GNB_RT_LOWMID, 0 },
    { "XMPL04", "Ditto Dojo", "Cliff Hanger", 0, 8, 8, GNB_F_SPECT | GNB_F_FULL | GNB_F_RUN, 0, 2, 0, GNB_RT_DITTO, 0 },
    { "XMPL05", "Mirror Match", "Tipper Tom", 0, 2, 32, GNB_F_PASS, 0, 3, 0, GNB_RT_DITTO, 0 },
    { "XMPL06", "Practice", "Wobble Pilot", 0, 6, 12, GNB_F_RUN, 0, 0, 0, GNB_RT_CHILL, 0 },
    { "XMPL07", "Frozen Jam", "Pocket Fox", 0, 4, 8, GNB_F_SPECT, 0, 4, 2, GNB_RT_WILD, 0 },
    { "XMPL08", "Late Night", "Lylat Lou", 0, 1, 8, 0, 0, 2, 2, GNB_RT_WILD_CRAZY, 0 },
};
#define EX_N 8u

static const unsigned char SIZES[] = { 2, 4, 8, 16, 32 };           /* Create Room > Players */
#define SIZE_N (sizeof SIZES / sizeof SIZES[0])
enum { CF_TYPE, CF_SIZE, CF_LIST, CF_SPECT, CF_PASS, CF_FSKIP, CF_GO, CF_N };

static const char *stg(unsigned smode) { return smode == 2u ? "All" : "Legal"; }      /* the stage rule of a room (Stadium is always in the pool) */

/* the six room types (ids and descriptions are 's, tests/test_gnb_types.py compares them): the Create form's Type row cycles them, the description is shown under the form,
   the list column has the short name (the column is about 64 px wide) */
static const char *const RT_NAME[GNB_RT_N] = { "Chill", "Sweaty", "Wild", "Wild And Crazy", "Ditto Dojo", "Low-Mid Fun" };
static const char *const RT_DESC[GNB_RT_N] = { "Single games, random legal stage.", "Strike, then loser picks. Best of 3.", "Single games, random stage from ALL stages.", "Wild, plus all items at maximum.",
                                               "Same character for both players.", "Chill without the top tiers." };
/* the room's name on the wire IS its mode (the Mode replaces the Name): the name field holds 12 characters, so "Wild And Crazy" goes out as "Wild n Crazy"; the screens show the full mode name for a room whose name is its mode's wire name (older rooms keep their own name) */
static const char *const RT_WIRE[GNB_RT_N] = { "Chill", "Sweaty", "Wild", "Wild n Crazy", "Ditto Dojo", "Low-Mid Fun" };
static const char *shown_name(const char *name, unsigned typed, unsigned rtype)
{
    unsigned i = 0;
    if (!typed || rtype >= GNB_RT_N) return name;
    while (name[i] && name[i] == RT_WIRE[rtype][i]) i++;
    return (!name[i] && !RT_WIRE[rtype][i]) ? RT_NAME[rtype] : name;
}
static const char *const RT_SHORT[GNB_RT_N] = { "Chill", "Sweaty", "Wild", "Crazy", "Dojo", "Low-Mid" };
const char *gnb_type_name(unsigned t) { return t < GNB_RT_N ? RT_NAME[t] : ""; }
const char *gnb_type_desc(unsigned t) { return t < GNB_RT_N ? RT_DESC[t] : ""; }
const char *gnb_type_short(unsigned t) { return t < GNB_RT_N ? RT_SHORT[t] : "-"; }
int gnb_type_open(const struct gnb *g, unsigned t)       /* Wild and Wild And Crazy ride the admin All-stages switch, Wild And Crazy also the items switch (list v6 flags) */
{
    if (t == GNB_RT_WILD || t == GNB_RT_WILD_CRAZY) { if (!g->all_ok) return 0; }
    if (t == GNB_RT_WILD_CRAZY && !g->items_ok) return 0;
    return 1;
}

/* ---- the list filters and the sort: More popup (Stages, Hide full rooms, Sort, Region).  Applied to every room as a list page arrives, so a
   change of a filter fetches the list again; the server's own stage filter (list v4) is a first cut, the same test here also serves a server that sends everything. ---- */
static int filters_active(const struct gnb *g) { return g->f_reg || g->f_stage || g->f_hide; }
static int room_ok(const struct gnb *g, const struct gnb_room *r)
{
    if (g->f_reg && r->region != g->f_reg - 1u) return 0;                    /* a room of unknown region (0xFF) shows under Any only */
    if (g->f_stage && g->rt_ok && r->rtype + 1u != g->f_stage) return 0;     /* room types served: the filter is a type */
    if (g->f_stage && !g->rt_ok && (r->smode == 2u) != (g->f_stage == 2u)) return 0;
    if (g->f_hide && (r->mem >= r->cap || (r->flags & GNB_F_FULL))) return 0;
    return 1;
}
static int sort_before(const struct gnb *g, const struct gnb_room *a, const struct gnb_room *b)      /* a strictly before b */
{
    int fa = a->mem >= a->cap || (a->flags & GNB_F_FULL), fb = b->mem >= b->cap || (b->flags & GNB_F_FULL);
    if (fa != fb) return fb;                                            /* a room you can join comes before a full one, whatever the sort */
    return g->f_sort == GNB_SORT_NEWEST ? a->minutes < b->minutes : a->mem > b->mem;
}
static void sort_rooms(struct gnb *g)                                   /* stable insertion sort of the cache (at most GNB_MAX rooms) */
{
    unsigned i, j;
    struct gnb_room t;
    if (g->f_sort > GNB_SORT_NEWEST) return;
    for (i = 1; i < g->n; i++) {
        t = g->r[i];
        for (j = i; j > 0 && sort_before(g, &t, &g->r[j - 1u]); j--) g->r[j] = g->r[j - 1u];
        g->r[j] = t;
    }
}

static void ex_load(struct gnb *g)
{
    unsigned i, n = 0;
    for (i = 0; i < EX_N; i++) {
        struct gnb_room *r = &g->r[n];
        *r = EX[i];
        if (g->xst == GNB_X_FULL || g->xst == GNB_X_SPECONLY) {
            r->mem = r->cap;
            r->flags = (unsigned char)((r->flags | GNB_F_FULL) & ~GNB_F_SPECT);
            if (g->xst == GNB_X_SPECONLY) { r->flags |= GNB_F_SPECT; }
        }
        if (room_ok(g, r)) n++;
    }
    g->n = (unsigned char)n; g->got = (unsigned char)EX_N; g->total = (unsigned char)n;
    sort_rooms(g);
    switch (g->xst) {
    case GNB_X_LOADING: g->net = GNB_N_LOADING; break;
    case GNB_X_UNREACH: g->net = GNB_N_UNREACH; break;
    case GNB_X_EMPTY: g->net = GNB_N_EMPTY; g->n = 0; g->total = 0; break;
    default: g->net = GNB_N_OK; break;
    }
}

static void zero(void *p, unsigned n) { unsigned char *b = (unsigned char *)p; while (n--) *b++ = 0; }

void gnb_enter(struct gnb *g, int live)
{
    zero(g, (unsigned)sizeof *g);
    g->pick_mine = GNB_CHAR_NONE;
    g->live = (unsigned char)(live != 0);
    g->scr = GNB_LIST;
    g->dirty = GNB_D_ALL;
    g->c_listed = 1; g->c_size = 2;                                     /* Players: 8 */
    g->c_spect = 1;                                                     /* Spectators: yes (the room list showed no spectating because the default was no) */
    g->c_fskip = 1;                                                     /* Friends Skip Passcode: Yes */
    g->pick_secs = 0xFFu; g->act_cos = 0xFFu;
    g->reg_seen = 0xFFu;                                                /* the account region is taken over on the first frame */
    gnp_clear(&g->prep);
    if (live) { g->net = GNB_N_LOADING; g->req = GNB_REQ_LIST; g->lpage = 0; }
    else { g->xst = GNB_X_LIVE; g->f_sort = GNB_SORT_SERVER; g->rt_ok = 1; g->all_ok = 1; g->items_ok = 1; ex_load(g); }      /* (the invented rooms keep the order they were written in until the player picks a sort) */
}

/* the browser opened again (after every game of a set): as gnb_enter, but the fighter locked last and its costume survive, so a character step's OK sends the same fighter again (Slippi's GameSetup does the same with last game's fighter) */
void gnb_enter_keep(struct gnb *g, int live)
{
    unsigned char keep_mine = g->pick_mine, keep_ok = g->pick_ok, keep_cos[GNB_PICK_N];
    unsigned i;
    for (i = 0; i < GNB_PICK_N; i++) keep_cos[i] = g->pick_cos[i];
    gnb_enter(g, live);
    g->pick_ok = keep_ok; g->pick_mine = keep_ok ? keep_mine : (unsigned char)GNB_CHAR_NONE;
    for (i = 0; i < GNB_PICK_N; i++) g->pick_cos[i] = keep_cos[i];
}

unsigned gnb_rows(const struct gnb *g)
{
    if (g->scr == GNB_CREATE || (g->scr == GNB_PASS && g->p_for == 1u)) return CF_N;
    if (g->scr == GNB_LOBBY || (g->scr == GNB_BUSY && g->busy_what == GNB_REQ_LEAVE)) return 2u + g->lob.nnext;
    if (g->net != GNB_N_OK) return 0;
    return g->n;
}
unsigned gnb_max_first(const struct gnb *g)
{
    unsigned n = gnb_rows(g);
    return n > GNB_VIS ? n - GNB_VIS : 0u;
}
static unsigned cur(const struct gnb *g) { return (unsigned)g->first + g->page; }
static void clamp(struct gnb *g)
{
    unsigned n = gnb_rows(g), mf = gnb_max_first(g);
    if (g->first > mf) g->first = (unsigned char)mf;
    if (n == 0) { g->page = 0; return; }
    if (cur(g) >= n) g->page = (unsigned char)(n - 1u - g->first);
}

/* ---- example lobby (the Mockups variant): the room joined, five invented members ---- */
static void ex_lobby(struct gnb *g, unsigned idx)
{
    struct gnb_lobby *v = &g->lob;
    const struct gnb_room *r = &g->r[idx < g->n ? idx : 0u];
    zero(v, (unsigned)sizeof *v);
    v->have = 1; v->phase = 0; v->members = 5; v->qlen = 2; v->pos = 1; v->side = 0xFFu; v->streak = 2; v->secs = 0xFFu;
    v->flags = GNB_V_QUEUED; v->rflags = (unsigned char)(r->flags & (GNB_F_SPECT | GNB_F_PASS)); v->stadium = r->stadium;
    cpy(v->code, r->code, 8); cpy(v->rname, r->name, 12);
    v->v6 = 1; v->rtype = r->rtype < GNB_RT_N ? r->rtype : 0u; v->best_of = (unsigned char)(v->rtype == GNB_RT_SWEATY ? 3u : 1u); v->forced = GNB_RT_NONE; v->excl = v->rtype == GNB_RT_LOWMID ? 0x00188205ul : 0ul;   /* the room's type, rules and set length (example data) */
    v->side_on[0] = 1; cpy(v->side_name[0], "Pocket Fox", 12); v->side_crowns[0] = 1; v->side_char[0] = 2u;      /* Fox */
    v->side_on[1] = 1; cpy(v->side_name[1], "Wobble Pilot", 12); v->side_char[1] = 9u;                            /* Marth */
    v->nnext = 2; cpy(v->next[0], "You", 12); cpy(v->next[1], "Lylat Lou", 12);
    g->lob_ex = 3u;
}

static void to_list(struct gnb *g, const char *msg)
{
    g->ros_open = 0;
    g->scr = GNB_LIST; g->msg = msg; g->dirty = GNB_D_ALL;
    clamp(g);
}

static void refresh(struct gnb *g, int silent)
{
    if (g->live) {
        if (g->req != GNB_REQ_NONE) return;
        g->req = GNB_REQ_LIST; g->lpage = 0;
        if (g->net != GNB_N_OK) g->net = GNB_N_LOADING;
        if (!silent) g->msg = g->net == GNB_N_OK ? "Refreshing" : 0;
    } else {
        g->timer = GNB_LOAD_FRAMES;
        if (g->xst == GNB_X_UNREACH || g->xst == GNB_X_LOADING) g->xst = GNB_X_LIVE;
        g->net = GNB_N_LOADING; g->msg = 0;
    }
    g->timer = g->live ? 0 : g->timer;
    g->dirty = GNB_D_ALL;
}

/* ---- the list ---- */
static void move(struct gnb *g, int down)
{
    unsigned n = gnb_rows(g);
    if (!n) return;
    if (!down) {
        if (g->page) { g->page--; g->dirty |= GNB_D_CURSOR | GNB_D_PANEL; }
        else if (g->first) { g->first--; g->dirty |= GNB_D_ROWS | GNB_D_PANEL; }
        else return;
    } else {
        if (cur(g) + 1u >= n) return;
        if (g->page + 1u < GNB_VIS) { g->page++; g->dirty |= GNB_D_CURSOR | GNB_D_PANEL; }
        else { g->first++; g->dirty |= GNB_D_ROWS | GNB_D_PANEL; }
    }
    g->sfx = GNB_SFX_MOVE;
    if (g->scr == GNB_LIST) g->msg = 0;
}
static void page9(struct gnb *g, int down)
{
    unsigned mf = gnb_max_first(g), f = g->first;
    if (down) f = f + GNB_VIS < mf ? f + GNB_VIS : mf;
    else f = f >= GNB_VIS ? f - GNB_VIS : 0u;
    if (f == g->first) return;
    g->first = (unsigned char)f;
    clamp(g);
    g->sfx = GNB_SFX_MOVE; g->dirty |= GNB_D_ROWS | GNB_D_PANEL;
}

static void deny(struct gnb *g, const char *msg) { g->msg = msg; g->sfx = GNB_SFX_DENY; g->dirty |= GNB_D_PANEL; }

static void start_pass(struct gnb *g, int for_create)
{
    unsigned i;
    for (i = 0; i < 4u; i++) g->p_pin[i] = (char)(for_create == 1 && g->c_pass ? g->c_pin[i] : '0');
    g->p_pin[4] = 0; g->p_pos = 0; g->p_for = (unsigned char)for_create;
    g->scr = GNB_PASS; g->sfx = GNB_SFX_FWD; g->msg = 0; g->dirty = GNB_D_ALL;
}

static void join(struct gnb *g)
{
    if (g->live) { g->req = GNB_REQ_JOIN; g->scr = GNB_BUSY; g->busy_what = GNB_REQ_JOIN; g->msg = 0; }
    else { ex_lobby(g, g->sel); g->scr = GNB_LOBBY; g->first = 0; g->page = 0; g->msg = 0; }
    g->sfx = GNB_SFX_FWD; g->dirty = GNB_D_ALL;
}

void gnb_join_friend(struct gnb *g, const char *room, unsigned rflags)
{
    unsigned i;
    for (i = 0; i < 4u && room[i]; i++) g->fcode[i] = room[i];
    g->fcode[i] = 0;
    g->fj_lob = (unsigned char)(g->scr == GNB_LOBBY && g->lob.have);
    g->fj = 1; g->fpin = 0; g->frf = (unsigned char)(rflags & 3u);
    g->p_pin[0] = 0;
    if (g->frf & 1u) { start_pass(g, 0); return; }          /* the passcode is asked of this console: the typed one (the list loads behind it, B goes back to it) */
    join(g);                                                /* the host's friend, or an open room: straight in */
}

static void list_a(struct gnb *g)
{
    const struct gnb_room *r;
    if (!gnb_rows(g)) { deny(g, g->net == GNB_N_EMPTY ? "Press Z to create a room" : 0); return; }
    g->sel = (unsigned char)cur(g);
    r = &g->r[g->sel];
    if (r->mem >= r->cap || (r->flags & GNB_F_FULL)) { deny(g, r->flags & GNB_F_SPECT ? "Room is full" : "Room is full"); return; }
    if (r->flags & GNB_F_PASS) { start_pass(g, 0); return; }
    g->p_pin[0] = 0;
    join(g);
}

/* the action row (original Rooms list): Create / Join (by code) / Random */
static void create_form(struct gnb *g)
{
    if (g->live && (g->net == GNB_N_UNREACH || g->net == GNB_N_NOGN || g->net == GNB_N_SIGNIN)) { deny(g, "Server not available"); return; }
    g->scr = GNB_CREATE; g->first = 0; g->page = 0; g->c_row = 0; g->msg = 0; g->sfx = GNB_SFX_FWD; g->dirty = GNB_D_ALL;
}
static int tail_is(const char *code, const char *pin)          /* the code is shown and typed as its last four characters */
{
    unsigned n = 0, i;
    while (code[n]) n++;
    if (n < 4u) return 0;
    for (i = 0; i < 4u; i++) if (code[n - 4u + i] != pin[i]) return 0;
    return 1;
}
static void join_code(struct gnb *g)
{
    unsigned i;
    for (i = 0; i < g->n; i++) if (tail_is(g->r[i].code, g->p_pin)) break;
    if (i >= g->n) {
        g->scr = GNB_LIST;
        if (!g->live) { deny(g, "Room not found"); g->dirty = GNB_D_ALL; return; }
        cpy(g->fcode, g->p_pin, 4); g->fcode[4] = 0;               /* not in the loaded list (unlisted, or past the cached rooms): the server decides, as the Friends join does; one refusal covers "no such room" and "wrong passcode" */
        g->fj = 1; g->fpin = 0; g->frf = 4u; g->fj_lob = 0; g->p_pin[0] = 0;
        join(g);
        return;
    }
    g->scr = GNB_LIST; g->sel = (unsigned char)i;
    if (g->r[i].mem >= g->r[i].cap || (g->r[i].flags & GNB_F_FULL)) { deny(g, "Room is full"); g->dirty = GNB_D_ALL; return; }
    if (g->r[i].flags & GNB_F_PASS) { start_pass(g, 0); return; }
    g->p_pin[0] = 0;
    join(g);
}
static void join_random(struct gnb *g)
{
    unsigned i, n = g->n, k = g->rnd;
    for (i = 0; i < n; i++) {
        unsigned j = (k + i) % n;
        const struct gnb_room *r = &g->r[j];
        if (r->mem >= r->cap || (r->flags & (GNB_F_FULL | GNB_F_PASS))) continue;
        g->sel = (unsigned char)j; g->p_pin[0] = 0;
        join(g);
        return;
    }
    deny(g, "No room to join");
}
/* a filter changed: the list is fetched again (EXAMPLE data is filtered again in place) */
static void reload(struct gnb *g)
{
    g->first = 0; g->page = 0; g->sfx = GNB_SFX_FWD;
    if (g->live) { g->n = 0; g->got = 0; g->total = 0; g->net = GNB_N_LOADING; g->req = GNB_REQ_NONE; refresh(g, 1); }
    else ex_load(g);
    g->msg = 0; g->dirty = GNB_D_ALL;
}
/* the Region plate: Auto -> Any -> NAW -> NAC -> NAE -> EU -> LA -> AS -> OC -> Auto.  Auto (reg_user 0) follows the account's region (not set: Any); the rest are the player's own pick */
static void reg_step(struct gnb *g, int dir)
{
    static const unsigned char nx[8] = { 2, 3, 7, 4, 5, 6, 0, 1 }, pv[8] = { 6, 7, 0, 1, 3, 4, 5, 2 };      /* values 0 Any, 1..7 = ms_region.CODES + 1; west to east: Any, NAW, NAC, NAE, EU, LA, AS, OC */
    unsigned f = g->f_reg % GNB_REG_N, up = dir > 0 ? 1u : 0u;
    if (!g->reg_user) { g->reg_user = 1; g->f_reg = up ? 0u : pv[0]; }
    else if (up ? f == pv[0] : f == 0u) { g->reg_user = 0; g->f_reg = (unsigned char)(g->acct_rgn >= 1u && g->acct_rgn <= GNB_REG_N - 1u ? g->acct_rgn : 0u); g->reg_seen = g->acct_rgn; }
    else g->f_reg = up ? nx[f] : pv[f];
}
static void more_change(struct gnb *g)                                  /* A on a More row */
{
    switch (g->more_row) {
    case GNB_MORE_STAGES:
        if (g->rt_ok) { g->f_stage = (unsigned char)(g->f_stage >= GNB_RT_N ? 0u : g->f_stage + 1u); break; }      /* room types served: every room -> Chill -> Sweaty -> ... -> Low-Mid Fun -> every room */
        g->f_stage = (unsigned char)(g->f_stage + 1u > (g->all_ok ? 2u : 1u) ? 0u : g->f_stage + 1u); break;      /* an older server: every room -> Legal -> All (All only while the server lets it) -> every room */
    case GNB_MORE_HIDE: g->f_hide ^= 1u; break;
    case GNB_MORE_REGION:                                               /* the region filter: Any -> NAE -> NAW -> EU -> LA -> AS -> OC -> NAC -> Any */
        reg_step(g, 1);
        break;
    default: g->f_sort = (unsigned char)(g->f_sort == GNB_SORT_NEWEST ? GNB_SORT_PLAYERS : GNB_SORT_NEWEST); break;
    }
    g->more_chg = 1; g->sfx = GNB_SFX_MOVE; g->dirty |= GNB_D_PANEL;
}
static void more_frame(struct gnb *g, unsigned edge, int up, int down)
{
    if (edge & GNB_B_B) {                                               /* done: the list is fetched again when a value changed */
        g->more = 0; g->hold_b = GNB_HOLD_B_FRAMES + 1u;                /* (this B must not also count as the start of the leave hold) */
        if (g->more_chg) { g->more_chg = 0; reload(g); }
        g->sfx = GNB_SFX_BACK; g->dirty = GNB_D_ALL;
        return;
    }
    if (up && g->more_row) { g->more_row--; g->sfx = GNB_SFX_MOVE; g->dirty |= GNB_D_PANEL; }
    else if (down && g->more_row + 1u < GNB_MORE_N) { g->more_row++; g->sfx = GNB_SFX_MOVE; g->dirty |= GNB_D_PANEL; }
    else if (edge & GNB_B_A) more_change(g);
}
static void action_run(struct gnb *g)
{
    switch (g->act_sel) {
    case GNB_ACT_CREATE: create_form(g); break;
    case GNB_ACT_JOIN:
        if (g->live && g->net != GNB_N_OK && g->net != GNB_N_EMPTY) { deny(g, "Server not available"); break; }
        start_pass(g, 2); break;
    case GNB_ACT_RANDOM: if (g->net != GNB_N_OK) deny(g, "No room to join"); else join_random(g); break;
    case GNB_ACT_MORE:
        g->more = 1; g->more_row = 0; g->more_chg = 0; g->sfx = GNB_SFX_FWD; g->msg = 0; g->dirty = GNB_D_ALL;
        break;
    default: break;
    }
}

static void list_frame(struct gnb *g, unsigned edge)
{
    if (g->hold_b == GNB_HOLD_B_FRAMES) { g->hold_b++; g->exit = 1; g->sfx = GNB_SFX_BACK; return; }     /*: B HELD 1.0 s leaves Game Night for the menu as leaving a room */
    if (edge & GNB_B_B) { g->hold_b = GNB_HOLD_B_FRAMES + 1u; g->exit = 1; g->sfx = GNB_SFX_BACK; return; }      /*: a TAP of B goes back to the menu, like Friends and Standings (the footer says "B Back"); the hold stays only inside a room, where leaving costs something */
    if (edge & (GNB_B_LEFT | GNB_B_RIGHT)) {
        if (g->focus) {
            if ((edge & GNB_B_RIGHT) && g->act_sel + 1u < GNB_ACT_N) g->act_sel++;
            else if ((edge & GNB_B_LEFT) && g->act_sel) g->act_sel--;
        }
        g->focus = 1; g->sfx = GNB_SFX_MOVE; g->msg = 0; g->dirty |= GNB_D_PANEL; return;
    }
    if (edge & GNB_B_A) { if (g->focus) action_run(g); else list_a(g); return; }
    if (edge & GNB_B_Z) {
        if (g->live && (g->net == GNB_N_UNREACH || g->net == GNB_N_NOGN || g->net == GNB_N_SIGNIN)) { deny(g, "Server not available"); return; }
        g->scr = GNB_CREATE; g->first = 0; g->page = 0; g->c_row = 0; g->msg = 0; g->sfx = GNB_SFX_FWD; g->dirty = GNB_D_ALL;
        return;
    }
    if (edge & GNB_B_START) {
        if (g->cool) return;
        g->cool = GNB_REFRESH_GAP;
        g->sfx = GNB_SFX_FWD;
        refresh(g, 0);
        return;
    }
    if ((edge & (GNB_B_L | GNB_B_R)) && g->live) {                      /*: L / R cycle the Region filter right on the list (the footer says "L R: Region"): Any -> NAE ->... -> OC -> NAC -> Any, L backwards; the list is fetched again like leaving the More popup */
        reg_step(g, (edge & GNB_B_R) ? 1 : -1);
        g->msg = 0; g->timer = 0; reload(g);
        g->sfx = GNB_SFX_MOVE; g->dirty = GNB_D_ALL;
        return;
    }
    if ((edge & GNB_B_R) && !g->live) {                                 /* EXAMPLE data: next state (the screenshot tests) */
        g->xst = (unsigned char)((g->xst + 1u) % GNB_X_N);
        g->first = 0; g->page = 0; g->msg = 0; g->timer = 0;
        ex_load(g);
        g->sfx = GNB_SFX_MOVE; g->dirty = GNB_D_ALL;
        return;
    }
    if (edge & GNB_B_X) page9(g, 1);
    else if (edge & GNB_B_Y) page9(g, 0);
}

/* ---- Create Room form ---- */
static void form_change(struct gnb *g, int dir)
{
    switch (g->c_row) {
    case CF_SIZE: g->c_size = (unsigned char)((g->c_size + (dir > 0 ? 1u : SIZE_N - 1u)) % SIZE_N); break;
    case CF_LIST: g->c_listed ^= 1u; break;
    case CF_SPECT: g->c_spect ^= 1u; break;
    case CF_TYPE:
        if (g->rt_ok) {      /* the six modes; the description under the form follows; the room is named after its mode */
            g->c_type = (unsigned char)((g->c_type + (dir > 0 ? 1u : GNB_RT_N - 1u)) % GNB_RT_N);
            break;
        }
        if (!g->all_ok) { deny(g, "All stages: Coming soon"); return; } g->c_all ^= 1u; break;      /* an older server: Legal <-> All; All only while the server lets it */
    case CF_PASS: if (g->c_pass) g->c_pass = 0; else { start_pass(g, 1); return; } break;
    case CF_FSKIP: if (!g->c_pass && g->c_listed) { deny(g, "Set a passcode first"); return; } g->c_fskip ^= 1u; break;
    default: return;
    }
    g->sfx = GNB_SFX_MOVE; g->msg = 0; g->dirty |= GNB_D_ROWS | GNB_D_PANEL;
}
/* Create the room with the form as it is (the Create room row, and Start from any row:) */
static void form_go(struct gnb *g)
{
    if (g->rt_ok && !gnb_type_open(g, g->c_type)) { deny(g, "Coming soon"); return; }      /* Wild / Wild And Crazy while the server's switch is off */
    if (g->live) { g->req = GNB_REQ_CREATE; g->scr = GNB_BUSY; g->busy_what = GNB_REQ_CREATE; g->msg = 0; g->sfx = GNB_SFX_FWD; g->dirty = GNB_D_ALL; }
    else {                                                       /* EXAMPLE: the new room first in the list, then its lobby */
        struct gnb_room *r = &g->r[0];
        unsigned i;
        for (i = (g->n < GNB_MAX ? g->n : GNB_MAX - 1u); i > 0; i--) g->r[i] = g->r[i - 1u];
        zero(r, (unsigned)sizeof *r);
        cpy(r->code, "XMPL09", 8); cpy(r->name, g->rt_ok ? RT_WIRE[g->c_type % GNB_RT_N] : "Game Night", 12); cpy(r->host, "You", 12);
        r->mem = 1; r->cap = SIZES[g->c_size % SIZE_N]; r->flags = (unsigned char)((g->c_spect ? GNB_F_SPECT : 0u) | (g->c_pass ? GNB_F_PASS : 0u)); r->smode = g->rt_ok ? (g->c_type == GNB_RT_WILD || g->c_type == GNB_RT_WILD_CRAZY ? 2u : 0u) : g->c_all; r->rtype = g->rt_ok ? g->c_type : (g->c_all ? GNB_RT_WILD : GNB_RT_CHILL);
        if (g->n < GNB_MAX) g->n++;
        g->total = g->n; g->net = GNB_N_OK;
        g->sel = 0;
        join(g);
        g->lob.flags = GNB_V_OWNER; g->lob.pos = 0xFFu; g->lob.members = 1; g->lob.qlen = 0; g->lob.nnext = 0;
        g->lob.side_on[0] = 0; g->lob.side_on[1] = 0;
        g->lob_ex = 0;
    }
}
static void form_frame(struct gnb *g, unsigned edge)
{
    if (edge & GNB_B_B) { g->sfx = GNB_SFX_BACK; to_list(g, 0); g->first = 0; g->page = 0; clamp(g); return; }
    if (edge & (GNB_B_LEFT | GNB_B_RIGHT)) { form_change(g, (edge & GNB_B_RIGHT) ? 1 : -1); return; }
    if (edge & GNB_B_START) { form_go(g); return; }                       /* Start creates the room from any row */
    if (edge & GNB_B_A) {
        switch (g->c_row) {
        case CF_GO:
            form_go(g);
            return;
        default: form_change(g, 1); return;
        }
    }
}

static void join_code(struct gnb *g);
/* ---- passcode entry ---- */
static void pass_frame(struct gnb *g, unsigned edge, int up, int down)
{
    char *d = &g->p_pin[g->p_pos & 3u];
    if (edge & GNB_B_B) { g->sfx = GNB_SFX_BACK; g->scr = g->p_for == 1u ? GNB_CREATE : GNB_LIST; g->msg = 0; g->fj = 0; g->dirty = GNB_D_ALL; return; }
    if (up) { *d = (char)(*d >= '9' ? '0' : *d + 1); g->sfx = GNB_SFX_MOVE; g->dirty |= GNB_D_PANEL; }
    if (down) { *d = (char)(*d <= '0' ? '9' : *d - 1); g->sfx = GNB_SFX_MOVE; g->dirty |= GNB_D_PANEL; }
    if ((edge & GNB_B_RIGHT) && g->p_pos < 3u) { g->p_pos++; g->sfx = GNB_SFX_MOVE; g->dirty |= GNB_D_PANEL; }
    if ((edge & GNB_B_LEFT) && g->p_pos > 0u) { g->p_pos--; g->sfx = GNB_SFX_MOVE; g->dirty |= GNB_D_PANEL; }
    if (edge & GNB_B_A) {
        if (g->p_pos < 3u) { g->p_pos++; g->sfx = GNB_SFX_MOVE; g->dirty |= GNB_D_PANEL; return; }
        if (g->p_for == 1u) { cpy(g->c_pin, g->p_pin, 4); g->c_pass = 1; g->scr = GNB_CREATE; g->sfx = GNB_SFX_FWD; g->dirty = GNB_D_ALL; return; }
        if (g->p_for == 2u) { join_code(g); return; }
        if (g->fj) g->fpin = 1;                                                    /* the Friends list's join: the typed passcode goes out now */
        if (!g->live && !(g->p_pin[0] == '1' && g->p_pin[1] == '2' && g->p_pin[2] == '3' && g->p_pin[3] == '4')) {
            g->scr = GNB_LIST; deny(g, "Room not found or wrong passcode"); g->dirty = GNB_D_ALL; return;      /* EXAMPLE: the passcode is 1234 */
        }
        join(g);
    }
}

/* ---- the lobby ---- */
/* is it this member's pick turn (view v3: the winner side picks first, then the challenger) */
static int my_turn(const struct gnb_lobby *v)
{
    if (!(v->v3 && v->phase == 1 && (v->flags & GNB_V_CALLED) && (v->flags & GNB_V_ONSIDE))) return 0;
    if (v->v6) return v->step == GNB_STEP_PICK_CHAR && v->step_owner == v->side;      /* view v6: the turn byte is our of ANY open step (strike, stage pick); only the character step opens the grid */
    return v->turn == v->side;
}

/* the stage prep screen's requests and sounds go to the glue / the front end through the lobby's own fields */
/* the two fighters of the timeline bar from the lobby's slots (0xFF: not picked yet); a change redraws the screen */
static void prep_chars(struct gnb *g)
{
    const struct gnb_lobby *lv = &g->lob;
    unsigned char a, b;
    if (!g->prep.open || !(lv->flags & GNB_V_ONSIDE) || lv->side > 1u) return;
    a = (unsigned char)gnb_side_char(g, lv->side); b = (unsigned char)gnb_side_char(g, lv->side ^ 1u);
    if (a != g->prep.ch[0] || b != g->prep.ch[1]) { g->prep.ch[0] = a; g->prep.ch[1] = b; g->prep.dirty = 1; }
    { unsigned okc = g->pick_ok && g->pick_mine < 26u && gnb_cell_ok(g, g->pick_mine); g->prep.prevc = (unsigned char)(okc ? g->pick_mine : 0xFFu); g->prep.cos = (unsigned char)(okc ? g->pick_cos[g->pick_mine] : 0xFFu); }      /* what OK sends on a character step: the fighter and costume locked last */
}

static void pick_open(struct gnb *g, int lock);
static void prep_out(struct gnb *g)
{
    struct gnp *p = &g->prep;
    if (p->sfx) { g->sfx = p->sfx; p->sfx = 0; }
    if (p->grid) { p->grid = 0; pick_open(g, 0); }                          /* a character step: Change opens the lobby's grid on the screen of the room (B there keeps the fighter) */
    if (p->act && g->req == GNB_REQ_NONE) {
        g->act = p->act; g->act_val = p->val; g->act_cos = (p->act == GNP_A_CHAR && p->cos != 0xFFu) ? p->cos : 0xFFu; g->req = GNB_REQ_ACTION; p->act = 0;
        if (g->act == GNP_A_CHAR) { g->pick_mine = (unsigned char)g->act_val; g->pick_ok = 1; g->msg = "Picked"; }
    }
    if (p->dirty) { p->dirty = 0; g->dirty |= GNB_D_ALL; }
}

static void lobby_leave(struct gnb *g)
{
    g->autow = 0; g->autoc = 0;                                           /* leaving the room ends the auto-watch */
    if (g->live) { g->req = GNB_REQ_LEAVE; g->scr = GNB_BUSY; g->busy_what = GNB_REQ_LEAVE; g->msg = 0; g->sfx = GNB_SFX_BACK; g->dirty = GNB_D_ALL; }
    else if (g->frd) { g->frd = 0; g->back_frd = 1; g->exit = 1; g->sfx = GNB_SFX_BACK; }
    else { g->sfx = GNB_SFX_BACK; to_list(g, 0); g->first = 0; g->page = g->sel < GNB_VIS ? g->sel : 0u; clamp(g); }
}

/* ---- the pick grid ---- */
void gnb_pick_cell(unsigned cell, unsigned *row, unsigned *col)
{
    if (cell >= GNB_PICK_N) cell = GNB_PICK_N - 1u;
    *row = cell / GNB_PICK_COLS; *col = cell % GNB_PICK_COLS;
}
static unsigned row_len(unsigned row) { return row + 1u < GNB_PICK_ROWS ? GNB_PICK_COLS : GNB_PICK_N - (GNB_PICK_ROWS - 1u) * GNB_PICK_COLS; }
/* the room's character rules (view v6 trailer): the excluded mask greys cells (Low-Mid Fun), a forced character locks the pick (Ditto Dojo: the picker picks for both) */
int gnb_cell_ok(const struct gnb *g, unsigned cell)
{
    if (cell >= GNB_PICK_N) return 0;
    if (cell == GNB_CHAR_RANDOM) return 1;                              /* Random is always allowed: the server resolves it among the allowed fighters */
    return !(g->lob.v6 && ((g->lob.excl >> cell) & 1ul));
}
unsigned gnb_forced_char(const struct gnb *g) { return g->lob.v6 && g->lob.forced < 26u ? g->lob.forced : GNB_RT_NONE; }
static const char *const CHAR_NAME[GNB_PICK_N] = {
    "Captain Falcon", "Donkey Kong", "Fox", "Mr. Game And Watch", "Kirby", "Bowser", "Link", "Luigi", "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu",
    "Ice Climbers", "Jigglypuff", "Samus", "Yoshi", "Zelda", "Sheik", "Falco", "Young Link", "Dr. Mario", "Roy", "Pichu", "Ganondorf", "Random" };
const char *gnb_char_name(unsigned id) { return id < GNB_PICK_N ? CHAR_NAME[id] : ""; }
const char *gnb_cell_label(unsigned id)
{
    static char up[GNB_PICK_N][16];                     /* the names in capitals, built once (the retail icons carry their names in capitals) */
    unsigned k;
    if (id >= GNB_PICK_N) return "";
    if (!up[id][0]) {
        for (k = 0; CHAR_NAME[id][k] && k < 15u; k++) { char c = CHAR_NAME[id][k]; up[id][k] = (char)((c >= 'a' && c <= 'z') ? c - 32 : c); }
        up[id][k] = 0;
    }
    return up[id];
}

/* retail costume counts by external character id, checked against CostumeListsForeachCharacter (0x803C0EC0, numCostumes) read from RAM in a Dolphin run: all 25 fighters match; Roy (external 23) has no row in that table's first 26 entries and is taken as 5 (unverified) */
static const unsigned char COSTUMES[26] = { 6, 5, 4, 4, 6, 4, 5, 4, 5, 5, 4, 4, 5, 4, 4, 5, 5, 6, 5, 5, 4, 5, 5, 5, 4, 5 };
unsigned gnb_costumes(unsigned id) { return id < 26u ? COSTUMES[id] : 0u; }
static void pick_cos_step(struct gnb *g, int dir)
{
    unsigned n = gnb_costumes(g->pick_cur), c = g->pick_cos[g->pick_cur];
    if (n < 2u) { g->sfx = GNB_SFX_DENY; return; }                   /* Random has no costume */
    c = dir > 0 ? (c + 1u < n ? c + 1u : 0u) : (c ? c - 1u : n - 1u);
    g->pick_cos[g->pick_cur] = (unsigned char)c; g->sfx = GNB_SFX_MOVE; g->dirty |= GNB_D_PANEL;
}
static void pick_close(struct gnb *g) { g->pick_open = 0; g->pick_lock = 0; g->dirty = GNB_D_ALL; }
static void pick_open(struct gnb *g, int lock)
{
    unsigned c = g->pick_mine;
    if (c >= GNB_PICK_N) c = g->lob.side < 2u && g->lob.side_char[g->lob.side] < GNB_PICK_N ? g->lob.side_char[g->lob.side] : 0u;   /* the last pick, else the view's */
    if (lock && gnb_forced_char(g) != GNB_RT_NONE) c = gnb_forced_char(g);                      /* the locked view stands on the room's character */
    if (!gnb_cell_ok(g, c)) { for (c = 0; c < GNB_PICK_N && !gnb_cell_ok(g, c); c++) { } }     /* an excluded fighter (the view's, or a stale pick): the first allowed cell */
    g->pick_open = 1; g->pick_lock = (unsigned char)(lock != 0); g->pick_cur = (unsigned char)c;
    g->sfx = GNB_SFX_FWD; g->dirty = GNB_D_ALL;
    g->msg = lock ? "Set for both players" : g->lob.v6 && g->lob.excl ? "Dimmed fighters are not allowed" : (g->lob.v6 && g->lob.rtype == GNB_RT_DITTO) ? "Your pick is played by both" : 0;
}
/* the gold cursor: left / right wrap inside a row, up / down stop at the edges (a column past the short last row lands on its last cell); a cell the room excludes is skipped (the move
   goes on to the next allowed one in the same direction; with none the cursor stays) */
static void pick_move(struct gnb *g, int dx, int dy)
{
    unsigned r, c, n, k, t;
    if (g->pick_lock) return;
    gnb_pick_cell(g->pick_cur, &r, &c);
    for (k = 0; k < GNB_PICK_N; k++) {
        if (dy < 0 && r > 0u) r--;
        else if (dy > 0 && r + 1u < GNB_PICK_ROWS) r++;
        else if (dy) break;                                             /* at the top / bottom edge */
        n = row_len(r);
        if (c >= n) c = n - 1u;
        if (dx < 0) c = c ? c - 1u : n - 1u;
        else if (dx > 0) c = c + 1u < n ? c + 1u : 0u;
        t = r * GNB_PICK_COLS + c;
        if (gnb_cell_ok(g, t)) {
            if (t != g->pick_cur) { g->pick_cur = (unsigned char)t; g->sfx = GNB_SFX_MOVE; g->dirty |= GNB_D_PANEL | GNB_D_CURSOR; }
            return;
        }
    }
}
static void pick_frame(struct gnb *g, unsigned edge, int up, int down)
{
    if (edge & GNB_B_B) { g->hold_b = GNB_HOLD_B_FRAMES + 1u; g->sfx = GNB_SFX_BACK; pick_close(g); return; }      /* back to the room: the previous pick stays (a held B must be released before it can leave the room) */
    if (g->pick_lock) { if (edge & (GNB_B_A | GNB_B_START)) g->sfx = GNB_SFX_DENY; return; }     /* the room's character is set: nothing to choose, B goes back */
    if (edge & (GNB_B_A | GNB_B_START)) {
        if (!gnb_cell_ok(g, g->pick_cur)) { g->sfx = GNB_SFX_DENY; return; }                  /* (the cursor never rests on an excluded fighter; the server refuses one too) */
        g->act = 4; g->act_val = g->pick_cur; g->act_cos = (unsigned char)(g->pick_cur < 26u ? g->pick_cos[g->pick_cur] : 0xFFu); g->req = GNB_REQ_ACTION;     /* GN_ACTION ready, value = the character (26 Random), third byte = its costume */
        g->pick_mine = g->pick_cur; g->pick_ok = 1; g->msg = "Picked"; g->sfx = GNB_SFX_FWD;
        if (g->prep.open) g->prep.busy = 1;                                          /* (a character step of the prep screen: its buttons wait for the next view) */
        pick_close(g);
        return;
    }
    if (edge & GNB_B_L) pick_cos_step(g, -1);
    else if (edge & GNB_B_R) pick_cos_step(g, 1);                      /* L / R: the costume of the character under the cursor (the stick and the d-pad move the cursor) */
    if (up || down) pick_move(g, 0, up ? -1 : 1);
    if (edge & GNB_B_LEFT) pick_move(g, -1, 0);
    else if (edge & GNB_B_RIGHT) pick_move(g, 1, 0);
}

/* a spectatable room whose Stadium policy is Removed (the server accepts uploads and watchers only then, ms_spec: Stadium stays out of spectatable rooms) */
static int watchable(const struct gnb *g) { return (g->lob.rflags & GNB_F_SPECT) != 0; }      /* the room allows spectators (any Stadium policy) */

static void lobby_frame(struct gnb *g, unsigned edge)
{
    struct gnb_lobby *v = &g->lob;
    if (g->rk.on) {                                                       /* a Grandmaster set (gnb_rk.c): hold B forfeits it (the glue), the pick on this console's character step, A / B at the end; nothing else of the room */
        if (g->hold_b == GNB_HOLD_B_FRAMES) { g->hold_b++; lobby_leave(g); return; }
        if (g->rk.fin) { if (edge & (GNB_B_A | GNB_B_B | GNB_B_START)) { g->exit = 1; g->sfx = GNB_SFX_BACK; } return; }
        if ((edge & (GNB_B_A | GNB_B_START)) && my_turn(v)) pick_open(g, 0);
        return;
    }
    if (g->hold_b == GNB_HOLD_B_FRAMES) {                                 /* hold B 1.0 s: leave the room (a tap does nothing) */
        g->hold_b++;
        lobby_leave(g);
        return;
    }
    if (g->hold_x == GNB_HOLD_B_FRAMES) {                                 /* hold X 1.0 s: stop watching (a tap or an early release does nothing) */
        g->hold_x++;
        if (g->autow) { g->autow = 0; g->autoc = 0; g->msg = "Stopped watching"; g->sfx = GNB_SFX_BACK; g->dirty |= GNB_D_PANEL; return; }
    }
    if (g->tn.on) edge &= ~(unsigned)(GNB_B_Y | GNB_B_Z | GNB_B_X);        /* a tournament match room: no Standings, no queue, no watching (the footer lists none of them) */
    if (g->ros_open) {                                                    /* the crowns panel: Y or B closes it, nothing else is taken (hold B still leaves) */
        if (edge & (GNB_B_Y | GNB_B_B)) { g->ros_open = 0; if (edge & GNB_B_B) g->hold_b = GNB_HOLD_B_FRAMES + 1u; g->sfx = GNB_SFX_BACK; g->dirty = GNB_D_ALL; }
        return;
    }
    if (edge & GNB_B_Y) {                                                 /* "Y: Crowns": everyone in the room, most crowns first */
        g->ros_open = 1; g->sfx = GNB_SFX_FWD; g->dirty = GNB_D_ALL;
        if (g->live) { g->ros_have = 0; g->req = GNB_REQ_ROSTER; }
        else {                                                              /* the example data: the sides and the queue */
            unsigned i, n = 0;
            for (i = 0; i < 2u; i++) if (v->side_on[i] && n < GNB_ROSTER_MAX) { cpy(g->ros.name[n], v->side_name[i], 12); g->ros.crowns[n] = v->side_crowns[i]; g->ros.place[n] = i ? 0xFDu : 0xFEu; g->ros.pstreak[n] = 0; g->ros.plead[n] = 0; n++; }
            for (i = 0; i < v->nnext && n < GNB_ROSTER_MAX; i++) { cpy(g->ros.name[n], v->next[i], 12); g->ros.crowns[n] = 0; g->ros.place[n] = (unsigned char)(i + 1u); g->ros.pstreak[n] = 0; g->ros.plead[n] = 0; n++; }
            g->ros.n = (unsigned char)n; g->ros.streak = v->streak; g->ros_have = 1;
        }
        return;
    }
    if (edge & (GNB_B_A | GNB_B_START)) {
        if (!g->live) {
            if (v->flags & GNB_V_OWNER) { g->msg = "Waiting for players"; g->dirty |= GNB_D_PANEL; return; }
            g->lob_ex ^= 4u;
            g->msg = (g->lob_ex & 4u) ? "You are ready" : "You are not ready";
            g->sfx = GNB_SFX_FWD; g->dirty |= GNB_D_PANEL | GNB_D_ROWS;
            return;
        }
        if (my_turn(v)) { pick_open(g, 0); return; }                                      /* the pick: the grid (B there keeps the character) */
        if (v->v6 && v->phase == 1 && (v->flags & GNB_V_CALLED) && gnb_forced_char(g) != GNB_RT_NONE) { pick_open(g, 1); return; }      /* Ditto Dojo, the other player: the character the picker chose, locked */
        if (v->v3 && v->phase == 1 && (v->flags & GNB_V_CALLED)) { deny(g, v->turn == GNB_TURN_GO ? "Starting the match" : "Wait for your turn"); return; }
        if ((v->flags & GNB_V_CALLED) && !(v->flags & GNB_V_READY)) { g->act = 4; g->act_val = 0; g->req = GNB_REQ_ACTION; g->sfx = GNB_SFX_FWD; return; }
        if (!(v->flags & (GNB_V_QUEUED | GNB_V_ONSIDE))) { g->autow = watchable(g) ? 1u : 0u; g->autoc = 0; g->act = 1; g->act_val = 0; g->req = GNB_REQ_ACTION; g->sfx = GNB_SFX_FWD; return; }      /* (joining the queue ARMS the auto-watch,: game night is sitting down together; a queued player watches the running game and the next ones until called, and the call-up ends the watch) */
        if (g->tn.on) return;                                              /* a tournament match room: the right box already says where you are */
        deny(g, v->flags & GNB_V_ONSIDE ? "You are on stage" : "You are in the queue");
        return;
    }
    if ((edge & GNB_B_X) && g->live && g->autow && watchable(g)) return;                    /* watching is on (opt-out): a tap does nothing, X held 1.0 s stops it */
    if ((edge & GNB_B_X) && g->live) {                                                    /* "X: Last game" (Watch Last Game): only in a spectatable room (Frozen Stadium policy) */
        if (!watchable(g)) { deny(g, "Watching is not on here"); return; }
        if (v->phase != 0 && (v->flags & (GNB_V_CALLED | GNB_V_ONSIDE))) { deny(g, "Not while you are playing"); return; }      /* (between sets the winner on stage may watch the set that just ended) */
        if (v->phase == 2u ? (v->wfacts & GNB_VF_NOLIVE) : (v->wfacts & GNB_VF_NOREPLAY)) { g->msg = v->phase == 2u ? "No live game yet" : "No game to replay yet"; g->sfx = GNB_SFX_BACK; g->dirty |= GNB_D_PANEL; return; }      /* a status, not an error: neutral colour */      /* the view says the server would answer "nothing" (v4,  4) */
        g->autow = 1; g->autoc = 0; g->hold_x = GNB_HOLD_B_FRAMES + 1u;                     /* X starts watching AND turns the auto-watch on (until Hold X stops it or Hold B leaves); this press must be released before it can count as the stop hold */
        g->watch_live = (unsigned char)(v->phase == 2u); g->watch = 1; g->msg = g->watch_live ? "Starting the live replay" : "Starting the replay"; g->sfx = GNB_SFX_FWD; g->dirty |= GNB_D_PANEL;
        return;
    }
    if (edge & GNB_B_Z) {
        int inq = (v->flags & GNB_V_QUEUED) && !(v->flags & (GNB_V_ONSIDE | GNB_V_CALLED));          /* in the queue (not on stage, not called): the server lets it go at any time (ms_rooms A_QUEUE_LEAVE); */
        if (g->live && (inq || ((v->flags & GNB_V_ONSIDE) && v->phase == 0))) { g->act = 2; g->act_val = 0; g->req = GNB_REQ_ACTION; g->sfx = GNB_SFX_BACK; return; }   /* on stage only between sets */
        deny(g, "Nothing to leave");
    }
}

static int chat_frame(struct gnb *g, unsigned edge);
void gnb_frame(struct gnb *g, unsigned held, int stick_up, int stick_down)
{
    unsigned edge = held & ~(unsigned)g->prev;
    unsigned rheld = g->real_on ? (unsigned)g->real_held : held, redge = rheld & ~(unsigned)g->real_prev;      /* the real pad: the stick's sideways push is not a D-Pad press for the quick-chat picker */
    int up = stick_up || (edge & GNB_B_UP) != 0, down = stick_down || (edge & GNB_B_DOWN) != 0;
    g->real_prev = (unsigned short)rheld;
    g->prev = (unsigned short)held;
    /* held d-pad repeat */
    if (held & (GNB_B_UP | GNB_B_DOWN)) {
        unsigned d = (held & GNB_B_UP) ? 1u : 2u;
        if (d != g->rep_dir || (edge & (GNB_B_UP | GNB_B_DOWN))) { g->rep_dir = (unsigned char)d; g->rep = 0; }
        else if (++g->rep >= GNB_REP_FIRST) { g->rep = (unsigned char)(GNB_REP_FIRST - GNB_REP_NEXT); if (d == 1u) up = 1; else down = 1; }
    } else { g->rep = 0; g->rep_dir = 0; }
    if (g->cool) g->cool--;
    if (!g->reg_user && g->acct_rgn != g->reg_seen && g->scr == GNB_LIST) {      /* the Region row starts at the account's region (not set: Any) until the player cycles it */
        unsigned want = g->acct_rgn >= 1u && g->acct_rgn <= GNB_REG_N - 1u ? g->acct_rgn : 0u;
        g->reg_seen = g->acct_rgn;
        if (want != g->f_reg) { g->f_reg = (unsigned char)want; if (g->live && g->net != GNB_N_OK && g->net != GNB_N_EMPTY) g->dirty = GNB_D_ALL; else { reload(g); g->sfx = GNB_SFX_NONE; } }      /* (a list still on its way is filtered as it arrives; a loaded one is fetched again) */
    }
    g->rnd++;
    g->clk++;
    if (g->tn.on && g->tn.secs && g->lob.members < 2u) {                      /* the match room's "Join in m:ss" is live: redraw the panel when the second changes */
        unsigned long gone = (g->clk - g->tn.t0) / 30ul;
        unsigned short left = gone >= g->tn.secs ? 0u : (unsigned short)(g->tn.secs - gone);
        if (left != g->tn.shown) { g->tn.shown = left; g->dirty |= GNB_D_PANEL; }
    }
    gnrk_tick(g);
    if (g->scr == GNB_LOBBY) {                                                  /* chat bookkeeping: a send waits for its turn, the mute lapses, a new line stops flashing */
        unsigned i;
        if (g->chat_pend && g->req == GNB_REQ_NONE && !g->chat_busy) { g->chat_arg = (unsigned char)(g->chat_pend - 1u); g->chat_pend = 0; g->chat_busy = 1; g->req = GNB_REQ_CHATSEND; }
        if (g->chat_mute_left && !--g->chat_mute_left) g->dirty |= GNB_D_PANEL;
        for (i = 0; i < g->ncl; i++) if (g->cl[i].flash && !--g->cl[i].flash) g->dirty |= GNB_D_PANEL;
        for (i = 0; i < g->ncl; ) {                                            /* a line leaves the strip GNB_CL_LIFE_S seconds after its post */
            if (g->cl[i].ttl && !--g->cl[i].ttl) { unsigned k; for (k = i + 1u; k < g->ncl; k++) g->cl[k - 1u] = g->cl[k]; g->ncl--; g->dirty |= GNB_D_PANEL; }
            else i++;
        }
    }
    if (g->scr == GNB_LOBBY && (held & GNB_B_X)) { if (g->hold_x <= GNB_HOLD_B_FRAMES) g->hold_x++; } else if (!(held & GNB_B_X)) g->hold_x = 0;
    if (g->scr == GNB_BUSY && g->busy_what != GNB_REQ_LEAVE) { if (g->busy_t < 0xFFFFu) g->busy_t++; } else g->busy_t = 0;
    if ((g->scr == GNB_LOBBY || g->scr == GNB_LIST || (g->rk.on && g->scr == GNB_BUSY) || (g->scr == GNB_BUSY && g->busy_t >= GNB_BUSY_ESC)) && (held & GNB_B_B) && !g->pick_open && !g->more) { if (g->hold_b <= GNB_HOLD_B_FRAMES) g->hold_b++; } else if (!(held & GNB_B_B)) g->hold_b = 0;
    if (g->pick_secs != 0xFFu && g->pick_frames < 0xFFFFu && ++g->pick_frames % GNB_FPS == 0u) g->dirty |= GNB_D_PANEL;     /* the pick clock ticks */
    if (!g->live && g->timer && --g->timer == 0) { ex_load(g); g->msg = "Updated just now"; g->dirty = GNB_D_ALL; }
    if (g->live && g->scr == GNB_LIST && !g->more && (g->net == GNB_N_OK || g->net == GNB_N_EMPTY) && ++g->timer >= GNB_AUTO_FRAMES) { g->timer = 0; refresh(g, 1); }
    if (g->live && g->scr == GNB_LOBBY && ++g->timer >= (g->prep.open ? GNB_PREP_POLL : GNB_POLL_FRAMES)) { g->timer = 0; if (g->req == GNB_REQ_NONE) g->req = (g->ros_open && (g->polls & 1u)) ? GNB_REQ_ROSTER : GNB_REQ_STATE; }
    switch (g->scr) {
    case GNB_LIST:
        if (g->more) { more_frame(g, edge, up, down); break; }           /* the More popup has the pad */
        if ((up || down) && g->focus) { g->focus = 0; g->dirty |= GNB_D_PANEL; }
        if (up) move(g, 0); else if (down) move(g, 1);
        list_frame(g, edge);
        break;
    case GNB_CREATE:
        if (up || down) {
            unsigned r = g->c_row;
            r = up ? (r ? r - 1u : 0u) : (r + 1u < CF_N ? r + 1u : r);
            if (r != g->c_row) { g->c_row = (unsigned char)r; g->page = (unsigned char)r; g->sfx = GNB_SFX_MOVE; g->msg = 0; g->dirty |= GNB_D_CURSOR | GNB_D_PANEL; }
        }
        form_frame(g, edge);
        break;
    case GNB_PASS: pass_frame(g, edge, up, down); break;
    case GNB_LOBBY:
        if (g->prep.open && !g->pick_open) {                                   /* the stage prep screen has the pad (a set room's strike / stage pick, the wait between two games); hold B still leaves */
            if (g->hold_b == GNB_HOLD_B_FRAMES) { g->hold_b++; lobby_leave(g); break; }
            prep_chars(g);                                                     /* a fighter locked since the last view shows on the bar at once */
            gnp_frame(&g->prep, edge, up, down);
            prep_out(g);
            break;
        }
        if (g->pick_open) { pick_frame(g, edge, up, down); break; }
        if (chat_frame(g, (edge & ~(unsigned)(GNB_B_LEFT | GNB_B_RIGHT)) | (redge & (unsigned)(GNB_B_LEFT | GNB_B_RIGHT)))) break;                                    /* the quick-chat picker took this frame (the D-Pad opens it; only the plain lobby: not the pick grid, not the crowns panel) */
        if (up) move(g, 0); else if (down) move(g, 1);
        lobby_frame(g, edge);
        break;
    default:                                                             /* GNB_BUSY: the glue answers; B is not taken (a request in flight is never abandoned half way) ... */
        if (g->rk.on && g->scr == GNB_BUSY && g->busy_what != GNB_REQ_LEAVE && g->hold_b == GNB_HOLD_B_FRAMES) { g->hold_b++; lobby_leave(g); }
        else if (!g->rk.on && g->scr == GNB_BUSY && g->busy_what != GNB_REQ_LEAVE && g->busy_t >= GNB_BUSY_ESC && g->hold_b == GNB_HOLD_B_FRAMES) { g->hold_b++; g->exit = 1; g->sfx = GNB_SFX_BACK; }      /* a request that has been in flight for 3 s: hold B 1.0 s backs out to the menu (the late answer finds the overlay closed) */      /* ... except in a Grandmaster set: the first view or a poll that never comes (cube 1083, after a failed connect) must not trap the player; hold B forfeits and leaves */
        break;
    }
}

/* ---- room chat: the strip and the D-Pad quick chat in the lobby ---- */
static void chat_safe(char *d, const char *s, unsigned max)         /* the console's chat characters: the menu font plus the punctuation the glue expands to full-width (gnb_ev.c sis_print); quote, backslash and backtick never arrive */
{
    static const char extra[] = " .,'-:!?;()[]+/%#&*@$=<>~|_^{}";
    unsigned i = 0, j;
    while (s[i] && i < max) {
        char ch = s[i];
        int ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
        for (j = 0; !ok && extra[j]; j++) if (extra[j] == ch) ok = 1;
        d[i] = ok ? ch : ' ';
        i++;
    }
    d[i] = 0;
}
void gnb_set_texts(struct gnb *g, const char (*t)[QC_TXT], unsigned n)
{
    unsigned i;
    g->ntxt = 0;
    for (i = 0; i < QC_N; i++) {
        g->txt[i][0] = 0;
        if (i >= n || !t[i][0] || (t[i][0] == '.' && !t[i][1])) continue;
        chat_safe(g->txt[i], t[i], QC_TXT - 1u);
        g->ntxt++;
    }
}
/* the console's texts: the player's own where the list holds a real text, else the default of that place (qc_pick.h).  own may be NULL (no client yet).  The hub client (mnc_init with defaults 0)
   fills every slot it has no text for with ".", which counts as no text */
void gnb_load_texts(struct gnb *g, const char (*own)[QC_TXT], unsigned n)
{
    char tmp[QC_N][QC_TXT];
    unsigned i, j;
    for (i = 0; i < QC_N; i++) {
        const char *t = (own && i < n) ? own[i] : "";
        if (!t[0] || (t[0] == '.' && !t[1])) t = qc_default(i);
        for (j = 0; j + 1u < QC_TXT && t[j]; j++) tmp[i][j] = t[j];
        tmp[i][j] = 0;
    }
    gnb_set_texts(g, (const char (*)[QC_TXT])tmp, QC_N);
}
static int chat_same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void chat_reset(struct gnb *g)
{
    g->ncl = 0; g->chat_first = 0; g->chat_cur = 0; g->chat_pend = 0; g->chat_busy = 0; g->chat_mute_left = 0;
    g->chat_tn = 0;
    qc_close(&g->qc);
}
static void chat_push(struct gnb *g, const char *name, const char *text, unsigned flash, unsigned ttl)
{
    unsigned i;
    if (g->ncl == GNB_CL_N) { for (i = 1; i < GNB_CL_N; i++) g->cl[i - 1u] = g->cl[i]; g->ncl--; }
    chat_safe(g->cl[g->ncl].name, name, GNB_CL_NAME - 1u);
    chat_safe(g->cl[g->ncl].text, text, GNB_CL_TXT - 1u);
    g->cl[g->ncl].flash = (unsigned char)(flash ? GNB_CL_FLASH : 0u);
    g->cl[g->ncl].ttl = (unsigned short)ttl;
    g->ncl++;
}
void gnb_net_chat(struct gnb *g, unsigned n, const struct gnb_chat_in *l)
{
    unsigned i, first = !g->chat_first;
    for (i = 0; i < n; i++) {
        if (l[i].id <= g->chat_cur) continue;
        g->chat_cur = l[i].id;
        if (l[i].age >= GNB_CL_LIFE_S) continue;                           /* already old when it arrives (a console that just joined): not shown at all */
        chat_push(g, l[i].name, l[i].text, !first, (GNB_CL_LIFE_S - l[i].age) * GNB_FPS);                      /* the lines that were there when we came in are not flashed */
    }
    g->chat_first = 1;
    if (n) g->dirty |= GNB_D_PANEL;
}
void gnb_net_chat_sent(struct gnb *g)
{
    g->chat_busy = 0; g->chat_mute_left = 0;
    g->msg = "Sent"; g->dirty |= GNB_D_PANEL;
    g->req = g->req == GNB_REQ_NONE ? GNB_REQ_CHAT : g->req;           /* our own line comes back through the next FETCH at once */
}
void gnb_net_chat_fail(struct gnb *g, unsigned why)
{
    g->chat_busy = 0;
    if (why == GNB_CW_MUTED) { g->chat_mute_left = GNB_CHAT_MUTE_FRAMES; g->msg = "The host muted you"; }
    else g->msg = why == GNB_CW_RATE ? "Slow down" : "Couldn't send the message";
    g->sfx = GNB_SFX_DENY; g->dirty |= GNB_D_PANEL;
}
unsigned gnb_chat_line(const struct gnb *g, unsigned i, char *text)
{
    unsigned n = g->ncl, rows = g->chat_mute_left ? GNB_CL_N - 1u : GNB_CL_N, shown, at;
    text[0] = 0;
    if (g->scr != GNB_LOBBY || g->pick_open || g->ros_open || g->qc.open || i >= GNB_CL_N) return 0;
    if (g->chat_mute_left && i == GNB_CL_N - 1u) { cpy(text, "The host muted you", 46); return 2u; }       /* the notice takes the strip's last line */
    if (i >= rows) return 0;
    shown = n < rows ? n : rows;
    if (i >= shown) return 0;
    at = n - shown + i;                                                   /* the newest lines, oldest first */
    cpy(text, g->cl[at].name, 46); cat(text, ": ", 46); cat(text, g->cl[at].text, 46);
    return g->cl[at].flash ? 1u : 3u;                                     /* 1 flashing, 3 plain */
}
unsigned gnb_qc_open(const struct gnb *g) { return g->qc.open; }
void gnb_qc_cell(const struct gnb *g, unsigned pos, char *out) { qc_cell(&g->qc, (const char (*)[QC_TXT])g->txt, pos, QC_TXT, out); }
void gnb_qc_head(const struct gnb *g, char *out)
{
    out[0] = 0;
    if (!g->qc.open) return;
    cpy(out, "Chat: ", 24); cat(out, qc_group_name(g->qc.group), 24);
}
static int chat_rate_ok(struct gnb *g, int take)                         /* the server allows 6 lines in 10 s: GNB_FPS frames a second */
{
    unsigned i, k = 0;
    for (i = 0; i < g->chat_tn; i++) if (g->clk - g->chat_t[i] < 10ul * GNB_FPS) g->chat_t[k++] = g->chat_t[i];
    g->chat_tn = (unsigned char)k;
    if (k >= 6u) return 0;
    if (take) g->chat_t[g->chat_tn++] = g->clk;
    return 1;
}
/* the lobby's D-Pad: 1 = the picker took the frame (the lobby sees no key) */
static int chat_frame(struct gnb *g, unsigned edge)
{
    unsigned idx = 0, r, pressed = edge & (QC_DPAD | QC_B);
    if (!g->qc_on || g->scr != GNB_LOBBY || g->pick_open || g->ros_open) { if (g->qc.open) { qc_close(&g->qc); g->dirty |= GNB_D_PANEL; } return 0; }
    if (!g->qc.open) {
        if (!(pressed & QC_DPAD)) return 0;
        if (g->lob.members < 2u) { g->msg = "Nobody else is here"; g->sfx = GNB_SFX_DENY; g->dirty |= GNB_D_PANEL; return 1; }       /* (it opened with nobody to hear it) */
        if (!g->ntxt) { g->msg = "No quick chat texts yet"; g->sfx = GNB_SFX_DENY; g->dirty |= GNB_D_PANEL; return 1; }
        if (g->chat_mute_left) { g->msg = "The host muted you"; g->sfx = GNB_SFX_DENY; g->dirty |= GNB_D_PANEL; return 1; }       /* the picker refuses with the same line */
        pressed &= QC_DPAD;
    }
    r = qc_frame(&g->qc, pressed, g->clk, GNB_FPS, !g->chat_busy && !g->chat_pend, &idx);
    if (r == QC_NONE) return g->qc.open;
    g->dirty |= GNB_D_PANEL;
    if (r & QC_OPENED) { g->msg = 0; g->sfx = GNB_SFX_MOVE; }
    if (r & QC_BLOCKED) g->sfx = GNB_SFX_DENY;
    if ((r & QC_CLOSED) && !(r & QC_SENT)) { g->sfx = GNB_SFX_BACK; if (edge & QC_B) g->hold_b = GNB_HOLD_B_FRAMES + 1u; }      /* (a B that closes the picker must not also count as the start of the leave hold) */
    if (r & QC_SENT) {
        if (idx >= QC_N || !g->txt[idx][0]) { g->msg = "No text there"; g->sfx = GNB_SFX_DENY; return 1; }
        if (!chat_rate_ok(g, 1)) { g->msg = "Slow down"; g->sfx = GNB_SFX_DENY; return 1; }
        g->sfx = GNB_SFX_FWD;
        if (g->live) { g->chat_pend = (unsigned char)(idx + 1u); g->msg = "Sending"; }
        else { chat_push(g, "You", g->txt[idx], 1u, GNB_CL_LIFE_S * GNB_FPS); g->msg = "Sent"; }
    }
    return 1;
}

/* ---- answers ---- */
void gnb_net_list(struct gnb *g, unsigned total, unsigned page, unsigned n, const struct gnb_room *e)
{
    unsigned i, at;
    if (page == 0) { g->n = 0; g->got = 0; }
    at = g->n;
    for (i = 0; i < n && at < GNB_MAX; i++) {
        struct gnb_room *r = &g->r[at];
        g->got = (unsigned char)(g->got + 1u);
        if (!room_ok(g, &e[i])) continue;
        r->mode = e[i].mode; r->mem = e[i].mem; r->cap = e[i].cap; r->flags = e[i].flags; r->stadium = e[i].stadium; r->region = e[i].region; r->smode = e[i].smode; r->rtype = e[i].rtype < GNB_RT_N ? e[i].rtype : 0u; r->minutes = e[i].minutes;
        cpy(r->code, e[i].code, 8);
        safe_cpy(r->name, e[i].name, 12);
        safe_cpy(r->host, e[i].host, 12);
        at++;
    }
    g->n = (unsigned char)at;
    sort_rooms(g);
    g->total = (unsigned char)(total > 255u ? 255u : total);
    g->net = g->n ? GNB_N_OK : GNB_N_EMPTY;
    if (g->got < g->total && g->got < GNB_MAX && n) { g->req = GNB_REQ_LIST; g->lpage = (unsigned char)(page + 1u); }
    else if (g->msg && g->msg[0] == 'R' && g->msg[1] == 'e') g->msg = "Updated just now";
    if (g->scr == GNB_LIST) clamp(g);
    g->dirty = GNB_D_ALL;
}

static const char *why_text(unsigned why)
{
    switch (why) {
    case GNB_W_UNREACH: case GNB_W_NOGN: return "Server not available";
    case GNB_W_SIGNIN: return "Game Night needs an account";      /* no identity, or a guest (ACCOUNT_NEEDED) */
    case GNB_W_NOTFOUND: return "Room not found or wrong passcode";
    case GNB_W_FULL: return "Room is full";
    case GNB_W_BUILD: return "This room plays another build";
    case GNB_W_RULES: return "This room plays other rules";
    case GNB_W_RATE: return "Too many tries, wait a moment";
    case GNB_W_INROOM: return "Leave your other room first";
    case GNB_W_BAD: return "The server refused that";
    default: return "No answer from the server";
    }
}

void gnb_net_fail(struct gnb *g, unsigned what, unsigned why)
{
    if (what == GNB_REQ_LIST) {
        if (why == GNB_W_RATE && g->n) { g->msg = why_text(why); g->dirty |= GNB_D_PANEL; return; }   /* keep the cached list */
        g->net = why == GNB_W_NOGN ? GNB_N_NOGN : why == GNB_W_SIGNIN ? GNB_N_SIGNIN : GNB_N_UNREACH;
        g->msg = 0;
        if (g->scr == GNB_LIST) { g->first = 0; g->page = 0; }
        g->dirty = GNB_D_ALL;
        return;
    }
    if (what == GNB_REQ_STATE && g->scr == GNB_BUSY) {                  /* the resume after a set: no room (any failure) is the list */
        to_list(g, why == GNB_W_NOTFOUND ? "You are no longer in the room" : why_text(why)); g->req = GNB_REQ_LIST; g->lpage = 0; g->first = 0; g->page = 0; return;
    }
    if (what == GNB_REQ_STATE || what == GNB_REQ_ACTION || what == GNB_REQ_ROSTER) {
        if (why == GNB_W_NOTFOUND && g->tn.on && g->frd) { g->frd = 0; g->back_frd = 1; g->exit = 1; g->dirty = GNB_D_ALL; return; }   /* the match room of a tournament was closed (set decided, grace over): the bracket */
        if (why == GNB_W_NOTFOUND) { to_list(g, "You are no longer in the room"); g->req = GNB_REQ_LIST; g->lpage = 0; g->first = 0; g->page = 0; return; }
        if (what == GNB_REQ_ROSTER) { g->ros_open = 0; g->dirty = GNB_D_ALL; }          /* a failed roster request: no crowns panel */
        if (what == GNB_REQ_ACTION && g->prep.open) { g->prep.busy = 0; g->prep.dirty = 1; if (g->prep.step == GNP_CHAR) g->pick_ok = 0; }      /* (a refused OK on a character step: the next A is Change, the grid) */   /* a refused strike / pick (late, out of turn): the screen waits for the next view and lets the player press again */
        if (what == GNB_REQ_STATE && (why == GNB_W_UNREACH || why == GNB_W_SERVER) && ++g->st_miss < 3u) return;   /* a lost poll is not an outage: the next poll (2 s) asks again */
        g->msg = why_text(why); g->dirty |= GNB_D_PANEL;
        return;
    }
    if (what == GNB_REQ_LEAVE) { to_list(g, 0); g->req = GNB_REQ_LIST; g->lpage = 0; return; }
    /* join / create */
    if (what == GNB_REQ_JOIN && g->fj && !g->fpin && why == GNB_W_NOTFOUND && (g->frf & 7u)) {      /* a Friends-list join the server turned away (no longer the host's friend, say) in a room that has a passcode: the passcode screen */
        start_pass(g, 0);
        g->msg = (g->frf & 4u) ? "No such room, or it needs a passcode" : "Passcode required";      /* a typed code: the server answers alike for both */
        return;
    }
    if (what == GNB_REQ_JOIN && g->fj && g->fj_lob) {                                            /* an invite's room could not be joined from inside a room: the server kept us in ours, go back to it */
        g->fj = 0; g->fj_lob = 0;
        gnb_resume(g);
        g->msg = why_text(why);
        return;
    }
    if (what == GNB_REQ_JOIN && g->fj) { g->fj = 0; g->req = GNB_REQ_LIST; g->lpage = 0; }       /* the list was never fetched behind a Friends-list join: ask for it now */
    g->scr = what == GNB_REQ_CREATE ? GNB_CREATE : GNB_LIST;
    g->dirty = GNB_D_ALL;
    deny(g, why_text(why));
    if (g->scr == GNB_LIST) clamp(g);
}

/* the call-up ticket name of the set in the view: GN<room code>S<set id> (ms_rooms Room._start_set) */
static void play_name(struct gnb *g)
{
    char *d = g->play_name;
    clr(d);
    cat(d, "GN", 19);
    cat(d, g->lob.code, 19);
    cat(d, "S", 19);
    catu(d, g->lob.set_id, 19);
    if (g->lob.v6 && g->lob.best_of > 1u && g->lob.game >= 1u && g->lob.game <= 9u) { cat(d, "G", 19); catu(d, g->lob.game, 19); }      /* a Sweaty set: every game has its own ticket (GN<code>S<set>G<game>) */
}

/* the match room's name is a round code of the server (ms_tourney.match_name, 12 characters on the wire): the title the bracket shows ("W R1" -> "Winners Round 1", "L F" -> "Losers Final",
   "GF2" -> "Grand Final Reset"); anything else (an older server's cut title) is shown as it is.  out: 24 bytes. */
static void tn_round_title(char *out, const char *code)
{
    const char *p = code;
    out[0] = 0;
    if (p[0] == 'G' && p[1] == 'F' && (!p[2] || (p[2] == '2' && !p[3]))) { cpy(out, p[2] ? "Grand Final Reset" : "Grand Final", 23); return; }
    if ((p[0] == 'W' || p[0] == 'L') && p[1] == ' ') { cpy(out, p[0] == 'W' ? "Winners " : "Losers ", 23); p += 2; }
    if (p[0] == 'F' && !p[1]) cat(out, "Final", 23);
    else if (p[0] == 'S' && p[1] == 'F' && !p[2]) cat(out, "Semifinal", 23);
    else if (p[0] == 'Q' && p[1] == 'F' && !p[2]) cat(out, "Quarterfinal", 23);
    else if (p[0] == 'R' && p[1] >= '1' && p[1] <= '9' && (!p[2] || (p[2] >= '0' && p[2] <= '9' && !p[3]))) { cat(out, "Round ", 23); cat(out, p + 1, 23); }
    else cpy(out, code, 20);
}
void gnb_net_view(struct gnb *g, const struct gnb_lobby *v)
{
    unsigned i, was = g->scr;
    if (was != GNB_LOBBY || !g->lob.have || !chat_same(g->lob.code, v->code)) chat_reset(g);        /* another room: its chat starts clean */
    g->st_miss = 0;
    if (g->msg && (g->msg == why_text(GNB_W_UNREACH) || g->msg == why_text(GNB_W_SERVER))) { g->msg = 0; g->dirty |= GNB_D_PANEL; }    /* an answer: the outage line goes */
    g->lob = *v;
    g->lob.have = 1;
    safe_cpy(g->lob.rname, v->rname, 12);
    for (i = 0; i < 2u; i++) safe_cpy(g->lob.side_name[i], v->side_name[i], 12);
    for (i = 0; i < g->lob.nnext && i < 8u; i++) safe_cpy(g->lob.next[i], v->next[i], 12);
    /* A tournament match room seen again after a game of its set.  g->tn (and sg_entry's tn_back) are overlay RAM, and the overlay is loaded afresh after every game: the room came back
       as a plain Game Night room ("Room 0828, Sweaty, Waiting for players") and stayed up after the set.  The room itself says what it is: only a match room of the
       tournament driver has two seats and is neither listed, watched nor locked (ms_rooms.create_hidden); its name is the round ("Winners Semifi", "Losers R1").  When the set is decided
       the room is frozen, nothing is called any more: the room is left for the bracket by itself (three views in a row, a lost view must not do it). */
    if (g->live && g->resumed == 2u && g->lob.v3 && g->lob.cap == 2u && !(g->lob.rflags & 7u)) {
        if (!g->tn.on) {
            unsigned me = (g->lob.flags & GNB_V_ONSIDE) ? g->lob.side : 0xFFu;
            g->tn.on = 1; g->tn.bo = (unsigned char)(g->lob.v6 && g->lob.best_of ? g->lob.best_of : 3u); g->tn.secs = 0; g->tn.shown = 0; g->tn.t0 = g->clk; g->tn.over = 0;
            cpy(g->tn.name, "Tournament", 19); tn_round_title(g->tn.round, g->lob.rname);
            if (g->lob.nnext && g->lob.next[0][0]) {                       /* the tournament's own name: the server sends it as the match room's "queue" names, two pieces of 12 */
                unsigned a = 0, b;
                while (a < 12u && g->lob.next[0][a]) { g->tn.name[a] = g->lob.next[0][a]; a++; }
                if (g->lob.nnext > 1u) for (b = 0; b < 12u && a < 18u && g->lob.next[1][b]; b++) g->tn.name[a++] = g->lob.next[1][b];
                g->tn.name[a] = 0;
                if (g->tn.name[0] == 'O' && g->tn.name[1] == 'f' && g->tn.name[2] == 'f' && g->tn.name[3] == 'i' && g->tn.name[4] == 'c' && g->tn.name[5] == 'i' && g->tn.name[6] == 'a' && g->tn.name[7] == 'l' && !g->tn.name[8])
                    cpy(g->tn.name, "Multishine dailies", 19);                 /* the server's name of an official cup; the console says "Multishine dailies" everywhere (gnt.c GNT_OFFICIAL_NAME): cube 392, the header read "Official" after a game */
            }
            cpy(g->tn.opp, me <= 1u ? g->lob.side_name[1u - me] : "", 13);
            g->frd = 1;                                                    /* leaving returns to the Tournament screens (sg_entry gn_glue) */
        }
        if (g->lob.phase == 0u && !(g->lob.flags & GNB_V_CALLED) && g->lob.turn != GNB_TURN_WAIT && (!g->lob.v6 || g->lob.step == 0u) && g->scr != GNB_BUSY) {
            if (g->tn.over < 255u) g->tn.over++;
            if (g->tn.over == 3u) { lobby_leave(g); return; }
        } else g->tn.over = 0;
    }
    /* the pick turn's clock (counts down locally until the next view) and the grid: it closes when it is no longer this console's turn */
    g->pick_secs = (unsigned char)(g->lob.v3 && g->lob.phase == 1 && g->lob.turn <= 1u && (!g->lob.v6 || g->lob.step == GNB_STEP_PICK_CHAR) ? g->lob.secs : 0xFFu); g->pick_frames = 0;
    if (g->pick_open && !(g->pick_lock ? (g->lob.v6 && g->lob.phase == 1 && (g->lob.flags & GNB_V_CALLED) && gnb_forced_char(g) != GNB_RT_NONE) : my_turn(&g->lob))) { g->pick_open = 0; g->pick_lock = 0; }
    if (g->live && !g->rk.on) {                                          /* the stage prep screen (view v6 set rooms): strike / pick the stage, the wait between two games (a Grandmaster set feeds it from its own view, gnb_rk.c) */
        const struct gnb_lobby *lv = &g->lob;
        struct gnp_t6 t6;
        unsigned i;
        t6.have = lv->v6; t6.rtype = lv->rtype; t6.best_of = lv->best_of; t6.game = lv->game; t6.sc[0] = lv->score_w; t6.sc[1] = lv->score_c; t6.step = lv->step; t6.owner = lv->step_owner;
        t6.secs = lv->step_secs; t6.avail = lv->stage_avail; t6.n = lv->stage_n; t6.forced = lv->forced; t6.struck = lv->struck;
        for (i = 0; i < 8u; i++) t6.st[i] = lv->stage_list[i];
        gnp_from_room(&g->prep, &t6, lv->flags & GNB_V_ONSIDE ? lv->side : 0xFFu, lv->phase, (lv->flags & GNB_V_CALLED) != 0, lv->turn, lv->stage, lv->side_name[0], lv->side_name[1]);
        if (g->prep.open && !(g->prep.step == GNP_CHAR && g->pick_open)) { g->pick_open = 0; g->ros_open = 0; }      /* (a character step keeps the grid the player opened with Change) */
        prep_chars(g);
    } else if (!g->live) gnp_clear(&g->prep);
    if (g->lob.flags & GNB_V_CALLED) g->ros_open = 0;                    /* a call-up takes the screen back from the crowns panel */
    if (was != GNB_LOBBY) {
        g->fj = 0; g->fj_lob = 0;
        g->pick_open = 0; g->pick_lock = 0;
        g->scr = GNB_LOBBY; g->first = 0; g->page = 0; g->timer = 0;
        g->msg = 0;                                   /* no arrival note ("Room created" / "You joined" / "Back in the room" stayed under the panel and said nothing useful) */
    }
    /* the match loop: both sides picked and we are one of them: the browser closes and the character screen searches the set's name */
    if (g->live && !g->play && g->lob.v3 && g->lob.phase == 1 && (g->lob.flags & GNB_V_CALLED) && !(g->lob.flags & GNB_V_READY) && g->lob.turn == GNB_TURN_GO) {
        play_name(g);
        g->play = 1;
        g->msg = "Starting the match";
        g->sfx = GNB_SFX_FWD;
    }
    /* AUTO-WATCH: the player watched once and has not left: a running set (phase 2) that has a game being uploaded is watched from its start, no key.  Never while this
       player is in the set (called or on stage), never twice in a row without the lobby having seen the set end (GNB_AUTO_MAX bounds a watch that fails at once and comes back). */
    if (g->live && g->lob.phase != 2u) g->autoc = 0;
    if (g->live && g->autow && !g->watch && !g->play && g->scr == GNB_LOBBY && watchable(g) && g->lob.phase == 2u && !(g->lob.flags & (GNB_V_CALLED | GNB_V_ONSIDE)) && !(g->lob.wfacts & GNB_VF_NOLIVE)) {
        if (g->autoc >= GNB_AUTO_MAX) { g->autow = 0; g->autoc = 0; g->msg = "Auto watch stopped"; }
        else { g->autoc++; g->watch_live = 1; g->watch = 1; g->msg = "Auto watch starting"; g->sfx = GNB_SFX_FWD; }
    }
    g->polls++;
    clamp(g);
    g->dirty = GNB_D_ALL;
}

void gnb_resume(struct gnb *g)
{
    g->resumed = 1;
    g->scr = GNB_BUSY; g->busy_what = GNB_REQ_STATE; g->req = GNB_REQ_STATE; g->lob.have = 0;
    g->msg = 0; g->dirty = GNB_D_ALL;
}

/* display order of the crowns panel: the winner side, the challenger side, the queue in order, then the rest by crowns (most first) and name; the sort is client-side so the
   wire format stays as it is (the crowns byte carries bit 7 = "this is you", so a count is at most 127) */
static unsigned ros_rank(const struct gnb_roster *r, unsigned i)
{
    unsigned p = r->place[i];
    return p == 0xFEu ? 0u : p == 0xFDu ? 1u : p ? 1u + p : 1000u;
}
static int ros_before(const struct gnb_roster *r, unsigned a, unsigned b)      /* a sorts before b */
{
    unsigned ra = ros_rank(r, a), rb = ros_rank(r, b), ca = r->crowns[a] & 0x7Fu, cb = r->crowns[b] & 0x7Fu;
    if (ra != rb) return ra < rb;
    if (ca != cb) return ca > cb;
    if (r->pstreak[a] != r->pstreak[b]) return r->pstreak[a] > r->pstreak[b];      /* equal crowns: more streak wins first; a further tie keeps the server's order (the latest streak win first, then joined) */
    return 0;
}
void gnb_net_roster(struct gnb *g, const struct gnb_roster *r)
{
    unsigned i, j, k;
    g->ros = *r; g->ros_have = 1; g->polls++;
    if (g->ros.n > GNB_ROSTER_MAX) g->ros.n = GNB_ROSTER_MAX;
    for (i = 1; i < g->ros.n; i++)                                       /* insertion sort, at most 8 rows */
        for (j = i; j > 0 && ros_before(&g->ros, j, j - 1u); j--) {
            char tn[13]; unsigned char tc = g->ros.crowns[j], tp = g->ros.place[j], ts = g->ros.pstreak[j], tl = g->ros.plead[j];
            for (k = 0; k < 13u; k++) { tn[k] = g->ros.name[j][k]; g->ros.name[j][k] = g->ros.name[j - 1u][k]; g->ros.name[j - 1u][k] = tn[k]; }
            g->ros.crowns[j] = g->ros.crowns[j - 1u]; g->ros.place[j] = g->ros.place[j - 1u]; g->ros.pstreak[j] = g->ros.pstreak[j - 1u]; g->ros.plead[j] = g->ros.plead[j - 1u];
            g->ros.crowns[j - 1u] = tc; g->ros.place[j - 1u] = tp; g->ros.pstreak[j - 1u] = ts; g->ros.plead[j - 1u] = tl;
        }
    if (g->ros_open) g->dirty |= GNB_D_ALL;
}

/* one row of the standings panel: the player (" (You)" appended for the viewer), the crowns column (the plain count: the lead is said in the header line, gnb_lead_text), the status words.
   Returns 0 = no such row, else 1 + (1 viewer) + (2 stage row) */
unsigned gnb_roster_row(const struct gnb *g, unsigned i, char *name, char *crowns, char *status)
{
    unsigned p, c;
    if (i >= g->ros.n) return 0;
    cpy(name, g->ros.name[i], 12);
    c = g->ros.crowns[i] & 0x7Fu;
    crowns[0] = 0;
    catu(crowns, c, 6);
    p = g->ros.place[i];
    if (p == 0xFEu) cpy(status, "On stage", GNB_NUM);
    else if (p == 0xFDu) cpy(status, "Challenger", GNB_NUM + 4u);
    else if (p) { cpy(status, "Queue ", GNB_NUM); catu(status, p, GNB_NUM); }
    else cpy(status, "Watching", GNB_NUM);
    if (g->ros.crowns[i] & 0x80u) cat(name, " (You)", 20u);
    return 1u + ((g->ros.crowns[i] & 0x80u) ? 1u : 0u) + ((p >= 0xFDu) ? 2u : 0u);
}

void gnb_roster_streak(const struct gnb *g, unsigned i, char *out)
{
    out[0] = 0;
    if (i >= g->ros.n) { cpy(out, "-", 4); return; }
    catu(out, g->ros.pstreak[i], 4);
}

void gnb_lead_text(const struct gnb *g, char *out, unsigned maxname)
{
    unsigned k, n = 0, first = 0, second = 0;
    char a[14], b[14];
    for (k = 0; k < g->ros.n; k++) if (g->ros.plead[k] && (g->ros.crowns[k] & 0x7Fu)) { if (!n) first = k; else if (n == 1u) second = k; n++; }
    out[0] = 0;
    if (!n) { cpy(out, "No crowns yet", 40); return; }
    if (maxname > 12u) maxname = 12u;
    if (maxname < 3u) maxname = 3u;
    cpy(a, g->ros.name[first], 12);
    if (n == 1u) {
        if (slen(a) > maxname) { a[maxname - 1u] = '.'; a[maxname] = 0; }
        cpy(out, a, 40); cat(out, " currently in the lead", 40);
    } else if (n == 2u) {
        cpy(b, g->ros.name[second], 12);
        if (slen(a) > maxname) { a[maxname - 1u] = '.'; a[maxname] = 0; }
        if (slen(b) > maxname) { b[maxname - 1u] = '.'; b[maxname] = 0; }
        cpy(out, a, 40); cat(out, " and ", 40); cat(out, b, 40); cat(out, " are in the lead", 40);
    } else {
        catu(out, n, 40); cat(out, " players tied for the lead", 40);
    }
}

void gnb_lead_lines(const struct gnb *g, char *l1, char *l2, unsigned compact)
{
    unsigned k, n = 0, first = 0, second = 0;
    for (k = 0; k < g->ros.n; k++) if (g->ros.plead[k] && (g->ros.crowns[k] & 0x7Fu)) { if (!n) first = k; else if (n == 1u) second = k; n++; }
    l1[0] = 0; l2[0] = 0;
    if (!n) { cpy(l1, "No crowns yet", 40); return; }
    if (n == 1u) { cpy(l1, g->ros.name[first], 13); cpy(l2, "currently in the lead", 40); return; }
    if (n == 2u && !compact) { cpy(l1, g->ros.name[first], 13); cat(l1, " and ", 40); cat(l1, g->ros.name[second], 40); cpy(l2, "are in the lead", 40); return; }
    catu(l1, n, 40); cat(l1, " players", 40);
    cpy(l2, n == 2u ? "in the lead" : "tied for the lead", 40);
}

void gnb_net_left(struct gnb *g)
{
    if (g->frd) { g->frd = 0; g->back_frd = 1; g->exit = 1; g->sfx = GNB_SFX_BACK; g->dirty = GNB_D_ALL; return; }     /* joined from Friends: straight back to the Friends list */
    to_list(g, 0);
    g->req = GNB_REQ_LIST; g->lpage = 0; g->first = 0; g->page = 0;
}

/* ---- texts ---- */
static int is_mock(const struct gnb_room *r) { return (r->flags & GNB_F_MOCK) != 0; }

static void count(char *d, unsigned a, unsigned b) { clr(d); catu(d, a, GNB_NUM); cat(d, " of ", GNB_NUM); catu(d, b, GNB_NUM); }

static const char *role_of(const struct gnb_lobby *v, unsigned i)
{
    if (i == 0) return "Stage";
    if (i == 1) return "Next";
    (void)v;
    return "Queue";
}

unsigned gnb_row(const struct gnb *g, unsigned slot, char *num, char *title)
{
    unsigned i = (unsigned)g->first + slot;
    clr(num); clr(title);
    if (g->scr == GNB_CREATE || (g->scr == GNB_PASS && g->p_for == 1u)) {
        if (slot >= CF_N) return 0;
        switch (slot) {
        /* no grey label column (the labels did not match their values): every row says what it is in its own text, at the left edge (gnb_ev.c row_draw) */
        case CF_SIZE: cpy(title, "Players: ", GNB_TITLE); catu(title, SIZES[g->c_size % SIZE_N], GNB_TITLE); break;
        case CF_LIST: cpy(title, g->c_listed ? "Show in list: yes" : "Show in list: no", GNB_TITLE); break;
        case CF_SPECT: cpy(title, g->c_spect ? "Spectators: yes" : "Spectators: no", GNB_TITLE); break;
        case CF_TYPE:
            if (g->rt_ok) { cpy(title, "Mode: ", GNB_TITLE); cat(title, RT_NAME[g->c_type % GNB_RT_N], GNB_TITLE); return gnb_type_open(g, g->c_type) ? 0u : 1u; }      /* a type the server keeps closed looks dim (A says "Coming soon") */
            if (g->all_ok) { cpy(title, "Stages: ", GNB_TITLE); cat(title, stg(g->c_all ? 2u : 0u), GNB_TITLE); return 0; }       /* a server without room types: the old Stages row */
            cpy(title, "Stages: Legal, All soon", GNB_TITLE); return 1;      /* All is greyed until the server lets it: the row cannot change, and looks it; with All on the row is normal */
        case CF_PASS: if (g->c_pass) { cpy(title, "Passcode: ", GNB_TITLE); cat(title, g->c_pin, GNB_TITLE); } else cpy(title, "Passcode: none", GNB_TITLE); break;
        case CF_FSKIP: cpy(title, g->c_fskip ? "Skip passcode: yes" : "Skip passcode: no", GNB_TITLE); return (g->c_pass || !g->c_listed) ? 0u : 1u;   /* dim while there is no passcode to skip (A says so) */
        case CF_GO: cpy(title, "Create room", GNB_TITLE); return 0;
        default: break;
        }
        return 0;
    }
    if (g->scr == GNB_LOBBY || (g->scr == GNB_BUSY && g->busy_what == GNB_REQ_LEAVE)) {
        const struct gnb_lobby *v = &g->lob;
        if (i >= 2u + v->nnext) return 0;
        cpy(num, role_of(v, i), GNB_NUM);
        if (i >= 2u) { cat(num, " ", GNB_NUM); catu(num, i - 1u, GNB_NUM); }
        if (i < 2u) {
            if (!v->side_on[i]) { cpy(title, "Open", GNB_TITLE); return 1; }
            cpy(title, v->side_name[i], 12);
            if (!g->live && i < 2u && (g->lob_ex >> i) & 1u) cat(title, " Ready", GNB_TITLE);
            return (v->flags & GNB_V_ONSIDE) && v->side == i ? 2u : 0u;
        }
        cpy(title, v->next[i - 2u], 12);
        if (!g->live && i == 2u && (g->lob_ex & 4u)) cat(title, " Ready", GNB_TITLE);
        return (v->flags & GNB_V_QUEUED) && v->pos == i - 1u ? 2u : 0u;
    }
    if (g->net != GNB_N_OK) {                                           /* a state screen: the message sits in the first rows */
        const char *a = 0, *b = 0;
        switch (g->net) {
        case GNB_N_LOADING: a = "Loading"; break;
        case GNB_N_EMPTY: a = filters_active(g) ? "No rooms match" : "No rooms yet"; b = "Press Z to create one"; break;
        case GNB_N_NOGN: a = "No Game Night here"; b = "No rooms on this server"; break;      /* short lines: the list box is about 22 characters wide (the old two lines ran past it) */
        case GNB_N_SIGNIN: a = "Needs an account"; b = "Link this console first"; break;
        default: a = "Can't reach the server"; b = "Press Start to retry"; break;
        }
        if (slot == 0 && a) { cpy(title, a, 30); return 0; }
        if (slot == 1 && b) { cpy(title, b, 30); return 1; }
        return 0;
    }
    if (i >= g->n) return 0;
    {
        const struct gnb_room *r = &g->r[i];
        count(num, r->mem, r->cap);
        cpy(title, shown_name(r->name, g->rt_ok, r->rtype), 14);
        if (is_mock(r)) cat(title, " Mock", GNB_TITLE);
        return r->mem >= r->cap ? 1u : 0u;
    }
}

static int same_code(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int lobby_is_mock(const struct gnb *g)           /* the room of the lobby, found by its code in the list cache (a created room is never mock) */
{
    unsigned i;
    if (!g->live || !g->lob.code[0]) return 0;
    for (i = 0; i < g->n; i++) if (same_code(g->r[i].code, g->lob.code)) return is_mock(&g->r[i]);
    return 0;
}
static const char *phase_text(const struct gnb_lobby *v);
/* the lobby's status line (panel line 1): Rooms' "Waiting for players", the pick turn with its countdown, the set running */
static void lobby_status(const struct gnb *g, char *out)
{
    const struct gnb_lobby *v = &g->lob;
    clr(out);
    if (g->pick_open && g->pick_lock) { cpy(out, "Set for both players", GNB_LINE); return; }                     /* Ditto Dojo: the locked view */
    if (g->pick_open) {
        {   const char *nm = gnb_char_name(g->pick_cur);
            unsigned k = slen(nm);
            if (g->pick_cur < 26u && k + 15u > GNB_LINE) { clr(out); }                       /* a long name: the costume alone, the name is in the cell label */
            else { cpy(out, nm, GNB_LINE); if (g->pick_cur < 26u) cat(out, "  ", GNB_LINE); }
            if (g->pick_cur < 26u) { cat(out, "Costume ", GNB_LINE); catu(out, (unsigned)g->pick_cos[g->pick_cur] + 1u, GNB_LINE); cat(out, " of ", GNB_LINE); catu(out, gnb_costumes(g->pick_cur), GNB_LINE); }
            else { cpy(out, "Pick: ", GNB_LINE); cat(out, nm, GNB_LINE); }
        }
        return;
    }
    if (v->v3 && v->phase == 1) {
        if (v->v6 && gnb_forced_char(g) != GNB_RT_NONE && !my_turn(v)) {                                    /* Ditto Dojo: the picker has chosen, both play it (also while the match starts) */
            const char *nm = gnb_char_name(gnb_forced_char(g));
            if (slen(nm) + 10u <= GNB_LINE) { cpy(out, "Both play ", GNB_LINE); cat(out, nm, GNB_LINE); } else cpy(out, "Same fighter for both", GNB_LINE);
            return;
        }
        if (v->turn == GNB_TURN_GO) { cpy(out, (v->flags & GNB_V_CALLED) ? "Starting the match..." : "Starting the match", GNB_LINE); return; }
        if (v->turn == GNB_TURN_WAIT) { cpy(out, "Waiting for both players", GNB_LINE); return; }           /* Sweaty between two games */
        if (v->v6 && v->step != GNB_STEP_PICK_CHAR) {                                                       /* strike, ban, stage pick: the prep screen is piece E1's; until it is there a plain line */
            cpy(out, v->step == 2u ? "Striking stages" : v->step == 3u ? "Banning a stage" : v->step == 4u ? "Picking the stage" : "Getting ready", GNB_LINE);
            return;
        }
        if (my_turn(v)) {
            cpy(out, (v->v6 && v->rtype == GNB_RT_DITTO) ? "Pick for both: A " : "Your pick: press A ", GNB_LINE);
            if (gnb_pick_left(g) != 0xFFu) { catu(out, gnb_pick_left(g), GNB_LINE); cat(out, "s", GNB_LINE); }
        } else if (v->turn < 2u && v->side_name[v->turn][0]) { cpy(out, v->side_name[v->turn], GNB_LINE); cat(out, " is choosing", GNB_LINE); }   /* someone else's turn: no countdown (two countdowns in a row looked like one timer running twice) */
        else cpy(out, v->turn == 0 ? "Stage holder is choosing" : "Challenger is choosing", GNB_LINE);
        return;
    }
    if (g->tn.on && v->phase == 0) { cpy(out, v->members >= 2u ? "Opponent is here" : "Waiting for opponent", GNB_LINE); return; }     /* a tournament match room: the other seat is one named player */
    cpy(out, phase_text(v), GNB_LINE);
}

static const char *phase_text(const struct gnb_lobby *v)
{
    if (v->phase == 1) return (v->flags & GNB_V_CALLED) ? ((v->flags & GNB_V_READY) ? "Call-up: you are ready" : "Call-up: press A") : "Call-up";
    if (v->phase == 2) return "Set running";
    return "Waiting for players";
}

/* informational status lines (neutral); every other message is a refusal or a failure (red) */
static int msg_is_status(const char *m)
{
    static const char *const OK[] = { "You joined", "Room created", "Back in the room", "Updated just now", "Starting the match", "You are no longer in the room",
        "Refreshing", "Stopped watching", "Picked", "Waiting for players", "You are ready", "You are not ready", "Starting the live replay", "Starting the replay", "No live game yet", "No game to replay yet",
        "Set for both players", "Dimmed fighters are not allowed", "Your pick is played by both" };
    unsigned i;
    for (i = 0; i < sizeof OK / sizeof OK[0]; i++) if (same_code(m, OK[i])) return 1;
    return 0;
}

/* the passcode entry's panel of the Rooms look (no dashes, no second hint line): the title, the four digits, the cell under the cursor */
unsigned gnb_pass_view(const struct gnb *g, char *title, char *digits)
{
    clr(title); clr(digits);
    cpy(title, g->p_for == 1u ? "Set a passcode" : g->p_for == 2u ? "Enter the room code" : "Enter the passcode", GNB_LINE);
    cpy(digits, g->p_pin, 4);
    return g->p_pos & 3u;
}

static unsigned gnrk_panel(const struct gnb *g, unsigned line, char *out);      /* gnb_rk.c */
unsigned gnb_panel(const struct gnb *g, unsigned line, char *out)
{
    clr(out);
    if (line == GNB_PANEL - 1u) {                                       /* the last line: the status message */
        if (g->msg) { cpy(out, g->msg, 40); return msg_is_status(g->msg) ? 1u : 3u; }       /* red only for refusals (GUI audit RM-05) */
        if (g->scr == GNB_CREATE && g->rt_ok) { gnb_create_desc(g, out); return gnb_type_open(g, g->c_type) ? 0u : 1u; }      /* the one-line description of the chosen type (white; dim when the type is not open yet) */
        return 0;
    }
    if (g->scr == GNB_PASS) {
        unsigned k;
        switch (line) {
        case 0: cpy(out, g->p_for == 1u ? "Set a passcode" : g->p_for ? "Enter room code" : "Passcode required", GNB_LINE); return 2;
        case 2:
            for (k = 0; k < 4u; k++) {
                char t[2];
                t[0] = g->p_pin[k]; t[1] = 0;
                cat(out, t, GNB_LINE);
                if (k < 3u) cat(out, " ", GNB_LINE);
            }
            return 2;
        default: return 0;
        }
    }
    if (g->scr == GNB_BUSY) {
        if (line == 0) { cpy(out, g->busy_what == GNB_REQ_CREATE ? "Creating room" : g->busy_what == GNB_REQ_LEAVE ? "Leaving room" : "Joining room", GNB_LINE); return 2; }
        if (line == 1) { cpy(out, "Waiting for the server", GNB_LINE); return 1; }
        return 0;
    }
    if (g->scr == GNB_CREATE) {
        switch (line) {
        case 0: cpy(out, "Create room", GNB_LINE); return 2;
        case 1: if (g->rt_ok) { cpy(out, "Mode: ", GNB_LINE); cat(out, RT_NAME[g->c_type % GNB_RT_N], GNB_LINE); return 0; } cpy(out, "Stages: ", GNB_LINE); cat(out, stg(g->c_all ? 2u : 0u), GNB_LINE); return 0;
        case 2: cpy(out, g->c_spect ? "Spectators: allowed" : "Spectators: no", GNB_LINE); return 0;
        case 3: cpy(out, "Players: ", GNB_LINE); catu(out, SIZES[g->c_size % SIZE_N], GNB_LINE); return 0;
        case 4: cpy(out, g->c_pass ? "Passcode: set" : "Passcode: none", GNB_LINE); return 0;
        case 5: cpy(out, g->c_listed ? "Listed: yes" : "Listed: no, code only", GNB_LINE); return 1;
        default: return 0;
        }
    }
    if (g->scr == GNB_LOBBY && g->rk.on) return gnrk_panel(g, line, out);       /* a Grandmaster set: its own panel (gnb_rk.c) */
    if (g->scr == GNB_LOBBY) {
        const struct gnb_lobby *v = &g->lob;
        switch (line) {
        case 0: cpy(out, shown_name(v->rname, v->v6, v->rtype), 14); if (lobby_is_mock(g)) cat(out, " Mock", GNB_LINE); return 2;
        case 1: lobby_status(g, out); return v->phase == 1 && (v->flags & GNB_V_CALLED) ? 2u : 0u;
        case 2:
            if (v->flags & GNB_V_ONSIDE) cpy(out, v->side == 0 ? "You: on stage" : "You: challenger", GNB_LINE);
            else if (v->flags & GNB_V_QUEUED) { cpy(out, "You: queued ", GNB_LINE); catu(out, v->pos, GNB_LINE); }
            else cpy(out, (v->flags & GNB_V_OWNER) ? "You: host, not queued" : "You: not queued", GNB_LINE);
            return 0;
        case 3: cpy(out, "Players: ", GNB_LINE); catu(out, v->members, GNB_LINE); cat(out, "  Queue: ", GNB_LINE); catu(out, v->qlen, GNB_LINE); return 0;
        case 4: cpy(out, "Streak: ", GNB_LINE); catu(out, v->streak, GNB_LINE); cat(out, "  Crowns: ", GNB_LINE); catu(out, v->crowns, GNB_LINE); return 0;
        case 5: if (v->v6) { cpy(out, "Mode: ", GNB_LINE); cat(out, RT_NAME[v->rtype % GNB_RT_N], GNB_LINE); return 0; } cpy(out, "Stages: ", GNB_LINE); cat(out, stg(v->smode), GNB_LINE); return 0;
        case 6:
            if (!watchable(g) || g->watch) return 0;
            if (g->autow) { cpy(out, "Watching every game", GNB_LINE); return 2u; }                          /* waiting for the next game of a set */
            if (v->phase == 2u) { if (v->wfacts & GNB_VF_NOLIVE) { cpy(out, "No live game yet", GNB_LINE); return 1u; } cpy(out, "X: Watch live", GNB_LINE); return 1u; }
            if (v->wfacts & GNB_VF_NOREPLAY) { cpy(out, "No game to replay yet", GNB_LINE); return 1u; }      /* greyed: X would only end with the watcher's own message */
            cpy(out, "X: Replay last game", GNB_LINE); return 1u;
        default: return 0;
        }
    }
    /* the list */
    if (g->net != GNB_N_OK) {
        switch (g->net) {
        case GNB_N_LOADING: if (line == 0) { cpy(out, "Loading rooms", GNB_LINE); return 2; } return 0;
        case GNB_N_EMPTY: if (line == 0) { cpy(out, filters_active(g) ? "No rooms match" : "No rooms yet", GNB_LINE); return 2; } if (line == 1) { cpy(out, "Z: Create room", GNB_LINE); return 0; } return 0;
        case GNB_N_NOGN: if (line == 0) { cpy(out, "No Game Night here", GNB_LINE); return 3; } if (line == 1) { cpy(out, "The server has no rooms", GNB_LINE); return 1; } return 0;
        case GNB_N_SIGNIN: if (line == 0) { cpy(out, "Needs an account", GNB_LINE); return 3; } if (line == 1) { cpy(out, "Link one, then retry", GNB_LINE); return 1; } return 0;
        default: if (line == 0) { cpy(out, "Server not reachable", GNB_LINE); return 3; } if (line == 1) { cpy(out, "Start: Try again", GNB_LINE); return 1; } return 0;
        }
    }
    {
        const struct gnb_room *r = &g->r[cur(g) < g->n ? cur(g) : 0u];
        switch (line) {
        case 0: cpy(out, shown_name(r->name, g->rt_ok, r->rtype), 14); if (is_mock(r)) cat(out, " Mock", GNB_LINE); return 2;
        case 1: cpy(out, "Host: ", GNB_LINE); cat(out, r->host, GNB_LINE); return 0;
        case 2: cpy(out, "Players: ", GNB_LINE); catu(out, r->mem, GNB_LINE); cat(out, " of ", GNB_LINE); catu(out, r->cap, GNB_LINE); return r->mem >= r->cap ? 3u : 0u;
        case 3: cpy(out, r->mem >= r->cap ? "Status: room is full" : (r->flags & GNB_F_RUN) ? "Status: set running" : "Status: waiting", GNB_LINE); return r->mem >= r->cap ? 3u : 0u;
        case 4: cpy(out, (r->flags & GNB_F_SPECT) ? "Spectators: allowed" : "Spectators: no", GNB_LINE); return 0;
        case 5: if (g->rt_ok) { cpy(out, "Mode: ", GNB_LINE); cat(out, RT_NAME[r->rtype % GNB_RT_N], GNB_LINE); } else { cpy(out, "Stages: ", GNB_LINE); cat(out, stg(r->smode), GNB_LINE); }
            if (r->flags & GNB_F_PASS) { clr(out); cpy(out, "Passcode: required", GNB_LINE); return 2; } return 0;
        default: return 0;
        }
    }
}

void gnb_badge(const struct gnb *g, char *out)
{
    clr(out);
    if (g->scr == GNB_LOBBY) { catu(out, g->lob.members, 12); cat(out, " in room", 12); return; }
    if (g->scr != GNB_LIST) return;
    if (g->net == GNB_N_LOADING) { cpy(out, "Loading", 12); return; }
    if (g->net != GNB_N_OK) { cpy(out, "0 rooms", 12); return; }
    { unsigned k = filters_active(g) ? g->n : g->total; clr(out); catu(out, k, 12); cat(out, k == 1u ? " room" : " rooms", 12); }
}

int gnb_char_box(const struct gnb *g)
{
    return g->scr == GNB_LOBBY && g->lob.have && g->lob.phase == 1 && (g->lob.flags & GNB_V_CALLED) ? 1 : 0;
}

void gnb_head(const struct gnb *g, char *out)
{
    clr(out);
    if (g->scr == GNB_LOBBY || (g->scr == GNB_BUSY && g->busy_what == GNB_REQ_LEAVE)) { cpy(out, g->lob.rname[0] ? shown_name(g->lob.rname, g->lob.v6, g->lob.rtype) : "Room", 14); return; }
    if (g->scr == GNB_CREATE || (g->scr == GNB_PASS && g->p_for == 1u) || (g->scr == GNB_BUSY && g->busy_what == GNB_REQ_CREATE)) { cpy(out, "New room", 12); return; }
    cpy(out, "Rooms", 12);
}

const char *gnb_title(const struct gnb *g)
{
    switch (g->scr) {
    case GNB_CREATE: return "Create room";
    case GNB_PASS: return g->p_for == 1u ? "Create room" : "Join room";
    case GNB_LOBBY: return "Room lobby";
    case GNB_BUSY: return g->busy_what == GNB_REQ_CREATE ? "Create room" : g->busy_what == GNB_REQ_LEAVE ? "Room lobby" : "Join room";
    default: return "Game Night";
    }
}

static const char *foot_base(const struct gnb *g)
{
    const struct gnb_lobby *v = &g->lob;
    if (!g->live) return "Start: Ready  Hold B: Leave";
    if (g->ros_open) return "Y: Back  Hold B: Leave";
    if (v->v3 && v->phase == 1 && (v->flags & GNB_V_CALLED)) {            /* Rooms' PICKING (view v3): the turn, then the hand-off */
        if (v->turn == GNB_TURN_GO) return "Starting the match";
        if (g->pick_open) return g->pick_lock ? "B: Back" : "L R: Costume  A: Lock  B: Back";                 /* the stick needs no hint: three keys, equal shares */
        if (v->v6 && v->turn == GNB_TURN_WAIT) return "Next game soon  Hold B: Leave";
        if (v->v6 && gnb_forced_char(g) != GNB_RT_NONE && !my_turn(v)) return "A: See pick  Hold B: Leave";
        return my_turn(v) ? "A: Pick  Hold B: Leave" : "Wait for your pick  Hold B: Leave";
    }
    if ((v->flags & GNB_V_CALLED) && !(v->flags & GNB_V_READY)) return "A: Ready  Hold B: Leave";
    if (g->tn.on) return "Hold B: Leave";                                  /* a tournament match room: nothing else is on offer */
    {   /* "X: Watch" only in a spectatable room (watchable) and not while this player is in the set, and not greyed by the view (v4: no game to replay / no live game); "Y: Crowns" always;
           with the auto-watch on, "R: Auto watch off" takes the X item's place (the panel line says X: still works); the bar holds 53 columns */
        int playing = v->phase != 0 && (v->flags & (GNB_V_CALLED | GNB_V_ONSIDE));
        int w = watchable(g) && !g->watch && !playing && !(v->phase == 2u ? (v->wfacts & GNB_VF_NOLIVE) : (v->wfacts & GNB_VF_NOREPLAY));
        int au = watchable(g) && g->autow;
        if (v->flags & (GNB_V_QUEUED | GNB_V_ONSIDE)) {
            if (v->phase == 0) return "Y: Standings  Z: Leave queue  Hold B: Leave";                  /* (no X: Watch here: the bar holds 52 columns) */
            if (!(v->flags & (GNB_V_ONSIDE | GNB_V_CALLED))) {                                        /* queued while a set runs: Z still leaves the queue (Y still opens Standings, unlisted: the bar holds 52 columns) */
                if (au) return "Hold X: Stop watching  Z: Leave queue  Hold B: Leave";
                return w ? (v->phase == 2u ? "X: Live  Z: Leave queue  Hold B: Leave" : "X: Replay  Z: Leave queue  Hold B: Leave") : "Y: Standings  Z: Leave queue  Hold B: Leave";
            }
            if (au) return "Hold X: Stop watching  Y: Standings  Hold B: Leave";
            return w ? (v->phase == 2u ? "X: Live  Y: Standings  Hold B: Leave" : "X: Replay  Y: Standings  Hold B: Leave") : "Y: Standings  Hold B: Leave";
        }
        if (au) return "Hold X: Stop watching  Start: Join  Hold B: Leave";                                  /* (Y: Standings gives its place: the bar holds 52 columns, "R: Auto Off  Y: Standings  Start: Join  Hold B: Leave" is 53) */
        return w ? (v->phase == 2u ? "X: Live  Y: Standings  Start: Join  Hold B: Leave" : "X: Replay  Y: Standings  Start: Join  Hold B: Leave") : "Y: Standings  Start: Join  Hold B: Leave";   /* 53 columns at most */
    }
}

/* the lobby footer; "D-Pad: Chat"  joins it where the bar has room and while the picker is open the bar says how to use it */
const char *gnb_foot(const struct gnb *g)
{
    static char buf[64];
    const char *b;
    unsigned n, k = 0, toks = 1;
    if (g->qc.open) return "D-Pad: Send  B: Close";
    if (g->live && g->prep.open && !g->pick_open && g->scr == GNB_LOBBY) return gnp_foot(&g->prep);
    b = foot_base(g);
    if (!g->live || !g->qc_on || !g->ntxt || g->pick_open || g->ros_open) return b;
    n = slen(b);
    for (k = 0; k + 1u < n; k++) if (b[k] == ' ' && b[k + 1u] == ' ') toks++;
    if (toks >= 3u || n + 13u > 52u || (b[0] == 'S' && b[1] == 't' && b[2] == 'a' && b[3] == 'r' && b[4] == 't' && b[5] == 'i' && b[6] == 'n' && b[7] == 'g')) return b;     /* no room, or "Starting the match" */
    cpy(buf, b, 62); cat(buf, "  D-Pad: Chat", 62);
    return buf;
}

int gnb_act_enabled(const struct gnb *g, unsigned i)
{
    (void)g;
    return i < GNB_ACT_N;
}
static const char *const REG_SHORT[GNB_REG_N] = { "Any", "NAE", "NAW", "EU", "LA", "AS", "OC", "NAC" };      /* f_reg order: 0 Any, then ms_region.CODES + 1 */
static const char *const REG_LONG[GNB_REG_N] = { "all regions", "North America East", "North America West", "Europe", "Latin America", "Asia", "Oceania", "North America Central" };
const char *gnb_act_label(const struct gnb *g, unsigned i)
{
    static const char *const L[GNB_ACT_N] = { "Random", "Create", "Code", "More" };
    (void)g;
    return i < GNB_ACT_N ? L[i] : "";
}
const char *gnb_more_label(const struct gnb *g, unsigned row, char *out)
{
    out[0] = 0;
    switch (row) {
    case GNB_MORE_STAGES:
        if (g->rt_ok) { cpy(out, "Mode: ", 25); cat(out, g->f_stage && g->f_stage <= GNB_RT_N ? RT_NAME[g->f_stage - 1u] : "Any", 25); break; }
        cpy(out, "Stages: ", 25); cat(out, g->f_stage == 1u ? "Legal" : g->f_stage == 2u ? "All" : "Any", 25); break;
    case GNB_MORE_HIDE: cpy(out, "Hide full rooms: ", 25); cat(out, g->f_hide ? "Yes" : "No", 25); break;
    case GNB_MORE_SORT: cpy(out, "Sort: ", 25); cat(out, g->f_sort == GNB_SORT_NEWEST ? "Newest" : "Most players", 25); break;
    case GNB_MORE_REGION: cpy(out, "Region: ", 25); cat(out, !g->reg_user ? "Auto" : REG_SHORT[g->f_reg % GNB_REG_N], 25); break;
    default: break;
    }
    return out;
}
void gnb_status_text(const struct gnb *g, char *out)
{
    cpy(out, "Showing: ", 55);
    cat(out, REG_LONG[g->f_reg % GNB_REG_N], 55);
    if (!g->reg_user && g->f_reg) cat(out, " (auto)", 55);
    if (g->rt_ok) { if (g->f_stage && g->f_stage <= GNB_RT_N) { cat(out, ", ", 55); cat(out, RT_SHORT[g->f_stage - 1u], 55); cat(out, " rooms", 55); } else cat(out, ", all modes", 55); }
    else cat(out, g->f_stage == 1u ? ", legal stages" : g->f_stage == 2u ? ", all stages" : ", any stages", 55);
    if (g->f_hide) cat(out, ", no full rooms", 55);
}
unsigned gnb_create_smode(const struct gnb *g) { return !g->rt_ok && g->c_all && g->all_ok ? 2u : 0xFFu; }
unsigned gnb_create_type(const struct gnb *g) { return g->rt_ok ? (unsigned)(g->c_type % GNB_RT_N) : GNB_RT_NONE; }
unsigned gnb_list_filter(const struct gnb *g)
{
    if (g->rt_ok) return g->f_stage && g->f_stage <= GNB_RT_N ? (unsigned)g->f_stage - 1u : 0xFFu;
    return g->f_stage == 1u ? 0u : g->f_stage == 2u ? 2u : 0xFFu;
}
void gnb_create_desc(const struct gnb *g, char *out)
{
    clr(out);
    if (g->rt_ok) cpy(out, gnb_type_open(g, g->c_type) ? RT_DESC[g->c_type % GNB_RT_N] : "Coming soon", 60);
}
unsigned gnb_room_type(const struct gnb *g)
{
    unsigned i;
    if (g->lob.v6) return g->lob.rtype < GNB_RT_N ? g->lob.rtype : 0u;
    for (i = 0; i < g->n; i++) if (g->lob.code[0] && same_code(g->r[i].code, g->lob.code)) return g->r[i].rtype;
    return 0u;
}

unsigned gnb_layout(const struct gnb *g)
{
    if (g->scr == GNB_LOBBY || (g->scr == GNB_BUSY && g->busy_what == GNB_REQ_LEAVE)) return 3u;
    if (g->scr == GNB_CREATE || (g->scr == GNB_PASS && g->p_for == 1u) || (g->scr == GNB_BUSY && g->busy_what == GNB_REQ_CREATE)) return 2u;
    return g->net == GNB_N_OK ? 0u : 1u;
}

void gnb_code4(const char *code, char *out)
{
    unsigned n = 0, k;
    clr(out);
    while (code[n]) n++;
    for (k = n > 4u ? n - 4u : 0u; code[k]; k++) { char t[2]; t[0] = code[k]; t[1] = 0; cat(out, t, 4); }
}

unsigned gnb_lobby_cap(const struct gnb *g)
{
    unsigned i;
    if (g->lob.v3 && g->lob.cap) return g->lob.cap;                     /* view v3 carries the capacity */
    for (i = 0; i < g->n; i++) if (g->lob.code[0] && same_code(g->r[i].code, g->lob.code)) return g->r[i].cap;
    return 8u;
}
unsigned gnb_side_char(const struct gnb *g, unsigned side)
{
    const struct gnb_lobby *v = &g->lob;
    unsigned c;
    if (side > 1u || !v->have || !v->side_on[side]) return GNB_CHAR_NONE;
    c = v->side_char[side];
    if (g->live && (v->flags & GNB_V_ONSIDE) && v->side == side && g->pick_mine != GNB_CHAR_NONE && v->v3 && v->phase == 1) c = g->pick_mine;   /* a locked pick shows at once */
    return c < GNB_CHAR_RANDOM ? c : GNB_CHAR_NONE;
}
unsigned gnb_pick_left(const struct gnb *g)
{
    unsigned s = g->pick_secs, gone = g->pick_frames / GNB_FPS;
    if (s == 0xFFu || g->scr != GNB_LOBBY) return 0xFFu;
    if (g->lob.v3 && g->lob.phase == 1 && g->lob.turn < 2u && !((g->lob.flags & GNB_V_ONSIDE) && g->lob.turn == g->lob.side)) return 0xFFu;   /* the header clock too: only on this player's own turn */
    return s > gone ? s - gone : 0u;
}
void gnb_pick_clock(const struct gnb *g, char *out)
{
    unsigned s = gnb_pick_left(g);
    clr(out);
    if (g->prep.open && !g->pick_open) { gnp_clock(&g->prep, out); return; }                  /* the stage prep screen's clock sits in the same header slot */
    if (s == 0xFFu) return;
    catu(out, s / 60u, 8);
    cat(out, s % 60u < 10u ? ":0" : ":", 8);
    catu(out, s % 60u, 8);
}
const char *gnb_lobby_mode(const struct gnb *g) { (void)g; return "Singles"; }
/* the lobby header's top right (only Singles exists, so the mode is not shown): the rules in two parts, "Random Stages" (big) and the Stadium rule (small) */
void gnb_tn_set(struct gnb *g, const char *name, const char *round, const char *opp, unsigned bo, unsigned secs)
{
    g->tn.on = 1; g->tn.bo = (unsigned char)bo; g->tn.secs = (unsigned short)secs; g->tn.shown = (unsigned short)secs; g->tn.t0 = g->clk;
    cpy(g->tn.name, name, 18); cpy(g->tn.round, round, 23); cpy(g->tn.opp, opp, 12);
    g->dirty = GNB_D_ALL;
}
void gnb_tn_clock(const struct gnb *g, char *out)
{
    unsigned long gone = (g->clk - g->tn.t0) / 30ul;                      /* the overlay runs 30 frames a second */
    unsigned left;
    out[0] = 0;
    if (!g->tn.on || !g->tn.secs || g->lob.members >= 2u || gone >= g->tn.secs) return;
    left = (unsigned)(g->tn.secs - gone);
    cpy(out, "Join in ", 20); catu(out, left / 60u, 20); cat(out, left % 60u < 10u ? ":0" : ":", 20); catu(out, left % 60u, 20);
}
void gnb_open_text(const struct gnb *g, char *out)
{
    unsigned m = g->lob.minutes, h = m / 60u;
    out[0] = 0;
    if (g->tn.on) { cpy(out, g->tn.round, 20); return; }                     /* the round: "Losers Round 1", "Winners Semifinal" */
    if (g->rk.on) { cpy(out, "Ranked set", 20); return; }                    /* (a set has no open time) */
    if (!m) { cpy(out, "Just opened", 20); return; }
    cat(out, "Open for ", 20);
    if (h >= 99u) { cat(out, "99+ h", 20); return; }
    if (!h) { catu(out, m, 20); cat(out, " min", 20); return; }
    catu(out, h, 20); cat(out, " h", 20);
    if (h < 24u && m % 60u) { cat(out, " ", 20); catu(out, m % 60u, 20); cat(out, " min", 20); }      /* a day or more: whole hours only */
}
void gnb_lobby_rule_parts(const struct gnb *g, char *a, char *b)
{
    const char *r = gnb_lobby_rule(g);
    unsigned i = 0, k = 0;
    if (g->tn.on) {                                                      /* a tournament match: the set length (big), the opponent (small) */
        cpy(a, "Best of ", 23); catu(a, g->tn.bo ? g->tn.bo : 3u, 23);
        cpy(b, "vs ", 23); cat(b, g->tn.opp, 23);
        return;
    }
    if (g->lob.v6) {                                                     /* room types: the type's name (big); a Sweaty set adds the game and the set score (small): "Game 2 of 3  Set 1-0", between sets "Best of 3" */
        const struct gnb_lobby *v = &g->lob;
        cpy(a, r, 23); b[0] = 0;
        if (v->best_of > 1u) {
            if (v->game) { cpy(b, "Game ", 23); catu(b, v->game, 23); cat(b, " of ", 23); catu(b, v->best_of, 23); cat(b, "  Set ", 23); catu(b, v->score_w, 23); cat(b, "-", 23); catu(b, v->score_c, 23); }
            else { cpy(b, "Best of ", 23); catu(b, v->best_of, 23); }
        }
        return;
    }
    while (r[i] && !(r[i] == ',' && r[i + 1u] == ' ')) { a[i] = r[i]; i++; }
    a[i] = 0;
    if (r[i]) i += 2u;
    while (r[i]) b[k++] = r[i++];
    b[k] = 0;
    if (b[0] >= 'a' && b[0] <= 'z') b[0] = (char)(b[0] - 32);            /* the second part starts its own line */
}
/* The hand-off asks this when a call-up starts the match.  Until it also required stadium == 2; the  68a74d70 lets a spectatable room keep the normal Stadium
   (the server default), and a room made so (a lab room, the default of a client that sends the server default) never got an uploader, so nobody could watch it live. The uploader and the replay
   are proven for the normal Stadium, so the policy no longer decides; the server still refuses uploads for a room that is not spectatable. */
unsigned gnb_upload_room(unsigned rflags, unsigned stadium)
{
    (void) stadium;
    return (rflags & 2u) ? 1u : 0u;
}

const char *gnb_lobby_rule(const struct gnb *g)
{
    if (g->rk.on) return "Grandmaster";                                  /* a Grandmaster set (gnb_rk.c), not a room type */
    if (g->lob.v6) return RT_NAME[g->lob.rtype % GNB_RT_N];
    return g->lob.smode == 2u ? "All stages" : "Legal stages";      /*: one field, two values; no Stadium rule any more */
}

unsigned gnb_cols(const struct gnb *g, unsigned slot, char *code, char *host, char *region, char *mode, char *cnt)
{
    unsigned i = (unsigned)g->first + slot, n = 0, k;
    const struct gnb_room *r;
    clr(code); clr(host); clr(region); clr(mode); clr(cnt);
    if (gnb_layout(g) != 0u || i >= g->n) return 0;
    r = &g->r[i];
    (void)n; (void)k;
    gnb_code4(r->code, code);
    cpy(host, r->host[0] ? r->host : "-", 12);
    {
        cpy(region, r->region < GNB_REG_N - 1u ? REG_SHORT[r->region + 1u] : "-", 4);       /* list v3: the room's region index (6 NAC); 0xFF unknown */
    }
    if (g->rt_ok) cpy(mode, RT_SHORT[r->rtype % GNB_RT_N], 8); else cpy(mode, r->mode == 0 ? "Singles" : "-", 8);        /* room types: the type's short name */
    count(cnt, r->mem, r->cap);
    return r->mem >= r->cap ? 1u : 0u;
}

const char *gnb_hint(const struct gnb *g)
{
    switch (g->scr) {
    case GNB_CREATE: return "A: Select  Start: Create  B: Back";                       /*: no stick help */
    case GNB_PASS: return "A: Next  B: Cancel";                         /*: no stick help; A in its usual place */
    case GNB_LOBBY: return gnb_foot(g);
    case GNB_BUSY: return "Working...";
    default:
        if (g->more) return "A: Change  B: Done";
        return g->live ? "A: Join  Z: Create room  Start: Refresh  B: Back" : "A: Join  Z: Create room  R: Next state  B: Back";
    }
}

const char *gnb_example_rooms_tag(void) { return "Example data"; }

const char *gnb_join_code(const struct gnb *g) { return g->fj ? g->fcode : g->sel < g->n ? g->r[g->sel].code : ""; }
const char *gnb_join_pin(const struct gnb *g)
{
    if (g->fj) return g->fpin ? g->p_pin : "";             /* a Friends-list join sends the typed passcode only after the passcode screen */
    return (g->sel < g->n && (g->r[g->sel].flags & GNB_F_PASS)) ? g->p_pin : "";
}
void gnb_create_args(const struct gnb *g, const char **name, unsigned *flags, unsigned *policy, const char **pin)
{
    *name = g->rt_ok ? RT_WIRE[g->c_type % GNB_RT_N] : "Game Night";      /* the Mode is the name */
    *flags = (g->c_listed ? 1u : 0u) | (g->c_spect ? 2u : 0u) | (g->c_fskip ? 8u : 0u);   /* 8: friends skip the passcode (meaningless, and harmless, where the room has none) */
    *policy = 0xFFu;                                                    /* the server's default: the normal Stadium in every room ( /) */
    *pin = g->c_pass ? g->c_pin : "";
}
unsigned gnb_create_cap(const struct gnb *g) { return SIZES[g->c_size % SIZE_N]; }   /* the room size asked for (2, 4, 8, 16, 32); the server clamps it to its own limit */

/* ---- the character-cell rendering (host golden tests, console fallback) ---- */
static void put(struct sg_view *v, unsigned r, unsigned attr, const char *s)
{
    unsigned c = 0;
    if (r >= SG_ROWS) return;
    while (s[c] && c < SG_COLS) { v->t[r][c] = s[c]; c++; }
    if (s[c]) sg_cut_count++;
    v->t[r][c] = 0;
    v->attr[r] = (unsigned char)attr;
}
static const unsigned char ATTR[4] = { SG_A_BODY, SG_A_DIM, SG_A_HI, SG_A_BAD };

void gnb_text(const struct gnb *g, struct sg_view *v, int mock_tag)
{
    char b[64], num[GNB_NUM + 1], title[40], ln[64], badge[16];
    unsigned s, a, i;
    clr(b);
    cpy(b, gnb_title(g), 28);
    if (mock_tag) { while (slen(b) < SGM_TAG_COL) cat(b, " ", 60); cat(b, SGM_TAG, 60); }
    put(v, 0, SG_A_HEAD, b);
    gnb_badge(g, badge); clr(b);
    cat(b, g->live ? "Rooms server" : gnb_example_rooms_tag(), 60);
    if (badge[0]) { cat(b, "   ", 60); cat(b, badge, 60); }
    put(v, 1, SG_A_DIM, b);
    for (s = 0; s < GNB_VIS; s++) {
        a = gnb_row(g, s, num, title);
        clr(ln);
        cat(ln, s == g->page && gnb_rows(g) ? "- " : "  ", 47);
        if (num[0] || title[0]) {
            cat(ln, num, 47);
            while (slen(ln) < 2u + GNB_NUM + 1u) cat(ln, " ", 47);
            cat(ln, title, 47);
        }
        put(v, 3u + s, s == g->page && gnb_rows(g) ? SG_A_HI : ATTR[a & 3u], ln);
    }
    if (gnb_max_first(g)) {
        clr(ln); cat(ln, "  ", 47); catu(ln, g->first + 1u, 47); cat(ln, " - ", 47);
        catu(ln, g->first + GNB_VIS < gnb_rows(g) ? g->first + GNB_VIS : gnb_rows(g), 47); cat(ln, " of ", 47); catu(ln, gnb_rows(g), 47);
        put(v, 12, SG_A_DIM, ln);
    }
    for (i = 0; i < GNB_PANEL; i++) {
        a = gnb_panel(g, i, ln);
        if (i == GNB_PANEL - 1u && !g->msg && g->scr == GNB_CREATE && g->rt_ok && slen(ln) > 44u) {      /* a description longer than the row (Ditto Dojo) wraps to a second row */
            unsigned k = 44u;
            { unsigned j = k; while (j && !(ln[j] == ' ' && ln[j - 1u] == '.')) j--; if (j) k = j; else while (k && ln[k] != ' ') k--; }      /* after a sentence when one fits, else at the last space */
            ln[k] = 0; put(v, 14u + i, ATTR[a & 3u], ln); put(v, 15u + i, ATTR[a & 3u], ln + k + 1u);
        } else
        put(v, 14u + i, ATTR[a & 3u], ln);
    }
    if (g->scr == GNB_LOBBY && g->prep.open && !g->pick_open) {                           /* the stage prep screen replaces the rows (the same texts the console front end draws) */
        char pl[14][56];
        unsigned k, np = gnp_text(&g->prep, pl, 14u);
        for (k = 0; k < GNB_VIS; k++) put(v, 3u + k, SG_A_BODY, "");
        for (k = 0; k < np && k < GNB_VIS; k++) put(v, 3u + k, k == 0u ? SG_A_HI : SG_A_BODY, pl[k]);
    }
    put(v, SG_ROWS - 1u, SG_A_DIM, gnb_hint(g));
}

#include "gnb_prep.c"      /* the stage prep model (gnb_prep.h) is one translation unit with the lobby: every build that lists gnb.c carries it, no build list needs a new source */
#include "gnb_rk.c"        /* the Grandmaster set on the browser (gnb.h gnrk_*), the same way */
