/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Multishine > Tournament, the model (gnt.h).  Strict C89, no libc, no heap, no Melee address, no network. */
#include "gnt.h"
#include "gnt_tmb.h"
#include "gnt_tmb.c"
#include "sg_ui.h"
#include "../ms_fmt.h"

/* ---- small string helpers (no libc) ---- */
static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static char *cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i < max) { d[i] = s[i]; i++; }
    d[i] = 0;
    return d + i;
}
static void cat(char *d, const char *s, unsigned max) { unsigned n = slen(d); cpy(d + n, s, max > n ? max - n : 0u); }
static void catu(char *d, unsigned long v, unsigned max)
{
    char r[12];
    r[ms_fmt_u(r, v, 11u)] = 0;
    cat(d, r, max);
}
static int drawable(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == ',' || c == '\'' || c == '-' || c == ':';
}
static void safe_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i < max) { d[i] = drawable(s[i]) ? s[i] : ' '; i++; }
    d[i] = 0;
}
static void zero(void *p, unsigned n) { unsigned char *b = (unsigned char *)p; while (n--) *b++ = 0; }
static void say(struct gnt *g, const char *m, int warn) { cpy(g->say, m, GNT_SAY); g->say_warn = (unsigned char)warn; g->dirty |= GNT_D_PANEL; }

/* ---- bracket arithmetic ---- */
unsigned gnt_size_for(unsigned n)
{
    unsigned s = 2u;
    while (s < n && s < 64u) s <<= 1;
    return s;
}
unsigned gnt_round_count(unsigned size)
{
    unsigned r = 0;
    while (size > 1u) { size >>= 1; r++; }
    return r;
}
unsigned gnt_round_matches(unsigned size, unsigned round)
{
    unsigned m = size >> 1, r;
    for (r = 1; r < round && m; r++) m >>= 1;
    return round >= 1u && round <= gnt_round_count(size) ? m : 0u;
}
unsigned gnt_round_start(unsigned size, unsigned round)
{
    unsigned s = 0, r;
    for (r = 1; r < round; r++) s += gnt_round_matches(size, r);
    return s;
}
const char *gnt_round_name(unsigned rounds, unsigned round, char *buf)
{
    unsigned from_end = rounds - round;
    buf[0] = 0;
    if (round < 1u || round > rounds) { cat(buf, "Round", 14); return buf; }
    if (from_end == 0u) cat(buf, "Final", 14);
    else if (from_end == 1u) cat(buf, "Semifinal", 14);
    else if (from_end == 2u && rounds >= 4u) cat(buf, "Quarterfinal", 14);
    else { cat(buf, "Round ", 14); catu(buf, round, 14); }
    return buf;
}

/* ---- list geometry ---- */
static unsigned has_create(const struct gnt *g) { return g->can_create || g->mock; }
/* the list shows only cups (Create and Sign up by ID are footer keys X and Y); g->v maps the shown rows to g->e: the filter applied, the Multishine dailies on top (soonest first), then the custom cups in the server's order */
static unsigned list_rows(const struct gnt *g) { return g->net == GNT_N_OK ? (unsigned)g->nv : 0u; }
static const struct gnt_ent *vent(const struct gnt *g, unsigned i) { return &g->e[g->v[i]]; }
static unsigned is_official(const struct gnt_ent *e) { return (e->flags & GNT_F_OFFICIAL) != 0u; }
static unsigned long official_key(const struct gnt_ent *e) { return e->state <= GNT_S_CHECKIN ? e->secs : 0xFFFFFFFFul; }   /* a started or finished daily after the waiting ones */
static unsigned region_shown(const struct gnt *g, const struct gnt_ent *e) { return g->filter == 0u || e->region == 0u || e->region == g->filter; }      /* a cup open to any region shows under every filter */
static const char *const REGION_NAME[GNT_REG_N] = { "Any", "NAE", "NAW", "EU", "LA", "AS", "OC", "NAC" };
static void list_view(struct gnt *g)          /* rebuild g->v after a new list or a filter change; the cursor stays on the same cup when it is still shown. Row 0 is ONE row for all dailies (the one in dsel), then the custom cups */
{
    unsigned i, k, n = 0, id = 0, had = 0, all = 0;
    if (g->sel < g->nv) { id = g->e[g->v[g->sel]].id; had = 1; }
    for (i = 0; i < g->n; i++) {                                                         /* the dailies, soonest first (an insertion sort keeps equal ones in the server's order) */
        const struct gnt_ent *e = &g->e[i];
        if (!is_official(e)) continue;
        for (k = all; k > 0u && official_key(&g->e[g->dl[k - 1u]]) > official_key(e); k--) g->dl[k] = g->dl[k - 1u];
        g->dl[k] = (unsigned char)i; all++;
    }
    g->dsel = 0;
    for (i = 0; i < all; i++) if (g->e[g->dl[i]].id == g->did) { g->dsel = (unsigned char)i; break; }      /* the daily stays the shown one across a refresh; none known: the soonest */
    if (i >= all)                                                                        /* ... or, first, the daily you are IN that is not over (staging 391: a console that opened the list while its daily was
                                                                                            running saw the next upcoming one, A opened that one's rules and the called match was never found) */
        for (i = 0; i < all; i++) { const struct gnt_ent *e = &g->e[g->dl[i]]; if ((e->flags & GNT_F_JOINED) && e->state <= GNT_S_RUNNING) { g->dsel = (unsigned char)i; break; } }
    if (all) g->did = g->e[g->dl[g->dsel]].id;
    g->dn = (unsigned char)all;                                                          /* the dailies are region Any: shown under every region filter */
    if (g->dn) g->v[n++] = g->dl[g->dsel];
    for (i = 0; i < g->n; i++) if (!is_official(&g->e[i]) && region_shown(g, &g->e[i])) g->v[n++] = (unsigned char)i;
    g->nv = (unsigned char)n;
    if (had) for (i = 0; i < n; i++) if (g->e[g->v[i]].id == id) { g->sel = (unsigned char)i; break; }
    if (g->sel >= n) g->sel = (unsigned char)(n ? n - 1u : 0u);
}
const char *gnt_filter_name(const struct gnt *g) { return !g->f_user ? "Auto" : REGION_NAME[g->filter % GNT_REG_N]; }      /* Auto: the account's region (not set: Any) */
static void fix(struct gnt *g);

void gnt_set_account_region(struct gnt *g, unsigned rgn)
{
    g->acct_rgn = (unsigned char)(rgn >= 1u && rgn <= GNT_REG_N - 1u ? rgn : 0u); if (!g->cf_region_set) g->cf_region = g->acct_rgn;
    if (!g->f_user && g->filter != g->acct_rgn) { g->filter = g->acct_rgn; if (g->scr == GNT_LIST) { list_view(g); fix(g); g->dirty = GNT_D_ALL; } }      /* Auto follows the account */
}
unsigned gnt_create_region(const struct gnt *g) { return g->cf_region % GNT_REG_N; }
static unsigned page_rows(const struct gnt *g);
/* the call is shown and played only on the tab and round it belongs to */
static unsigned call_here(const struct gnt *g) { return g->called && g->b_side == g->call_side && g->b_round == g->call_round; }
/* the matches of one side and round, in id order (the wire lists them by id: winners rounds, losers rounds, GF1, the reset) */
static unsigned br_cnt(const struct gnt *g, unsigned side, unsigned round)
{
    unsigned i, n = 0;
    for (i = 0; i < GNT_MATCHES; i++) if (g->b_m[i].mid && g->b_m[i].side == side && g->b_m[i].round == round) n++;
    return n;
}
static const struct gnt_match *br_at(const struct gnt *g, unsigned side, unsigned round, unsigned k)
{
    unsigned i, n = 0;
    for (i = 0; i < GNT_MATCHES; i++) if (g->b_m[i].mid && g->b_m[i].side == side && g->b_m[i].round == round) { if (n == k) return &g->b_m[i]; n++; }
    return 0;
}
static unsigned br_nrounds(const struct gnt *g, unsigned side)
{
    unsigned i, r = 0;
    for (i = 0; i < GNT_MATCHES; i++) if (g->b_m[i].mid && g->b_m[i].side == side && g->b_m[i].round > r) r = g->b_m[i].round;
    return r;
}
static unsigned br_rows(const struct gnt *g)
{
    return g->b_have ? br_cnt(g, g->b_side, g->b_round) : 0u;
}
/* the tab title and the round title: "Winners Semifinal", "Losers Round 3", "Grand Final", "Reset" */
static void br_title(const struct gnt *g, unsigned side, unsigned round, char *buf)
{
    char rn[16];
    buf[0] = 0;
    if (side == GNT_SIDE_F) { cat(buf, round >= 2u ? "Grand Final Reset" : "Grand Final", 30); return; }
    if (side == GNT_SIDE_L) {
        unsigned nr = br_nrounds(g, GNT_SIDE_L);
        if (round == nr) cat(buf, "Losers Final", 30);
        else { cat(buf, "Losers Round ", 30); catu(buf, round, 30); }
        return;
    }
    if (g->b_format == GNT_FMT_DOUBLE) cat(buf, "Winners ", 30);
    cat(buf, gnt_round_name(g->b_rounds, round, rn), 30);
}
void gnt_round_title(const struct gnt *g, unsigned side, unsigned round, char *buf) { br_title(g, side, round, buf); }
unsigned gnt_tree_on(const struct gnt *g) { return g->scr == GNT_BRACKET && g->b_have && g->b_tree && g->gfx; }
/* the Create form (Tournament v2): the rows by id (Preset first); Sign-up window hides for a scheduled start, Start day / hour show for "Pick time" */
enum { CR_PRESET, CR_NAME, CR_FORMAT, CR_SIZE, CR_SIGNUP, CR_CHECK, CR_SEED, CR_HOST, CR_START, CR_DAY, CR_HOUR, CR_REGION, CR_PASS, CR_GO, CR_MAX };
enum { JR_ID, JR_PASS, JR_GO, JR_MAX };
#define START_PICK 5u
static unsigned cr_id(const struct gnt *g, unsigned i)
{
    unsigned id, n = 0;
    if (g->scr == GNT_JOIN) return i < JR_MAX ? i : (unsigned)JR_GO;
    for (id = 0; id < CR_MAX; id++) {
        if (id == CR_SIGNUP && g->cf_start) continue;
        if ((id == CR_DAY || id == CR_HOUR) && g->cf_start != START_PICK) continue;
        if (n == i) return id;
        n++;
    }
    return CR_GO;
}
static unsigned create_rows(const struct gnt *g)
{
    unsigned n = 0, id;
    for (id = 0; id < CR_MAX; id++) {
        if (id == CR_SIGNUP && g->cf_start) continue;
        if ((id == CR_DAY || id == CR_HOUR) && g->cf_start != START_PICK) continue;
        n++;
    }
    return n;
}
unsigned gnt_rows(const struct gnt *g)
{
    switch (g->scr) {
    case GNT_LIST: return list_rows(g);
    case GNT_PAGE: return page_rows(g);
    case GNT_CREATE: return create_rows(g);
    case GNT_JOIN: return JR_MAX;
    case GNT_RULES: return 0u;
    default: return br_rows(g);
    }
}
unsigned gnt_cur_first(const struct gnt *g) { return g->scr == GNT_BRACKET ? g->b_first : g->first; }
unsigned gnt_cur_page(const struct gnt *g) { if (g->scr == GNT_CREATE || g->scr == GNT_JOIN) return g->cf_row - g->first; return (g->scr == GNT_BRACKET ? g->b_sel : g->sel) - gnt_cur_first(g); }
unsigned gnt_vis(const struct gnt *g) { return g->scr == GNT_CREATE || g->scr == GNT_JOIN ? GNT_FORM_VIS : GNT_LIST_VIS; }
unsigned gnt_max_first(const struct gnt *g) { unsigned n = gnt_rows(g), v = gnt_vis(g); return n > v ? n - v : 0u; }
unsigned gnt_scroll_more(const struct gnt *g)
{
    unsigned f = g->first;
    if (g->scr != GNT_LIST && g->scr != GNT_PAGE && g->scr != GNT_CREATE && g->scr != GNT_JOIN) return 0u;
    if (gnt_rows(g) == 0u) return 0u;
    return (f > 0u ? 1u : 0u) | (f < gnt_max_first(g) ? 2u : 0u);
}
static void sync_scroll(struct gnt *g, unsigned *sel, unsigned *first)
{
    unsigned n = gnt_rows(g);
    if (n == 0u) { *sel = 0; *first = 0; return; }
    if (*sel >= n) *sel = n - 1u;
    if (*sel < *first) *first = *sel;
    if (*sel >= *first + gnt_vis(g)) *first = *sel + 1u - gnt_vis(g);
    if (*first > gnt_max_first(g)) *first = gnt_max_first(g);
}
static void fix(struct gnt *g)
{
    unsigned s, f;
    if (g->scr == GNT_BRACKET) { s = g->b_sel; f = g->b_first; sync_scroll(g, &s, &f); g->b_sel = (unsigned char)s; g->b_first = (unsigned char)f; }
    else { s = g->sel; f = g->first; sync_scroll(g, &s, &f); g->sel = (unsigned char)s; g->first = (unsigned char)f; }
}
static void daily_step(struct gnt *g, int d)     /* Left Right on the dailies row: the next / previous daily, wrapping */
{
    unsigned n = g->dn;
    unsigned s = d > 0 ? (g->dsel + 1u) % n : (g->dsel + n - 1u) % n;
    g->did = g->e[g->dl[s]].id;
    g->say[0] = 0; list_view(g); fix(g); g->sfx = GNT_SFX_MOVE; g->dirty = GNT_D_ALL;
}

/* ---- life cycle ---- */
static void ask(struct gnt *g, unsigned req, unsigned id)
{
    g->req = (unsigned char)req; g->req_id = (unsigned short)id; g->busy = 1;
    g->dirty |= GNT_D_PANEL;
}
static void ask_list(struct gnt *g) { ask(g, GNT_REQ_LIST, 0); g->auto_t = 0; }

