/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Grandmaster standings screen (Multishine > Standings): the MODEL.  Pure C89, no libc, no heap, no Melee
 * address, no network.  Read only: a list (rank, display name, rating, tier) of one page of the server's ladder (STANDINGS_LIST 0x33,
 * 9), the selected row's detail (name, games, tier; never a connect code), your own line ("You: 12th, 1840"), the states (Loading, "No ranked games yet.", "Server not available.").
 * Two front ends draw it, like the Game Night browser (gnb.h):
 *   - gnb_ev.c (console only, -DGNB_MELEE): the retail Event Match screen,
 *   - stb_text (here): the same texts in the sign overlay's character-cell view (host tests; console fallback when the Event screen cannot be built).
 * The network is the glue's (sg_entry.c with client_net/gnc.c): the model raises stb.req and the glue answers through stb_net_*.
 * Paging: one server page = one screen of STB_VIS rows (request size 9).  Up / Down move the cursor and cross into the next / previous page, L / R turn a page,
 * Start reloads the page, B leaves.  Text: the menu font's set plus what the Event front end maps to full-width codes (the '#' of a connect code).
 */
#ifndef STB_H
#define STB_H

#define STB_VIS 9u                 /* rows of the retail Event list = rows asked per request */
#define STB_LINE 46u               /* characters of a panel line */
#define STB_NOTE_FRAMES 120u       /* the "not on the board" note stays this long (4 s at the overlay's 30 frames/s) */
#define STB_REFRESH_GAP 60u        /* frames between two Start reloads (2 s at the overlay's 30 frames/s) */
#define STB_REP_FIRST 12u          /* held Up / Down: first repeat after 12 frames, then every 3 */
#define STB_REP_NEXT 3u
#define STB_TIER_PLACEMENT 255u
#define STB_TIER_NONE 254u              /* no server tier known (stb_rank_icon): the rating limits decide */

/* buttons (HSD_PadStatus.button bits, the same values as gnb.h / sg_ui.h) */
#define STB_B_DOWN 0x0004u
#define STB_B_UP 0x0008u
#define STB_B_R 0x0020u
#define STB_B_L 0x0040u
#define STB_B_A 0x0100u
#define STB_B_B 0x0200u
#define STB_B_X 0x0400u
#define STB_B_START 0x1000u

/* list state */
enum { STB_N_LOADING, STB_N_OK, STB_N_EMPTY, STB_N_OFFLINE, STB_N_NOSUP, STB_N_BAD, STB_N_RATE };
/* why a request failed (stb_net_fail) */
enum { STB_W_UNREACH, STB_W_NOSUP, STB_W_BAD, STB_W_RATE };
enum { STB_REQ_NONE, STB_REQ_LIST };
/* sounds (the retail menu ids, as gnb.h) */
enum { STB_SFX_NONE = 0, STB_SFX_BACK = 1, STB_SFX_FWD = 2, STB_SFX_MOVE = 3, STB_SFX_DENY = 4 };
#define STB_D_ROWS 1u
#define STB_D_PANEL 2u
#define STB_D_ALL 3u

struct stb_row { unsigned short rank, rating, games; unsigned char tier, you; char name[13]; };   /* rank 0 = in placement; tier = the server's tier byte (kept on the wire, not used for the badge), 255 placement */

struct stb {
    unsigned char net;             /* STB_N_* */
    unsigned char n;               /* rows of the page shown */
    unsigned char page;            /* server page shown (0-based, STB_VIS rows each) */
    unsigned char sel;             /* cursor row 0..n-1 */
    unsigned char req, req_page;   /* STB_REQ_LIST and the page asked for; cleared by the glue when taken */
    unsigned char busy;            /* a request is out (a page turn keeps the old rows until the answer) */
    unsigned char want_sel;        /* cursor row to land on when the page arrives (0xFF: keep, clamped) */
    unsigned char mock, have_you;  /* the server flagged the data as example data; your own row follows */
    unsigned char exit, sfx, dirty, gfx, gfx_tried;   /* gfx: the Event screen front end owns the screen (the glue then performs the exit) */
    unsigned char rep, rep_dir;
    unsigned char find_me;         /* X: Me asked for a page: the cursor goes to the row the server marked as yours when it arrives */
    unsigned short total, season, cool, prev, note_t;      /* note_t: frames left of the note (X: Me while not on the board) */
    unsigned long updated;         /* unix seconds of the server's table build */
    unsigned char days_left;       /* days left of the season counting today (the server's UTC month; the console has no clock); 0 = not yet known: the box stays empty */
    unsigned short loads;          /* answers taken (RAM proof) */
    struct stb_row r[STB_VIS], you;
};

/* ---- life cycle and input ---- */
void stb_enter(struct stb *g);                                         /* Loading, page 0 asked */
void stb_frame(struct stb *g, unsigned held, int stick_up, int stick_down);
/* ---- answers from the glue ---- */
void stb_net_list(struct stb *g, unsigned flags, unsigned season, unsigned long updated, unsigned total, unsigned page, unsigned n,
                  const struct stb_row *rows, const struct stb_row *you);  /* you = 0 when the reply has none; flags bit 0 = example data */
