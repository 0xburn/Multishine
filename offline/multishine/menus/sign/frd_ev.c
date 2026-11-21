/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Friends screen on the retail Event Match screen: the Melee glue of the frd model.
 * CONSOLE ONLY (-DGNB_MELEE, ms/sign.ovl).  The same technique as the Event look of the Game Night browser, written again
 * for this model so neither screen depends on the other's state:
 *   open: the current menu think's callback is swapped for our launcher (proc context); the launcher builds the retail Event screen (mnEvent_8024E838(0, 0)), frees
 *         the old think and swaps the retail Event think (which would start an event match on A) for ours;
 *   draw: our rows in the retail slots (number column = a coloured status dot, title = name and code, online in gold, offline dim), the selected entry's lines in the
 *         description box (line 0 in the retail slot), "n Online" in the retail score box, the title and key line, the retail cursor and scroll arrows; the
 *         portrait is the empty frame, the event type label, the "Select Character" box and the score strip are hidden;
 *   leave: our think goes back to the screen we came from (mn_80229894(kind, row, 3)) like the retail B handler; the retail procs tear the screen down.
 *   tabs: the retail title tabs of the panel (joints 84 / 85 of MenMainPanel_Top) read "1-P Mode | Event Match"; while the screen is up the live tab images are
 *         overwritten with our "Main Menu | Friends" strips (frd_tab_art.h, GX tile order, 2688 B each; the retail pixels are kept in two heap blocks and put back on every exit);
 *   dots: the number slot of a row holds a big "." in the status colour (green menus, yellow busy, red searching or in a match, grey offline, white blinking request); the bottom
 *         box's last line is the legend (four coloured words).  Colour map: frd.c dot_of.
 * Original work; retail facts cited from the decompilation: , , gobj*.c. Soft-float: float sums through gnbev_madd.
 */
#ifdef GNB_MELEE
#include "frd_ev.h"
#include "evo.h"               /* the shared open sequence and row reveal of the Event-view screens */
#include "frd_tab_art.h"
#include "gnb_art.h"           /* gart_badge: the Standings badge pieces carry the unread marker */
#include "stb.h"               /* STB_SH_PLACEMENT */
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
static const float F_TITLE_ROWS = -2.2f, F_HINT_ROWS = 12.9f, F_LEG_ROW = 12.26f;
/* (the dot offsets are gone: the status is a word now) */
static const float LEG_X[4] = { 0.0f, 4.0f, 8.1f, 12.8f };
static const float F_DESC[FRD_PANEL] = { 10.8f, 11.35f, 11.9f, 12.45f };   /* the quick-chat picker's cells (small font) */
static const float F_BODY[4][4] = FRD_BODY_ROWS;                    /* body rows by line count, see frd.h (three lines at the small size touched, the box had room) */
#define BODY_FONT 0.030f
#define BODY_DX 0.30f                                                 /* the small left margin of the body lines, units (the first line sat on the box's top line; all inside the box) */
static float lin(float base, unsigned k, float step) { float o, kk = K[k & 15u]; gnbev_madd(&o, &base, &kk, &step); return o; }
static float add(float a, float b) { float o; gnbev_madd(&o, &a, &ONE, &b); return o; }
static float sub(float a, float b) { float o; gnbev_madd(&o, &a, &MINUS1, &b); return o; }
static float mula(float a, float b, float m) { float o; gnbev_madd(&o, &a, &b, &m); return o; }
#define NUM_FONT 0.027f
#define TITLE_FONT 0.045f
#define ROW_FONT 0.030f                    /* the name AND the code (one size; 12 characters of name and 8 of code at tp_gw widths fit their columns); the retail box stretch (S.bw / S.bh) applies to both */
#define PANEL_FONT 0.024f
#define STATUS_FONT 0.030f                         /* the centred status message of the list ('perform some centering and legibility work'): 1.25 times the panel text */
#define COL_WARN 0xFF8C7AFFu                       /* the warning colour of that message: a lighter coral than COL[3], readable on the dark box */
#define STATUS_CX 9.6f                             /* centre of the box, units right of the number column (the panel is about 18 units wide, as the quick-chat cross) */
static const float STATUS_ROW[3][3] = { { 11.14f, 0.0f, 0.0f }, { 10.74f, 11.54f, 0.0f }, { 10.4f, 11.14f, 11.88f } };   /*: the message lives in the description box below the list, centred on the box (the panel lines F_DESC span 10.8..12.45: centre 11.6), not on the grid */
#define HINT_FONT 0.019f
#define FOOT_UP_PX 0.0f                  /* Friends keeps y 497.6: the Settings y 464 lands inside the retail description box (y 425..479) and that art is the retail Event screen; one footer helper otherwise (x spread) */
#define DOT_FONT 0.075f
#define CODE_FONT ROW_FONT
#define WORD_FONT 0.0235f                /* the status word at the largest size that keeps the widest ("In match", "In queue", 760 gw) clear of the name column  */
static const float ROW_DY_WORD = 0.06f, ROW_DY_NAME = 0.048f, ROW_DY_CODE = 0.048f;      /* name, code and word share one look (font + retail stretch), so one baseline: the old offsets belonged to the three smaller fonts */   /* "really pixel peep": cube capture v2 (row 27.6 px): the status word sat 4.3 px, the code 3.4 px, the name 1.3 px above the row centre (a text is placed by its top, the smaller fonts sat high) */
static const float CODE_DX = 9.9f;                /* left edge of the dimmed connect code, units right of the name column (right-aligned codes looked ragged in the proportional font) */
static const u32 COL[4] = { 0xFFFFFFFFu, 0x9C9C9CFFu, 0xFFD040FFu, 0xFF6A5AFFu };
#define COL_NUM 0xCABC9FFFu
static const u32 DOTC[10] = { 0u, 0x58E060FFu, 0xFFA030FFu, 0xFF4A3AFFu, 0x8A8A8AFFu, 0xFFD040FFu, 0x58E060FFu, 0x9C9C9CFFu, 0xFFA030FFu, 0xFFD040FFu };   /* FRD_DOT_*: none, green = ready, amber = busy, red = in a game, grey = offline, gold = request (blinks to DOT_REQ2), green = a room to join, (slippi: unused), amber = searching */
#define DOT_REQ2 0xA88A30FFu
static const char *const LEG_W[4] = { "Menu", "Busy", "Match", "Off" };