void gnt_enter(struct gnt *g)
{
    zero(g, (unsigned)sizeof *g);
    g->scr = GNT_LIST;
    g->net = GNT_N_LOADING;
    g->cf_size = 1; g->cf_format = GNT_FMT_DOUBLE;
    { unsigned q; for (q = 0; q < 8u; q++) g->cf_pass[q] = g->jid[q] = g->jpw[q] = '0'; g->cf_pass[8] = g->jid[8] = g->jpw[8] = 0; }
    g->cf_signup = 2; g->cf_hostplays = 1; g->cf_start = 0; g->cf_pass_on = 0; g->cf_pass_pos = g->jid_pos = g->jpw_pos = 0; g->jpw_on = 0;
    g->cf_days = 1; g->cf_hours = 0;
    ask_list(g);
    g->dirty = GNT_D_ALL;
}

static void to_screen(struct gnt *g, unsigned scr)
{
    g->scr = (unsigned char)scr;
    g->say[0] = 0; g->say_warn = 0;
    g->dirty = GNT_D_ALL;
}

/* ---- the page's action rows ---- */
enum { PA_JOIN, PA_LEAVE, PA_CHECKIN, PA_START, PA_BRACKET, PA_FORFEIT, PA_CALLOFF, PA_REFRESH, PA_ID, PA_BACK };   /* PA_ID: an info row, the 8 digit id */
static unsigned no_play_host(const struct gnt_ent *e) { return (e->flags & GNT_F_HOST) && !(e->flags & GNT_F_JOINED) && !e->host_plays; }     /* the organiser of a cup they do not play in: the server refuses their join (TN_NOT_ALLOWED) */
static unsigned page_action(const struct gnt *g, unsigned row)
{
    unsigned a[10], n = 0;
    const struct gnt_ent *e = &g->cur;
    unsigned in = (e->flags & GNT_F_JOINED) != 0u, early = e->state <= GNT_S_CHECKIN, off = (e->flags & GNT_F_OFFICIAL) != 0u;
    if (off && e->state == GNT_S_CHECKIN && !in && !(e->flags & GNT_F_LATE)) early = 0;             /* an official cup in check-in: only late registration lets you in */
    if (e->state == GNT_S_CHECKIN && in && !(e->flags & GNT_F_CHECKED)) a[n++] = PA_CHECKIN;           /* the urgent one first: the cursor starts on it */
    if (early && !in && !no_play_host(e)) a[n++] = PA_JOIN;
    if (early && in) a[n++] = PA_LEAVE;
    if (e->state == GNT_S_REG && !off && (e->flags & GNT_F_HOST)) a[n++] = PA_START;                                 /* (no host controls on an official cup) */
    if (e->state == GNT_S_RUNNING || e->state == GNT_S_DONE) a[n++] = PA_BRACKET;
    if (e->state == GNT_S_RUNNING && in && !(e->flags & GNT_F_OUT)) a[n++] = PA_FORFEIT;      /* leaving a running tournament forfeits it (TN_LEAVE result 1) */
    if ((e->flags & GNT_F_HOST) && !off && e->state <= GNT_S_RUNNING) a[n++] = PA_CALLOFF;         /* the host calls the cup off (hold A); a cup that is over has no such row */
    a[n++] = PA_REFRESH;
    if (e->pubid) a[n++] = PA_ID;
    return row < n ? a[row] : PA_BACK;
}
static unsigned page_rows(const struct gnt *g)
{
    unsigned n = 0;
    while (page_action(g, n) != PA_BACK) n++;
    return n;
}

/* ---- Z: sign up / leave / check in for the cup under the cursor (Z signs up or leaves, A opens the rules) ---- */
enum { ZA_NONE, ZA_JOIN, ZA_LEAVE, ZA_CHECKIN };
static unsigned z_action(const struct gnt_ent *e)
{
    unsigned in = (e->flags & GNT_F_JOINED) != 0u, off = (e->flags & GNT_F_OFFICIAL) != 0u;
    if (e->state > GNT_S_CHECKIN) return ZA_NONE;
    if (in) return (e->state == GNT_S_CHECKIN && !(e->flags & GNT_F_CHECKED)) ? ZA_CHECKIN : ZA_LEAVE;
    if (off && e->state == GNT_S_CHECKIN && !(e->flags & GNT_F_LATE)) return ZA_NONE;          /* an official cup in check-in: only late registration lets you in */
    if (no_play_host(e)) return ZA_NONE;
    return ZA_JOIN;
}
static const struct gnt_ent *z_ent(const struct gnt *g)
{
    if (g->scr == GNT_RULES || g->scr == GNT_PAGE) return &g->cur;
    if (g->scr == GNT_LIST && g->net == GNT_N_OK && g->sel < g->nv) return vent(g, g->sel);
    return 0;
}
const char *gnt_z_label(const struct gnt *g)
{
    const struct gnt_ent *e = z_ent(g);
    switch (e ? z_action(e) : ZA_NONE) {
    case ZA_JOIN: return "Sign up";
    case ZA_LEAVE: return "Leave";
    case ZA_CHECKIN: return "Check in";
    default: return "";
    }
}
static void z_press(struct gnt *g)
{
    const struct gnt_ent *e = z_ent(g);
    unsigned a = e ? z_action(e) : (unsigned)ZA_NONE;
    if (g->busy) { g->sfx = GNT_SFX_DENY; return; }
    if (!e) { g->sfx = GNT_SFX_DENY; return; }
    if (a == ZA_NONE) { say(g, e->state > GNT_S_CHECKIN ? "Entry is closed." : (e->flags & GNT_F_HOST) ? "You host this one." : "Sign-up is closed now.", 1); g->sfx = GNT_SFX_DENY; return; }
    if (g->scr == GNT_LIST) g->cur = *e;                                                       /* the glue's join / leave / check in read the tournament from cur (id and passcode) */
    g->sfx = GNT_SFX_FWD;
    if (a == ZA_JOIN) { ask(g, GNT_REQ_JOIN, g->cur.id); say(g, "Signing up...", 0); }
    else if (a == ZA_LEAVE) { ask(g, GNT_REQ_LEAVE, g->cur.id); say(g, "Leaving...", 0); }
    else { ask(g, GNT_REQ_CHECKIN, g->cur.id); say(g, "Checking in...", 0); }
}

static const char *const CF_NAMES[4] = { "Priority", "Friday Night", "Open Bracket", "Weekly" };
static const unsigned char CF_SIZES[4] = { 8, 16, 32, 64 };
static const unsigned char CF_CHECK[3] = { 0, 5, 10 };

/* ---- input ---- */
static void move(struct gnt *g, int dir)
{
    unsigned n = gnt_rows(g), *sp, tmp;
    if (g->scr == GNT_LIST && g->net != GNT_N_OK && g->net != GNT_N_EMPTY) return;
    if (n == 0u) return;
    tmp = g->scr == GNT_BRACKET ? g->b_sel : g->sel;
    sp = &tmp;
    if (dir > 0) { if (*sp + 1u < n) (*sp)++; else { g->sfx = GNT_SFX_DENY; return; } }
    else { if (*sp > 0u) (*sp)--; else { g->sfx = GNT_SFX_DENY; return; } }
    if (g->scr == GNT_BRACKET) g->b_sel = (unsigned char)tmp; else g->sel = (unsigned char)tmp;
    fix(g);
    if (g->scr == GNT_LIST) g->say[0] = 0;
    g->sfx = GNT_SFX_MOVE; g->dirty |= GNT_D_ALL;
}

static void request_bracket(struct gnt *g)
{
    if (g->busy) return;
    ask(g, GNT_REQ_BRACKET, g->cur.id);
    g->poll = 0;
}
static void open_page(struct gnt *g, const struct gnt_ent *e)
{
    g->cur = *e;
    g->sel = 0; g->first = 0;
    g->my_kind = 0; g->my_place = 0; g->my_asked = 0;
    to_screen(g, GNT_PAGE);
    g->sfx = GNT_SFX_FWD;
    ask(g, GNT_REQ_INFO, e->id);
}
static void open_bracket(struct gnt *g)
{
    g->b_have = 0;
    g->b_side = GNT_SIDE_W; g->b_round = 1; g->b_sel = 0; g->b_first = 0;
    g->called = 0; g->dismissed = 0; g->b_tree = 1;
    to_screen(g, GNT_BRACKET);
    g->sfx = GNT_SFX_FWD;
    request_bracket(g);
}
/* the row of your match in the shown side and round, else 0 */
static unsigned my_row(const struct gnt *g)
{
    unsigned n = br_rows(g), i;
    if (g->b_me == GNT_NONE) return 0;
    for (i = 0; i < n; i++) { const struct gnt_match *m = br_at(g, g->b_side, g->b_round, i); if (m && (m->a == g->b_me || m->b == g->b_me)) return i; }
    return 0;
}
/* the tab and round to show first: the winners round the server is playing; a finished tournament shows the final */
static void pick_round(struct gnt *g)
{
    unsigned r = g->cur.round;
    g->b_side = GNT_SIDE_W;
    if (g->cur.state == GNT_S_DONE && br_nrounds(g, GNT_SIDE_F)) { g->b_side = GNT_SIDE_F; r = 1; }
    else if (r < 1u || r > g->b_rounds) r = 1u;
    g->b_round = (unsigned char)r;
}
/* the next tab with matches (Y): winners, losers, grand final */
static void next_side(struct gnt *g)
{
    unsigned k, s = g->b_side;
    for (k = 0; k < 3u; k++) {
        s = (s + 1u) % 3u;
        if (br_nrounds(g, s)) { g->b_side = (unsigned char)s; g->b_round = 1; g->b_sel = 0; g->b_first = 0; g->sfx = GNT_SFX_MOVE; g->dirty = GNT_D_ALL; return; }
    }
}

static void page_press(struct gnt *g)
{
    unsigned a = page_action(g, g->sel);
    if (g->busy) { g->sfx = GNT_SFX_DENY; return; }
    switch (a) {
    case PA_ID: g->sfx = GNT_SFX_DENY; break;
    case PA_JOIN: ask(g, GNT_REQ_JOIN, g->cur.id); g->sfx = GNT_SFX_FWD; say(g, "Joining...", 0); break;
    case PA_LEAVE:
    case PA_FORFEIT: ask(g, GNT_REQ_LEAVE, g->cur.id); g->sfx = GNT_SFX_FWD; say(g, "Leaving...", 0); break;
    case PA_CHECKIN: ask(g, GNT_REQ_CHECKIN, g->cur.id); g->sfx = GNT_SFX_FWD; say(g, "Checking in...", 0); break;
    case PA_START: ask(g, GNT_REQ_START, g->cur.id); g->sfx = GNT_SFX_FWD; say(g, "Starting...", 0); break;
    case PA_BRACKET: open_bracket(g); break;
    case PA_CALLOFF: say(g, "Hold A to call off.", 1); g->sfx = GNT_SFX_DENY; break;               /* a tap only shows the hint; the hold (gnt_frame) does it */
    default: ask(g, GNT_REQ_INFO, g->cur.id); g->sfx = GNT_SFX_FWD; break;
    }
}

/* the passcode ring (Create and Sign up by ID): None, then the 8 digits; Left Right walk it, A turns the digit under the cursor */
static void pw_move(unsigned char *on, unsigned char *pos, int dir)
{
    if (dir > 0) { if (!*on) { *on = 1; *pos = 0; } else if (*pos >= 7u) *on = 0; else (*pos)++; }
    else { if (!*on) { *on = 1; *pos = 7; } else if (*pos == 0u) *on = 0; else (*pos)--; }
}
static void digit_up(char *d, unsigned pos) { d[pos & 7u] = (char)(d[pos & 7u] >= '9' || d[pos & 7u] < '0' ? '0' : d[pos & 7u] + 1); d[8] = 0; }
static void digit_down(char *d, unsigned pos) { d[pos & 7u] = (char)(d[pos & 7u] > '9' || d[pos & 7u] <= '0' ? '9' : d[pos & 7u] - 1); d[8] = 0; }
static void pw_digit(unsigned char *on, unsigned char *pos, char *d, int dir) { if (!*on) { *on = 1; *pos = 0; } else if (dir > 0) digit_up(d, *pos); else digit_down(d, *pos); }
static void pick_fix(struct gnt *g)                                              /* Pick time: at least 1 hour, at most 30 days */
{
    if (g->cf_days >= 30u) { g->cf_days = 30; g->cf_hours = 0; }
    else if (g->cf_days == 0u && g->cf_hours < 1u) g->cf_hours = 1;
}
/* keep the cursor and the scroll inside the form (the form has up to 12 rows, the box shows 9) */
static void cf_fix(struct gnt *g)
{
    unsigned s = g->cf_row, f = g->first;
    sync_scroll(g, &s, &f);
    g->cf_row = (unsigned char)s; g->first = (unsigned char)f;
}
static void cf_goto(struct gnt *g, unsigned id)                                  /* the cursor on the row `id` (the visible rows shift when Start changes) */
{
    unsigned i, n = gnt_rows(g);
    for (i = 0; i < n; i++) if (cr_id(g, i) == id) { g->cf_row = (unsigned char)i; break; }
    cf_fix(g);
}
/* region values 0 Any, 1..7 = ms_region.CODES index + 1; the pickers cycle west to east: Any, NAW, NAC, NAE, EU, LA, AS, OC */
static const unsigned char REG_NX[8] = { 2, 3, 7, 4, 5, 6, 0, 1 }, REG_PV[8] = { 6, 7, 0, 1, 3, 4, 5, 2 };
static unsigned char ring(unsigned v, unsigned n, int dir) { return (unsigned char)((v + (dir > 0 ? 1u : n - 1u)) % n); }

/* the Create presets: Custom first, then Grand Open, then the rest in a fixed order (Quick Draw dropped); Left Right on the Preset row flicks through them (wrapping).  A named preset sets
   format, size, sign-up, check-in, seeding, host plays (always Yes: a console that creates plays in it) and start (Name and Region stay the player's, the passcode goes to None); any later change to one of those rows shows Custom again.
   Custom is the player's own values, saved when the row leaves Custom and put back when it returns.  Client side only: the request is built from the rows as before. */
static const char *const PRESET_NAME[GNT_PRESETS] = { "Custom", "Grand Open", "Midnight Cup", "Pocket Bracket", "Locals Night", "Evening Cup", "Last Call" };
static const char *const PRESET_TXT[GNT_PRESETS] = { "Your own values on every row.", "64 players, double, tomorrow.", "16 players, single, tonight.",
    "8 players, starts in 30 min.", "32 players, double, tonight.", "16 players, double, in 1 hour.", "16 players, starts in 15 min." };
