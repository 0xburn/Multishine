/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Grandmaster standings, the model (stb.h).  Strict C89, no libc, no heap, no Melee address, no network. */
#include "stb.h"
#include "sg_ui.h"
#include "../ms_fmt.h"
#include "stb_tiers.h"       /* generated from docs/data/grandmaster_tiers.json */

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
/* a server string reduced to what the screen can draw (the menu font's set; '#' and the Multishine '@' as the Event front end prints them full-width): anything else is a space */
static int drawable(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == ',' || c == '\'' || c == '-' || c == ':' || c == '#' || c == '@';
}
static void safe_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i < max) { d[i] = drawable(s[i]) ? s[i] : ' '; i++; }
    d[i] = 0;
}

/* ---- the badge of a row: shape x colour from the data file; the shape carries the order ---- */
static int tied_at(const struct stb *g, unsigned slot)     /* 1 = an EARLIER row of the page has this rank: only the first row on rank 1 is the Grandmaster (never two) */
{
    unsigned i;
    unsigned rank = g->r[slot].rank;
    if (!rank) return 0;
    for (i = 0; i < slot; i++) if (g->r[i].rank == rank) return 1;
    return 0;
}
/* shape (STB_SH_*) of a row; *rgb 0xRRGGBB, *name the step's full name, *shrt its short name */
static unsigned mark_of(const struct stb_row *r, int tied, unsigned long *rgb, const char **name, const char **shrt)
{
    unsigned i;
    if (r->rank == 0u || r->tier == STB_TIER_PLACEMENT) { *rgb = STB_PL_RGB; *name = STB_PL_NAME; *shrt = STB_PL_SHORT; return STB_PL_SHAPE; }
    if (r->rank == 1u && !tied) { *rgb = STB_GM_RGB; *name = STB_GM_NAME; *shrt = STB_GM_SHORT; return STB_GM_SHAPE; }
    if (r->tier < STB_NSTEPS) i = STB_NSTEPS - 1u - r->tier;          /* the server's tier byte (0 = lowest step): the monthly percentile band, or the rating limits on a small field */
    else for (i = 0; i + 1u < STB_NSTEPS; i++) if (r->rating >= STB_STEP_MIN[i]) break;
    *rgb = STB_STEP_RGB[i]; *name = STB_STEP_NAME[i]; *shrt = STB_STEP_SHORT[i];
    return STB_STEP_SHAPE[i];
}
unsigned stb_rank_icon(unsigned rank, unsigned rating, unsigned tier, int tied, int in_placement, unsigned long *rgb, const char **name)
{
    struct stb_row r;
    unsigned long c = 0;
    const char *n = "", *sh = "";
    unsigned shape;
    r.rank = (unsigned short)(in_placement ? 0u : rank); r.rating = (unsigned short)rating; r.games = 0; r.tier = (unsigned char)tier; r.you = 0; r.name[0] = 0;
    shape = mark_of(&r, tied, &c, &n, &sh);
    if (rgb) *rgb = c;
    if (name) *name = n;
    return shape;
}
unsigned stb_mark(const struct stb *g, unsigned slot, unsigned long *rgb)
{
    const char *n, *sh;
    unsigned long c = 0;
    unsigned shape;
    if (slot >= stb_rows(g)) return STB_SH_NONE;
    shape = mark_of(&g->r[slot], tied_at(g, slot), &c, &n, &sh);
    if (rgb) *rgb = c;
    return shape;
}
const char *stb_mark_name(const struct stb *g, unsigned slot)
{
    const char *n = "", *sh;
    unsigned long c;
    if (slot < stb_rows(g)) mark_of(&g->r[slot], tied_at(g, slot), &c, &n, &sh);
    return n;
}

void stb_ordinal(char *out, unsigned n)
{
    unsigned t = n % 100u, u = n % 10u;
    out[0] = 0;
    catu(out, n, 10);
    cat(out, (t >= 11u && t <= 13u) ? "th" : u == 1u ? "st" : u == 2u ? "nd" : u == 3u ? "rd" : "th", 14);
}