struct tslot { void *t; char s[48]; u32 col; int tag; };        /* tag: a position stamp (the key line's texts: a key that moves is made again); 0 = none */
#define KX_N 16u                        /* the key line's slots: the four action texts, then three a key (letter 0, letter 1, Hold prefix) */
static struct {
    int state;
    unsigned err;
    struct frd *f;
    u32 ret_kind, ret_row;
    void *orig, *think, *scr;
    u8 *md;
    float nx, ny0, nz, tx, dy, bw, bh, sx, sy, cur_dy;
    char rnum[FRD_VIS][FRD_NUM + 1], rtit[FRD_VIS][FRD_TITLE + 1];
    u32 rcol[FRD_VIS];
    struct tslot pan[FRD_PANEL], title, hint, sub, leg[4], box[3], X[KX_N], rcode[FRD_VIS], tpl, ttx;     /* tpl / ttx: the result toast's plate and line */
    unsigned bdg;                     /* gart_badges_open worked: the marker pieces are available */
    u32 rdot[FRD_VIS];
    u8 shown_page, shown_first, shown_scr, want_exit;
    u8 blink;
    u16 tick;
    u32 subj[4];                                   /* the panel DObjs hidden for the retail "SUBJECT TABLE" lettering */
    u8 subj_was[4], nsubj;
    u8 *tab_dat[2], *tab_save[2];                  /* the live tab image data we overwrote and the retail pixels we hold */
} S;

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