static const unsigned char PRESET_V[GNT_PRESETS - 1u][7] = {   /* cf_format (0 double, 1 single), cf_size (8/16/32/64), cf_signup (15/30/60/120), cf_checkin (Off/5/10), cf_seed, cf_hostplays, cf_start (Now/1h/3h/Tonight/Tomorrow) */
    { 0, 3, 2, 2, 0, 1, 4 },    /* Grand Open: 64, double, Tomorrow, check-in 10 */
    { 1, 1, 2, 0, 0, 1, 3 },    /* Midnight Cup: 16, single, Tonight */
    { 1, 0, 1, 0, 0, 1, 0 },    /* Pocket Bracket: 8, single, 30 min, Now */
    { 0, 2, 2, 2, 0, 1, 3 },    /* Locals Night: 32, double, Tonight, check-in 10 */
    { 0, 1, 2, 1, 0, 1, 0 },    /* Evening Cup: 16, double, 60 min, check-in 5, Now */
    { 1, 1, 0, 0, 0, 1, 0 } };  /* Last Call: 16, single, 15 min, Now */
static void cf_vals(struct gnt *g, unsigned char *v, unsigned put)
{
    unsigned char *f[GNT_PRESET_VALS];
    unsigned k;
    f[0] = &g->cf_format; f[1] = &g->cf_size; f[2] = &g->cf_signup; f[3] = &g->cf_checkin; f[4] = &g->cf_seed; f[5] = &g->cf_hostplays; f[6] = &g->cf_start; f[7] = &g->cf_days; f[8] = &g->cf_hours; f[9] = &g->cf_pass_on;
    for (k = 0; k < GNT_PRESET_VALS; k++) { if (put) *f[k] = v[k]; else v[k] = *f[k]; }
}
static void preset_step(struct gnt *g, int dir)
{
    unsigned p = ring(g->cf_preset, GNT_PRESETS, dir);
    if (!g->cf_preset) cf_vals(g, g->cf_own, 0u);                               /* leaving Custom: remember the player's values */
    if (p) { unsigned char v[GNT_PRESET_VALS]; unsigned k; for (k = 0; k < 7u; k++) v[k] = PRESET_V[p - 1u][k]; v[7] = 1; v[8] = 0; v[9] = 0; cf_vals(g, v, 1u); }
    else cf_vals(g, g->cf_own, 1u);
    g->cf_preset = (unsigned char)p;
    cf_goto(g, CR_PRESET);                                                      /* (the Start value changes which rows show) */
}
static void create_value(struct gnt *g, int dir)
{
    unsigned r = cr_id(g, g->cf_row);
    if (r != CR_PRESET && r != CR_NAME && r != CR_REGION && r != CR_GO) g->cf_preset = 0;     /* a row a preset sets was changed: the values are the player's own now (the day / hour rows show only for Pick time, i.e. on Custom already) */
    switch (r) {
    case CR_PRESET: preset_step(g, dir); break;
    case CR_NAME: g->cf_name = ring(g->cf_name, 4u, dir); break;
    case CR_FORMAT: g->cf_format = (unsigned char)(g->cf_format ^ 1u); break;
    case CR_SIZE: g->cf_size = ring(g->cf_size, 4u, dir); break;
    case CR_SIGNUP: g->cf_signup = ring(g->cf_signup, 4u, dir); break;
    case CR_CHECK: g->cf_checkin = ring(g->cf_checkin, 3u, dir); break;
    case CR_SEED: g->cf_seed = ring(g->cf_seed, 3u, dir); break;
    case CR_REGION: g->cf_region = (g->cf_region < GNT_REG_N ? (dir > 0 ? REG_NX : REG_PV)[g->cf_region] : 0u); g->cf_region_set = 1; break;
    case CR_HOST: g->cf_hostplays = (unsigned char)(g->cf_hostplays ^ 1u); break;
    case CR_START: g->cf_start = ring(g->cf_start, 6u, dir); if (g->cf_start == START_PICK) pick_fix(g); cf_goto(g, CR_START); break;
    case CR_DAY: g->cf_days = ring(g->cf_days, 31u, dir); pick_fix(g); break;
    case CR_HOUR: {
        unsigned lo = g->cf_days == 0u ? 1u : 0u, h = g->cf_hours;
        if (g->cf_days >= 30u) { g->sfx = GNT_SFX_DENY; return; }                   /* 30 days is the horizon: no hours on top */
        if (dir > 0) h = h >= 23u ? lo : h + 1u; else h = h <= lo ? 23u : h - 1u;
        g->cf_hours = (unsigned char)h;
        break;
    }
    case CR_PASS: pw_move(&g->cf_pass_on, &g->cf_pass_pos, dir); break;
    default: g->sfx = GNT_SFX_DENY; return;
    }
    g->sfx = GNT_SFX_MOVE; g->dirty |= GNT_D_ALL;
}
static void create_x(struct gnt *g)                                              /* X on the passcode row: the digit under the cursor minus one */
{
    if (cr_id(g, g->cf_row) != CR_PASS) return;
    g->cf_preset = 0; pw_digit(&g->cf_pass_on, &g->cf_pass_pos, g->cf_pass, -1); g->sfx = GNT_SFX_MOVE; g->dirty |= GNT_D_ALL;
}
static void create_a(struct gnt *g)
{
    unsigned r = cr_id(g, g->cf_row);
    if (r == CR_GO) { if (g->busy) g->sfx = GNT_SFX_DENY; else { ask(g, GNT_REQ_CREATE, 0); g->sfx = GNT_SFX_FWD; say(g, "Creating...", 0); } }
    else if (r == CR_PASS) { g->cf_preset = 0; pw_digit(&g->cf_pass_on, &g->cf_pass_pos, g->cf_pass, 1); g->sfx = GNT_SFX_MOVE; g->dirty |= GNT_D_ALL; }
    else create_value(g, 1);
}
/* Sign up by ID: ID (8 digits), Passcode (None or 8 digits, private cups), Join */
static void join_value(struct gnt *g, int dir)
{
    unsigned r = g->cf_row;
    if (r == JR_ID) g->jid_pos = (unsigned char)((g->jid_pos + (dir > 0 ? 1u : 7u)) & 7u);
    else if (r == JR_PASS) pw_move(&g->jpw_on, &g->jpw_pos, dir);
    else { g->sfx = GNT_SFX_DENY; return; }
    g->sfx = GNT_SFX_MOVE; g->dirty |= GNT_D_ALL;
}
static void join_x(struct gnt *g)                                                /* X on the ID / Passcode rows: the digit under the cursor minus one */
{
    unsigned r = g->cf_row;
    if (r == JR_ID) digit_down(g->jid, g->jid_pos);
    else if (r == JR_PASS) pw_digit(&g->jpw_on, &g->jpw_pos, g->jpw, -1);
    else return;
    g->sfx = GNT_SFX_MOVE; g->dirty |= GNT_D_ALL;
}
static void join_a(struct gnt *g)
{
    unsigned r = g->cf_row;
    if (r == JR_ID) { digit_up(g->jid, g->jid_pos); g->sfx = GNT_SFX_MOVE; g->dirty |= GNT_D_ALL; }
    else if (r == JR_PASS) { pw_digit(&g->jpw_on, &g->jpw_pos, g->jpw, 1); g->sfx = GNT_SFX_MOVE; g->dirty |= GNT_D_ALL; }
    else if (g->busy) g->sfx = GNT_SFX_DENY;
    else if (!gnt_join_id(g)) { say(g, "Type the 8 digit ID first.", 1); g->sfx = GNT_SFX_DENY; }
    else { ask(g, GNT_REQ_JOINID, 0); g->sfx = GNT_SFX_FWD; say(g, "Joining...", 0); }
}