void stb_day(char *out, unsigned long s)
{
    static const char *const MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    unsigned long z = s / 86400ul + 719468ul, era = z / 146097ul, doe = z - era * 146097ul;
    unsigned long yoe = (doe - doe / 1460ul + doe / 36524ul - doe / 146096ul) / 365ul;
    unsigned long doy = doe - (365ul * yoe + yoe / 4ul - yoe / 100ul), mp = (5ul * doy + 2ul) / 153ul;
    unsigned long d = doy - (153ul * mp + 2ul) / 5ul + 1ul, m = mp < 10ul ? mp + 3ul : mp - 9ul;
    out[0] = 0;
    cat(out, MON[(m - 1ul) % 12ul], 4);
    cat(out, " ", 5);
    catu(out, d, 8);
}

/* ---- life cycle ---- */
static void ask(struct stb *g, unsigned page, unsigned want_sel)
{
    g->req = STB_REQ_LIST; g->req_page = (unsigned char)page; g->busy = 1; g->want_sel = (unsigned char)want_sel;
    g->dirty |= STB_D_PANEL;
}

void stb_enter(struct stb *g)
{
    unsigned char *b = (unsigned char *)g;
    unsigned i;
    for (i = 0; i < sizeof *g; i++) b[i] = 0;
    g->net = STB_N_LOADING;
    ask(g, 0, 0);
    g->dirty = STB_D_ALL;
}

static void turn(struct stb *g, int dir, unsigned land)
{
    unsigned np = stb_pages(g);
    if (g->busy) return;
    if (dir > 0 && (unsigned)g->page + 1u < np) { ask(g, g->page + 1u, land); g->sfx = STB_SFX_MOVE; }
    else if (dir < 0 && g->page > 0) { ask(g, g->page - 1u, land); g->sfx = STB_SFX_MOVE; }
    else g->sfx = STB_SFX_DENY;
}

static void move(struct stb *g, int dir)
{
    if (g->net != STB_N_OK || g->busy) return;
    if (dir > 0) {
        if ((unsigned)g->sel + 1u < g->n) { g->sel++; g->sfx = STB_SFX_MOVE; g->dirty |= STB_D_PANEL; }
        else if ((unsigned)g->page + 1u < stb_pages(g)) turn(g, 1, 0);        /* past the last row: the next page, cursor on its first row */
    } else {
        if (g->sel > 0) { g->sel--; g->sfx = STB_SFX_MOVE; g->dirty |= STB_D_PANEL; }
        else if (g->page > 0) turn(g, -1, STB_VIS - 1u);                      /* above the first row: the previous page, cursor on its last row */
    }
}

void stb_frame(struct stb *g, unsigned held, int stick_up, int stick_down)
{
    unsigned edge = held & ~(unsigned)g->prev;
    int up = (edge & STB_B_UP) || stick_up, down = (edge & STB_B_DOWN) || stick_down;
    g->prev = (unsigned short)held;
    if (g->cool) g->cool--;
    if (g->note_t) { g->note_t--; if (!g->note_t || (edge & (STB_B_UP | STB_B_DOWN | STB_B_L | STB_B_R)) || stick_up || stick_down) { g->note_t = 0; g->dirty |= STB_D_ALL; } }
    if (edge & STB_B_B) { g->exit = 1; g->sfx = STB_SFX_BACK; return; }
    if (held & (STB_B_UP | STB_B_DOWN)) {                                    /* held D-pad: repeat */
        unsigned dir = (held & STB_B_DOWN) ? 1u : 2u;
        if (dir != g->rep_dir) { g->rep_dir = (unsigned char)dir; g->rep = 0; }
        else if (++g->rep >= STB_REP_FIRST) { g->rep = (unsigned char)(STB_REP_FIRST - STB_REP_NEXT); if (dir == 1u) down = 1; else up = 1; }
    } else { g->rep_dir = 0; g->rep = 0; }
    if (down) move(g, 1);
    else if (up) move(g, -1);
    if (g->net == STB_N_OK && (edge & STB_B_X)) {                              /* X: Me */
        if (!stb_me_ok(g) || g->busy) { g->sfx = STB_SFX_DENY; if (!g->busy) { g->note_t = STB_NOTE_FRAMES; g->dirty |= STB_D_ALL; } }      /*: not on the board = a short note instead of nothing */
        else {
            unsigned pg, row;
            stb_me_page(g->you.rank, &pg, &row);
            g->sfx = STB_SFX_MOVE;
            if (pg == g->page && row < g->n) { g->sel = (unsigned char)row; g->dirty |= STB_D_PANEL; }
            else { g->find_me = 1; ask(g, pg, row); }
        }
    }
    if (g->net == STB_N_OK && (edge & STB_B_R)) turn(g, 1, 0xFFu);
    if (g->net == STB_N_OK && (edge & STB_B_L)) turn(g, -1, 0xFFu);
    if ((edge & STB_B_START) && !g->busy) {
        if (g->cool) g->sfx = STB_SFX_DENY;
        else { ask(g, g->net == STB_N_OK ? g->page : 0u, 0xFFu); g->cool = STB_REFRESH_GAP; g->sfx = STB_SFX_FWD; if (g->net != STB_N_OK) g->net = STB_N_LOADING; g->dirty = STB_D_ALL; }
    }
}