static u32 dot_color(unsigned dot)
{
    if (dot == FRD_DOT_REQ) return S.blink ? DOT_REQ2 : DOTC[FRD_DOT_REQ];
    return DOTC[dot <= FRD_DOT_INV ? dot : 0u];
}
static void row_draw(unsigned i, int force)
{
    char num[FRD_NUM + 1], tit[FRD_TITLE + 1], cod[10];
    unsigned a = frd_row_parts(S.f, i, num, tit, cod);
    u32 col = COL[a & 3u], dc = dot_color(frd_dot(S.f, i));
    void **tn, **tt;
    float y = lin(S.ny0, i, S.dy);
    if (i >= evo_rows()) { num[0] = 0; tit[0] = 0; cod[0] = 0; dc = 0; }  /* not revealed yet (evo.h) */
    tn = (void **)(S.md + MD_TEXTS + 4u * i); tt = (void **)(S.md + MD_ICONS + 4u * i);
    if (!force && same(num, S.rnum[i]) && same(tit, S.rtit[i]) && col == S.rcol[i] && dc == S.rdot[i] && same(cod, S.rcode[i].s)) return;
    tfree(tn); tfree(tt);
    copy(S.rnum[i], num, FRD_NUM); copy(S.rtit[i], tit, FRD_TITLE); S.rcol[i] = col; S.rdot[i] = dc;
    if (num[0]) { *tn = text_at(S.nx, y, NUM_FONT, COL_NUM, 0); if (*tn) sis_print(*tn, num); }
    else if (dc) { *tn = text_at(S.nx, mula(y, S.dy, ROW_DY_WORD), WORD_FONT, dc, 0); if (*tn) { F((u8 *)*tn + TX_BW) = S.bw; F((u8 *)*tn + TX_BH) = S.bh; sis_print(*tn, frd_dot_label(frd_dot(S.f, i))); } }      /* the status as a short word in its colour (the dot is in the emblem column, mark_draw) */
    if (tit[0]) { u8 *t = (u8 *)text_at(S.tx, mula(y, S.dy, ROW_DY_NAME), ROW_FONT, col, 0); *tt = t; if (t) { F(t + TX_BW) = S.bw; F(t + TX_BH) = S.bh; sis_print(t, tit); } }
    kev_tag = 0;
    ts_big = 1;
    tslot_set(&S.rcode[i], add(S.tx, CODE_DX), mula(y, S.dy, ROW_DY_CODE), CODE_FONT, COL[1], 0, cod); ts_big = 0;                   /* the connect code, dimmed, one left-aligned column */
}
static void panel_draw(void)
{
    unsigned i, sv = evo_chrome() ? frd_status_view(S.f) : 0u;
    char ln[64];
    if (sv) {                                                            /* a status message of the list: centred in the box, bigger, in a colour that reads on the dark box (the Friends tab stays clear of it) */
        for (i = 0; i < FRD_PANEL; i++) {
            unsigned a = frd_panel(S.f, i, ln);
            u32 col = (a & 3u) == 3u ? COL_WARN : COL[0];
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
    {   /* the body of the description box ('more legible'): the non-empty lines packed from the top (even spacing, no hole where a status used to sit), white, with a small left margin; one to three
           lines are drawn at BODY_FONT with the retail stretch of the row names (rows by line count: FRD_BODY_ROWS), four keep the small size */
        char L[FRD_PANEL][FRD_LINE + 1];
        unsigned A[FRD_PANEL], m = 0, big;
        for (i = 0; i < FRD_PANEL; i++) {
            unsigned a = frd_panel(S.f, i, ln);
            if (evo_chrome() && !frd_qc_open(S.f) && ln[0] && ln[0] != ' ') { copy(L[m], ln, FRD_LINE); A[m] = a; m++; }
        }
        big = m >= 1u && m <= 3u;
        for (i = 0; i < FRD_PANEL; i++) {
            float y = mula(S.ny0, F_BODY[(m ? m : 1u) - 1u][i], S.dy), x = add(S.nx, BODY_DX);
            char one[FRD_LINE + 1];
            u32 col = (i < m && (A[i] & 3u) == 3u) ? COL_WARN : COL[0];
            copy(one, i < m ? L[i] : "", FRD_LINE);
            ts_big = (int)big; kev_tag = big ? 7 : 0;
            if (i == 0u) {                                                   /* line 0 lives in the retail description slot (the retail exit frees it: never NULL there) */
                void **d = (void **)(S.md + MD_DESC);
                S.pan[0].t = *d;
                if (!one[0]) { one[0] = ' '; one[1] = 0; }
                tslot_set(&S.pan[0], x, y, big ? BODY_FONT : PANEL_FONT, col, 0, one);
                *d = S.pan[0].t;
            } else
                tslot_set(&S.pan[i], x, y, big ? BODY_FONT : PANEL_FONT, col, 0, one);
            ts_big = 0; kev_tag = 0;
        }
    }
}
/* THE RESULT TOAST: the answer of Add a friend is a plate at the top right, a few seconds, the style of the menu toasts (toast_pill.h,: font 0.028, plate shrink-wrapped to
   the text by the one glyph table, right edge fixed, 0.62 unit padding).  Positions are in this canvas's units from the table origin: screen px X right = nx - 7.634 + X * PXK (as mark_draw), the plate top
   at 1.95 rows above the first row (screen row about 87 px: the band between the title tabs and the table, clear of the tab text; at 4.15 rows a long line covered the Friends tab).  The model (frd.c toast) owns the text and the 5 s clock; moving the cursor clears it. */
#define TOAST_FONT 0.028f
static void toast_draw(void)
{
    char tx[FRD_LINE + 1];
    unsigned st = evo_chrome() ? frd_toast(S.f, tx) : 0u, n;
    float wu, xr, ytop;
    if (!st) {
        if (S.tpl.t || S.ttx.t) { tfree(&S.tpl.t); tfree(&S.ttx.t); }
        S.tpl.s[0] = 0; S.ttx.s[0] = 0;
        return;
    }
    if (S.tpl.t && S.ttx.t && same(S.ttx.s, tx) && S.ttx.col == (st == 2u ? COL_WARN : COL[0])) return;
    tfree(&S.tpl.t); tfree(&S.ttx.t);
    n = (tp_text_gw(tx) * 106u + 9999u) / 10000u;                          /* the line's width in 0.56 unit steps at font 0.028 (hub_soc.c hs_n) */
    if (n > 44u) n = 44u;
    wu = lin(lin(1.24f, n & 15u, 0.56f), n >> 4, 8.96f);                   /* plate width in units: 1.24 + 0.56 n (0.62 padding each side) */
    xr = add(S.nx, 25.27f);                                                  /* right edge: screen px 612 */
    ytop = mula(S.ny0, -1.95f, S.dy);
    S.tpl.t = text_at(sub(xr, wu), ytop, TOAST_FONT, 0xFFFFFFFFu, 0);
    if (S.tpl.t) {
        u8 *t = (u8 *)S.tpl.t;
        F(t + TX_BW) = mula(ZERO, wu, 35.714f); F(t + TX_BH) = 60.714f;      /* the plate's box in box units (units / font): 1.70 units high, one line */
        tp_attach(t);
        sis_print(t, " ");
    }
    S.ttx.t = text_at(add(sub(xr, wu), 0.62f), add(ytop, 0.42f), TOAST_FONT, st == 2u ? COL_WARN : COL[0], 0);
    if (S.ttx.t) sis_print(S.ttx.t, tx);
    copy(S.tpl.s, tx, 47); copy(S.ttx.s, tx, 47); S.ttx.col = st == 2u ? COL_WARN : COL[0];
}
static void legend_draw(void)
{
    unsigned k, on = frd_legend(S.f) && evo_chrome();
    float y = mula(S.ny0, F_LEG_ROW, S.dy);
    if (frd_qc_open(S.f) && evo_chrome()) {                                /* the quick-chat picker: the four texts of the open group as a cross in the panel, reusing the legend's four (unused) text slots:
                                                                              Up on the first line (centred), Left and Right on the second (left and right edge), Down on the third (centred); the title bar says "Chat: Up" */
        static const float QX[4] = { 9.0f, 0.0f, 18.0f, 9.0f };           /* units right of the number column: the panel is about 18 units wide */
        static const unsigned QL[4] = { 0u, 1u, 1u, 2u };                  /* the panel line of each cell */
        static const int QA[4] = { 1, 0, 2, 1 };                           /* 0 left, 1 centred, 2 right */
        for (k = 0; k < 4u; k++) {
            char cl[16];
            frd_qc_cell(S.f, k, cl);
            tslot_set(&S.leg[k], add(S.nx, QX[k]), mula(S.ny0, F_DESC[QL[k]], S.dy), PANEL_FONT, COL[0], QA[k], cl[0] ? cl : "-");
        }
        return;
    }
    for (k = 0; k < 4u; k++) tslot_set(&S.leg[k], add(S.nx, LEG_X[k]), y, PANEL_FONT, DOTC[k + 1u], 0, on ? LEG_W[k] : "");
}
static void badge_draw(void)
{
    char b[32];
    void **n = (void **)(S.md + MD_NAME);
    struct tslot s;
    frd_req_badge(S.f, b);                                                 /* the Online count is the small box (box_draw) */
    if (!b[0] || !evo_chrome()) { b[0] = ' '; b[1] = 0; }
    s.t = *n; copy(s.s, S.sub.s, 47); s.col = S.sub.col; s.tag = 0;                 /* the retail score box slot (freed unconditionally by the retail exit) */
    tslot_set(&s, S.sx, S.sy, 0.03f, COL[0], 2, b);
    *n = s.t; copy(S.sub.s, s.s, 47); S.sub.col = s.col;
}
static void box_draw(void)
{   /* the small black box at the bottom right (retail subject portrait frame): three centred lines, the online count big, then "friends" and "online".
       Placed from the retail score text (S.sx, S.sy: the panel's centre line) by pixel estimates of the 640 x 480 picture: centre 226 px right, lines 14, 42 and 56 px below. */
    static const float DX = 8.15f, ROWS[3] = { 0.5f, 2.0f, 2.4f }, FNT[3] = { 0.05f, 0.02f, 0.024f };      /* (cube): the pair sat right and low, 'online' touched the frame */
    unsigned k;
    for (k = 0; k < 3u; k++) {
        char ln[12];
        (void)frd_box(S.f, k, ln);
        if (!evo_chrome()) ln[0] = 0;
        tslot_set(&S.box[k], add(S.sx, DX), mula(S.sy, ROWS[k], S.dy), FNT[k], COL[0], 1, ln);
    }
}
static const float PXK = 0.05376f, PYROW = 0.037313f, PX0 = -7.634f, MARK_PX = 118.0f, BDG_SX = 0.216f, BDG_SY = 0.453f, BDG_DY = 13.5f;     /* the Standings badge recipe (gnb_ev.c st_badges) */
/* the Friends discs and rings are true circles on a 4:3 TV by the shared factors of gnb_art.h (GART_CIRC_*, one recipe with the Standings tier discs). */
static const float DISC_K = 1.0f, DISC_DX = GART_CIRC_DX, DISC_DY = 0.0f, RING_DX = GART_CIRC_DX, RING_DY = 0.0f, ROW_STEPK = GART_CIRC_STEPK;   /* the disc at the ring's full size (the quad scales from its corner: at 1.0 it covers the hole like the ring did); ROW_STEPK: Dolphin c2: the dots stepped 28.0 px a row against the rows' 27.0 */
static void mark_draw(void)
{   /* the unread marker in the narrow left column: a white ring on every friend row, the ring filled white while the friend has left an unread message.  Pieces of the
       Standings tier-badge path (gnb_art.c gart_badge, shape "placement" = a ring): slot i the ring, slots 9 + 3 i + k the fill rings */
    unsigned i, k;
    float pyk, tx0;
    if (!S.bdg) return;
    pyk = mula(ZERO, S.dy, PYROW); tx0 = add(S.nx, PX0);
    for (i = 0; i < FRD_VIS; i++) {
        unsigned dt = evo_chrome() && i < evo_rows() ? frd_dot(S.f, i) : 0u;
        unsigned unread = dt && dt != FRD_DOT_REQ && frd_mark(S.f, i) == 2u;
        unsigned rgb = DOTC[dt <= FRD_DOT_INV ? dt : 0u] & 0xFFFFFF00u;
        float y = lin(S.ny0, i, mula(ZERO, S.dy, ROW_STEPK)), x = mula(tx0, MARK_PX, PXK), yy = sub(ZERO, mula(y, BDG_DY, pyk)), z = add(S.nz, ONE);     /* (a deeper or nearer z moves and spreads the dots: the menu camera has perspective) */
        if (!dt) { gart_hide(i); for (k = 0; k < 3u; k++) gart_hide(9u + 3u * i + k); continue; }
        /* the status dot: ONE filled disc in the status colour over the row's hole; while the friend has left an unread message a white ring sits around it */
        gart_badge(i, STB_SH_DISC, add(x, DISC_DX), add(yy, DISC_DY), z, mula(ZERO, BDG_SX * GART_CIRC_SX_K, DISC_K), mula(ZERO, BDG_SY * GART_CIRC_SY_K, DISC_K), rgb);
        if (unread) gart_badge(9u + 3u * i, STB_SH_RING, add(x, RING_DX), add(yy, RING_DY), z, BDG_SX * GART_CIRC_SX_K, BDG_SY * GART_CIRC_SY_K, 0xFFFFFF00u); else gart_hide(9u + 3u * i);
        for (k = 1; k < 3u; k++) gart_hide(9u + 3u * i + k);
    }
}
static void cursor_draw(void)
{
    unsigned n = frd_rows(S.f), pg = frd_cur_page(S.f), first = frd_cur_first(S.f);
    joint_y(0xBu, lin(ZERO, pg, S.cur_dy));
    joint_hide(0xBu, n == 0u);
    joint_hide(3u, first == 0u);
    joint_hide(2u, first >= frd_max_first(S.f));
    S.shown_page = (u8)pg; S.shown_first = (u8)first; S.shown_scr = S.f->scr;
}
static void draw_all(int force)
{
    unsigned i;
    for (i = 0; i < FRD_VIS; i++) row_draw(i, force);
    panel_draw();
    legend_draw();
    badge_draw();
    box_draw();
    mark_draw();
    toast_draw();
    joint_hide(4u, 1); joint_hide(6u, 1);
    cursor_draw();
    tslot_set(&S.title, S.nx, mula(S.ny0, F_TITLE_ROWS, S.dy), TITLE_FONT, COL[2], 0, "");   /* no gold title on any Friends screen ( item 20): the tab says Friends, the panel header says what the screen is about */
    {   /* the key line under the box: on the list always (the panel's last line is the legend or a status message, 0.64 rows above); elsewhere only when that line is empty */
        char l3[64];
        (void)frd_panel(S.f, FRD_PANEL - 1u, l3);
        static const unsigned KA[4] = { 0u, 1u, 2u, 3u };
        tslot_set(&S.hint, S.nx, mula(S.ny0, F_HINT_ROWS, S.dy), HINT_FONT, COL[1], 0, "");                 /* (the key line below: badges, one footer style) */
        key_line(KA, 4u, (l3[0] && S.f->scr != FRD_LIST) || !evo_chrome() ? "" : frd_hint(S.f), add(S.nx, FOOT_DX), FOOT_WQ, sub(mula(S.ny0, F_HINT_ROWS, S.dy), mula(ZERO, FOOT_UP_PX, PYROW)), 240u, COL[1]);       /* the Settings family's footer: keys in equal shares of x 78..562 px at y 464 (one footer style) */
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
    if (S.tab_dat[i] && S.tab_save[i]) { for (k = 0; k < FRD_TAB_BYTES; k++) S.tab_dat[i][k] = S.tab_save[i][k]; DC_STORE(S.tab_dat[i], FRD_TAB_BYTES); }
    if (!S.tab_save[i]) { S.tab_save[i] = (u8 *)HSD_MEMALLOC(FRD_TAB_BYTES); if (!S.tab_save[i]) return; }
    for (k = 0; k < FRD_TAB_BYTES; k++) S.tab_save[i][k] = d[k];
    for (k = 0; k < FRD_TAB_BYTES; k++) d[k] = art[k];
    DC_STORE(d, FRD_TAB_BYTES);
    S.tab_dat[i] = d;
    GX_INV_TEX();
}
static void tabs_apply(void)
{
#ifndef FRD_OWN_TABS
#define FRD_OWN_TABS 0         /* 1: the fixed strips of tools/frd_tab_art.py (host font, thinner than Melee's).  0 since (header font): the menu overlay draws the tabs live from Melee's
                                  menu font (ml_glue.c tab_event: "More | Friends" from More, "Main Menu | Friends" otherwise), like every menu screen; nothing here overwrites them */
#endif
    if (!FRD_OWN_TABS) return;
    tab_put(0, frd_tab_main);
    tab_put(1, frd_tab_friends);
}
static void tabs_restore(void)
{
    unsigned i, k;
    for (i = 0; i < 2u; i++) {
        if (S.tab_dat[i] && S.tab_save[i]) { for (k = 0; k < FRD_TAB_BYTES; k++) S.tab_dat[i][k] = S.tab_save[i][k]; DC_STORE(S.tab_dat[i], FRD_TAB_BYTES); }
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
    if (!S.want_exit || S.state != FRDEV_UP) return;
    if (S.md) {
        for (i = 1; i < FRD_PANEL; i++) tfree(&S.pan[i].t);
        for (i = 0; i < 4u; i++) tfree(&S.leg[i].t);
        for (i = 0; i < 3u; i++) tfree(&S.box[i].t);
        
        tfree(&S.title.t); tfree(&S.hint.t); tfree(&S.tpl.t); tfree(&S.ttx.t); S.tpl.s[0] = 0; S.ttx.s[0] = 0; { unsigned q; for (q = 0; q < FRD_VIS; q++) { tfree(&S.rcode[q].t); S.rcode[q].s[0] = 0; } } { unsigned q; for (q = 0; q < KX_N; q++) { tfree(&S.X[q].t); S.X[q].s[0] = 0; S.X[q].tag = 0; } }
        if (!W((u32)S.md + MD_DESC)) W((u32)S.md + MD_DESC) = (u32)text_at(S.nx, S.ny0, 0.03f, COL[0], 0);
        if (!W((u32)S.md + MD_NAME)) W((u32)S.md + MD_NAME) = (u32)text_at(S.sx, S.sy, 0.03f, COL[0], 0);
        for (i = 0; i < FRD_VIS; i++) { u32 go = W((u32)S.md + MD_GOBJS + 4u * i); if (go) { GOBJ_FREE((void *)go); W((u32)S.md + MD_GOBJS + 4u * i) = 0; } }
    }
    if (S.bdg) { gart_close(); S.bdg = 0; }
    tabs_restore();
    subject_show();
    S.state = FRDEV_CLOSING;
    B(MENU_FLOW + 0x11u) = 0;
    MN_GOTO((int)S.ret_kind, (u16)S.ret_row, 3);                         /* frees THIS gobj, builds the screen we came from, adds its think */
    S.think = 0; S.scr = 0; S.md = 0;
    frd_handoff(S.f);                                                    /* a waiting hand-off (accepted Mano a mano, Slippi friend) goes on now that the Event screen is gone */
    S.state = FRDEV_CLOSED;
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
}

static void ev_launch(void *gobj)
{
    u32 *cb = 0;
    unsigned i;
    if (S.state != FRDEV_LAUNCH) return;
    AUDIO(1);
    EV_ENTER(0, 0);
    GOBJ_FREE(gobj);
    S.orig = 0;
    S.scr = (void *)W(EV_SCREEN);
    S.think = find_proc(EV_THINK, &cb);
    if (S.think && cb) *cb = (u32)ev_think;
    if (!S.scr || !S.think) {
        S.err = S.think ? 3u : 2u;
        if (S.think && cb) { S.state = FRDEV_UP; S.want_exit = 1; S.md = S.scr ? (u8 *)W((u32)S.scr + 0x2Cu) : 0; }
        else S.state = FRDEV_FAILED;
        return;
    }
    S.md = (u8 *)W((u32)S.scr + 0x2Cu);
    capture();
    for (i = 0; i < FRD_VIS; i++) { S.rnum[i][0] = 1; S.rtit[i][0] = 1; }       /* never equal to a real string: force the first draw */
    evo_prep(S.scr, FRD_VIS);                                                   /* the shared open (evo.h): retail marks and texts out, empty portrait, the reveal starts */
    S.pan[0].t = 0; S.sub.t = 0; S.sub.s[0] = 1;
    S.state = FRDEV_UP;
    tabs_apply();
    subject_hide();
    S.bdg = (unsigned)gart_badges_open();
    draw_all(1);
}

int frdev_open(struct frd *f)
{
    u32 kind = B(MENU_FLOW), *cb = 0;
    void *go;
    if (S.state != FRDEV_IDLE && S.state != FRDEV_CLOSED && S.state != FRDEV_FAILED) return -1;
    zero(&S, (unsigned)sizeof S);
    S.f = f;
    if (kind >= 0x22u || kind == EV_KIND || !W(TAB + kind * 20u + 0x10u)) { S.err = 1; S.state = FRDEV_FAILED; return -1; }
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
    if (!go || !cb) { S.err = 1; S.state = FRDEV_FAILED; return -1; }
    S.ret_kind = kind; S.ret_row = H(MENU_FLOW + 2u);
    S.orig = go;
    S.state = FRDEV_LAUNCH;
    *cb = (u32)ev_launch;
    return 0;
}
void frdev_frame(void)
{
    int rv;
    if (S.state != FRDEV_UP || S.want_exit) return;
    S.tick++;
    if ((S.tick & 7u) == 0u) tabs_apply();                                 /* the panel's own animation may swap the tab image after the screen is built: follow it */
    if (((S.tick >> 4) & 1u) != S.blink) {                                 /* the request dot blinks */
        unsigned i;
        S.blink = (u8)((S.tick >> 4) & 1u);
        for (i = 0; i < FRD_VIS; i++) if (frd_dot(S.f, i) == FRD_DOT_REQ) row_draw(i, 0);
        mark_draw();
    }
    if (S.shown_scr != S.f->scr) evo_skip();                                    /* into or back from a sub screen: all at once */
    else if (S.shown_first != frd_cur_first(S.f)) evo_page();                  /* a page turn: quick replay */
    rv = evo_frame(frd_rows(S.f));
    if (S.f->dirty || S.shown_scr != S.f->scr) draw_all(S.shown_scr != S.f->scr);
    else if (rv || S.shown_page != frd_cur_page(S.f) || S.shown_first != frd_cur_first(S.f)) draw_all(0);
    sound();
}
void frdev_close(void) { if (S.state == FRDEV_UP) { sound(); S.want_exit = 1; } }
int frdev_state(void) { return S.state; }
unsigned frdev_err(void) { return S.err; }
void frdev_reset(void) { if (S.state == FRDEV_CLOSED || S.state == FRDEV_FAILED) S.state = FRDEV_IDLE; }
void frdev_abort(void)
{
    u32 *cb = 0;
    unsigned i;
    if (S.state == FRDEV_LAUNCH && S.orig && find_proc((u32)ev_launch, &cb) && cb) *cb = W(TAB + S.ret_kind * 20u + 0x10u);
    if (S.state == FRDEV_UP || S.state == FRDEV_CLOSING) {
        if (find_proc((u32)ev_think, &cb) && cb) *cb = NOP_PROC;
        for (i = 1; i < FRD_PANEL; i++) tfree(&S.pan[i].t);
        for (i = 0; i < 4u; i++) tfree(&S.leg[i].t);
        for (i = 0; i < 3u; i++) tfree(&S.box[i].t);
        
        tfree(&S.title.t); tfree(&S.hint.t); tfree(&S.tpl.t); tfree(&S.ttx.t); S.tpl.s[0] = 0; S.ttx.s[0] = 0; { unsigned q; for (q = 0; q < FRD_VIS; q++) { tfree(&S.rcode[q].t); S.rcode[q].s[0] = 0; } } { unsigned q; for (q = 0; q < KX_N; q++) { tfree(&S.X[q].t); S.X[q].s[0] = 0; S.X[q].tag = 0; } }
        if (S.bdg) { gart_close(); S.bdg = 0; }
        tabs_restore();
        subject_show();
    }
    S.state = FRDEV_IDLE;
}
#endif