void gnt_frame(struct gnt *g, unsigned held, int stick_up, int stick_down)
{
    unsigned edge = held & ~(unsigned)g->prev, lr_ok = (g->lr_cool == 0u);
    int up = (edge & GNT_B_UP) || stick_up, down = (edge & GNT_B_DOWN) || stick_down;
    g->prev = (unsigned short)held;
    if (held & (GNT_B_L | GNT_B_R)) g->lr_cool = (unsigned char)GNT_LR_GAP;       /* L R held (or chattering) keeps the next press blocked until both have been up for GNT_LR_GAP frames */
    else if (g->lr_cool) g->lr_cool--;
    if (g->cool) g->cool--;
    g->clk++;
    if (g->called && g->scr == GNT_BRACKET && g->call_secs && g->clk % 30ul == 0ul) g->dirty |= GNT_D_PANEL;       /* the call's clock runs down on the screen */
    /* timers: the list refreshes itself, the bracket is asked again */
    if (g->scr == GNT_LIST && g->net == GNT_N_OK && !g->busy && ++g->auto_t >= GNT_AUTO_FRAMES) ask_list(g);
    if (g->scr == GNT_BRACKET && g->b_have && !g->busy && ++g->poll >= GNT_POLL_FRAMES) request_bracket(g);
    if (g->scr == GNT_PAGE && !g->busy && g->cur.state <= GNT_S_CHECKIN && ++g->poll >= GNT_POLL_FRAMES) { g->poll = 0; ask(g, GNT_REQ_INFO, g->cur.id); }      /* the window may open or the tournament start while the page is up */
    /* cube 391 (an official cup on staging): the rules of a cup stayed on "Check-in is open / You are checked in." while the cup started and the player's match was called; nothing on that
       page said so.  The rules ask like the page while the cup is not running, and a cup you are in that starts takes you to its page (which looks for your call) */
    if (g->scr == GNT_RULES && !g->busy && g->cur.id && g->cur.state <= GNT_S_CHECKIN && ++g->poll >= GNT_POLL_FRAMES) { g->poll = 0; ask(g, GNT_REQ_INFO, g->cur.id); }
    if (g->scr == GNT_RULES && !g->busy && g->cur.id && g->cur.state == GNT_S_RUNNING && (g->cur.flags & GNT_F_JOINED) && !(g->cur.flags & GNT_F_OUT)) { to_screen(g, GNT_PAGE); g->sel = 0; g->first = 0; g->poll = GNT_POLL_FRAMES; }
    if (g->scr == GNT_LIST && g->net == GNT_N_OK && !g->busy) {                      /* the list carries no host or round: the entry under the cursor asks for its page data once (INFO), merged into the row */
        const struct gnt_ent *pe = z_ent(g);
        if (pe && !pe->host[0] && !(pe->flags & GNT_F_OFFICIAL) && g->peeked != pe->id) { if (++g->dwell >= 12u) { g->dwell = 0; g->peeked = pe->id; ask(g, GNT_REQ_INFO, pe->id); } }
        else g->dwell = 0;
    }
    if (g->scr == GNT_PAGE && !g->busy && g->cur.state == GNT_S_DONE && (g->cur.flags & GNT_F_JOINED) && g->cur.you != GNT_Y_WON && !g->my_asked) { g->my_asked = 1; ask(g, GNT_REQ_MYMATCH, g->cur.id); }     /* a finished one you played: your place, once */
    if (g->scr == GNT_PAGE && !g->busy && g->cur.state == GNT_S_RUNNING && (g->cur.flags & GNT_F_JOINED) && !(g->cur.flags & GNT_F_OUT) && ++g->poll >= GNT_POLL_FRAMES) { g->poll = 0; ask(g, GNT_REQ_MYMATCH, g->cur.id); }     /* waiting on the page: the call is looked for too */
    /* Back is a TAP of B everywhere (was a 1.0 s hold since): rules close, forms leave, bracket -> page, page -> list, list -> hub */
    if (held & GNT_B_B) { if (g->hold_b <= GNT_HOLD_B_FRAMES) g->hold_b++; } else g->hold_b = 0;
    if (g->scr == GNT_PAGE && page_action(g, g->sel) == PA_CALLOFF) {                      /* the host's Call Off Tournament row: HOLD A for the shared hold length (1.0 s); a release cancels, a tap only shows the hint */
        if (edge & GNT_B_A) g->hold_a = 1;
        else if ((held & GNT_B_A) && g->hold_a >= 1u && g->hold_a <= GNT_HOLD_B_FRAMES) {
            g->hold_a++;
            if (g->hold_a % 5u == 0u) g->dirty |= GNT_D_ROWS;                                      /* the "Holding n%" on the row */
            if (g->hold_a > GNT_HOLD_B_FRAMES) {
                if (g->busy) { g->hold_a = GNT_HOLD_B_FRAMES; return; }                          /* a poll in flight (a running cup asks for its call every 5 s): the hold is kept, the request goes out the frame it is free */
                g->hold_a = GNT_HOLD_B_FRAMES + 1u; ask(g, GNT_REQ_CANCEL, g->cur.id); g->sfx = GNT_SFX_FWD; say(g, "Calling off...", 0); return;
            }
        } else if (!(held & GNT_B_A)) { if (g->hold_a) g->dirty |= GNT_D_ROWS; g->hold_a = 0; }
    } else g->hold_a = 0;
    if ((edge & GNT_B_B) && g->scr == GNT_RULES) { g->hold_b = GNT_HOLD_B_FRAMES + 1u; g->sfx = GNT_SFX_BACK; to_screen(g, GNT_LIST); ask_list(g); return; }     /* (this press must not also run into the hold that leaves the list) */
    if ((edge & GNT_B_B) && (g->scr == GNT_CREATE || g->scr == GNT_JOIN)) {                         /* the forms: a single B press leaves, nothing is sent */
        g->hold_b = GNT_HOLD_B_FRAMES + 1u;
        if (g->busy) { g->sfx = GNT_SFX_DENY; return; }
        g->sfx = GNT_SFX_BACK; to_screen(g, GNT_LIST); ask_list(g); return;
    }
    if (edge & GNT_B_B) {                                                                           /* a TAP of B goes back one screen on the list (to the hub), page and bracket (nothing is lost by leaving) */
        g->hold_b = GNT_HOLD_B_FRAMES + 1u;
        g->sfx = GNT_SFX_BACK;
        if (g->scr == GNT_LIST) { g->exit = 1; return; }
        if (g->scr == GNT_BRACKET) { if (g->called) { unsigned q; for (q = 0; q < 8u && g->call_room[q]; q++) g->dis_room[q] = g->call_room[q]; g->dis_room[q] = 0; g->dismissed = 1; } g->called = 0; to_screen(g, GNT_PAGE); g->sel = 0; g->first = 0; ask(g, GNT_REQ_INFO, g->cur.id); return; }
        to_screen(g, GNT_LIST);
        ask_list(g);
        return;
    }
    if (g->scr == GNT_CREATE || g->scr == GNT_JOIN) {
        if (down && g->cf_row + 1u < gnt_rows(g)) { g->cf_row++; g->sfx = GNT_SFX_MOVE; g->dirty |= GNT_D_ALL; }
        else if (up && g->cf_row > 0u) { g->cf_row--; g->sfx = GNT_SFX_MOVE; g->dirty |= GNT_D_ALL; }
        cf_fix(g);
        if (g->scr == GNT_CREATE) {
            if (edge & GNT_B_RIGHT) create_value(g, 1);
            if (edge & GNT_B_LEFT) create_value(g, -1);
            if (edge & GNT_B_A) create_a(g);
            if (edge & GNT_B_X) create_x(g);
        } else {
            if (edge & GNT_B_RIGHT) join_value(g, 1);
            if (edge & GNT_B_LEFT) join_value(g, -1);
            if (edge & GNT_B_A) join_a(g);
            if (edge & GNT_B_X) join_x(g);
        }
        return;
    }
    if (g->scr == GNT_RULES) {
        if (edge & GNT_B_Z) z_press(g);
        if ((edge & GNT_B_START) && !g->busy) { if (g->cool) g->sfx = GNT_SFX_DENY; else { ask(g, GNT_REQ_INFO, g->cur.id); g->cool = GNT_REFRESH_GAP; g->sfx = GNT_SFX_FWD; } }
        return;
    }
    if (held & (GNT_B_UP | GNT_B_DOWN)) {
        unsigned dir = (held & GNT_B_DOWN) ? 1u : 2u;
        if (dir != g->rep_dir) { g->rep_dir = (unsigned char)dir; g->rep = 0; }
        else if (++g->rep >= GNT_REP_FIRST) { g->rep = (unsigned char)(GNT_REP_FIRST - GNT_REP_NEXT); if (dir == 1u) down = 1; else up = 1; }
    } else { g->rep_dir = 0; g->rep = 0; }
    if (down) move(g, 1);
    else if (up) move(g, -1);
    if (g->scr == GNT_BRACKET) {
        if (call_here(g) && (edge & GNT_B_A)) { if (g->busy) g->sfx = GNT_SFX_DENY; else { ask(g, GNT_REQ_PLAY, g->cur.id); g->sfx = GNT_SFX_FWD; } return; }
        if (g->b_have && ((edge & GNT_B_R) || (edge & GNT_B_RIGHT)) && g->b_round < br_nrounds(g, g->b_side)) { g->b_round++; g->b_sel = 0; g->b_first = 0; g->sfx = GNT_SFX_MOVE; g->dirty = GNT_D_ALL; }
        else if (g->b_have && ((edge & GNT_B_L) || (edge & GNT_B_LEFT)) && g->b_round > 1u) { g->b_round--; g->b_sel = 0; g->b_first = 0; g->sfx = GNT_SFX_MOVE; g->dirty = GNT_D_ALL; }
        if (g->b_have && (edge & GNT_B_Y)) next_side(g);
        /* Z: the tree (gnt_tree.h, drawn by this overlay) -> the rows of the round -> the animated bracket (the retail Tourney scene, tmv: the glue packs and hands over, or answers 'No animated view' and the rows
           come back).  The animated view was off from to a visit of the retail scene froze the next online scene (the codeset's r13 slots, ml_glue.c tn_unrestore). */
        if (g->b_have && (edge & GNT_B_Z)) {
            if (g->b_tree && g->gfx) { g->b_tree = 0; g->sfx = GNT_SFX_FWD; g->dirty = GNT_D_ALL; }      /* the bracket opens as the tree: tree -> rows -> animated (back from it: the tree again) */
            else if (g->busy) g->sfx = GNT_SFX_DENY;
            else { ask(g, GNT_REQ_ANIM, g->cur.id); g->sfx = GNT_SFX_FWD; }
        }
        if (g->b_have && (edge & GNT_B_X)) {                                       /* X: Mine: the tab and round of your next match, else the round being played */
            unsigned s, r, done = 0;
            for (s = 0; s < 3u && !done; s++)
                for (r = 1; r <= br_nrounds(g, s) && !done; r++) {
                    unsigned k, n = br_cnt(g, s, r);
                    for (k = 0; k < n; k++) {
                        const struct gnt_match *m = br_at(g, s, r, k);
                        if (m && g->b_me != GNT_NONE && (m->a == g->b_me || m->b == g->b_me) && m->state >= GNT_M_READY && m->state <= GNT_M_LIVE) { g->b_side = (unsigned char)s; g->b_round = (unsigned char)r; g->b_sel = (unsigned char)k; done = 1; break; }
                    }
                }
            if (!done) { g->b_side = GNT_SIDE_W; g->b_round = (unsigned char)(g->cur.round >= 1u && g->cur.round <= g->b_rounds ? g->cur.round : 1u); g->b_sel = (unsigned char)my_row(g); }
            fix(g); g->sfx = GNT_SFX_MOVE; g->dirty = GNT_D_ALL;
        }
        if ((edge & GNT_B_START) && !g->busy) { if (g->cool) g->sfx = GNT_SFX_DENY; else { request_bracket(g); g->cool = GNT_REFRESH_GAP; g->sfx = GNT_SFX_FWD; } }
        return;
    }
    if (g->scr == GNT_PAGE) {
        if (edge & GNT_B_A) page_press(g);
        if (edge & GNT_B_Z) z_press(g);
        if ((edge & GNT_B_START) && !g->busy) { if (g->cool) g->sfx = GNT_SFX_DENY; else { ask(g, GNT_REQ_INFO, g->cur.id); g->cool = GNT_REFRESH_GAP; g->sfx = GNT_SFX_FWD; } }
        return;
    }
    /* the list */
    if (edge & GNT_B_A) {
        if (g->net != GNT_N_OK && g->net != GNT_N_EMPTY) { if (!g->busy) { g->net = GNT_N_LOADING; ask_list(g); g->sfx = GNT_SFX_FWD; g->dirty = GNT_D_ALL; } }
        else if (list_rows(g) && g->sel < g->nv) {
            const struct gnt_ent *e = vent(g, g->sel);
            if ((e->flags & GNT_F_OFFICIAL) && e->state <= GNT_S_CHECKIN) {              /* an official cup before its start: A opens its rules (Z signs up) */
                g->cur = *e; g->say[0] = 0; g->say_warn = 0;
                to_screen(g, GNT_RULES);
                g->sfx = GNT_SFX_FWD;
                ask(g, GNT_REQ_INFO, e->id);
            } else open_page(g, e);
        }
    }
    if (edge & GNT_B_Z) z_press(g);
    if (g->net == GNT_N_OK && g->sel == 0u && g->dn > 1u && (edge & (GNT_B_LEFT | GNT_B_RIGHT))) daily_step(g, (edge & GNT_B_RIGHT) ? 1 : -1);      /* the dailies row under the cursor: Left Right pick which daily (L R are the filter) */
    if (g->net == GNT_N_OK || g->net == GNT_N_EMPTY) {                                   /* the list's own keys: X Create (when allowed), Y Sign up by ID, L R the region filter */
        if ((edge & GNT_B_X) && has_create(g)) { g->cf_row = 0; g->first = 0; to_screen(g, GNT_CREATE); g->sfx = GNT_SFX_FWD; return; }
        if (edge & GNT_B_Y) { g->cf_row = 0; g->first = 0; g->jid_pos = 0; g->jpw_pos = 0; to_screen(g, GNT_JOIN); g->sfx = GNT_SFX_FWD; return; }
        if ((edge & (GNT_B_R | GNT_B_L)) && lr_ok) {                                   /* a fresh press only: not while held, not on trigger chatter */
            {   /* Auto -> Any -> NAW -> NAC -> NAE -> EU.. OC -> Auto (as the Game Night plate); the dailies are region Any and show under every one */
                unsigned f = g->filter % GNT_REG_N, up = (edge & GNT_B_R) ? 1u : 0u;
                if (!g->f_user) { g->f_user = 1; g->filter = (unsigned char)(up ? 0u : REG_PV[0]); }
                else if (up ? f == REG_PV[0] : f == 0u) { g->f_user = 0; g->filter = g->acct_rgn; }
                else g->filter = (up ? REG_NX : REG_PV)[f];
            }
            list_view(g); fix(g); g->say[0] = 0; g->sfx = GNT_SFX_MOVE; g->dirty = GNT_D_ALL;
        }
    }
    if ((edge & GNT_B_START) && !g->busy) {
        if (g->cool) g->sfx = GNT_SFX_DENY;
        else { ask_list(g); g->cool = GNT_REFRESH_GAP; g->sfx = GNT_SFX_FWD; if (g->net != GNT_N_OK && g->net != GNT_N_EMPTY) g->net = GNT_N_LOADING; g->dirty = GNT_D_ALL; }
    }
}

