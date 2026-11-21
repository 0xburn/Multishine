/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Tournament screens on the retail Event Match screen: the Melee glue of the gnt model (list, page, Create form and the
 * bracket as the rounds of the server bracket).  CONSOLE ONLY (-DGNB_MELEE, ms/sign.ovl).  The technique of the Friends screen (frd_ev.c, itself the Event look of the Game Night browser),
 * written again for this model so no screen depends on another's state:
 *   open: the current menu think's callback is swapped for our launcher; the launcher builds the retail Event screen (mnEvent_8024E838(0, 0)), frees the old think and swaps the retail
 *         Event think (which would start an event match on A) for ours;
 *   draw: our rows in the retail slots (number column = a short state word, title = name, right edge = the state or the score), the selected entry's lines in the description box (line 0
 *         in the retail slot), a small box ("5 of 16 entered", "round 2 of 4"), the key line, the retail cursor and scroll arrows; the portrait is the empty frame, the event type label, the
 *         "Select Character" box and the score strip are hidden;
 *   leave: our think goes back to the screen we came from (mn_80229894(kind, row, 3)) like the retail B handler;
 *   tabs: the title tabs of the panel read "Multishine | Tournament" (gnt_tab_art.h, tools/gnt_tab_art.py), the retail pixels are put back on every exit.
 */
#ifdef GNB_MELEE
#include "gnt_ev.h"
#include "evo.h"               /* the shared open sequence and row reveal of the Event-view screens */
#include "gnt_tab_art.h"
#include "gnt_geo.h"          /* the row / select box geometry, ONE place */
#include "gnb_art.h"           /* Tournament Melee's own art by reference (GmTou1p): its animated background, the rounded translucent boxes; gart_badge: the Friends status circle */
#include "stb.h"               /* STB_SH_DISC */
#include "gnt_tree.h"         /* the bracket as a tree (Z): the window of matches, drawn below (tree_draw) */
#include "keyhint.h"          /* the key line's button badges, their texts: keyhint_ev.h below */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define W(a) (*(volatile u32 *)(a))
#define H(a) (*(volatile u16 *)(a))
#define B(a) (*(volatile u8 *)(a))
#define F(a) (*(volatile float *)(a))

#define MENU_FLOW 0x804A04F0u
#define TAB 0x803EB6B0u
#define PLINK_HEAD 0x804D782Cu
#define GOBJ_INIT 0x804CE380u
#define EV_SCREEN 0x804D6C60u
#define EV_THINK 0x8024D864u
#define NOP_PROC 0x80081540u
#define PANEL_PROC 0x80229BF4u            /* fn_80229BF4: think of the main menu panel gobj (MenMainPanel_Top: the title tabs) */
#define TAB_PARENT 84u                    /* panel joints of the two title tabs (menu layout ml_glue.c tab_install) */
#define TAB_CHILD 85u
#define EV_KIND 7u
#define CANVAS 1
#define JOBJ_HIDDEN 0x10u
#define JOBJ_MTX_DIRTY 0x40u
#define JOBJ_USER_DEF_MTX 0x800000u
#define JOBJ_MTX_INDEP_SRT 0x2000000u
#define CHKIND_NONE 0x21
typedef void (*fn_ii)(int, int);
typedef void (*fn_p)(void *);
typedef void (*fn_pi)(void *, int);
typedef void (*fn_pu)(void *, u32);
typedef int (*fn_jobj)(void *, void **, ...);
typedef void *(*fn_text_new)(int, int);
typedef void *(*fn_alloc)(u32);
typedef void (*fn_range)(void *, u32);
typedef void (*fn_v)(void);
typedef void (*fn_goto)(int, u16, int);
#define EV_ENTER ((fn_ii)0x8024E838u)
#define EV_PORTRAIT ((fn_pi)0x8024D0CCu)
#define GOBJ_FREE ((fn_p)0x80390228u)
#define LB_JOBJ ((fn_jobj)0x80011E24u)
#define JOBJ_SETFLAGS ((fn_pu)0x80371D9Cu)
#define JOBJ_CLRFLAGS ((fn_pu)0x80371F9Cu)
#define JOBJ_DIRTYSUB ((fn_p)0x803732E8u)
#define HSD_MEMALLOC ((fn_alloc)0x8037F1E4u)
#define HSD_FREE ((fn_p)0x8037F1B0u)
#define DC_STORE ((fn_range)0x80344840u)
#define GX_INV_TEX ((fn_v)0x8033F270u)
#define TEXT_NEW ((fn_text_new)0x803A6754u)
#define TEXT_FREE ((fn_p)0x803A5CC4u)
#define AUDIO ((void (*)(int))0x80024030u)
#define MN_GOTO ((fn_goto)0x80229894u)
#define MD_GOBJS 0x08u
#define MD_TEXTS 0x2Cu
#define MD_ICONS 0x50u
#define MD_NAME 0x74u
#define MD_DESC 0x78u
#define TX_X 0x00u
#define TX_Y 0x04u
#define TX_Z 0x08u
#define TX_BW 0x0Cu
#define TX_BH 0x10u
#define TX_FX 0x24u
#define TX_FY 0x28u
#define TX_COL 0x30u
#define TX_KERN 0x49u
#define TX_ALIGN 0x4Au

int gnbev_sis(void *t, const char *fmt, const char *s);
void gnbev_madd(float *out, const float *a, const float *b, const float *c);
static const float K[16] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f };
static const float ONE = 1.0f, MINUS1 = -1.0f, ZERO = 0.0f;
static const float F_TITLE_ROWS = -2.2f, F_HINT_ROWS = 12.9f;
/* (the dot offsets are gone: the status is a word now) */
static const float F_BODY[3][3] = { { 11.1f, 0.0f, 0.0f }, { 10.7f, 11.5f, 0.0f }, { 10.15f, 10.95f, 11.75f } };      /* one, two, three body lines at BODY_FONT, pitch 0.8 rows (22 px) in the info box (box 421..521 of the 540 line picture, key line at 512): none touches a border or the key line */                   /* one or two body lines at BODY_FONT: even pitch 0.84 rows, the pair centred in the box body */
#define BODY_FONT 0.030f
#define BODY_DX 0.30f                                                 /* the small left margin of the body lines, units (the first line sat on the box's top line; all inside the box) */
static float lin(float base, unsigned k, float step) { float o, kk = K[k & 15u]; gnbev_madd(&o, &base, &kk, &step); return o; }
static float add(float a, float b) { float o; gnbev_madd(&o, &a, &ONE, &b); return o; }
static float sub(float a, float b) { float o; gnbev_madd(&o, &a, &MINUS1, &b); return o; }
static float mula(float a, float b, float m) { float o; gnbev_madd(&o, &a, &b, &m); return o; }
#define NUM_FONT GNT_GEO_NUM_FONT
#define TITLE_FONT 0.045f
#define ROW_FONT GNT_GEO_ROW_FONT                    /* the name AND the code (one size; 12 characters of name and 8 of code at tp_gw widths fit their columns); the retail box stretch (S.bw / S.bh) applies to both */
#define RULE_FONT 0.028f                    /* the cup rules: up to 38 characters across the list box, with the row stretch like the rows (one text look on the screen) */
#define STATUS_FONT 0.030f                         /* the centred status message of the list ('perform some centering and legibility work'): 1.25 times the panel text */
#define COL_WARN 0xFF8C7AFFu                       /* the warning colour of that message: a lighter coral than COL[3], readable on the dark box */
#define STATUS_CX 9.6f                             /* centre of the box, units right of the number column (the panel is about 18 units wide, as the quick-chat cross) */
static const float STATUS_ROW[3][3] = { { 11.1f, 0.0f, 0.0f }, { 10.7f, 11.5f, 0.0f }, { 10.15f, 10.95f, 11.75f } };   /*: the message lives in the description box below the list, centred on the box (the panel lines F_DESC span 10.8..12.45: centre 11.6), not on the grid */
#define HINT_FONT 0.019f
#define HEAD_HINT_FONT 0.026f            /* "Next in 16m" top right: bigger than the key line and on the title's baseline: the title (0.045, y 60) has its line box 31.5 px tall (baseline about y 85); this one is 18 px tall, so its top sits at 85 - 0.8 * 18.2 */
#define HEAD_HINT_Y 70.5f
#define FOOT_UP_PX 0.0f                  /* Friends keeps y 497.6: the Settings y 464 lands inside the retail description box (y 425..479) and that art is the retail Event screen; one footer helper otherwise (x spread) */
#define CODE_FONT ROW_FONT
#define KEYBOX_CY_PX 456.0f                 /* the list's key box centre y (picture px): visible half height 32 like the title bar, bottom 488, clear of the list box (ends 416) */
#define KEYBOX_BASE_PX 497.6f               /* the picture y that fy (F_HINT_ROWS) stands for */
#define KEYBOX_X0_PX 81.0f                  /* the key block spans the box (x 61..581) minus 20 px each side: 480 px = 1691040 Q16 units (3523 a pixel) */
#define KEYBOX_WQ 1691040
#define KEYBOX_ROW_PX 14.0f                 /* the two key rows sit this far above / below the box centre (28 px pitch, evenly inside the 64 px box) */
#define KEYBOX_UP_PX 62.0f                  /* the list's action key line sits this many picture px above the footer bar's line (box centre y 448, bar y 510) */
static const float ROW_DY_NAME = 0.048f, ROW_DY_CODE = 0.048f;      /* one baseline for the name and the state word (the Friends rows' offsets) */
#define EXT_X GNT_GEO_STATE_DX                      /* the right edge of the state word / score, units right of the number column (the box is about 18.6 units wide) */
static const u32 COL[4] = { 0xFFFFFFFFu, 0x9C9C9CFFu, 0xFFD040FFu, 0xFF6A5AFFu };
#define COL_NUM 0xCABC9FFFu
static const float PYROW = 0.037313f, PXK = 0.05376f, PX0 = -7.634f, PY0 = -152.0f, BOX_DY = 10.0f;     /* the pixel frame of the Game Night room screen (gnb_ev.c): the same measured scale */
/* the Tournament look (the retail Tournament Melee frame and background, not the Friends blue list): GmTou1p's animated background (gart_bg), its rounded translucent boxes
   tinted orange for the title bar and dark for the panels, our rows and texts over them; the retail Event frame (the menu's panel) is not drawn meanwhile.  Slots: boxes 0..7 (no status circle: gold text marks your tournaments). */