void stb_net_days(struct stb *g, unsigned days);                        /* the version 2 reply's season days left (1..31; anything else = unknown); call after stb_net_list */
/* the small box at the bottom right of the info panel: three centred lines "24" (big), "days", "left" ("day" for 1); false (0) = unknown, nothing is shown */
unsigned stb_season_box(const struct stb *g, char *num, char *w1, char *w2);
void stb_net_fail(struct stb *g, unsigned why);                         /* STB_W_* */

/* ---- what the screen shows ---- */
unsigned stb_rows(const struct stb *g);                                 /* rows of the list being shown (0 in a state screen) */
unsigned stb_pages(const struct stb *g);                                /* pages of the whole ladder (at least 1) */
/* row `slot` (0..8): rank ("-" in placement), name, rating, tier name; returns 0 for an empty slot, else 1 + attribute (0 normal, 1 dim: placement, 2 highlight: your row) */
unsigned stb_cols(const struct stb *g, unsigned slot, char *rank, char *name, char *rating, char *tier);
const char *stb_note(const struct stb *g);                              /* the short note after X: Me when the player is not on the board ("You are not on the board yet"), 0 = none; any cursor move clears it */
const char *stb_state_text(const struct stb *g);                        /* the message of a state screen ("No ranked games yet.", "Server not available.", ...), 0 on the list */
/* panel line 0..2 under the list: the selected row's name; its games and tier; your own line ("You: 12th, 1840") or the example-data note.
   returns attribute 0 normal, 1 dim, 2 highlight */
unsigned stb_panel(const struct stb *g, unsigned line, char *out);
void stb_head(const struct stb *g, char *out);                          /* "Updated Oct 14" ("" until the first answer) */
void stb_badge(const struct stb *g, char *out);                         /* the small box at the top right: "Example" (mock), "Loading", "2 of 3" */
/* the info box under the table (no example / update wording on the console): the selected row's display name (left) and tier title (right, "Grandmaster" for rank 1), both on the top row;
   the bottom row: "Position: 12" (left), "102 Games" (middle), "ELO 1990" (right).  All empty when no row is shown. */
#define STB_TOP_MAX 20u                  /* stb_top left: at most this many characters (buffer STB_TOP_MAX + 1) */
#define STB_BOTTOM_MAX 40u               /* stb_bottom left: the same */
void stb_top(const struct stb *g, char *left, char *right);
void stb_bottom(const struct stb *g, char *left, char *mid, char *right);
/* X: Me: 0 = the viewer is not on the board (no reply row, or in placement): the key hint is greyed and X refuses; 1 = available.  stb_me_page: the page (and the row on it) that holds rank `rank`
   (server pages are STB_VIS rows: page = (rank - 1) / STB_VIS, row = (rank - 1) % STB_VIS). */
unsigned stb_me_ok(const struct stb *g);
void stb_me_page(unsigned rank, unsigned *page, unsigned *row);
const char *stb_title(const struct stb *g);                             /* "Grandmaster" */
const char *stb_hint(const struct stb *g);                              /* the key line */
/* the row's icon (docs/data/grandmaster_tiers.json): the icon id STB_SH_* (the tier index: T1 lowest.. T6 highest = Novice.. Virtuoso, then the Grandmaster and
   the "?" of an unrated player; STB_SH_NONE for an empty slot), its colour 0xRRGGBB in *rgb (the tier's text colour; the icon itself is a colour texture, drawn untinted), and the step's name
   ("Virtuoso", "Grandmaster", "Placement"). Only the first row on rank 1 wears the Grandmaster icon (the server gives rank 1 to one player; a second row on rank 1 from an older server is a plain step). The icons are Project Slippi's rank badges (stb_rank_tex.h;
   gnb_art.h gart_rank_icon draws them).  The icon never depends on the tier's name, so renaming a tier edits the data file's text only. */
#define STB_SH_T1 0u                    /* the icon ids, in the order of ms_badges.TEXTURE_SHAPES (checked by tests/test_tier_badges.py) */
#define STB_SH_T6 5u
#define STB_SH_TMAX STB_SH_T6            /* the highest tier icon */
#define STB_SH_GM 6u
#define STB_SH_PLACEMENT 7u             /* unrated / in placement: the "?" icon */
#define STB_SH_RING 8u                  /* the plain white ring and the filled white disc of the Friends unread marker (tint white at draw time); not tier icons */
#define STB_SH_DISC 9u
#define STB_SH_NONE 255u
unsigned stb_mark(const struct stb *g, unsigned slot, unsigned long *rgb);
const char *stb_mark_name(const struct stb *g, unsigned slot);
/* the same mapping for any player (the Grandmaster character screen's rank block): rank 0 or in_placement = the unrated "?" icon; rank 1 not tied = Grandmaster; else the step of `rating` (the
   displayed number) unless `tier` (the server's tier byte, 0 = lowest step) names one; STB_TIER_NONE = rating limits.  *rgb and *name (either may be 0) as above.  Returns an STB_SH_* icon id (0 .. STB_SH_PLACEMENT). */
unsigned stb_rank_icon(unsigned rank, unsigned rating, unsigned tier, int tied, int in_placement, unsigned long *rgb, const char **name);
void stb_ordinal(char *out, unsigned n);                                /* 1st 2nd 3rd 4th .. 11th 12th 13th .. 21st */
void stb_day(char *out, unsigned long unix_s);                          /* "Oct 14" */

struct sg_view;
void stb_text(const struct stb *g, struct sg_view *v);                  /* the character-cell view (52 x 26) */
#endif