/* ---- answers ---- */
void gnt_net_list(struct gnt *g, unsigned flags, unsigned n, const struct gnt_ent *e)
{
    unsigned i;
    g->busy = 0; g->loads++;
    if (n > GNT_MAX) n = GNT_MAX;
    g->mock = (unsigned char)(flags & 1u);
    g->can_create = (unsigned char)((flags >> 1) & 1u);
    for (i = 0; i < n; i++) { g->e[i] = e[i]; safe_cpy(g->e[i].name, e[i].name, GNT_NAME); safe_cpy(g->e[i].host, e[i].host, GNT_NAME); }
    g->n = (unsigned char)n;
    g->net = (unsigned char)(n ? GNT_N_OK : GNT_N_EMPTY);                                /* (Create and Sign up by ID are footer keys: an empty list still has them) */
    list_view(g);
    g->peeked = 0; g->dwell = 0;
    if (g->scr == GNT_LIST) fix(g);
    g->dirty |= GNT_D_ALL;
    if (g->auto_mine && g->scr == GNT_LIST) {                                        /* back from a match room: the bracket of the running cup the player is in */
        unsigned pick = GNT_MAX;
        g->auto_mine--;
        for (i = 0; i < g->n; i++) if ((g->e[i].flags & GNT_F_JOINED) && g->e[i].state == GNT_S_RUNNING) { pick = i; break; }
        if (pick == GNT_MAX && !g->auto_mine)                                        /* no running cup in the last list: the set just played was the cup's last, its final bracket (the newest finished cup of the player) */
            for (i = 0; i < g->n; i++) if ((g->e[i].flags & GNT_F_JOINED) && g->e[i].state == GNT_S_DONE) { pick = i; break; }
        if (pick != GNT_MAX) {
            g->auto_mine = 0; g->cur = g->e[pick];
            g->b_have = 0; g->b_side = GNT_SIDE_W; g->b_round = 1; g->b_tree = 1;
            to_screen(g, GNT_BRACKET);
            ask(g, GNT_REQ_BRACKET, g->cur.id);
        }
    }
}
void gnt_net_info(struct gnt *g, const struct gnt_ent *e)
{
    unsigned i;
    g->busy = 0;
    if (g->scr == GNT_LIST || e->id != g->cur.id) {                                  /* a peek for the list's description box (or an answer for a page that is gone): into the row only */
        for (i = 0; i < g->n; i++) if (g->e[i].id == e->id) { g->e[i] = *e; safe_cpy(g->e[i].name, e->name, GNT_NAME); safe_cpy(g->e[i].host, e->host, GNT_NAME); }
        g->dirty |= GNT_D_ALL;
        return;
    }
    g->cur = *e;
    safe_cpy(g->cur.name, e->name, GNT_NAME); safe_cpy(g->cur.host, e->host, GNT_NAME);
    for (i = 0; i < g->n; i++) if (g->e[i].id == e->id) g->e[i] = g->cur;
    if (g->scr == GNT_PAGE) fix(g);
    g->dirty |= GNT_D_ALL;
}
void gnt_net_done(struct gnt *g, unsigned what, unsigned ok)
{
    g->busy = 0;
    if (what == GNT_REQ_JOIN) {
        if (ok) { g->cur.flags |= GNT_F_JOINED; if (g->cur.joined < 255u) g->cur.joined++; say(g, "You are in.", 0); g->sfx = GNT_SFX_FWD; }
        else { say(g, "Could not join.", 1); g->sfx = GNT_SFX_DENY; }
        ask(g, GNT_REQ_INFO, g->cur.id);
    } else if (what == GNT_REQ_CANCEL) {
        if (ok) { to_screen(g, GNT_LIST); ask_list(g); say(g, "Tournament called off.", 0); g->sfx = GNT_SFX_FWD; }      /* back to the list; the cup stays visible to its host for a day as Called off */
        else { say(g, "Could not call off.", 1); g->sfx = GNT_SFX_DENY; }
    } else if (what == GNT_REQ_LEAVE) {
        if (ok) { g->cur.flags &= (unsigned char)~GNT_F_JOINED; if (g->cur.joined) g->cur.joined--; say(g, "You left.", 0); }
        else { say(g, "Could not leave.", 1); g->sfx = GNT_SFX_DENY; }
        ask(g, GNT_REQ_INFO, g->cur.id);
    }
    fix(g);
    g->dirty |= GNT_D_ALL;
}
void gnt_net_created(struct gnt *g, const struct gnt_ent *e)
{
    unsigned joined_by_id = g->scr == GNT_JOIN;
    g->busy = 0;
    g->cur = *e;
    safe_cpy(g->cur.name, e->name, GNT_NAME); safe_cpy(g->cur.host, e->host, GNT_NAME);
    g->sel = 0; g->first = 0;
    to_screen(g, GNT_PAGE);
    say(g, joined_by_id ? "You are in." : "Tournament created.", 0);
    g->sfx = GNT_SFX_FWD;
}
void gnt_br_begin(struct gnt *g, unsigned n, unsigned size, unsigned format)
{
    unsigned i;
    if (n > GNT_ENTRANTS) n = GNT_ENTRANTS;
    if (size < 2u || size > GNT_ENTRANTS) size = gnt_size_for(n);
    for (i = 0; i < GNT_MATCHES; i++) { zero(&g->b_m[i], (unsigned)sizeof g->b_m[i]); g->b_m[i].a = GNT_NONE; g->b_m[i].b = GNT_NONE; }
    g->b_n = (unsigned char)n; g->b_size = (unsigned char)size; g->b_rounds = (unsigned char)gnt_round_count(size); g->b_format = (unsigned char)(format ? GNT_FMT_SINGLE : GNT_FMT_DOUBLE);
    g->b_rx = 0;
}
void gnt_br_match(struct gnt *g, unsigned mid, const struct gnt_match *m)
{
    if (mid < 1u || mid > GNT_MATCHES) return;
    g->b_m[mid - 1u] = *m;
    g->b_m[mid - 1u].mid = (unsigned char)mid;
}
void gnt_br_name(struct gnt *g, unsigned i, const char *name, unsigned state)
{
    if (i < GNT_ENTRANTS) { safe_cpy(g->b_name[i], name, GNT_NAME); g->b_nstate[i] = (unsigned char)state; }
}
void gnt_br_end(struct gnt *g, unsigned me)
{
    g->busy = 0; g->loads++;
    g->b_me = (unsigned char)(me < g->b_n ? me : GNT_NONE);
    if (!g->b_have) { g->b_have = 1; pick_round(g); if (g->called) { g->b_side = g->call_side; g->b_round = g->call_round; } g->b_sel = (unsigned char)my_row(g); }
    if (g->b_round > br_nrounds(g, g->b_side)) { g->b_round = 1; }
    if (g->scr == GNT_BRACKET) fix(g);
    g->dirty |= GNT_D_ALL;
}
void gnt_net_bracket(struct gnt *g, unsigned n, unsigned size, unsigned me, const char (*names)[GNT_NAME + 1], unsigned nm, const struct gnt_match *m)
{
    unsigned i;
    gnt_br_begin(g, n, size, GNT_FMT_SINGLE);
    for (i = 0; i < g->b_n; i++) gnt_br_name(g, i, names[i], 0);
    for (i = 0; i < nm; i++) { struct gnt_match x = m[i]; if (!x.side && !x.bo) x.bo = 3; gnt_br_match(g, i + 1u, &x); }
    gnt_br_end(g, me);
}
void gnt_resume(struct gnt *g, const struct gnt_ent *cur)
{
    gnt_enter(g);
    g->cur = *cur;
    g->b_have = 0; g->b_side = GNT_SIDE_W; g->b_round = 1; g->b_tree = 1;
    to_screen(g, GNT_BRACKET);
    ask(g, GNT_REQ_BRACKET, cur->id);
}
void gnt_enter_mine(struct gnt *g) { gnt_enter(g); g->auto_mine = 1; }   /* (the list answer is the whole list: public pages and "mine", sg_entry tn_done) */
void gnt_net_call(struct gnt *g, unsigned side, unsigned round, const char *opp, const char *room, unsigned secs)
{
    unsigned i, fresh = !g->called, same = 1;
    g->busy = 0;                                                                     /* the TN_MYMATCH answer is in (a dismissed call asks for nothing more: busy must not stay set, or every press on the page is denied) */
    for (i = 0; i < 8u && (room[i] || g->dis_room[i]); i++) if (room[i] != g->dis_room[i]) same = 0;
    if (!same) g->dismissed = 0;
    g->call_clk = g->clk;
    g->called = 1; g->call_side = (unsigned char)side; g->call_round = (unsigned char)round; g->call_secs = (unsigned short)secs;
    safe_cpy(g->call_opp, opp, GNT_NAME);
    for (i = 0; i < 8u && room[i]; i++) g->call_room[i] = room[i];
    g->call_room[i] = 0;
    g->sfx = GNT_SFX_FWD; g->dirty |= GNT_D_ALL;
    if ((g->scr == GNT_PAGE || g->scr == GNT_LIST) && !(g->dismissed && same)) { g->b_have = 0; g->b_side = GNT_SIDE_W; g->b_round = 1; g->b_sel = 0; g->b_first = 0; to_screen(g, GNT_BRACKET); ask(g, GNT_REQ_BRACKET, g->cur.id); g->poll = 0; }
    if (fresh && g->scr == GNT_BRACKET && g->b_have) { g->b_side = (unsigned char)side; g->b_round = (unsigned char)round; g->b_sel = (unsigned char)my_row(g); fix(g); }     /* a new call: its own tab and round */
}
/* the plain reason of a refusal (texts in docs/data/ui_strings.json tn.*): never a silent no-op */
static const char *why_text(unsigned what, unsigned why)
{
    switch (why) {
    case GNT_W_NOSUP: return "Update Multishine.";
    case GNT_W_RATE: return what == GNT_REQ_JOINID ? "Too many tries, wait 10 minutes." : "Slow down a moment.";
    case GNT_W_FULL: return "The tournament is full.";
    case GNT_W_CLOSED: return what == GNT_REQ_CHECKIN ? "Check-in is not open now." : "Entry closed: it started.";
    case GNT_W_NOTLINKED: return "Link this console first.";
    case GNT_W_NOANIM: return "No animated view for this cup.";
    case GNT_W_LIMIT: return "You are in another one.";      /* */
    case GNT_W_DENIED: return what == GNT_REQ_JOINID ? "Wrong passcode." : what == GNT_REQ_JOIN ? "You cannot join this one." : "Only the host can do that.";
    case GNT_W_BAD: return what == GNT_REQ_JOINID ? "No tournament with that ID." : what == GNT_REQ_CREATE ? "That name is not allowed." : what == GNT_REQ_START ? "Not enough entrants yet." : "The server refused that.";
    default: return "Server not available.";
    }
}
void gnt_net_fail(struct gnt *g, unsigned what, unsigned why)
{
    g->busy = 0;
    g->fail = (unsigned char)(why + 1u);
    if (what == GNT_REQ_LIST) {
        g->net = why == GNT_W_NOSUP ? GNT_N_NOSUP : why == GNT_W_NOTLINKED ? GNT_N_SIGNIN : GNT_N_UNREACH;
    } else {
        say(g, why_text(what, why), 1);
        g->sfx = GNT_SFX_DENY;
    }
    g->dirty |= GNT_D_ALL;
}
void gnt_net_next(struct gnt *g, unsigned long secs) { g->next_cup = secs; g->dirty |= GNT_D_PANEL; }
void gnt_fmt_secs(char *out, unsigned long secs)
{
    out[0] = 0;
    if (secs >= 3600ul) { catu(out, secs / 3600ul, 12); cat(out, "h ", 20); catu(out, (secs % 3600ul) / 60ul, 12); cat(out, "m", 24); }
    else if (secs >= 60ul) { catu(out, secs / 60ul, 12); cat(out, "m", 24); }
    else cpy(out, "under 1m", 24);
}
const char *gnt_create_name(const struct gnt *g) { return CF_NAMES[g->cf_name & 3u]; }
unsigned gnt_create_cap(const struct gnt *g) { return CF_SIZES[g->cf_size & 3u]; }
unsigned gnt_create_checkin(const struct gnt *g) { return CF_CHECK[g->cf_checkin % 3u]; }
unsigned gnt_create_format(const struct gnt *g) { return g->cf_format ? 1u : 0u; }
unsigned gnt_create_flags(const struct gnt *g) { return g->cf_seed ? 2u : 0u; }
static const unsigned char CF_SIGNUP[4] = { 15, 30, 60, 120 };
static const unsigned long CF_START_IN[5] = { 0ul, 3600ul, 10800ul, 21600ul, 86400ul };   /* Now, 1 h, 3 h, Tonight, Tomorrow: the console has no trusted clock, so these are relative (Tonight = 6 h, Tomorrow = 24 h); Pick time = days and hours */
unsigned gnt_create_signup(const struct gnt *g) { return CF_SIGNUP[g->cf_signup & 3u]; }
unsigned gnt_create_seeding(const struct gnt *g) { return g->cf_seed > 2u ? 0u : g->cf_seed; }
unsigned gnt_create_hostplays(const struct gnt *g) { return g->cf_hostplays ? 1u : 0u; }
unsigned long gnt_create_start_in(const struct gnt *g)
{
    unsigned long v;
    if (g->cf_start != START_PICK) return g->cf_start < 5u ? CF_START_IN[g->cf_start] : 0ul;
    v = (unsigned long)g->cf_days * 86400ul + (unsigned long)g->cf_hours * 3600ul;
    return v < 3600ul ? 3600ul : v > 2592000ul ? 2592000ul : v;
}
const char *gnt_create_pass(const struct gnt *g) { return g->cf_pass_on ? g->cf_pass : ""; }
unsigned long gnt_join_id(const struct gnt *g)
{
    unsigned long v = 0;
    unsigned q;
    for (q = 0; q < 8u; q++) { if (g->jid[q] < '0' || g->jid[q] > '9') return 0; v = v * 10ul + (unsigned long)(g->jid[q] - '0'); }
    return (v >= 10000000ul && v <= 99999999ul) ? v : 0;
}
const char *gnt_join_pass(const struct gnt *g) { return g->jpw_on ? g->jpw : ""; }
void gnt_net_my(struct gnt *g, unsigned kind, unsigned place)
{
    g->busy = 0;
    g->my_kind = (unsigned char)kind; g->my_place = (unsigned char)place;
    if (g->called) g->called = 0;
    g->dirty |= GNT_D_ALL;
}
void gnt_played(struct gnt *g) { g->called = 0; g->dirty |= GNT_D_ALL; }
void gnt_back_from_room(struct gnt *g)
{
    g->scr = GNT_BRACKET; g->exit = 0; g->called = 0; g->busy = 0; g->poll = GNT_POLL_FRAMES; g->dirty = GNT_D_ALL;
}

/* ---- what the screen shows ---- */
static const char *state_word(const struct gnt_ent *e)
{
    return e->state == GNT_S_REG ? (e->scheduled ? "Upcoming" : "Open") : e->state == GNT_S_CHECKIN ? "Check in" : e->state == GNT_S_RUNNING ? "Playing" : e->state == GNT_S_DONE ? "Done" : "Called off";
}