enum { AT_HEAD, AT_HEAD_IN, AT_LIST, AT_LIST_IN, AT_INFO, AT_INFO_IN, AT_FOOT, AT_CUR };
#define TV_NEED (1u << GART_BOX)
#define TV_ORANGE 0xE8841CFFu
#define TV_DARK 0x0A0A14FFu
#define TV_CUR 0xE8B020FFu
#define TV_SLOT_ROWS GNT_VIS
#define RETAIL_ROWS 9u                    /* the retail Event list has 9 row slots (MnEventData gobjs / texts / icons); rows beyond use S.xtn / S.xtt */

struct tslot { void *t; char s[48]; u32 col; int tag; };        /* tag: a position stamp (the key line's texts: a key that moves is made again); 0 = none */
#define KX_N 32u                        /* the key lines' slots: per line the four action texts, then three a key (letter 0, letter 1, Hold prefix); the list has two lines (action keys above the footer bar) */
static struct {
    int state;
    unsigned err;
    struct gnt *f;
    u32 ret_kind, ret_row;
    void *orig, *think, *scr;
    u8 *md;
    float nx, ny0, nz, tx, dy, bw, bh, sx, sy, cur_dy;
    char rnum[GNT_VIS][GNT_NUM + 1], rtit[GNT_VIS][GNT_TITLE + 1];
    u32 rcol[GNT_VIS];
    struct tslot pan[GNT_PANEL], title, hint, sub, leg[4], box[3], X[KX_N], rcode[GNT_VIS], rstar[GNT_VIS], arl, arr, tpl, ttx;     /* tpl / ttx: the result toast's plate and line */
    u32 rdot[GNT_VIS];
    void *xtn[GNT_VIS > 9u ? GNT_VIS - 9u : 1u], *xtt[GNT_VIS > 9u ? GNT_VIS - 9u : 1u];     /* row texts of the rows past the retail nine */
    u8 shown_page, shown_first, shown_scr, want_exit;
    u8 blink;
    u16 tick;
    u32 subj[4];                                   /* the panel DObjs hidden for the retail "SUBJECT TABLE" lettering */
    u8 subj_was[4], nsubj;
    unsigned art;                                  /* gart_open mask (the Tournament look needs the box) */
    float tx0, ty0, pyk;                           /* screen pixels (640 x 528) to text coordinates: tx = tx0 + px * PXK, ty = ty0 + py * pyk (gnb_ev.c) */
    u8 *tab_dat[2], *tab_save[2];                  /* the live tab image data we overwrote and the retail pixels we hold */
    struct tslot tn[7][2], ts[7][2], tc[3];        /* the tree: the names, the scores and the column titles (7 plates: 4 + 2 + 1) */
    u16 tree_t, tree_sig;                          /* the tree's reveal clock (frames since the window last changed) and the window's signature */
    u8 tree_on;                                    /* the tree pieces are up (so they are hidden again when the rows come back) */
} S;
static float TX(float px) { return mula(S.tx0, px, PXK); }
static float TY(float py) { return mula(S.ty0, py, S.pyk); }
static int art_on(void) { return S.art && (S.art & TV_NEED) == TV_NEED; }
/* a box centred at text (tx, ty); bw, bh = the corner bones' half sizes in pixels (visible half size - 15); sy scales the box vertically (thin bars) */
static void art_box_t(unsigned slot, float tx, float ty, float bw, float bh, float sy, unsigned rgb, float dz)
{
    gart_put(slot, GART_BOX, tx, sub(ZERO, mula(ty, BOX_DY, S.pyk)), add(S.nz, dz), mula(ZERO, bw, PXK), mula(ZERO, bh, S.pyk), rgb, ONE);
    gart_scale(slot, ONE, sy);
}
static void art_box(unsigned slot, float cx, float cy, float bw, float bh, unsigned rgb, float dz) { art_box_t(slot, TX(cx), TY(cy), bw, bh, ONE, rgb, dz); }