/* ---- answers ---- */
void stb_net_list(struct stb *g, unsigned flags, unsigned season, unsigned long updated, unsigned total, unsigned page, unsigned n,
                  const struct stb_row *rows, const struct stb_row *you)
{
    unsigned i;
    g->busy = 0; g->loads++;
    if (n > STB_VIS) n = STB_VIS;
    if (n == 0u && page > 0u && total > 0u) {                                  /* the ladder shrank under us: back to the first page */
        ask(g, 0, 0);
        return;
    }
    g->mock = (unsigned char)(flags & 1u);
    g->season = (unsigned short)season; g->updated = updated; g->total = (unsigned short)total; g->page = (unsigned char)page; g->n = (unsigned char)n;
    for (i = 0; i < n; i++) {
        g->r[i] = rows[i];
        safe_cpy(g->r[i].name, rows[i].name, 12);
    }
    g->have_you = (unsigned char)(you != 0);                                   /* (a mock ladder may mark a row as yours, the lab's --standings-mock-you) */
    if (g->have_you) { g->you = *you; safe_cpy(g->you.name, you->name, 12); }
    g->net = n ? STB_N_OK : STB_N_EMPTY;
    if (g->want_sel != 0xFFu) g->sel = g->want_sel;
    if (g->find_me) {                                                          /* X: Me: the row the server marked as yours (ties share a rank, so the page math may be one row off) */
        for (i = 0; i < n; i++) if (g->r[i].you) { g->sel = (unsigned char)i; break; }
        g->find_me = 0;
    }
    if (n && (unsigned)g->sel >= n) g->sel = (unsigned char)(n - 1u);
    if (!n) g->sel = 0;
    g->dirty = STB_D_ALL;
}

void stb_net_fail(struct stb *g, unsigned why)
{
    g->busy = 0;
    if (g->net == STB_N_OK && why == STB_W_RATE) { g->dirty |= STB_D_PANEL; return; }    /* a refused reload keeps the page */
    g->net = why == STB_W_NOSUP ? STB_N_NOSUP : why == STB_W_BAD ? STB_N_BAD : why == STB_W_RATE ? STB_N_RATE : STB_N_OFFLINE;
    g->n = 0; g->sel = 0;
    g->dirty = STB_D_ALL;
}

/* ---- texts ---- */
unsigned stb_rows(const struct stb *g) { return g->net == STB_N_OK ? g->n : 0u; }
unsigned stb_pages(const struct stb *g) { unsigned p = ((unsigned)g->total + STB_VIS - 1u) / STB_VIS; return p ? p : 1u; }

const char *stb_note(const struct stb *g) { return g->note_t ? "You are not on the board yet" : 0; }
const char *stb_state_text(const struct stb *g)
{
    switch (g->net) {
    case STB_N_LOADING: return "Loading";
    case STB_N_EMPTY: return 0;                       /* the empty board says so in the panel below, the table stays clean */
    case STB_N_OFFLINE: return "Server not available";
    case STB_N_NOSUP: return "Needs a newer server";
    case STB_N_BAD: return "The server sent bad data";
    case STB_N_RATE: return "Too many requests, try later";
    default: return 0;
    }
}

static int is_you(const struct stb *g, const struct stb_row *r) { return g->have_you && r->you != 0u; }      /* the server marks the player's own row (flags bit 0); the console never sees anyone's connect code */