static const char *const CF_START_W[6] = { "Now", "In 1 hour", "In 3 hours", "Tonight", "Tomorrow", "Pick time" };
/* a digit entry: "None" when off, else the 8 digits, the one under the cursor inside < > while its row is selected ("1234<5>678") */
static void digits_text(char *extra, const char *d, unsigned pos, unsigned cur, unsigned on)
{
    unsigned q, k = 0;
    if (!on) { cpy(extra, "None", GNT_EXTRA); return; }
    for (q = 0; q < 8u; q++) {
        if (cur && q == (pos & 7u)) extra[k++] = '<';
        extra[k++] = d[q] ? d[q] : '0';
        if (cur && q == (pos & 7u)) extra[k++] = '>';
    }
    extra[k] = 0;
}
unsigned gnt_row(const struct gnt *g, unsigned slot, char *num, char *title, char *extra)
{
    unsigned i = g->scr == GNT_BRACKET ? g->b_first + slot : g->first + slot;
    num[0] = 0; title[0] = 0; extra[0] = 0;
    if (g->scr == GNT_LIST && slot == 0u && (g->net == GNT_N_EMPTY || (g->net == GNT_N_OK && g->nv == 0u))) {          /* nothing to list: the message is the first row (the box below is the key line now) */
        cpy(title, g->net == GNT_N_EMPTY || g->filter == 0u ? "No tournaments open." : "None for this region.", GNT_TITLE);
        return 2u;
    }
    if (i >= gnt_rows(g) || slot >= gnt_vis(g)) return 0;
    if (gnt_tree_on(g)) return 0;                                                   /* the tree draws the matches itself */
    if (g->scr == GNT_LIST) {
        const struct gnt_ent *e;
        e = vent(g, i);
        catu(num, e->joined, GNT_NUM); cat(num, " of max ", GNT_NUM); catu(num, e->cap, GNT_NUM);
        cpy(title, gnt_ent_name(e), GNT_TITLE);
        cpy(extra, state_word(e), 12);
        if (e->state <= GNT_S_CHECKIN && (is_official(e) || e->state == GNT_S_CHECKIN)) {      /* a daily before its start, any cup in check-in: the time to start, never the word "Check in" (the page offers it) */
            char t[24];
            gnt_fmt_secs(t, e->secs);
            cpy(extra, t, 12);
        }
        if (g->dn > 1u && i == 0u) {
            unsigned n0 = 0, dg = ((unsigned)g->dsel + 1u >= 10u ? 2u : 1u) + (g->dn >= 10u ? 2u : 1u) + 2u;
            while (extra[n0]) n0++;
            /* cube 391: on a daily you are in, "Playing 11/14" and "1h 54m 1/16" ran under the gold star (its column leaves room for about 8 characters, "33m 1/5").  A starred row whose
               text would be longer keeps the state and drops the counter; the arrows at the row's ends still say Left / Right */
            if (!(((e->flags & GNT_F_JOINED) && n0 + dg > 8u) || n0 + dg > 11u))      /* */ { cat(extra, " ", GNT_EXTRA); catu(extra, (unsigned)g->dsel + 1u, GNT_EXTRA); cat(extra, "/", GNT_EXTRA); catu(extra, g->dn, GNT_EXTRA); }
        }      /* the daily's place among the dailies (Left Right) */
        return 1u + ((e->flags & (GNT_F_JOINED | GNT_F_HOST)) ? 2u : e->state == GNT_S_DONE ? 1u : 0u);       /* gold = you are signed up / checked in / the host (no status circle) */
    }
    if (g->scr == GNT_PAGE) {
        static const char *const W[10] = { "Join Tournament", "Leave Tournament", "Check In", "Start Tournament", "View Bracket", "Forfeit", "Call Off Tournament", "Refresh", "ID", "Back" };
        unsigned a = page_action(g, i);
        cpy(title, W[a], GNT_TITLE);
        if (g->cur.flags & GNT_F_OFFICIAL) {
            if (a == PA_JOIN) { cpy(title, g->cur.state == GNT_S_CHECKIN ? "Join now" : "Sign up", GNT_TITLE); if (g->cur.state == GNT_S_CHECKIN) { catu(extra, g->cur.cap > g->cur.joined ? (unsigned)(g->cur.cap - g->cur.joined) : 0u, 8); cat(extra, " left", 12); } }
            else if (a == PA_LEAVE) cpy(title, "Cancel sign-up", GNT_TITLE);
        }
        if (a == PA_ID) { char t[12]; t[ms_fmt_u(t, g->cur.pubid, 11u)] = 0; cpy(extra, t, GNT_EXTRA); return 2u; }                      /* the 8 digit id, an info row */
        if (a == PA_CHECKIN && g->cur.secs) { char t[24]; gnt_fmt_secs(t, g->cur.secs); cpy(extra, t, 12); cat(extra, " left", 12); }       /* the window closes... */
        if (a == PA_START) { unsigned need = g->cur.format == GNT_FMT_SINGLE ? 2u : 3u; if (g->cur.joined < need) { catu(extra, need, 8); cat(extra, " needed", 12); return 2u; } }
        if (a == PA_JOIN && g->cur.cap && g->cur.joined >= g->cur.cap) { cpy(extra, "Full", 12); return 2u; }
        if (a == PA_CALLOFF) { if (g->hold_a > 0u && g->hold_a <= GNT_HOLD_B_FRAMES) { cpy(extra, "Holding ", 12); catu(extra, (unsigned)g->hold_a * 100u / GNT_HOLD_B_FRAMES, 12); cat(extra, "%", 12); } else cpy(extra, "Hold A", 12); return 4u; }
        return a == PA_FORFEIT ? 4u : a == PA_CHECKIN ? 3u : 1u;
    }
    if (g->scr == GNT_CREATE || g->scr == GNT_JOIN) {
        static const char *const L[CR_MAX] = { "Preset", "Name", "Format", "Size", "Sign-up window", "Check-in", "Seeding", "Host plays", "Start", "Start day", "Start hour", "Region", "Passcode", "Create Tournament" };
        static const char *const JL[JR_MAX] = { "ID", "Passcode", "Sign up" };
        unsigned r = cr_id(g, i), cur = i == g->cf_row;
        if (g->scr == GNT_JOIN) {
            cpy(title, JL[r], GNT_TITLE);
            if (r == JR_ID) digits_text(extra, g->jid, g->jid_pos, cur, 1u);
            else if (r == JR_PASS) digits_text(extra, g->jpw, g->jpw_pos, cur, g->jpw_on);
            return r == JR_GO ? 3u : 1u;
        }
        cpy(title, L[r], GNT_TITLE);
        switch (r) {
        case CR_PRESET: cpy(extra, PRESET_NAME[g->cf_preset % GNT_PRESETS], GNT_EXTRA); break;
        case CR_NAME: cpy(extra, CF_NAMES[g->cf_name & 3u], GNT_EXTRA); break;
        case CR_FORMAT: cpy(extra, g->cf_format ? "Single Elimination" : "Double Elimination", GNT_EXTRA); break;
        case CR_SIZE: cpy(extra, "Max ", GNT_EXTRA); catu(extra, CF_SIZES[g->cf_size & 3u], GNT_EXTRA); cat(extra, " players", GNT_EXTRA); break;
        case CR_SIGNUP: catu(extra, CF_SIGNUP[g->cf_signup & 3u], GNT_EXTRA); cat(extra, " min", GNT_EXTRA); break;
        case CR_CHECK: if (CF_CHECK[g->cf_checkin % 3u]) { catu(extra, CF_CHECK[g->cf_checkin % 3u], GNT_EXTRA); cat(extra, "m", GNT_EXTRA); } else cpy(extra, "Off", GNT_EXTRA); break;
        case CR_SEED: cpy(extra, g->cf_seed == 2u ? "Join order" : g->cf_seed ? "Rating" : "Random", GNT_EXTRA); break;
        case CR_REGION: cpy(extra, REGION_NAME[g->cf_region % GNT_REG_N], GNT_EXTRA); break;
        case CR_HOST: cpy(extra, g->cf_hostplays ? "Yes" : "No", GNT_EXTRA); break;
        case CR_START: cpy(extra, CF_START_W[g->cf_start < 6u ? g->cf_start : 0u], GNT_EXTRA); break;
        case CR_DAY: if (g->cf_days == 0u) cpy(extra, "Today", GNT_EXTRA); else { cpy(extra, "In ", GNT_EXTRA); catu(extra, g->cf_days, GNT_EXTRA); cat(extra, g->cf_days == 1u ? " day" : " days", GNT_EXTRA); } break;
        case CR_HOUR: cpy(extra, "+", GNT_EXTRA); catu(extra, g->cf_hours, GNT_EXTRA); cat(extra, g->cf_hours == 1u ? " hour" : " hours", GNT_EXTRA); break;
        case CR_PASS: digits_text(extra, g->cf_pass, g->cf_pass_pos, cur, g->cf_pass_on); break;
        default: break;
        }
        return r == CR_GO ? 3u : 1u;
    }
    {   /* bracket: one match of the round shown */
        const struct gnt_match *m = br_at(g, g->b_side, g->b_round, i);
        const char *na = m->a == GNT_NONE ? "" : g->b_name[m->a], *nb = m->b == GNT_NONE ? "" : g->b_name[m->b];
        char a10[11], b10[11];
        unsigned mine = g->b_me != GNT_NONE && (m->a == g->b_me || m->b == g->b_me);
        cpy(a10, na, 10); cpy(b10, nb, 10);
        cpy(num, m->state == GNT_M_BYE ? "Bye" : m->state == GNT_M_WALK ? "Done" : m->state == GNT_M_DONE ? "Done" : m->state == GNT_M_LIVE ? "Live" : m->state == GNT_M_CALLED ? "Called" : m->state == GNT_M_READY ? "Ready" : "Wait", GNT_NUM);
        if (m->a == GNT_NONE && m->b == GNT_NONE) cpy(title, "To be decided", GNT_TITLE);
        else if (m->b == GNT_NONE && m->state == GNT_M_BYE) { cpy(title, a10, GNT_TITLE); cat(title, " (bye)", GNT_TITLE); }
        else if (m->a == GNT_NONE && m->state == GNT_M_BYE) { cpy(title, b10, GNT_TITLE); cat(title, " (bye)", GNT_TITLE); }
        else {
            cpy(title, m->a == GNT_NONE ? "?" : a10, GNT_TITLE); cat(title, " v ", GNT_TITLE); cat(title, m->b == GNT_NONE ? "?" : b10, GNT_TITLE);
        }
        if (m->state == GNT_M_DONE && m->a != GNT_NONE && m->b != GNT_NONE) { catu(extra, m->wa, 8); cat(extra, "-", 8); catu(extra, m->wb, 8); }
        else if (m->state == GNT_M_LIVE && (m->wa || m->wb)) { catu(extra, m->wa, 8); cat(extra, "-", 8); catu(extra, m->wb, 8); }
        else if (m->state == GNT_M_WALK) cpy(extra, "Walkover", 12);                          /* in the right column like a score: the left one stays short, clear of the names */
        return 1u + (mine ? 2u : (m->state == GNT_M_DONE || m->state == GNT_M_BYE || m->state == GNT_M_WALK) ? 1u : 0u);
    }
}
unsigned gnt_is_mine(const struct gnt *g, unsigned slot)
{
    unsigned i = g->b_first + slot;
    const struct gnt_match *m;
    if (g->scr != GNT_BRACKET || i >= br_rows(g) || g->b_me == GNT_NONE) return 0;
    m = br_at(g, g->b_side, g->b_round, i);
    return m && (m->a == g->b_me || m->b == g->b_me);
    return m->a == g->b_me || m->b == g->b_me;
}

const char *gnt_ent_name(const struct gnt_ent *e) { return (e->flags & GNT_F_OFFICIAL) ? GNT_OFFICIAL_NAME : e->name; }
const char *gnt_cur_name(const struct gnt *g) { return gnt_ent_name(&g->cur); }

/* the rules of an official cup, one line each, at most 38 characters */
unsigned gnt_rules(const struct gnt *g, unsigned line, char *out)
{
    static const char *const R[GNT_RULE_N] = {
        "Double elimination, up to 64 players.", "Best of 3. The finals are best of 5.", "16+ players: top 8 is best of 5 too.",
        "Sign up for one tournament at a time.",
        "Check-in opens 15 min before start.", "Check in on the console only.", "Not checked in 5 min before: removed.",
        "Late sign-up in the last 5 minutes.", "Entry locks at the start.", "Called off with under 4 checked in." };
    (void)g;
    out[0] = 0;
    if (line >= GNT_RULE_N) return 0;
    cpy(out, R[line], GNT_RULE);
    return 1;
}

unsigned gnt_status_view(const struct gnt *g)
{
    if (g->scr != GNT_LIST) return 0;
    if (g->net == GNT_N_LOADING) return 1;
    if (g->net == GNT_N_UNREACH || g->net == GNT_N_NOSUP || g->net == GNT_N_SIGNIN) return 2;                 /* two lines: what happened, what to do */
    return 0;
}

const char *gnt_title(const struct gnt *g)
{
    return g->scr == GNT_LIST ? "Tournament" : g->scr == GNT_RULES ? "Tournament rules" : g->scr == GNT_CREATE ? "Create Tournament" : g->scr == GNT_JOIN ? "Sign up by ID" : g->cur.name[0] ? gnt_cur_name(g) : "Tournament";
}
const char *gnt_hint2(const struct gnt *g)            /* LIST: the action keys, shown as badges in the line above the footer bar (Create and Sign up by ID are keys, not rows) */
{
    static char h[72];
    const struct gnt_ent *e = z_ent(g);
    const char *z = gnt_z_label(g);
    h[0] = 0;
    if (g->scr != GNT_LIST || (g->net != GNT_N_OK && g->net != GNT_N_EMPTY)) return h;
    if (e) { cat(h, is_official(e) && e->state <= GNT_S_CHECKIN ? "A: Rules" : "A: Open", 72); if (z[0]) { cat(h, "  Z: ", 72); cat(h, z, 72); } }
    if (has_create(g)) { if (h[0]) cat(h, "  ", 72); cat(h, "X: Create", 72); }
    return h;
}
const char *gnt_hint(const struct gnt *g)
{
    static char h[72];
    const char *z = gnt_z_label(g);
    h[0] = 0;
    if (g->scr == GNT_LIST) {                                                           /* the footer bar: the list keys (the action keys A Z X Y are gnt_hint2, the line above) */
        if (g->net == GNT_N_LOADING) return "B: Back";
        if (g->net != GNT_N_OK && g->net != GNT_N_EMPTY) return "A: Try again  B: Back";
        cat(h, "Y: Sign up by ID  L R: Region ", 72); cat(h, gnt_filter_name(g), 72);          /* row 2 of the footer box (3 columns); Start still refreshes, the list refreshes itself every 30 s, so it is not shown */
        cat(h, "  B: Back", 72);
        return h;
    }
    if (g->scr == GNT_RULES) {
        if (z[0]) { cat(h, "Z: ", 72); cat(h, z, 72); cat(h, "  ", 72); }
        cat(h, "Start: Refresh  B: Close", 72);
        return h;
    }
    if (g->scr == GNT_PAGE) {
        cat(h, "A: Select", 72);
        if (z[0]) { cat(h, "  Z: ", 72); cat(h, z, 72); }
        cat(h, "  Start: Refresh  B: Back", 72);
        return h;
    }
    if (g->scr == GNT_CREATE) { unsigned r = cr_id(g, g->cf_row); return r == CR_GO ? "A: Create  B: Back" : r == CR_PASS ? "A Digit +  X Digit -  Left Right: Move" : r == CR_PRESET ? "Left Right: Preset  B: Back" : "Left Right: Change  B: Back"; }
    if (g->scr == GNT_JOIN) return g->cf_row == JR_GO ? "A: Join  B: Back" : "A Digit +  X Digit -  Left Right: Move";
    if (!g->b_have) return g->busy ? "B: Back" : "Start: Try again  B: Back";
    if (!g->gfx) return call_here(g) ? "A: Play  L R: Round  Y: Bracket  B: Back" : "L R: Round  Y: Bracket  X: Mine  B: Back";      /* the character view has no tree */
    if (call_here(g)) return g->b_tree ? "A: Play  L R: Round  Y: Bracket  Z: List  B: Back" : "A: Play  L R: Round  Y: Bracket  Z: Animated  B: Back";
    return g->b_tree ? "L R: Round  Y: Bracket  X: Mine  Z: List  B: Back" : "L R: Round  Y: Bracket  X: Mine  Z: Animated  B: Back";
}

unsigned gnt_calls(const struct gnt *g) { return g->scr == GNT_BRACKET && call_here(g); }
unsigned gnt_call_secs(const struct gnt *g)
{
    unsigned long gone = (g->clk - g->call_clk) / 30ul;
    if (!g->called && !g->call_opp[0]) return 0u;
    return gone >= g->call_secs ? 0u : (unsigned)(g->call_secs - gone);
}
unsigned gnt_call_info(const struct gnt *g, char *round_title, unsigned *bo)
{
    unsigned i;
    round_title[0] = 0; *bo = 1;
    if (!g->called && !g->call_opp[0]) return 0;
    br_title(g, g->call_side, g->call_round, round_title);
    for (i = 0; i < GNT_MATCHES; i++) { const struct gnt_match *m = &g->b_m[i]; if (m->mid && m->side == g->call_side && m->round == g->call_round && g->b_me != GNT_NONE && (m->a == g->b_me || m->b == g->b_me) && m->bo) { *bo = m->bo; break; } }
    return 1;
}
void gnt_state_text(const struct gnt *g, char *out)
{
    const struct gnt_ent *e = &g->cur;
    out[0] = 0;
    if (e->state == GNT_S_REG) cpy(out, e->scheduled ? "Upcoming" : "Sign-up open", 24);
    else if (e->state == GNT_S_CHECKIN) cpy(out, "Check-in open", 24);
    else if (e->state == GNT_S_RUNNING) { if (e->round) { cpy(out, "Round ", 24); catu(out, e->round, 24); cat(out, " of ", 24); catu(out, e->rounds, 24); } else cpy(out, "Playing", 24); }
    else if (e->state == GNT_S_DONE) cpy(out, "Finished", 24);
    else cpy(out, "Called off", 24);
}