static void zero(void *p, unsigned n) { u8 *b = (u8 *)p; while (n--) *b++ = 0; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void copy(char *d, const char *s, unsigned max) { unsigned i = 0; while (s[i] && i < max) { d[i] = s[i]; i++; } d[i] = 0; }

static void *find_proc(u32 cb, u32 **slot)
{
    void **heads = (void **)W(PLINK_HEAD);
    u32 n = (u32)B(GOBJ_INIT) + 1u, p;
    if (!heads) return 0;
    if (n > 64u) n = 64u;
    for (p = 0; p < n; p++) {
        u8 *go = (u8 *)heads[p];
        u32 guard = 0;
        while (go && guard++ < 4096u) {
            u8 *pr = (u8 *)W((u32)go + 0x18u);
            u32 g2 = 0;
            while (pr && g2++ < 64u) {
                if (W((u32)pr + 0x14u) == cb) { if (slot) *slot = (u32 *)(pr + 0x14); return go; }
                pr = (u8 *)W((u32)pr);
            }
            go = (u8 *)W((u32)go + 0x08u);
        }
    }
    return 0;
}
static void *joint(u32 idx)
{
    void *j = 0;
    if (!S.scr) return 0;
    (void)LB_JOBJ((void *)W((u32)S.scr + 0x28u), &j, (int)idx, -1);
    return j;
}
static void joint_hide(u32 idx, int hide)
{
    void *j = joint(idx);
    if (!j) return;
    if (hide) JOBJ_SETFLAGS(j, JOBJ_HIDDEN); else JOBJ_CLRFLAGS(j, JOBJ_HIDDEN);
}
static void joint_xy(u32 idx, float x, float y)
{
    u8 *j = (u8 *)joint(idx);
    u32 fl;
    if (!j) return;
    F(j + 0x38) = x; F(j + 0x3C) = y;
    fl = W(j + 0x14);
    if (!(fl & JOBJ_MTX_INDEP_SRT) && !(!(fl & JOBJ_USER_DEF_MTX) && (fl & JOBJ_MTX_DIRTY))) JOBJ_DIRTYSUB(j);
}
static void joint_y(u32 idx, float y)
{
    u8 *j = (u8 *)joint(idx);
    u32 fl;
    if (!j) return;
    F(j + 0x3C) = y;
    fl = W(j + 0x14);
    if (!(fl & JOBJ_MTX_INDEP_SRT) && !(!(fl & JOBJ_USER_DEF_MTX) && (fl & JOBJ_MTX_DIRTY))) JOBJ_DIRTYSUB(j);
}
static void *text_at(float x, float y, float font, u32 col, int align)
{
    u8 *t = (u8 *)TEXT_NEW(0, CANVAS);
    if (!t) return 0;
    F(t + TX_X) = x; F(t + TX_Y) = y; F(t + TX_Z) = S.nz;
    F(t + TX_FX) = font; F(t + TX_FY) = font;
    W(t + TX_COL) = col;
    B(t + TX_KERN) = 1;
    B(t + TX_ALIGN) = (u8)align;
    return t;
}
static const char SJ_CHR[] = "!?;()[]+/%#&*@$=<>~|_^{}";
static const unsigned char SJ_LO[] = { 0x49, 0x48, 0x47, 0x69, 0x6A, 0x6D, 0x6E, 0x7B, 0x5E, 0x93, 0x94, 0x95, 0x96, 0x97, 0x90, 0x81, 0x83, 0x84, 0x60, 0x62, 0x51, 0x4F, 0x6F, 0x70 };
static void sis_print(void *t, const char *in)
{
    char out[100];
    unsigned n = 0, i, j;
    for (i = 0; in[i] && n + 3u < sizeof out; i++) {
        int k = -1;
        for (j = 0; j < sizeof SJ_CHR - 1u; j++) if (SJ_CHR[j] == in[i]) k = (int)j;
        if (k < 0) out[n++] = in[i]; else { out[n++] = (char)0x81; out[n++] = (char)SJ_LO[k]; }
    }
    out[n] = 0;
    gnbev_sis(t, "%s", out);
}
static void tfree(void **p) { if (*p) TEXT_FREE(*p); *p = 0; }
static u32 kev_made;
static int kev_tag;
static int ts_big;                                                      /* 1: the next tslot_set text gets the retail box stretch like the row names (S.bw / S.bh): the same look at the same font */
static void tslot_set(struct tslot *s, float x, float y, float font, u32 col, int align, const char *str)
{
    if (s->t && same(s->s, str) && s->col == col && s->tag == kev_tag) return;
    tfree(&s->t);
    copy(s->s, str, 47);
    s->col = col;
    s->tag = kev_tag;
    if (!str[0]) return;
    s->t = text_at(x, y, font, col, align);
    if (s->t && ts_big) { F((u8 *)s->t + TX_BW) = S.bw; F((u8 *)s->t + TX_BH) = S.bh; }
    if (s->t) { sis_print(s->t, str); kev_made = 1; }
}
#define KEV_SLOT(i) (S.X[i])
static void kev_xs(unsigned i, float x, float y, float font, u32 col, int align, const char *str) { tslot_set(&S.X[i], x, y, font, col, align, str); }
#include "keyhint_ev.h"

/* ---- drawing: rows, description box, small box, cursor, key line ---- */
/* the dailies row's star: halfway between the end of "Multishine dailies" and the start of the right-aligned text (its right edge 20.3, about 0.343 a character).  A fixed 16.6 fitted
   "33m 1/5" only: "Called off" ran into it.  A table by text length: the overlay has no int-to-float */
static float star_daily_dx(const char *ext)
{
    static const float DX[16] = { 17.80f, 17.63f, 17.46f, 17.29f, 17.11f, 16.94f, 16.77f, 16.60f, 16.43f, 16.26f, 16.09f, 15.91f, 15.74f, 15.57f, 15.40f, 15.23f };
    unsigned n = 0;
    while (ext[n] && n < 15u) n++;
    return DX[n];
}

static void row_draw(unsigned i, int force)
{
    char num[GNT_NUM + 1], tit[GNT_TITLE + 1], ext[GNT_EXTRA + 4], star[2] = { '*', 0 };       /* the star: the menu font's '*' (full-width via sis_print), gold, in a column of its own between the name and the state word */
    unsigned a = gnt_row(S.f, i, num, tit, ext);
    u32 col = COL[(a ? a - 1u : 0u) & 3u];
    void **tn, **tt;
    float y = lin(S.ny0, i, S.dy);
    if (S.f->scr == GNT_CREATE || S.f->scr == GNT_JOIN) y = add(y, mula(ZERO, -GNT_GEO_FORM_SHIFT_PX, S.pyk));      /* the forms: the 9-row block sits centred in the list-sized box (equal margins) */
    if (S.f->scr == GNT_RULES) {                                           /* the cup rules: one line per row, smaller (no box stretch) */
        a = gnt_rules(S.f, i, tit) ? (i == 0u ? 3u : 1u) : 0u;
        col = COL[(a ? a - 1u : 0u) & 3u];
        num[0] = 0; ext[0] = 0;
        if (!art_on()) tit[0] = 0;
    }
    if (i >= evo_rows()) { num[0] = 0; tit[0] = 0; ext[0] = 0; }          /* not revealed yet (evo.h) */
    if (i < RETAIL_ROWS) { tn = (void **)(S.md + MD_TEXTS + 4u * i); tt = (void **)(S.md + MD_ICONS + 4u * i); }
    else { tn = &S.xtn[i - RETAIL_ROWS]; tt = &S.xtt[i - RETAIL_ROWS]; }                         /* rows past the retail nine: our own text slots (the retail arrays end at +0x74) */
    if (i >= evo_rows() || i >= gnt_vis(S.f) || !gnt_row_star(S.f, i)) star[0] = 0;      /* (a slot past the window draws nothing, not even a star) */
    if (!force && same(num, S.rnum[i]) && same(tit, S.rtit[i]) && col == S.rcol[i] && same(ext, S.rcode[i].s) && same(star, S.rstar[i].s)) return;
    tfree(tn); tfree(tt);
    copy(S.rnum[i], num, GNT_NUM); copy(S.rtit[i], tit, GNT_TITLE); S.rcol[i] = col;
    if (num[0]) { u8 *t = (u8 *)text_at(add(S.nx, GNT_GEO_NUM_DX), mula(y, S.dy, ROW_DY_NAME), NUM_FONT, COL_NUM, 0); *tn = t; if (t) { F(t + TX_BW) = S.bw; F(t + TX_BH) = S.bh; sis_print(t, num); } }      /* the count: the name's font, stretch and baseline (one size, one baseline per row) */
    if (tit[0] && S.f->scr == GNT_RULES) { u8 *t = (u8 *)text_at(add(S.nx, -1.2f), mula(y, S.dy, ROW_DY_NAME), RULE_FONT, col, 0); *tt = t; if (t) { F(t + TX_BW) = S.bw; F(t + TX_BH) = S.bh; sis_print(t, tit); } }
    else if (tit[0]) { u8 *t = (u8 *)text_at(add(S.nx, (S.f->scr == GNT_CREATE || S.f->scr == GNT_JOIN || S.f->scr == GNT_PAGE) ? GNT_GEO_NUM_DX : GNT_GEO_NAME_DX), mula(y, S.dy, ROW_DY_NAME), ROW_FONT, col, 0); *tt = t; if (t) { F(t + TX_BW) = S.bw; F(t + TX_BH) = S.bh; sis_print(t, tit); } }
    kev_tag = 0;
    ts_big = 1;
    tslot_set(&S.rcode[i], add(S.nx, EXT_X), mula(y, S.dy, ROW_DY_CODE), CODE_FONT, COL[1], 2, ext); ts_big = 0;      /* the state word / the score, dimmed, right aligned at the box's right edge */
    tslot_set(&S.rstar[i], add(S.nx, gnt_row_cycle(S.f, i) ? star_daily_dx(ext) : GNT_GEO_STAR_DX), mula(y, S.dy, ROW_DY_CODE), CODE_FONT, COL[2], 1, star);      /* enrolled: a gold star (same font and baseline as the row) */
    if (i == 0u) {                                                       /* the dailies row: '<' '>' OUTSIDE the select box show that Left Right cycle the dailies (same colour and baseline as the row) */
        int on = i < evo_rows() && gnt_row_cycle(S.f, i);
        float ay = add(mula(y, S.dy, ROW_DY_CODE), GNT_GEO_ARR_DY);
        tslot_set(&S.arl, add(S.nx, GNT_GEO_ARR_L_DX), ay, GNT_GEO_ARR_FONT, TV_CUR, 2, on ? "<" : "");      /* outside the select box, in its yellow, twice as big */
        tslot_set(&S.arr, add(S.nx, GNT_GEO_ARR_R_DX), ay, GNT_GEO_ARR_FONT, TV_CUR, 0, on ? ">" : "");
    }
}
static void panel_draw(void)
{
    unsigned i, sv = evo_chrome() ? gnt_status_view(S.f) : 0u;
    char ln[64];
    if (sv) {                                                            /* a status message of the list: centred in the box, bigger */
        for (i = 0; i < GNT_PANEL; i++) {
            unsigned a = gnt_panel(S.f, i, ln);
            u32 col = (a == 4u) ? COL_WARN : COL[0];
            if (i >= sv) ln[0] = 0;
            if (i == 0u) {
                void **d = (void **)(S.md + MD_DESC);
                S.pan[0].t = *d;
                if (!ln[0]) { ln[0] = ' '; ln[1] = 0; }
                tslot_set(&S.pan[0], add(S.nx, STATUS_CX), mula(S.ny0, STATUS_ROW[sv - 1u][0], S.dy), STATUS_FONT, col, 1, ln);
                *d = S.pan[0].t;
            } else
                tslot_set(&S.pan[i], add(S.nx, STATUS_CX), mula(S.ny0, STATUS_ROW[sv - 1u][i < 3u ? i : 2u], S.dy), STATUS_FONT, col, 1, ln);
        }
        return;
    }
    {   /* the body of the description box: the non-empty lines packed from the top, one or two lines large */
        char L[GNT_PANEL][GNT_LINE + 12];
        unsigned A[GNT_PANEL], m = 0;
        for (i = 0; i < GNT_PANEL; i++) {
            unsigned a = gnt_panel(S.f, i, ln);
            if (evo_chrome() && S.f->scr != GNT_LIST && ln[0] && ln[0] != ' ' && m < 3u) {         /* (the list's box is the key line: every row shows its own information) */ copy(L[m], ln, GNT_LINE + 10); A[m] = a; m++; }
        }
        for (i = 0; i < GNT_PANEL; i++) {
            float y = mula(S.ny0, m && i < m ? F_BODY[m - 1u][i] : F_BODY[2][2], S.dy), x = add(S.nx, BODY_DX);
            char one[GNT_LINE + 12];
            u32 col = (i < m && A[i] == 4u) ? COL_WARN : (i < m && A[i] == 3u) ? COL[2] : (i < m && A[i] == 2u) ? COL[1] : COL[0];       /* warning coral, yours gold, secondary grey */
            copy(one, i < m ? L[i] : "", GNT_LINE + 10);
            ts_big = 1; kev_tag = 7 + (int)m;                                  /* one size for every body line, re-made when the line count (so the positions) changes */
            if (i == 0u) {                                               /* line 0 lives in the retail description slot (the retail exit frees it: never NULL there) */
                void **d = (void **)(S.md + MD_DESC);
                S.pan[0].t = *d;
                if (!one[0]) { one[0] = ' '; one[1] = 0; }
                tslot_set(&S.pan[0], x, y, BODY_FONT, col, 0, one);
                *d = S.pan[0].t;
            } else
                tslot_set(&S.pan[i], x, y, BODY_FONT, col, 0, one);
            ts_big = 0; kev_tag = 0;
        }
    }
}
static void badge_draw(void)
{
    void **n = (void **)(S.md + MD_NAME);
    struct tslot s;
    s.t = *n; copy(s.s, S.sub.s, 47); s.col = S.sub.col; s.tag = 0;                 /* the retail score box slot (freed unconditionally by the retail exit) */
    tslot_set(&s, S.sx, S.sy, 0.03f, COL[0], 2, " ");
    *n = s.t; copy(S.sub.s, s.s, 47); S.sub.col = s.col;
}
static void box_draw(void)
{
    if (art_on()) { unsigned k; for (k = 0; k < 3u; k++) tslot_set(&S.box[k], 0.0f, 0.0f, 0.02f, COL[0], 1, ""); return; }   /* the small black box at the bottom right: three centred lines ("5", "of 16", "entered"; in the bracket "2", "of 4", "rounds") */
    static const float DX = 8.15f, ROWS[3] = { 0.5f, 2.0f, 2.4f }, FNT[3] = { 0.05f, 0.02f, 0.024f };
    unsigned k;
    for (k = 0; k < 3u; k++) {
        char ln[12];
        (void)gnt_box(S.f, k, ln);
        if (!evo_chrome()) ln[0] = 0;
        tslot_set(&S.box[k], add(S.sx, DX), mula(S.sy, ROWS[k], S.dy), FNT[k], COL[0], 1, ln);
    }
}
static void cursor_draw(void)
{
    unsigned n = gnt_rows(S.f), pg = gnt_cur_page(S.f), first = gnt_cur_first(S.f);
    joint_y(0xBu, lin(ZERO, pg, S.cur_dy));
    joint_hide(0xBu, n == 0u || art_on());                                   /* the Tournament look draws its own cursor bar */
    if (art_on()) {                                                          /* the Tournament look: the retail Event scroll triangles (joints 3 up, 2 down, parent 1) at the box's left inside edge, top and bottom corner, like the Game Night list (gnb_ev.c); only with rows above / below */
        unsigned more = evo_chrome() ? gnt_scroll_more(S.f) : 0u;
        joint_xy(1u, TX(GNT_GEO_SCROLL_X_PX), sub(ZERO, mula(TY(GNT_GEO_FORM_CY_PX), BOX_DY, S.pyk)));
        joint_xy(3u, ZERO, mula(ZERO, GNT_GEO_SCROLL_DY, S.pyk));
        joint_xy(2u, ZERO, mula(ZERO, -GNT_GEO_SCROLL_DY, S.pyk));
        joint_hide(1u, 0);
        joint_hide(3u, !(more & 1u));
        joint_hide(2u, !(more & 2u));
    } else {
        joint_hide(3u, first == 0u);
        joint_hide(2u, first >= gnt_max_first(S.f));
    }
    S.shown_page = (u8)pg; S.shown_first = (u8)first; S.shown_scr = S.f->scr;
}

/* ---- the bracket as a tree: plates and lines drawn with the Tournament look's own box art, the names as texts ----
   The window (gnt_tree.c) is up to three columns of plates: 4 + 2 + 1 for a winners pod.  A plate is an outer frame (its colour says the state; the cursor's is gold and bigger) and a dark inner
   (gart slots TP_OUT + p, TP_IN + p); a line is a box scaled to a 2 px bar (slots TL_0 ..).  Positions come from constant tables and the float helpers (the console build has no float arithmetic). */
#define TP_OUT 8u
#define TP_IN 15u
#define TL_0 22u
#define TL_N 24u
static const float TR_CX[3] = { 160.0f, 320.0f, 480.0f };                              /* plate centres, picture px (plates are 140 px wide: gaps of 20 px for the lines) */
static const float TR_Y[9] = { 156.0f, 183.0f, 210.0f, 237.0f, 264.0f, 291.0f, 318.0f, 345.0f, 372.0f };       /* the plate area in eighths: a plate at eighth e is centred on TR_Y[e] (4 plates: 183 237 291 345) */
static const float TR_VS[9] = { 0.0f, 0.9f, 1.8f, 2.7f, 3.6f, 4.5f, 5.4f, 6.3f, 7.2f };      /* a vertical bar of k eighths (27 px each) as the scale of the 30 px box */
#define TR_HS 0.34f                                                                     /* a 10 px stub */
#define TR_THICK 0.1f                                                                   /* 3 px */
#define TR_NAME_FONT 0.024f
#define TR_HEAD_FONT 0.021f
#define TR_COL_GAP 10u                                                                  /* reveal frames between columns */
#define COL_LINE 0xB4B4CCFFu
#define COL_LINE_ON 0xE8B020FFu
/* The plates and lines sit at depths (dz) in front of the texts and the camera is a perspective one: a piece drawn at picture x lands further from the picture's centre the deeper it is
   (gnb_prep_ev.h pr_ax: 1 / (1 - 0.03 dz)).  Uncorrected, the plates of the outer columns stood 10 px off their names (a 4 cup tree: left margins 23, 12 and 2 px in the
   three columns, the top plate above its lines of text and the bottom one below).  A piece is asked for the point pulled in by (1 - 0.03 dz) * 1.011 about (320, 264), the texts' own
   small factor measured on that capture. */
static float tr_k(float dz) { return mula(ZERO, sub(1.0f, mula(ZERO, 0.03f, dz)), 1.011f); }
static float tr_ax(float px, float dz) { return add(320.0f, mula(ZERO, sub(px, 320.0f), tr_k(dz))); }
static float tr_ay(float py, float dz) { return add(264.0f, mula(ZERO, sub(py, 264.0f), tr_k(dz))); }
static void tr_bar(unsigned slot, float cx, float cy, float sx, float sy, unsigned rgb, float dz)
{
    cx = tr_ax(cx, dz); cy = tr_ay(cy, dz);
    gart_put(slot, GART_BOX, TX(cx), sub(ZERO, mula(TY(cy), BOX_DY, S.pyk)), add(S.nz, dz), ZERO, ZERO, rgb, ONE);
    gart_scale(slot, sx, sy);
}
static void tree_free(void)
{
    unsigned c, k;
    for (c = 0; c < 7u; c++) for (k = 0; k < 2u; k++) { tfree(&S.tn[c][k].t); tfree(&S.ts[c][k].t); S.tn[c][k].s[0] = 0; S.ts[c][k].s[0] = 0; }
    for (c = 0; c < 3u; c++) { tfree(&S.tc[c].t); S.tc[c].s[0] = 0; }
}
static void tree_hide(void)
{
    unsigned i;
    if (!S.tree_on) return;
    for (i = TP_OUT; i < TL_0 + TL_N; i++) gart_hide(i);
    tree_free();
    S.tree_on = 0;
}
static unsigned tree_animating(void) { return S.tree_on && S.tree_t < 3u * TR_COL_GAP + 14u; }
static void tree_draw(void)
{
    struct gnt_tw w;
    unsigned c, k, nl = 0, sig, blink, i;
    u32 st = 1;
    if (!gnt_tree_layout(S.f, &w)) { tree_hide(); return; }
    S.tree_on = 1;
    sig = (unsigned)w.side[0] * 4096u + (unsigned)w.rnd[0] * 128u + (unsigned)w.pod;
    if (sig != S.tree_sig) { S.tree_sig = (u16)sig; S.tree_t = 0; }
    else if (S.tree_t < 0xFFF0u) S.tree_t++;
    blink = (S.tick >> 4) & 1u;
    for (c = 0; c < w.nc; c++) for (k = 0; k < w.n[c]; k++) st = st * 31u + w.y[c][k] + 7u * c;
    st += (unsigned)w.nc * 977u;
    for (i = TL_0; i < TL_0 + TL_N; i++) gart_hide(i);
    for (c = 0; c < GNT_TW_COLS; c++) {
        unsigned vis = 0;
        if (c < w.nc && S.tree_t >= c * TR_COL_GAP) { vis = 1u + (S.tree_t - c * TR_COL_GAP) / 3u; if (vis > 4u) vis = 4u; }
        kev_tag = (int)st; ts_big = 0;
        if (c < w.nc && vis) { char t[32]; gnt_round_title(S.f, w.side[c], w.rnd[c], t); tslot_set(&S.tc[c], TX(TR_CX[c]), TY(133.0f), TR_HEAD_FONT, COL[2], 1, t); }
        else tslot_set(&S.tc[c], 0.0f, 0.0f, TR_HEAD_FONT, COL[1], 1, "");
        for (k = 0; k < 4u; k++) {
            unsigned slot = (c == 0u) ? k : (c == 1u) ? 4u + k : 6u;
            if ((c == 1u && k >= 2u) || (c == 2u && k >= 1u)) continue;
            if (c < w.nc && k < w.n[c] && vis) {
                const struct gnt_match *m = &S.f->b_m[w.mi[c][k]];
                int cur = (c == 0u && k == w.cur);
                unsigned mine = S.f->b_me != GNT_NONE && (m->a == S.f->b_me || m->b == S.f->b_me), side;
                unsigned fin = m->state == GNT_M_DONE || m->state == GNT_M_BYE || m->state == GNT_M_WALK;
                u32 rgb = (m->state == GNT_M_CALLED) ? (blink ? TV_CUR : TV_ORANGE) : (m->state == GNT_M_LIVE) ? 0xC86A10FFu : mine ? 0x8A6A14FFu : fin ? 0x4A3A22FFu : 0x6A4A1CFFu;
                float ey = TR_Y[w.y[c][k] & 15u], cx = TR_CX[c];
                if (cur) art_box_t(TP_OUT + slot, TX(tr_ax(cx, 2.4f)), TY(tr_ay(ey, 2.4f)), 59.0f, 11.0f, ONE, TV_CUR, 2.4f);
                else art_box_t(TP_OUT + slot, TX(tr_ax(cx, 2.4f)), TY(tr_ay(ey, 2.4f)), 55.0f, 7.0f, ONE, rgb, 2.4f);
                art_box_t(TP_IN + slot, TX(tr_ax(cx, 2.0f)), TY(tr_ay(ey, 2.0f)), 51.0f, 3.0f, ONE, TV_DARK, 2.0f);
                for (side = 0; side < 2u; side++) {                                           /* the two name lines and their scores */
                    unsigned ent = side ? m->b : m->a, won = m->state == GNT_M_DONE && m->win == (side ? 2u : 1u), lost = m->state == GNT_M_DONE && m->win && !won;
                    char sc[4];
                    const char *nm = ent == GNT_NONE ? (m->state == GNT_M_BYE ? "Bye" : "?") : S.f->b_name[ent];
                    u32 col = (ent == GNT_NONE || lost) ? COL[1] : (S.f->b_me != GNT_NONE && ent == S.f->b_me) ? COL[2] : COL[0];
                    float ty = TY(add(ey, side ? 11.5f : -10.5f));      /* ("needs cleanup"): the two lines sat 10.7 px high, the upper name on the plate's top edge (cube capture: text centre 12 px above the plate's); the scores come 6 px in off the right border */
                    unsigned wins = side ? m->wb : m->wa;
                    sc[0] = 0;
                    if ((m->state == GNT_M_DONE || m->state == GNT_M_LIVE) && ent != GNT_NONE) { sc[0] = (char)('0' + (wins % 10u)); sc[1] = 0; }
                    tslot_set(&S.tn[slot][side], TX(add(cx, -62.0f)), ty, TR_NAME_FONT, col, 0, nm);
                    tslot_set(&S.ts[slot][side], TX(add(cx, 56.0f)), ty, TR_NAME_FONT, won ? COL[2] : COL[1], 2, sc);
                }
            } else {
                gart_hide(TP_OUT + slot); gart_hide(TP_IN + slot);
                tslot_set(&S.tn[slot][0], 0.0f, 0.0f, TR_NAME_FONT, COL[1], 0, ""); tslot_set(&S.tn[slot][1], 0.0f, 0.0f, TR_NAME_FONT, COL[1], 0, "");
                tslot_set(&S.ts[slot][0], 0.0f, 0.0f, TR_NAME_FONT, COL[1], 0, ""); tslot_set(&S.ts[slot][1], 0.0f, 0.0f, TR_NAME_FONT, COL[1], 0, "");
            }
        }
    }
    kev_tag = 0;
    for (c = 0; c < w.nc; c++) {                                                              /* the lines: a stub out of every plate with a successor, the bar joining a pair, the stub into the successor */
        unsigned vis = S.tree_t >= (c + 1u) * TR_COL_GAP;
        float x0 = add(TR_CX[c], 70.0f);
        if (!vis && c + 1u < w.nc) continue;
        for (k = 0; k < w.n[c]; k++) {
            const struct gnt_match *m = &S.f->b_m[w.mi[c][k]];
            unsigned on = m->state == GNT_M_DONE || m->state == GNT_M_WALK || m->state == GNT_M_BYE;
            u32 lc = on ? COL_LINE_ON : COL_LINE;
            if (c + 1u < w.nc && w.lk[c][k] != 0xFFu) {
                unsigned j = w.lk[c][k], q, lo = 255u, hi = 0u, anyon = 0;
                if (nl < TL_N) tr_bar(TL_0 + nl++, add(x0, 5.0f), TR_Y[w.y[c][k] & 15u], TR_HS, TR_THICK, lc, 1.6f);
                for (q = 0; q < w.n[c]; q++) if (w.lk[c][q] == j) { const struct gnt_match *f = &S.f->b_m[w.mi[c][q]]; if (w.y[c][q] < lo) lo = w.y[c][q]; if (w.y[c][q] > hi) hi = w.y[c][q]; if (f->state == GNT_M_DONE || f->state == GNT_M_WALK || f->state == GNT_M_BYE) anyon = 1; }
                if (w.y[c][k] == lo && hi > lo && nl < TL_N) tr_bar(TL_0 + nl++, add(x0, 10.0f), TR_Y[((lo + hi) / 2u) & 15u], TR_THICK, TR_VS[(hi - lo) & 15u], anyon ? COL_LINE_ON : COL_LINE, 1.6f);
                if (w.y[c][k] == lo && nl < TL_N) tr_bar(TL_0 + nl++, add(x0, 15.0f), TR_Y[w.y[c + 1u][j] & 15u], TR_HS, TR_THICK, anyon ? COL_LINE_ON : COL_LINE, 1.6f);
            } else if (c + 1u == w.nc && w.more && nl < TL_N) tr_bar(TL_0 + nl++, add(x0, 6.0f), TR_Y[w.y[c][k] & 15u], TR_HS, TR_THICK, lc, 1.6f);
        }
    }
}
static void art_hide_all(void) { unsigned i; for (i = 0; i <= AT_CUR; i++) gart_hide(i); tree_hide(); }
static void art_draw(void)
{
    struct gnt *f = S.f;
    unsigned n = gnt_rows(f), sv = gnt_status_view(f), pg = gnt_cur_page(f);
    char hd[48], ln[12];
    unsigned keybox = f->scr == GNT_LIST && !sv;
    gart_menu_panel(0);
    (void)gart_bg(1);
    art_box(AT_HEAD, 321.0f, 76.0f, 245.0f, 17.0f, TV_ORANGE, 2.4f);                            /* the title bar: orange frame, dark bar inside */
    art_box(AT_HEAD_IN, 321.0f, 76.0f, 241.0f, 13.0f, TV_DARK, 2.0f);
    if (f->scr == GNT_CREATE || f->scr == GNT_JOIN) {                                             /* the forms: 9 rows (cursor centres 157.36 + 25.46 k, k 0..8: 157..361, span 144.6..373.7, centre 259.2) in a box of their own with equal margins: visible 129..389.4 (half 130.2) */
        art_box(AT_LIST, 321.0f, 254.0f, 245.0f, 123.0f, TV_ORANGE, 2.4f);                          /* the list's own box (visible 116..392: 8 px below the title bar, 8 px above the info box), the rows centred in it */
        art_box(AT_LIST_IN, 321.0f, 254.0f, 241.0f, 119.0f, TV_DARK, 2.0f);
    } else {
    art_box(AT_LIST, 321.0f, keybox ? 266.0f : 254.0f, 245.0f, keybox ? 135.0f : 123.0f, TV_ORANGE, 2.4f);          /* the list: down to just above the key box (10 rows); the other screens keep the box that ends above their info box (y 400) */                          /* the list panel: ends right under row 9 (it was 25 px longer than its rows) */
    art_box(AT_LIST_IN, 321.0f, keybox ? 266.0f : 254.0f, 241.0f, keybox ? 131.0f : 119.0f, TV_DARK, 2.0f);
    }
    if (keybox) {                                                                                  /* the list: the box under it holds the action keys, one line (the info text is gone, every row says its own) */
        art_box(AT_INFO, 321.0f, KEYBOX_CY_PX, 245.0f, 17.0f, TV_ORANGE, 2.4f);                    /* same panel size as the title bar (half sizes 245 x 17, inner 241 x 13): both key lines sit inside */
        art_box(AT_INFO_IN, 321.0f, KEYBOX_CY_PX, 241.0f, 13.0f, TV_DARK, 2.0f);
    } else {
    art_box(AT_INFO, 321.0f, 445.0f, 245.0f, 30.0f, TV_ORANGE, 2.4f);                           /* the info box under it: 12 px taller at the top, three body lines clear of both borders and the key line */
    art_box(AT_INFO_IN, 321.0f, 445.0f, 241.0f, 26.0f, TV_DARK, 2.0f);
    }
    if (keybox) gart_hide(AT_FOOT);                                                                /* the list's keys are inside the box, no bar */
    else art_box_t(AT_FOOT, TX(321.0f), TY(510.0f), 245.0f, 0.0f, 0.9f, TV_DARK, 2.0f);        /* the key line's bar */
    if (n && !sv && f->scr != GNT_RULES && !gnt_tree_on(f)) art_box_t(AT_CUR, TX(GNT_GEO_CUR_X_PX), lin(TY((f->scr == GNT_CREATE || f->scr == GNT_JOIN) ? GNT_GEO_CUR_Y0_PX - GNT_GEO_FORM_SHIFT_PX : GNT_GEO_CUR_Y0_PX), pg, mula(ZERO, GNT_GEO_CUR_PITCH_PX, S.pyk)), GNT_GEO_CUR_HALF_W_PX, 0.0f, 0.85f, TV_CUR, 1.6f);
    else gart_hide(AT_CUR);
    if (gnt_tree_on(f)) tree_draw(); else tree_hide();
    hd[0] = 0;
    if (f->scr == GNT_LIST) {
        if (f->say[0]) copy(hd, f->say, 39);                                                       /* the answer of Z (the info box that held it is the key line now) */
        else if (f->next_cup != 0xFFFFFFFFul && f->net == GNT_N_OK) { copy(hd, "Next in ", 14); gnt_fmt_secs(hd + 8, f->next_cup); }
    }
    else if (f->scr == GNT_RULES) copy(hd, "Official", 14);
    else if (f->scr == GNT_PAGE) gnt_state_text(f, hd);
    else if (f->scr == GNT_BRACKET) { unsigned k, u = 0; for (k = 0; k < 3u; k++) { (void)gnt_box(f, k, ln); if (ln[0] && u + 10u < sizeof hd) { unsigned j = 0; if (u) hd[u++] = ' '; while (ln[j] && u + 1u < sizeof hd) hd[u++] = ln[j++]; hd[u] = 0; } } }
    if (gnt_tree_on(f)) {                                                                          /* the tree: which pod of the round is shown ("Pod 2 of 8") */
        unsigned rows = gnt_rows(f), pods = (rows + 3u) / 4u, pod = f->b_sel / 4u + 1u, u = 0, v;
        char num[12];
        if (pods > 1u) {
            while (hd[u]) u++;
            if (u && u + 14u < sizeof hd) hd[u++] = ' ';
            if (u + 13u < sizeof hd) {
                const char *w = "Pod ";
                while (*w) hd[u++] = *w++;
                for (v = 0; pod; v++) { num[v] = (char)('0' + pod % 10u); pod /= 10u; }
                while (v) hd[u++] = num[--v];
                w = " of ";
                while (*w) hd[u++] = *w++;
                for (v = 0; pods; v++) { num[v] = (char)('0' + pods % 10u); pods /= 10u; }
                while (v) hd[u++] = num[--v];
                hd[u] = 0;
            }
        }
    }
    tslot_set(&S.title, TX(84.0f), TY(60.0f), TITLE_FONT, COL[2], 0, evo_chrome() ? gnt_title(f) : "");
    tslot_set(&S.hint, TX(556.0f), TY(HEAD_HINT_Y), HEAD_HINT_FONT, COL[1], 2, evo_chrome() ? hd : "");
}
static void draw_all(int force)
{
    unsigned i;
    for (i = 0; i < GNT_VIS; i++) row_draw(i, force);   /* (a form's slots past its 9 rows get an empty gnt_row) */
    if (art_on()) art_draw();
    else art_hide_all();
    panel_draw();
    badge_draw();
    box_draw();
    joint_hide(4u, 1); joint_hide(6u, 1);
    cursor_draw();
    if (!art_on()) tslot_set(&S.title, S.nx, mula(S.ny0, F_TITLE_ROWS, S.dy), TITLE_FONT, COL[2], 0, "");   /* no gold title: the tab says Tournament, the panel says what the screen is */
    {
        static const unsigned KA[4] = { 0u, 1u, 2u, 3u };
        if (!art_on()) tslot_set(&S.hint, S.nx, mula(S.ny0, F_HINT_ROWS, S.dy), HINT_FONT, COL[1], 0, "");
        static const unsigned KB[4] = { 16u, 17u, 18u, 19u };
        float fy = sub(mula(S.ny0, F_HINT_ROWS, S.dy), mula(ZERO, FOOT_UP_PX, PYROW));
        int kb = art_on() && S.f->scr == GNT_LIST && !gnt_status_view(S.f);
        float ya = kb ? sub(fy, mula(ZERO, KEYBOX_BASE_PX - (KEYBOX_CY_PX + KEYBOX_ROW_PX), PYROW)) : fy;           /* the list: two rows of three keys inside the key box, symmetric about its centre (fy sits at y 497.6, not at the bar's 510: the first cube shot had the block 12 px high) */
        float yb = sub(fy, mula(ZERO, KEYBOX_BASE_PX - (KEYBOX_CY_PX - KEYBOX_ROW_PX), PYROW));
        int fw = kb ? KEYBOX_WQ : FOOT_WQ;
        float fx = kb ? TX(KEYBOX_X0_PX) : add(S.nx, FOOT_DX);                                                     /* three equal columns over the box, the same centres on both rows */
        key_line(KA, 4u, evo_chrome() ? gnt_hint(S.f) : "", fx, fw, ya, 240u, COL[1]);
        key_line(KB, 20u, evo_chrome() && kb ? gnt_hint2(S.f) : "", fx, fw, yb, 240u, COL[1]);
    }
    S.f->dirty = 0;
}

static void sound(void)
{
    static const int ID[5] = { -1, 0, 1, 2, 3 };
    unsigned s = S.f->sfx;
    S.f->sfx = 0;
    if (s && s < 5u) AUDIO(ID[s]);
}

/* ---- the title tabs ---- */
static int ok_ptr(u32 p) { return p >= 0x80000000u && p < 0x81800000u && !(p & 3u); }
static u8 *tab_data(u32 idx)                                               /* the pixels of the image the panel's tab joint shows now (168 x 28 I4), else 0 */
{
    void *root, *j = 0;
    u8 *go = (u8 *)find_proc(PANEL_PROC, 0);
    u32 d, m, t, o;
    if (!go) return 0;
    root = (void *)W((u32)go + 0x28u);
    if (!root || !ok_ptr((u32)root)) return 0;
    (void)LB_JOBJ(root, &j, (int)idx, -1);
    if (!j || !ok_ptr((u32)j)) return 0;
    d = W((u32)j + 0x18u);                                                 /* HSD_JObj.u.dobj, HSD_DObj.mobj +8, HSD_MObj.tobj +8, HSD_TObj.imagedesc (scanned: the layout is not trusted blind) */
    if (!ok_ptr(d)) return 0;
    m = W(d + 8u);
    if (!ok_ptr(m)) return 0;
    t = W(m + 8u);
    if (!ok_ptr(t)) return 0;
    for (o = 0x48u; o <= 0x70u; o += 4u) {
        u32 im = W(t + o);
        if (ok_ptr(im) && H(im + 4u) == 168u && H(im + 6u) == 28u && W(im + 8u) == 0u && ok_ptr(W(im))) return (u8 *)W(im);
    }
    return 0;
}
static void tab_put(unsigned i, const unsigned char *art)
{
    u8 *d = tab_data(i ? TAB_CHILD : TAB_PARENT);
    unsigned k;
    if (!d) return;
    if (S.tab_dat[i] == d) return;                                         /* already ours */
    if (S.tab_dat[i] && S.tab_save[i]) { for (k = 0; k < GNT_TAB_BYTES; k++) S.tab_dat[i][k] = S.tab_save[i][k]; DC_STORE(S.tab_dat[i], GNT_TAB_BYTES); }
    if (!S.tab_save[i]) { S.tab_save[i] = (u8 *)HSD_MEMALLOC(GNT_TAB_BYTES); if (!S.tab_save[i]) return; }
    for (k = 0; k < GNT_TAB_BYTES; k++) S.tab_save[i][k] = d[k];
    for (k = 0; k < GNT_TAB_BYTES; k++) d[k] = art[k];
    DC_STORE(d, GNT_TAB_BYTES);
    S.tab_dat[i] = d;
    GX_INV_TEX();
}
static void tabs_apply(void)
{
    tab_put(0, gnt_tab_main);
    tab_put(1, gnt_tab_tourn);
}
static void tabs_restore(void)
{
    unsigned i, k;
    for (i = 0; i < 2u; i++) {
        if (S.tab_dat[i] && S.tab_save[i]) { for (k = 0; k < GNT_TAB_BYTES; k++) S.tab_dat[i][k] = S.tab_save[i][k]; DC_STORE(S.tab_dat[i], GNT_TAB_BYTES); }
        if (S.tab_save[i]) HSD_FREE(S.tab_save[i]);
        S.tab_dat[i] = 0; S.tab_save[i] = 0;
    }
    GX_INV_TEX();
}

/* ---- the retail "SUBJECT TABLE" lettering ----
   The panel model (MenMainPanel_Top) carries a faint wide-spaced "SUBJECT TABLE" heading under the title tabs; with the tabs saying Friends it is clutter.  It is ONE DObj of
   the panel tree: the DObjs are numbered depth first (a JObj's own DObjs, then its child's subtree, then its next sibling; 44 in the retail file).  Found in Dolphin
: number 18 only.  DOBJ_HIDDEN (flag 1) is set on the numbers in SUBJ_MASK while the screen is up
   and cleared again on every exit.  Nothing is touched unless the tree has the retail 44 DObjs. */
#define SUBJ_MASK 0x40000u
#define SUBJ_COUNT 44u
#define SUBJ_MAX 64u
static void dobj_walk(u32 j, unsigned depth, u32 *list, unsigned *n)
{
    while (ok_ptr(j) && depth < 40u) {
        u32 d;
        if (!(W(j + 0x14u) & 0x30000u)) for (d = W(j + 0x18u); ok_ptr(d); d = W(d + 4u)) { if (*n < SUBJ_MAX) list[*n] = d; (*n)++; }
        dobj_walk(W(j + 0x10u), depth + 1u, list, n);
        j = W(j + 0x08u);
    }
}
static void subject_hide(void)
{
    u32 list[SUBJ_MAX], root;
    unsigned n = 0, i, k = 0;
    u8 *go;
    if (S.nsubj) return;
    go = (u8 *)find_proc(PANEL_PROC, 0);
    if (!go) return;
    root = W((u32)go + 0x28u);
    if (!ok_ptr(root)) return;
    dobj_walk(root, 0, list, &n);
    if (n != SUBJ_COUNT) return;
    for (i = 0; i < n && i < 32u; i++) {
        if (!((SUBJ_MASK >> i) & 1u) || k >= 4u) continue;
        S.subj[k] = list[i]; S.subj_was[k] = (u8)(W(list[i] + 0x14u) & 1u);
        W(list[i] + 0x14u) |= 1u;
        k++;
    }
    S.nsubj = (u8)k;
}
static void subject_show(void)
{
    unsigned i;
    for (i = 0; i < S.nsubj; i++) if (!S.subj_was[i]) W(S.subj[i] + 0x14u) &= ~1u;
    S.nsubj = 0;
}



static void ev_think(void *gobj)
{
    unsigned i;
    (void)gobj;
    if (!S.want_exit || S.state != GNTEV_UP) return;
    if (S.md) {
        for (i = 1; i < GNT_PANEL; i++) tfree(&S.pan[i].t);
        for (i = 0; i < 4u; i++) tfree(&S.leg[i].t);
        for (i = 0; i < 3u; i++) tfree(&S.box[i].t);
        
        tree_free(); tfree(&S.title.t); tfree(&S.hint.t); tfree(&S.tpl.t); tfree(&S.ttx.t); S.tpl.s[0] = 0; S.ttx.s[0] = 0; { unsigned q; for (q = 0; q < GNT_VIS; q++) { if (q >= RETAIL_ROWS) { tfree(&S.xtn[q - RETAIL_ROWS]); tfree(&S.xtt[q - RETAIL_ROWS]); S.rnum[q][0] = 1; S.rtit[q][0] = 1; } tfree(&S.rcode[q].t); S.rcode[q].s[0] = 0; tfree(&S.rstar[q].t); S.rstar[q].s[0] = 0; } } tfree(&S.arl.t); S.arl.s[0] = 0; tfree(&S.arr.t); S.arr.s[0] = 0; { unsigned q; for (q = 0; q < KX_N; q++) { tfree(&S.X[q].t); S.X[q].s[0] = 0; S.X[q].tag = 0; } }
        if (!W((u32)S.md + MD_DESC)) W((u32)S.md + MD_DESC) = (u32)text_at(S.nx, S.ny0, 0.03f, COL[0], 0);
        if (!W((u32)S.md + MD_NAME)) W((u32)S.md + MD_NAME) = (u32)text_at(S.sx, S.sy, 0.03f, COL[0], 0);
        for (i = 0; i < RETAIL_ROWS; i++) { u32 go = W((u32)S.md + MD_GOBJS + 4u * i); if (go) { GOBJ_FREE((void *)go); W((u32)S.md + MD_GOBJS + 4u * i) = 0; } }
    }
    gart_close();                                                                /* our pieces, the main menu panel drawn again, the archives freed */
    S.art = 0;
    tabs_restore();
    subject_show();
    S.state = GNTEV_CLOSING;
    B(MENU_FLOW + 0x11u) = 0;
    MN_GOTO((int)S.ret_kind, (u16)S.ret_row, 3);                         /* frees THIS gobj, builds the screen we came from, adds its think */
    S.think = 0; S.scr = 0; S.md = 0;
    S.state = GNTEV_CLOSED;
}

static void capture(void)
{
    u8 *t0 = (u8 *)W((u32)S.md + MD_TEXTS), *t1 = (u8 *)W((u32)S.md + MD_TEXTS + 4u), *i0 = (u8 *)W((u32)S.md + MD_ICONS);
    u8 *n = (u8 *)W((u32)S.md + MD_NAME);
    u8 *ja = (u8 *)joint(0xAu), *jc = (u8 *)joint(0xCu);
    S.nx = t0 ? F(t0 + TX_X) : -14.0f; S.ny0 = t0 ? F(t0 + TX_Y) : -8.0f; S.nz = t0 ? F(t0 + TX_Z) : 17.0f;
    S.dy = (t0 && t1) ? sub(F(t1 + TX_Y), F(t0 + TX_Y)) : 1.5f;
    S.tx = i0 ? F(i0 + TX_X) : add(S.nx, 4.8f);
    S.bw = i0 ? F(i0 + TX_BW) : 364.68f; S.bh = i0 ? F(i0 + TX_BH) : 38.39f;
    S.sx = n ? F(n + TX_X) : 3.8f; S.sy = n ? F(n + TX_Y) : 6.9f;
    S.cur_dy = (ja && jc) ? sub(F(jc + 0x3C), F(ja + 0x3C)) : mula(ZERO, MINUS1, S.dy);
    S.tx0 = add(S.nx, PX0);
    S.pyk = mula(ZERO, S.dy, PYROW);
    S.ty0 = mula(S.ny0, PY0, S.pyk);
}

static void ev_launch(void *gobj)
{
    u32 *cb = 0;
    unsigned i;
    if (S.state != GNTEV_LAUNCH) return;
    AUDIO(1);
    EV_ENTER(0, 0);
    GOBJ_FREE(gobj);
    S.orig = 0;
    S.scr = (void *)W(EV_SCREEN);
    S.think = find_proc(EV_THINK, &cb);
    if (S.think && cb) *cb = (u32)ev_think;
    if (!S.scr || !S.think) {
        S.err = S.think ? 3u : 2u;
        if (S.think && cb) { S.state = GNTEV_UP; S.want_exit = 1; S.md = S.scr ? (u8 *)W((u32)S.scr + 0x2Cu) : 0; }
        else S.state = GNTEV_FAILED;
        return;
    }
    S.md = (u8 *)W((u32)S.scr + 0x2Cu);
    capture();
    for (i = 0; i < GNT_VIS; i++) { S.rnum[i][0] = 1; S.rtit[i][0] = 1; }       /* never equal to a real string: force the first draw */
    evo_prep(S.scr, RETAIL_ROWS);                                                   /* the shared open (evo.h): retail marks and texts out, empty portrait, the reveal starts */
    S.pan[0].t = 0; S.sub.t = 0; S.sub.s[0] = 1;
    S.state = GNTEV_UP;
    S.art = gart_open();                                                        /* Tournament Melee's art from the disc (GmTou1p, TyMnView); 0 keeps the Event look */
    if ((S.art & TV_NEED) != TV_NEED) { gart_close(); S.art = gart_open(); }
    tabs_apply();
    subject_hide();
    draw_all(1);
}

int gntev_open(struct gnt *f)
{
    u32 kind = B(MENU_FLOW), *cb = 0;
    void *go;
    if (S.state != GNTEV_IDLE && S.state != GNTEV_CLOSED && S.state != GNTEV_FAILED) return -1;
    zero(&S, (unsigned)sizeof S);
    S.f = f;
    if (kind >= 0x22u || kind == EV_KIND || !W(TAB + kind * 20u + 0x10u)) { S.err = 1; S.state = GNTEV_FAILED; return -1; }
#ifdef FRD_TEST_1P
    {   /* DOLPHIN PROOF ONLY (SG_EXTRA_CFLAGS=-DFRD_TEST_1P, never shipped): the Main Menu's proc runs the 1-P slot's think, as after Back from Slippi's screen (the rig has no Slippi login) */
        u32 *c0 = 0;
        void *g0 = find_proc(W(TAB + kind * 20u + 0x10u), &c0);
        if (g0 && c0 && kind == 0u) *c0 = W(TAB + 20u + 0x10u);
    }
#endif
    go = find_proc(W(TAB + kind * 20u + 0x10u), &cb);
#ifndef FRD_TEST_NOFALLBACK                                              /* (the Dolphin control of the proof above: the refusal of 1075) */
    if ((!go || !cb) && kind == 0u) go = find_proc(W(TAB + 1u * 20u + 0x10u), &cb); /* The Main Menu reached by Back from Slippi's screen runs its think through the 1-P kind's slot (ml_glue.c: ml_think_1p, MENU_FLOW kind 0 meanwhile): the kind 0 think is then in no proc and the Event screen was refused for ever */
#endif
    if (!go || !cb) { S.err = 1; S.state = GNTEV_FAILED; return -1; }
    S.ret_kind = kind; S.ret_row = H(MENU_FLOW + 2u);
    S.orig = go;
    S.state = GNTEV_LAUNCH;
    *cb = (u32)ev_launch;
    return 0;
}
void gntev_frame(void)
{
    int rv;
    if (S.state != GNTEV_UP || S.want_exit) return;
    S.tick++;
    gart_tick();
    if (art_on()) gart_menu_panel(0);                                           /* every frame until it holds (gnb_ev.c): the retail panel's own zoom-in redraws it for about 50 frames */
    if ((S.tick & 7u) == 0u) tabs_apply();                                 /* the panel's own animation may swap the tab image after the screen is built: follow it */
    if (S.shown_scr != S.f->scr) evo_skip();                                    /* into or back from a sub screen: all at once */
    else if (S.shown_first != gnt_cur_first(S.f)) evo_page();                  /* a page turn: quick replay */
    rv = evo_frame(gnt_rows(S.f));
    if (S.f->dirty || S.shown_scr != S.f->scr) draw_all(S.shown_scr != S.f->scr);
    else if (rv || tree_animating() || (S.tree_on && (S.tick & 15u) == 0u) || S.shown_page != gnt_cur_page(S.f) || S.shown_first != gnt_cur_first(S.f)) draw_all(0);
    sound();
}
void gntev_close(void) { if (S.state == GNTEV_UP) { sound(); S.want_exit = 1; } }
int gntev_state(void) { return S.state; }
unsigned gntev_err(void) { return S.err; }
void gntev_reset(void) { if (S.state == GNTEV_CLOSED || S.state == GNTEV_FAILED) S.state = GNTEV_IDLE; }
void gntev_abort(void)
{
    u32 *cb = 0;
    unsigned i;
    if (S.state == GNTEV_LAUNCH && S.orig && find_proc((u32)ev_launch, &cb) && cb) *cb = W(TAB + S.ret_kind * 20u + 0x10u);
    if (S.state == GNTEV_UP || S.state == GNTEV_CLOSING) {
        if (find_proc((u32)ev_think, &cb) && cb) *cb = NOP_PROC;
        for (i = 1; i < GNT_PANEL; i++) tfree(&S.pan[i].t);
        for (i = 0; i < 4u; i++) tfree(&S.leg[i].t);
        for (i = 0; i < 3u; i++) tfree(&S.box[i].t);
        
        tree_free(); tfree(&S.title.t); tfree(&S.hint.t); tfree(&S.tpl.t); tfree(&S.ttx.t); S.tpl.s[0] = 0; S.ttx.s[0] = 0; { unsigned q; for (q = 0; q < GNT_VIS; q++) { if (q >= RETAIL_ROWS) { tfree(&S.xtn[q - RETAIL_ROWS]); tfree(&S.xtt[q - RETAIL_ROWS]); S.rnum[q][0] = 1; S.rtit[q][0] = 1; } tfree(&S.rcode[q].t); S.rcode[q].s[0] = 0; tfree(&S.rstar[q].t); S.rstar[q].s[0] = 0; } } tfree(&S.arl.t); S.arl.s[0] = 0; tfree(&S.arr.t); S.arr.s[0] = 0; { unsigned q; for (q = 0; q < KX_N; q++) { tfree(&S.X[q].t); S.X[q].s[0] = 0; S.X[q].tag = 0; } }
        gart_close();
        tabs_restore();
        subject_show();
    }
    S.state = GNTEV_IDLE;
}
#endif