unsigned stb_cols(const struct stb *g, unsigned slot, char *rank, char *name, char *rating, char *tier)
{
    const struct stb_row *r;
    rank[0] = name[0] = rating[0] = tier[0] = 0;
    if (slot >= stb_rows(g)) return 0;
    r = &g->r[slot];
    if (r->rank) catu(rank, r->rank, 5); else cat(rank, "-", 5);      /* ties share the rank (competition ranking: 8, 8, 10) */
    cpy(name, r->name, 12);
    catu(rating, r->rating, 5);
    { const char *n, *sh; unsigned long c; (void)mark_of(r, tied_at(g, slot), &c, &n, &sh); cpy(tier, n, 12); }      /* the tier written out, "Grandmaster" too */
    return 1u + (is_you(g, r) ? 2u : (r->rank == 0u ? 1u : 0u));
}

unsigned stb_me_ok(const struct stb *g) { return g->net == STB_N_OK && g->have_you && g->you.rank != 0u ? 1u : 0u; }
void stb_me_page(unsigned rank, unsigned *page, unsigned *row)
{
    unsigned i = rank ? rank - 1u : 0u;
    *page = i / STB_VIS; *row = i % STB_VIS;
}
void stb_top(const struct stb *g, char *left, char *right)
{
    left[0] = 0; right[0] = 0;
    if (g->net == STB_N_EMPTY) { cpy(left, "No ranked games yet", STB_TOP_MAX); return; }   /*: the empty board's text in the info box */
    if (!stb_rows(g)) return;
    cpy(left, g->r[g->sel].name, 12);
    cpy(right, stb_mark_name(g, g->sel), 12);
}
void stb_bottom(const struct stb *g, char *left, char *mid, char *right)
{
    const struct stb_row *r;
    left[0] = 0; mid[0] = 0; right[0] = 0;
    if (g->net == STB_N_EMPTY) { cpy(left, "Play Grandmaster to get listed.", STB_BOTTOM_MAX); return; }
    if (!stb_rows(g)) return;
    r = &g->r[g->sel];
    cpy(left, "Position: ", 12);
    if (r->rank) catu(left, r->rank, 20); else cat(left, "-", 20);
    catu(mid, r->games, 12); cat(mid, r->games == 1u ? " game" : " games", 12);
    cpy(right, "ELO ", 12); catu(right, r->rating, 12);
}

unsigned stb_panel(const struct stb *g, unsigned line, char *out)
{
    const struct stb_row *r = stb_rows(g) ? &g->r[g->sel] : 0;
    out[0] = 0;
    if (g->net == STB_N_EMPTY) {                          /*: the empty board's text in the panel, not in the table */
        if (line == 0u) { cat(out, "No ranked games yet", STB_LINE); return 2; }
        if (line == 1u) { cat(out, "Play Grandmaster to get listed.", STB_LINE); return 0; }
        return 0;
    }
    if (line == 0u && r) {
        cat(out, r->name, STB_LINE);
        if (is_you(g, r)) cat(out, "   You", STB_LINE);
        return 2;
    }
    if (line == 1u && r) {
        catu(out, r->games, STB_LINE); cat(out, r->games == 1u ? " game   " : " games   ", STB_LINE);
        if (r->rank == 0u || r->tier == STB_TIER_PLACEMENT) cat(out, "In placement", STB_LINE);
        else cat(out, stb_mark_name(g, g->sel), STB_LINE);
        return 0;
    }
    if (line == 2u) {
        if (g->busy && g->net == STB_N_OK) { cat(out, "Loading", STB_LINE); return 1; }
        if (g->have_you) {
            cat(out, "You: ", STB_LINE);
            if (g->you.rank) { char o[16]; stb_ordinal(o, g->you.rank); cat(out, o, STB_LINE); }
            else cat(out, "In placement", STB_LINE);
            cat(out, ", ", STB_LINE); catu(out, g->you.rating, STB_LINE);
            return 2;
        }
        if (g->mock && g->net == STB_N_OK) { cat(out, "Example data from the server", STB_LINE); return 1; }
        if (g->net == STB_N_OK && g->total) { cat(out, "Season ", STB_LINE); catu(out, g->season, STB_LINE); cat(out, "   ", STB_LINE); catu(out, g->total, STB_LINE); cat(out, " players", STB_LINE); return 1; }
    }
    return 0;
}

void stb_head(const struct stb *g, char *out)
{
    out[0] = 0;
    if (!g->updated) return;
    cat(out, "Updated ", 15);
    stb_day(out + slen(out), g->updated);
}