static void cup_state(const struct gnt_ent *e, char *out)                  /* the cup's line for you (3 lines per box:, the fourth overlapped the key line) */
{
    out[0] = 0;
    if (e->flags & GNT_F_JOINED) cpy(out, (e->flags & GNT_F_CHECKED) ? "You are checked in." : e->state == GNT_S_CHECKIN ? "Check in now!" : "You are signed up.", GNT_LINE);
    else if (e->state == GNT_S_CHECKIN) cpy(out, (e->flags & GNT_F_LATE) ? "Late sign-up: Z to join." : "Sign-up is closed.", GNT_LINE);
    else cpy(out, "Not signed up.", GNT_LINE);
}

/* the description lines of a tournament that is not an open official cup: who runs it, how far it is, where you stand (the list and the page say the same words) */
static void host_line(const struct gnt_ent *e, char *out)
{
    const char *f = e->format == GNT_FMT_SINGLE ? "single" : "double";
    if (e->host[0]) { cpy(out, "Host: ", GNT_LINE); cat(out, e->host, GNT_LINE); cat(out, ", ", GNT_LINE); cat(out, f, GNT_LINE); }
    else { cpy(out, (e->flags & GNT_F_OFFICIAL) ? "Official, " : e->format == GNT_FMT_SINGLE ? "Single " : "Double ", GNT_LINE); cat(out, (e->flags & GNT_F_OFFICIAL) ? f : "elimination", GNT_LINE); }
    if (e->region >= 1u && e->region <= GNT_REG_N - 1u) { cat(out, slen(out) + 10u + slen(REGION_NAME[e->region]) <= GNT_LINE ? ", Region: " : ", ", GNT_LINE); cat(out, REGION_NAME[e->region], GNT_LINE); }      /* "Region: EU" (short ", EU" when the line is full) */
}
static void ord(char *out, unsigned n)                                           /* 2 -> "2nd" */
{
    unsigned t = n % 100u, d = n % 10u;
    catu(out, n, GNT_LINE);
    cat(out, (t >= 11u && t <= 13u) || d == 0u || d > 3u ? "th" : d == 1u ? "st" : d == 2u ? "nd" : "rd", GNT_LINE);
}
static void state_line(const struct gnt_ent *e, char *out)
{
    out[0] = 0;
    if (e->state == GNT_S_CHECKIN && e->secs) { char t[24]; gnt_fmt_secs(t, e->secs); cpy(out, "Check-in open, ", GNT_LINE); cat(out, t, GNT_LINE); cat(out, " left", GNT_LINE); }
    else if (e->state == GNT_S_REG && e->scheduled) { if (e->signup_left) { char t[24]; gnt_fmt_secs(t, e->signup_left); cpy(out, "Starts in ", GNT_LINE); cat(out, t, GNT_LINE); } else cpy(out, "Starting soon", GNT_LINE); }
    else if (e->state == GNT_S_REG && e->signup_left) { char t[24]; gnt_fmt_secs(t, e->signup_left); catu(out, e->joined, GNT_LINE); cat(out, " of max ", GNT_LINE); catu(out, e->cap, GNT_LINE); cat(out, ", ", GNT_LINE); cat(out, t, GNT_LINE); cat(out, " left", GNT_LINE); }
    else if (e->state <= GNT_S_CHECKIN) { catu(out, e->joined, GNT_LINE); cat(out, " of max ", GNT_LINE); catu(out, e->cap, GNT_LINE); cat(out, " entered", GNT_LINE); }
    else if (e->state == GNT_S_RUNNING) { if (e->round) { cpy(out, "Round ", GNT_LINE); catu(out, e->round, GNT_LINE); cat(out, " of ", GNT_LINE); catu(out, e->rounds, GNT_LINE); } else cpy(out, "Playing now", GNT_LINE); }
    else if (e->state == GNT_S_DONE) { if (e->champion[0]) { cpy(out, "Winner: ", GNT_LINE); cat(out, e->champion, GNT_LINE); } else cpy(out, "Finished", GNT_LINE); }
    else cpy(out, e->reason == 1u ? "Called off, too few players." : "Called off", GNT_LINE);
}
static unsigned you_line(const struct gnt *g, const struct gnt_ent *e, char *out)           /* line 3 of a description: 0 none, 1 normal, 3 gold (yours) */
{
    out[0] = 0;
    if (e->flags & GNT_F_JOINED) {
        if (e->flags & GNT_F_OUT) { cpy(out, "You are out.", GNT_LINE); return 1u; }
        if (e->state == GNT_S_DONE) {
            if (e->you == GNT_Y_WON) cpy(out, "You won it!", GNT_LINE);
            else if (g->my_kind == 4u && g->my_place >= 2u && e->id == g->cur.id) { cpy(out, "You placed ", GNT_LINE); ord(out, g->my_place); cat(out, ".", GNT_LINE); }
            else cpy(out, "You played in it.", GNT_LINE);
            return 3u;
        }
        if (e->state == GNT_S_CANCEL) return 0u;
        cpy(out, (e->flags & GNT_F_CHECKED) ? "You are checked in." : e->state == GNT_S_CHECKIN ? "Check in now!" : e->state == GNT_S_RUNNING ? "You are in." : "You are signed up.", GNT_LINE);
        return 3u;
    }
    if (e->flags & GNT_F_HOST) { cpy(out, "You host this one.", GNT_LINE); return 3u; }
    return 0u;
}

unsigned gnt_panel(const struct gnt *g, unsigned line, char *out)
{
    out[0] = 0;
    if (line >= GNT_PANEL) return 0;
    if (g->scr == GNT_LIST) {
        const struct gnt_ent *e = 0;
        if (g->net == GNT_N_LOADING) { if (line == 0u) cpy(out, "Loading tournaments...", GNT_LINE); return 1; }
        if (g->net == GNT_N_UNREACH) { if (line == 0u) cpy(out, "Server not available.", GNT_LINE); else if (line == 1u) cpy(out, "Press A to try again.", GNT_LINE); return line == 0u ? 4u : 1u; }
        if (g->net == GNT_N_NOSUP) { if (line == 0u) cpy(out, "Not on this server yet.", GNT_LINE); else if (line == 1u) cpy(out, "Press A to try again.", GNT_LINE); return line == 0u ? 4u : 1u; }
        if (g->net == GNT_N_SIGNIN) { if (line == 0u) cpy(out, "Link this console first.", GNT_LINE); else if (line == 1u) cpy(out, "Then press A to try again.", GNT_LINE); return line == 0u ? 4u : 1u; }
        if (g->net == GNT_N_EMPTY) return 0;
        if (g->say[0]) { if (line == 0u) { cpy(out, g->say, GNT_SAY); return g->say_warn ? 4u : 3u; } line--; if (line >= 2u) return 0; }          /* an answer of Z / sign-up on top: the box holds three lines */
        if (g->sel < g->nv) e = vent(g, g->sel);
        if (!e || line >= 3u) return 0;
        if ((e->flags & GNT_F_OFFICIAL) && e->state <= GNT_S_CHECKIN) {
            if (line == 0u) { char t[24]; gnt_fmt_secs(t, e->secs); cpy(out, e->state == GNT_S_CHECKIN ? "Check-in is open" : "Starts in ", GNT_LINE); if (e->state != GNT_S_CHECKIN) cat(out, t, GNT_LINE); }
            else if (line == 1u) { catu(out, e->joined, GNT_LINE); cat(out, " of ", GNT_LINE); catu(out, e->cap, GNT_LINE); cat(out, " signed up", GNT_LINE); }
            else if (line == 2u) cup_state(e, out);
            return (e->flags & GNT_F_JOINED) ? 3u : 1u;
        }
        if (line == 0u) host_line(e, out);
        else if (line == 1u) state_line(e, out);
        else return you_line(g, e, out);
        return (e->flags & GNT_F_JOINED) ? 3u : 1u;
    }
    if (g->scr == GNT_RULES) {
        const struct gnt_ent *e = &g->cur;
        if (g->say[0]) { if (line == 0u) { cpy(out, g->say, GNT_SAY); return g->say_warn ? 4u : 3u; } line--; if (line >= 2u) return 0; }
        if (line == 0u) { char t[24]; gnt_fmt_secs(t, e->secs); cpy(out, e->state == GNT_S_CHECKIN ? "Check-in is open" : "Starts in ", GNT_LINE); if (e->state != GNT_S_CHECKIN) cat(out, t, GNT_LINE); }
        else if (line == 1u) { catu(out, e->joined, GNT_LINE); cat(out, " of ", GNT_LINE); catu(out, e->cap, GNT_LINE); cat(out, " signed up", GNT_LINE); }
        else cup_state(e, out);
        return (e->flags & GNT_F_JOINED) ? 3u : 1u;
    }
    if (g->scr == GNT_PAGE) {                                              /* three lines (the info box holds three; the fourth overlapped the key line) */
        const struct gnt_ent *e = &g->cur;
        unsigned l = line, at = 1u;
        if (g->say[0]) { if (line == 0u) { cpy(out, g->say, GNT_SAY); return g->say_warn ? 4u : 3u; } l = line - 1u; if (l >= 2u) return 0; }
        if (l == 0u && (e->flags & GNT_F_OFFICIAL) && e->state <= GNT_S_CHECKIN) {
            char t[24];
            gnt_fmt_secs(t, e->secs);
            cpy(out, "Starts in ", GNT_LINE); cat(out, t, GNT_LINE);
        } else if (l == 0u) host_line(e, out);
        else if (l == 1u) state_line(e, out);
        else if (l == 2u && e->state == GNT_S_CHECKIN && (e->flags & GNT_F_JOINED) && !(e->flags & GNT_F_CHECKED)) {
            cpy(out, "Check in now!", GNT_LINE);
            if (e->secs) { char t[24]; gnt_fmt_secs(t, e->secs); cat(out, " ", GNT_LINE); cat(out, t, GNT_LINE); cat(out, " left", GNT_LINE); }
            at = 3u;
        } else if (l == 2u && e->state <= GNT_S_CHECKIN && !(e->flags & GNT_F_JOINED)) {
            if (e->scheduled && e->state == GNT_S_REG) { catu(out, e->joined, GNT_LINE); cat(out, " of max ", GNT_LINE); catu(out, e->cap, GNT_LINE); cat(out, " entered", GNT_LINE); }
            else cpy(out, (e->flags & GNT_F_PASS) ? "Private tournament." : "Entry closes at the start.", GNT_LINE);
        } else if (l == 2u && g->called && e->state == GNT_S_RUNNING) {
            cpy(out, "Your match is ready!", GNT_LINE); at = 3u;
        } else if (l == 2u) {
            unsigned a = you_line(g, e, out);
            if (e->you == GNT_Y_ELIM && !out[0]) cpy(out, "You are out.", GNT_LINE);
            at = a ? a : 1u;
        }
        return out[0] ? (g->say[0] && line == 0u ? 3u : at) : 0u;
    }
    if (g->scr == GNT_JOIN) {
        static const char *const H[JR_MAX] = { "Type the 8 digit tournament ID.", "Private ones: its 8 digit passcode.", "Joins you to that tournament." };
        if (line == 0u) cpy(out, H[g->cf_row < JR_MAX ? g->cf_row : 0u], GNT_LINE + 8u);
        else if (line == 1u) cpy(out, g->say[0] ? g->say : "Ask the host for the ID.", GNT_SAY);
        return out[0] ? (g->say[0] && line == 1u ? (g->say_warn ? 4u : 3u) : 1u) : 0u;
    }
    if (g->scr == GNT_CREATE) {
        unsigned r = cr_id(g, g->cf_row);
        if (line == 0u) {
            switch (r) {
            case CR_PRESET: cpy(out, PRESET_TXT[g->cf_preset % GNT_PRESETS], GNT_LINE + 8u); break;
            case CR_NAME: cpy(out, "Pick a name with Left Right.", GNT_LINE + 8u); break;
            case CR_FORMAT: cpy(out, g->cf_format ? "Single: one loss and you are out." : "Double: a second chance.", GNT_LINE + 8u); break;
            case CR_SIZE: cpy(out, "Up to ", GNT_LINE + 8u); catu(out, CF_SIZES[g->cf_size & 3u], GNT_LINE + 8u); cat(out, " players. Byes fill gaps.", GNT_LINE + 8u); break;
            case CR_SIGNUP: cpy(out, "Time to sign up before it starts.", GNT_LINE + 8u); break;
            case CR_CHECK: cpy(out, "Entrants must confirm in time.", GNT_LINE + 8u); break;
            case CR_SEED: cpy(out, "Random draw, rating or join order.", GNT_LINE + 8u); break;
            case CR_HOST: cpy(out, g->cf_hostplays ? "You play in your own tournament." : "You only run it, you do not play.", GNT_LINE + 8u); break;
            case CR_START: cpy(out, "Now, or a start time up to 30 days.", GNT_LINE + 8u); break;
            case CR_DAY: cpy(out, "Whole days from now.", GNT_LINE + 8u); break;
            case CR_HOUR: cpy(out, "Hours on top of the days.", GNT_LINE + 8u); break;
            case CR_PASS: cpy(out, "8 digits make it private.", GNT_LINE + 8u); break;
            default: cpy(out, "Creates it and opens sign-up.", GNT_LINE + 8u); break;
            }
        } else if (line == 1u) {
            if (g->say[0]) cpy(out, g->say, GNT_SAY);
            else if (g->cf_start) { char t[24]; gnt_fmt_secs(t, gnt_create_start_in(g)); cpy(out, "Starts in ", GNT_LINE); cat(out, t, GNT_LINE); }
            else cpy(out, "Starts when sign-up ends.", GNT_LINE);
        } else if (line == 2u && !g->say[0]) cpy(out, "Entry closes at the start.", GNT_LINE);
        return out[0] ? (g->say[0] && line == 1u ? (g->say_warn ? 4u : 3u) : 1u) : 0u;
    }
    {   /* bracket */
        char rn2[32];
        const struct gnt_match *m = 0;
        unsigned i = g->b_sel;
        if (!g->b_have) {                                                  /* loading, or the server did not answer: say so and how to retry */
            if (line == 0u) { if (g->busy) cpy(out, "Loading bracket...", GNT_LINE); else if (g->say[0]) { cpy(out, g->say, GNT_SAY); return g->say_warn ? 4u : 1u; } else cpy(out, "Bracket not available.", GNT_LINE); return 1; }
            if (line == 1u && !g->busy) { cpy(out, "Press Start to try again.", GNT_LINE); return 2; }
            return 0;
        }
        if (call_here(g)) {
            if (line == 0u) { cpy(out, "Your match is ready!", GNT_LINE); return 3; }
            if (line == 1u) { br_title(g, g->call_side, g->call_round, rn2); cpy(out, rn2, GNT_LINE); cat(out, " v ", GNT_LINE); cat(out, g->call_opp, GNT_LINE); return 3; }
            if (line == 2u) {
                cpy(out, "Press A to play.", GNT_LINE);
                if (g->call_secs) { unsigned s = gnt_call_secs(g); if (s) { cat(out, " ", GNT_LINE); catu(out, s / 60u, GNT_LINE); cat(out, s % 60u < 10u ? ":0" : ":", GNT_LINE); catu(out, s % 60u, GNT_LINE); cat(out, " left", GNT_LINE); } else cpy(out, "Press A now!", GNT_LINE); }
                return 3;
            }
            return 0;
        }
        if (i < br_rows(g)) m = br_at(g, g->b_side, g->b_round, i);
        if (line == 0u) { br_title(g, g->b_side, g->b_round, rn2); cpy(out, rn2, GNT_LINE); return 1; }
        if (!m) return 0;
        if (line == 1u) { cpy(out, m->a == GNT_NONE ? "?" : g->b_name[m->a], GNT_LINE); cat(out, " v ", GNT_LINE); cat(out, m->b == GNT_NONE ? "?" : g->b_name[m->b], GNT_LINE); return 1; }
        if (line == 2u) {
            if ((m->state == GNT_M_DONE || m->state == GNT_M_WALK) && m->win && m->a != GNT_NONE && m->b != GNT_NONE) {      /* who won, and how: "PC Bot 5 won by walkover", "Nigzel won 2-1" */
                cpy(out, g->b_name[m->win == 1u ? m->a : m->b], GNT_LINE);
                if (m->state == GNT_M_WALK) cat(out, " won by walkover", GNT_LINE);
                else { cat(out, " won ", GNT_LINE); catu(out, m->win == 1u ? m->wa : m->wb, GNT_LINE); cat(out, "-", GNT_LINE); catu(out, m->win == 1u ? m->wb : m->wa, GNT_LINE); }
                return 1;
            }
            cpy(out, m->state == GNT_M_BYE ? "Bye" : m->state == GNT_M_WALK ? "Won by walkover" : m->state == GNT_M_DONE ? "Finished" : m->state == GNT_M_LIVE ? "Being played" : m->state == GNT_M_CALLED ? "Called to play" : m->state == GNT_M_READY ? "About to be called" : "Waiting for players", GNT_LINE);
            return 1;
        }
        if (line == 3u && m->state < GNT_M_DONE && m->bo) { cpy(out, "Best of ", GNT_LINE); catu(out, m->bo, GNT_LINE); return 2; }
        if (line == 3u && (m->state == GNT_M_DONE || m->state == GNT_M_WALK) && m->win && m->a != GNT_NONE && m->b != GNT_NONE) { cpy(out, "Winner: ", GNT_LINE); cat(out, g->b_name[m->win == 1u ? m->a : m->b], GNT_LINE); return 2; }
        if (line == 3u && g->my_kind == 5u) { cpy(out, "You won the tournament!", GNT_LINE); return 3; }
        if (line == 3u && g->my_kind == 4u) { cpy(out, "You are out. Place ", GNT_LINE); catu(out, g->my_place, GNT_LINE); return 2; }
        if (line == 3u && g->my_kind == 3u) { cpy(out, "Waiting for other matches.", GNT_LINE); return 2; }
        if (line == 3u && g->my_kind == 7u) { cpy(out, "Removed: not checked in.", GNT_LINE); return 4; }
        if (line == 3u && g->my_kind == 6u) { cpy(out, "Tournament called off.", GNT_LINE); return 4; }
        return 0;
    }
}