void stb_net_days(struct stb *g, unsigned days)
{
    g->days_left = (unsigned char)(days >= 1u && days <= 31u ? days : 0u);
    g->dirty = STB_D_ALL;
}

unsigned stb_season_box(const struct stb *g, char *num, char *w1, char *w2)
{
    unsigned d = g->days_left;
    num[0] = 0; w1[0] = 0; w2[0] = 0;
    if (d < 1u || d > 31u || (g->net != STB_N_OK && g->net != STB_N_EMPTY)) return 0;      /* also on an empty board: the season runs anyway */
    if (d >= 10u) { num[0] = (char)('0' + d / 10u); num[1] = (char)('0' + d % 10u); num[2] = 0; }
    else { num[0] = (char)('0' + d); num[1] = 0; }
    cpy(w1, d == 1u ? "day" : "days", 6);
    cpy(w2, "left", 6);
    return 1;
}

void stb_badge(const struct stb *g, char *out)
{
    out[0] = 0;
    if (g->net == STB_N_LOADING || (g->busy && g->net == STB_N_OK)) { cat(out, "Loading", 15); return; }
    if (g->mock) { cat(out, "Example", 15); return; }
    if (g->net == STB_N_OK && stb_pages(g) > 1u) { catu(out, g->page + 1u, 15); cat(out, " of ", 15); catu(out, stb_pages(g), 15); }
}

const char *stb_title(const struct stb *g) { (void)g; return "Grandmaster"; }
const char *stb_hint(const struct stb *g)
{
    if (g->net == STB_N_OK) return stb_pages(g) > 1u ? "L R: Page  Start: Reload  X: Me  B: Back" : "Start: Reload  X: Me  B: Back";     /* X: Me between the two (the one footer row) */   /* (50 characters drew a stray "." after Back: the SIS buffer regrow, GUI audit ST-04) */
    return "Start: Reload  B: Back";
}

/* ---- the character-cell view ---- */
static void put(struct sg_view *v, unsigned row, unsigned attr, const char *s)
{
    if (row >= SG_ROWS) return;
    cpy(v->t[row], s, SG_COLS);
    v->attr[row] = (unsigned char)attr;
}
static void pad(char *d, unsigned col, unsigned max) { while (slen(d) < col && slen(d) < max) cat(d, " ", max); }

void stb_text(const struct stb *g, struct sg_view *v)
{
    static const unsigned char ATTR[3] = { SG_A_BODY, SG_A_DIM, SG_A_HI };
    char b[SG_COLS + 1], rk[8], nm[16], ra[8], ti[12];
    unsigned s, a;
    const char *st = stb_state_text(g);
    b[0] = 0; cat(b, stb_title(g), SG_COLS);
    if (g->mock) { pad(b, 40, SG_COLS); cat(b, "Example", SG_COLS); }
    put(v, 0, SG_A_HEAD, b);
    b[0] = 0; stb_head(g, b);
    { char bd[16]; stb_badge(g, bd); if (bd[0]) { pad(b, 30, SG_COLS); cat(b, bd, SG_COLS); } }
    put(v, 1, SG_A_DIM, b);
    for (s = 0; s < STB_VIS; s++) {
        a = stb_cols(g, s, rk, nm, ra, ti);
        b[0] = 0;
        if (a) {
            int cur = s == g->sel;
            cat(b, cur ? "- " : "  ", SG_COLS);
            pad(b, 6u - slen(rk), SG_COLS); cat(b, rk, SG_COLS); cat(b, "  ", SG_COLS);
            cat(b, nm, SG_COLS); pad(b, 24, SG_COLS);
            pad(b, 29u - slen(ra), SG_COLS); cat(b, ra, SG_COLS); cat(b, "  ", SG_COLS);
            cat(b, ti, SG_COLS);
            put(v, 3u + s, cur ? SG_A_HI : ATTR[(a - 1u) % 3u], b);
        } else if (s == 2u && st) { cat(b, "  ", SG_COLS); cat(b, st, SG_COLS); put(v, 3u + s, g->net == STB_N_LOADING ? SG_A_DIM : SG_A_BAD, b); }
    }
    for (s = 0; s < 3u; s++) { a = stb_panel(g, s, b); put(v, 14u + s, ATTR[a % 3u], b); }
    put(v, SG_ROWS - 1u, SG_A_DIM, stb_hint(g));
}