unsigned gnt_box(const struct gnt *g, unsigned line, char *out)
{
    out[0] = 0;
    if (g->scr == GNT_LIST) {
        if ((g->net != GNT_N_OK) || g->sel >= g->nv) return 0;
        if (line == 0u) catu(out, vent(g, g->sel)->joined, 8);
        else if (line == 1u) { cpy(out, "of ", 8); catu(out, vent(g, g->sel)->cap, 8); }
        else cpy(out, "entered", 8);
        return 1;
    }
    if (g->scr == GNT_BRACKET && g->b_have) {
        if (g->b_side == GNT_SIDE_F) { if (line == 0u) cpy(out, "GF", 8); else if (line == 1u) cpy(out, g->b_round >= 2u ? "reset" : "final", 8); else out[0] = 0; return 1; }
        if (line == 0u) catu(out, g->b_round, 8);
        else if (line == 1u) { cpy(out, "of ", 8); catu(out, br_nrounds(g, g->b_side), 8); }
        else cpy(out, g->b_side == GNT_SIDE_L ? "losers" : g->b_format == GNT_FMT_DOUBLE ? "winners" : "rounds", 8);
        return 1;
    }
    return 0;
}

/* ---- the text view ---- */
static void put(struct sg_view *v, unsigned row, unsigned attr, const char *s)
{
    if (row >= SG_ROWS) return;
    cpy(v->t[row], s, SG_COLS);
    v->attr[row] = (unsigned char)attr;
}
static void pad(char *d, unsigned col, unsigned max) { while (slen(d) < col && slen(d) < max) cat(d, " ", max); }

void gnt_text(const struct gnt *g, struct sg_view *v)
{
    static const unsigned char ATTR[4] = { SG_A_BODY, SG_A_DIM, SG_A_HI, SG_A_BAD };
    char b[SG_COLS + 1], nu[GNT_NUM + 1], ti[40], ex[GNT_EXTRA + 4];
    unsigned s, a;
    b[0] = 0; cat(b, gnt_title(g), SG_COLS);
    if (g->mock) { pad(b, 40, SG_COLS); cat(b, "Example", SG_COLS); }
    put(v, 0, SG_A_HEAD, b);
    for (s = 0; s < GNT_VIS; s++) {
        a = gnt_row(g, s, nu, ti, ex);
        b[0] = 0;
        if (a) {
            int cur = s == gnt_cur_page(g);
            cat(b, cur ? "- " : "  ", SG_COLS);
            if (nu[0]) { cat(b, nu, SG_COLS); pad(b, 20, SG_COLS); cat(b, "  ", SG_COLS); }              /* (the forms have no count column) */
            cat(b, ti, SG_COLS);
            pad(b, slen(ex) + 46u > SG_COLS ? SG_COLS - slen(ex) : 46u, SG_COLS);                      /* the right column flush right, wide words ("Double Elimination") move left */
            cat(b, ex, SG_COLS);
            put(v, 2u + s, cur ? SG_A_HI : ATTR[(a - 1u) & 3u], b);
        }
    }
    if (g->scr == GNT_RULES) for (s = 0; s < GNT_RULE_N; s++) { (void)gnt_rules(g, s, b); put(v, 2u + s, SG_A_BODY, b); }
    for (s = 0; s < GNT_PANEL; s++) { a = gnt_panel(g, s, b); put(v, 13u + s, a ? ATTR[(a - 1u) & 3u] : SG_A_BODY, b); }
    if (g->scr == GNT_LIST) put(v, 12u, SG_A_DIM, gnt_hint2(g));
    put(v, SG_ROWS - 1u, SG_A_DIM, gnt_hint(g));
}
unsigned gnt_row_cycle(const struct gnt *g, unsigned slot)          /* LIST: 1 on the collapsed dailies row when more than one daily exists (Left Right cycle, wrapping: both arrows always) */
{
    unsigned i = g->first + slot;
    return g->scr == GNT_LIST && g->net == GNT_N_OK && g->dn > 1u && i == 0u && i < g->nv;
}
unsigned gnt_row_star(const struct gnt *g, unsigned slot)           /* LIST: 1 when the row's cup is one you are signed up for (a gold star next to the row) */
{
    unsigned i = g->first + slot;
    return g->scr == GNT_LIST && g->net == GNT_N_OK && i < g->nv && (vent(g, i)->flags & GNT_F_JOINED) != 0u;
}

/* ---- the animated bracket pack ---- */
unsigned gnt_tmv_pack(const struct gnt *g, struct tmb_pack *out, unsigned ver)
{
    struct tmb_box box[TMB_BOXES];
    struct tmb_state st;
    unsigned char leaf[64];                         /* entrant (server index) at each leaf */
    unsigned char rstart[8];                        /* first box of round r (1-based) */
    unsigned size, nbox, r, k, i, n1, nres = 0, bx = 0;
    if (!g->b_have || g->b_n < 4u) return 1u;
    size = g->b_size;
    if (size < 4u || size > 64u || (size & (size - 1u)) || g->b_n != size) return 1u;
    nbox = tmb_pow2_table(box, size);
    if (!nbox) return 1u;
    n1 = size >> 1;
    for (r = 1; r <= g->b_rounds && r <= 7u; r++) { rstart[r] = (unsigned char)bx; bx += size >> r; }
    for (k = 0; k < 64u; k++) leaf[k] = GNT_NONE;
    if (br_cnt(g, GNT_SIDE_W, 1u) != n1) return 2u;
    for (k = 0; k < n1; k++) {
        const struct gnt_match *m = br_at(g, GNT_SIDE_W, 1u, k);
        if (!m || m->a == GNT_NONE || m->b == GNT_NONE || m->a >= size || m->b >= size) return 2u;
        leaf[2u * k] = m->a; leaf[2u * k + 1u] = m->b;
    }
    zero(out, (unsigned)sizeof *out);
    out->size_idx = (unsigned char)(size == 4u ? 0u : size == 8u ? 2u : size == 16u ? 4u : size == 32u ? 6u : 8u);
    out->nent = (unsigned char)size; out->ver = (unsigned char)ver;
    for (i = 0; i < size; i++) {
        unsigned e = leaf[i];
        out->ent[i].kind = 1; out->ent[i].level = 5; out->ent[i].handicap = 0; out->ent[i].costume = 0;
        out->ent[i].ch = (unsigned char)((e * 7u + 3u) % 26u);                       /* the plate's character icon: any stable face per entrant (the server does not send characters) */
        out->ent[i].label = (unsigned short)(0x384u + e);
        { unsigned c; for (c = 0; c < 8u && g->b_name[e][c]; c++) out->name[i][c] = g->b_name[e][c]; out->name[i][c] = 0; }
    }
    for (r = 1; r <= g->b_rounds && r <= 7u; r++) {
        unsigned cnt = br_cnt(g, GNT_SIDE_W, r);
        if (cnt != (size >> r)) return 2u;
        for (k = 0; k < cnt; k++) {
            const struct gnt_match *m = br_at(g, GNT_SIDE_W, r, k);
            unsigned w, j, leafidx = 0xFFu;
            if (!m || (m->state != GNT_M_DONE && m->state != GNT_M_WALK) || !m->win) continue;
            w = m->win == 1u ? m->a : m->b;
            for (j = 0; j < size; j++) if (leaf[j] == w) { leafidx = j; break; }
            if (leafidx == 0xFFu || nres >= 63u) return 3u;
            out->res[nres].box = (unsigned char)(rstart[r] + k); out->res[nres].winner = (unsigned char)leafidx; nres++;
        }
    }
    out->nres = (unsigned char)nres;
    if (tmb_build(&st, box, nbox, out->ent, size, out->res, nres) != 0u) return 3u;
    if (g->b_format == GNT_FMT_DOUBLE) {                         /* the losers page: the server's losers matches and the grand finals, box by box */
        struct tmb_lbox lb[TMB_BOXES];
        unsigned char inv[64];
        unsigned nlb = tmb_lb_table(lb, size), rcs[12], nr = 0, c, first = 0, r, b, t, ok = 1;
        for (c = size >> 2; c >= 1u; c >>= 1) { rcs[nr++] = c; rcs[nr++] = c; }
        for (k = 0; k < 64u; k++) inv[k] = GNT_NONE;
        for (k = 0; k < size; k++) if (leaf[k] < 64u) inv[leaf[k]] = (unsigned char)k;
        for (b = 0; b < TMB_BOXES; b++) { out->lbs[b][0] = out->lbs[b][1] = 0xFFu; out->lbp[b][0] = out->lbp[b][1] = 3u; out->lbx[b] = 0; }
        for (r = 0; ok && nlb && r < nr + 2u; r++) {                  /* losers rounds 1..nr, then grand final (round 1) and its reset (round 2) */
            unsigned side = r < nr ? GNT_SIDE_L : GNT_SIDE_F, rd = r < nr ? r + 1u : r - nr + 1u, cnt = r < nr ? rcs[r] : 1u;
            if (br_cnt(g, side, rd) != cnt) { ok = 0; break; }
            for (k = 0; k < cnt; k++) {
                const struct gnt_match *m = br_at(g, side, rd, k);
                unsigned box = first + k, done, w;
                if (!m || box >= nlb) { ok = 0; break; }
                done = (m->state == GNT_M_DONE || m->state == GNT_M_WALK) && m->win;
                out->lbx[box] = (unsigned char)!done;
                for (t = 0; t < 2u; t++) {
                    unsigned e = t ? m->b : m->a;
                    if (e == GNT_NONE || e >= 64u || inv[e] == GNT_NONE) continue;
                    out->lbs[box][t] = inv[e];
                    if (done) { w = ((m->win == 1u) == (t == 0u)); out->lbp[box][t] = (unsigned char)(w ? 0u : 1u); if (w && lb[box].nxt < nlb) out->lbs[box][t] = 0xFFu; }
                }
            }
            first += cnt;
        }
        out->lb_ok = (unsigned char)(ok && nlb && first == nlb);
    }
    return 0u;
}
