/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Game Night browser on the retail Event Match screen: the Melee glue (gnb_ev.h).  CONSOLE ONLY (-DGNB_MELEE, ms/sign.ovl); soft-float C, the float arguments of the
 * SisLib print go through gnbev_sis (gnb_sis.S). Retail facts (NTSC 1.02) are cited from the decompilation: , , gobj*.c.
 * Original work; no retail code or data is copied, nothing in retail text is patched.
 */
#ifdef GNB_MELEE
#include "gnb_ev.h"
#include "gnb_art.h"           /* the original Rooms look on retail art (GmTou1p / TyMnView) */
#include "stb.h"               /* the Grandmaster standings (Multishine > Standings): the same screen in the Event look */
#include "evo.h"               /* the shared open sequence and row reveal of the Event-view screens */
#include "keyhint.h"          /* button badges for the key hints: a letter text on a coloured rounded plate (toast_pill.h); their texts: keyhint_ev.h below */
#ifndef GNB_EVENT_LOOK
#define GNB_EVENT_LOOK 0       /* 1: the Event Match look of release 385 (kept as the base of the future Friends list) */
#endif

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define W(a) (*(volatile u32 *)(a))
#define H(a) (*(volatile u16 *)(a))
#define B(a) (*(volatile u8 *)(a))
#define F(a) (*(volatile float *)(a))

/* ---- retail addresses (vendor/melee/config/GALE01/symbols.txt) ---- */
#define MENU_FLOW 0x804A04F0u            /* MenuFlow: +0 cur_menu u8, +1 prev_menu, +2 hovered_selection u16, +0x11 entering_menu */
#define TAB 0x803EB6B0u                  /* MenuKindData[0x22], 20 bytes, +0x10 think (the table our menu layout patches) */
#define PLINK_HEAD 0x804D782Cu           /* HSD_GObjPLinkHead: HSD_GObj** */
#define GOBJ_INIT 0x804CE380u            /* HSD_GObjLibInitData: +0 p_link_max u8 */
#define EV_SCREEN 0x804D6C60u            /* mnEvent_804D6C60: the Event screen gobj */
#define EV_THINK 0x8024D864u             /* fn_8024D864: the retail Event input think (A starts an event match: never let it run) */
#define NOP_PROC 0x80081540u             /* a 4-byte retail function (blr): the harmless callback left behind if the overlay is freed while our think exists */
#define EV_KIND 7u                       /* MENU_KIND_EVENT */
#define CANVAS 1                         /* the Event screen's SisLib canvas (HSD_SisLib_803A6754(0, 1)) */
#define JOBJ_HIDDEN 0x10u
#define JOBJ_MTX_DIRTY 0x40u
#define JOBJ_USER_DEF_MTX 0x800000u
#define JOBJ_MTX_INDEP_SRT 0x2000000u
#define CHKIND_NONE 0x21                 /* mnEvent_8024D0CC: ChKind_None shows portrait frame 25, the empty box */

typedef void (*fn_ii)(int, int);
typedef void (*fn_p)(void *);
typedef void (*fn_pi)(void *, int);
typedef void (*fn_pu)(void *, u32);
typedef int (*fn_jobj)(void *, void **, ...);
typedef void *(*fn_text_new)(int, int);
typedef void (*fn_goto)(int, u16, int);
#define EV_ENTER ((fn_ii)0x8024E838u)          /* mnEvent_8024E838(event_idx, first_time): load the art, build the screen (first_time 0), add the input think */
#define EV_PORTRAIT ((fn_pi)0x8024D0CCu)       /* mnEvent_8024D0CC(screen gobj, ckind) */
#define GOBJ_FREE ((fn_p)0x80390228u)          /* HSD_GObjFree */
#define LB_JOBJ ((fn_jobj)0x80011E24u)         /* lb_80011E24(tree, &jobj, index, -1) */
#define JOBJ_SETFLAGS ((fn_pu)0x80371D9Cu)     /* HSD_JObjSetFlagsAll */
#define JOBJ_CLRFLAGS ((fn_pu)0x80371F9Cu)     /* HSD_JObjClearFlagsAll */
#define JOBJ_DIRTYSUB ((fn_p)0x803732E8u)      /* HSD_JObjSetMtxDirtySub */
#define TEXT_NEW ((fn_text_new)0x803A6754u)    /* HSD_SisLib_803A6754(sis, canvas) */
#define TEXT_FREE ((fn_p)0x803A5CC4u)          /* HSD_SisLib_803A5CC4 */
#define AUDIO ((void (*)(int))0x80024030u)      /* lbAudioAx_80024030: 0 back, 1 forward, 2 move, 3 refusal */
#define MN_GOTO ((fn_goto)0x80229894u)          /* mn_80229894(kind, hover, state): frees the CURRENT proc's gobj, builds the kind, adds its think */

int gnbev_sis(void *t, const char *fmt, const char *s);    /* gnb_sis.S: HSD_SisLib_803A6B98(t, 0.0f, 0.0f, fmt, s) */
void gnbev_madd(float *out, const float *a, const float *b, const float *c);   /* gnb_sis.S: *out = *a + *b * *c with the FPU (no soft-float libgcc in devkitPPC) */
static const float K[16] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f };
static const float ONE = 1.0f, MINUS1 = -1.0f, ZERO = 0.0f;
static const float F_TITLE_ROWS = -2.2f, F_HINT_ROWS = 12.75f;
static float lin(float base, unsigned k, float step) { float o, kk = K[k & 15u]; gnbev_madd(&o, &base, &kk, &step); return o; }     /* base + k * step */
static float add(float a, float b) { float o; gnbev_madd(&o, &a, &ONE, &b); return o; }
static float sub(float a, float b) { float o; gnbev_madd(&o, &a, &MINUS1, &b); return o; }
static float mula(float a, float b, float m) { float o; gnbev_madd(&o, &a, &b, &m); return o; }                                 /* a + b * m */

/* MnEventData: +0 page u8, +4 first_event, +8 gobjs[9], +0x2C texts[9], +0x50 icons[9], +0x74 name_text, +0x78 desc_text */
#define MD_GOBJS 0x08u
#define MD_TEXTS 0x2Cu
#define MD_ICONS 0x50u
#define MD_NAME 0x74u
#define MD_DESC 0x78u
/* HSD_Text: +0 pos x y z, +0xC box w h, +0x24 font size x y, +0x30 text colour, +0x49 kerning, +0x4A alignment */
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
/* HSD_JObj: +0x14 flags, +0x38 translate x y z; HSD_GObj: +0x08 next, +0x18 proc, +0x28 hsd_obj, +0x2C user_data; HSD_GObjProc: +0x00 child, +0x14 on_invoke */

/* layout, relative to what the retail builder placed (captured at build): tuned from the Dolphin screenshots */
#define GNB_DESC_LINES 3u                /* lines in the retail description box (its strip on top holds the retail score box) */
#define MODE_FONT 0.027f                 /* the list's type column (gnb_cols mode) */
#define NUM_FONT 0.027f                  /* the number column: "12 of 16" must stay left of the title column */
#define HINT_WQ 1909466                  /* the hint line's keys spread over x 49..591 px = 542 px (the old line started 5 units left of the number column and had to fit the 4:3 picture), Q16 units */
#define HINT_DX -5.0f                    /* the key line starts this far left of the number column (the full line must fit the 4:3 picture) */
#define PANEL_FONT 0.024f
#define HINT_FONT 0.019f
#define TITLE_FONT 0.045f

static const u32 COL[4] = { 0xFFFFFFFFu, 0x9C9C9CFFu, 0xFFD040FFu, 0xFF6A5AFFu };    /* normal, dim, highlight, warning */
#define COL_NUM 0xCABC9FFFu              /* the retail number column colour (mnEvent_804D5028) */

struct tslot { void *t; char s[64]; u32 col; int tag; };        /* tag: a position stamp the text was made for (key hints: a key that moves keeps its string); 0 = none */
#define X_N 97u                          /* text slots of the layout (see S.X) */

static struct {
    int state;
    unsigned err;
    struct gnb *g;
    struct stb *sd;                      /* the standings model when the screen shows the Grandmaster standings (then g is 0) */
    u32 ret_kind, ret_row;
    void *orig;                          /* the hijacked menu think gobj (until the launcher frees it) */
    void *think;                         /* the retail Event think gobj, now running our think */
    void *scr;                           /* the Event screen gobj */
    u8 *md;                              /* its MnEventData */
    float nx, ny0, nz, tx, dy, bw, bh;   /* row 0 number column x / y, z, title x, row spacing (y grows downwards), title box */
    float dx_desc, dy_desc, sx, sy;      /* retail description and score box positions */
    float cur_dy;                        /* cursor joint step (y of joint 0xC - joint 0xA) */
    char rnum[GNB_VIS][GNB_NUM + 1], rtit[GNB_VIS][32];
    u32 rcol[GNB_VIS];
    struct tslot pan[4], title, hint, sub, head;
    char rest[48]; unsigned rest_a;                 /* the second line of a long description (panel_draw) */
    struct tslot X[X_N];                 /* original-style layout texts (54..56: the room footer's spread keys; 57..90: the key badges' letters and Hold prefixes, the hint line's keys) (columns, action row, room screen); freed when the layout kind changes */
    unsigned kind;                       /* gnb_layout the slots were made for */
    unsigned bdg;                        /* standings: the badge pieces are available (gart_badges_open); 0 keeps the retail marker circle */
    unsigned art;                        /* gart_open mask: the Rooms look is on when it has the box (room screen) / the frame (list) */
    float tx0, ty0, pyk;                 /* screen pixels (640 x 528, the Dolphin frames) to text coordinates: tx = tx0 + px * PXK, ty = ty0 + py * pyk */
    u8 shown_page, shown_first, want_exit, built, shown_sel, shown_pg;
    u8 nsubj, subj_was[4];               /* the retail "SUBJECT TABLE" DObjs hidden while the Standings note shows (subject_hide) */
    u32 subj[4];
} S;

static int art_on(void);
#define ART_NEED ((1u << GART_BOX) | (1u << GART_FRAME) | (1u << GART_BUTTON))      /* what the list (and with the box alone the room screen) needs for the Rooms look */
static void zero(void *p, unsigned n) { u8 *b = (u8 *)p; while (n--) *b++ = 0; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void copy(char *d, const char *s, unsigned max) { unsigned i = 0; while (s[i] && i < max) { d[i] = s[i]; i++; } d[i] = 0; }

/* ---- gobj / proc lookup ---- */
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

/* ---- the retail "SUBJECT TABLE" lettering (same as frd_ev.c): DObj 18 of the 44 in the panel tree, hidden with DOBJ_HIDDEN (flag 1) ---- */
#define PANEL_PROC 0x80229BF4u
#define SUBJ_MASK 0x40000u
#define SUBJ_COUNT 44u
#define SUBJ_MAX 64u
static int ok_ptr(u32 p) { return p >= 0x80000000u && p < 0x81800000u && !(p & 3u); }
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
    F(j + 0x3C) = y;                                            /* HSD_JObjSetTranslateY */
    fl = W(j + 0x14);
    if (!(fl & JOBJ_MTX_INDEP_SRT) && !(!(fl & JOBJ_USER_DEF_MTX) && (fl & JOBJ_MTX_DIRTY))) JOBJ_DIRTYSUB(j);
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

/* ---- texts ---- */
/* A text object that does not fit makes retail Melee PANIC (SisLib allocates from its own pool of about 13 KB: HSD_Text 0xA0 + 0x10 + 0x80 and three 12 byte headers = 340 bytes a text), so every
   text is checked first: gnb_pool.h simulates the retail allocator on a snapshot of the free list (a text cut from the head leaves GP_RESERVE; a text served from a freed block is always fine).
   Build 485 looked at the largest free block only, which refused every text once a long list had been freed (GUI audit GN-01 / GN-02).  A refused text stays empty (no panic, no reset) and is
   counted (gnbev_alloc_fail / gnbev_sis_min: read them with the RAM probe). */
#include "gnb_pool.h"
#define SIS_FREE_HEAD_ADDR 0x804D7970u           /* sislib free list: {next, data, size} blocks (on_bhold.c sis_largest) */
static u32 gb_alloc_fail, gb_sis_min, gb_force, gb_made;
static u32 gb_fxb;        /* nonzero: the next texts get this font x (bits) instead of the font given (the quick chat picker, qc_win.h) */
static int gb_tag;        /* gb_force: the two retail slots the retail exit frees unconditionally (description line 0, score-box badge) are NEVER left NULL: the guard is off for them */
static unsigned sis_snap(unsigned *fl)                 /* the free list sizes in list order (0 = the head); returns how many */
{
    u32 b = W(SIS_FREE_HEAD_ADDR);
    unsigned n = 0;
    while (b >= 0x80000000u && b < 0x81800000u && !(b & 3u) && n < GP_MAXB) { fl[n++] = W(b + 8u); b = W(b); }
    return n;
}
unsigned gnbev_alloc_fail(void) { return gb_alloc_fail; }
unsigned gnbev_sis_min(void) { return gb_sis_min; }
static void *text_at(float x, float y, float z, float font, u32 col, int align)
{
    u8 *t;
    unsigned fl[GP_MAXB], n = sis_snap(fl);
    if (n && (!gb_sis_min || fl[0] < gb_sis_min)) gb_sis_min = fl[0];
    if (!gb_force && !gp_text_ok(fl, n)) { gb_alloc_fail++; return 0; }
    t = (u8 *)TEXT_NEW(0, CANVAS);
    if (!t) { gb_alloc_fail++; return 0; }
    F(t + TX_X) = x; F(t + TX_Y) = y; F(t + TX_Z) = z;
    F(t + TX_FX) = font; F(t + TX_FY) = font;
    if (gb_fxb) W(t + TX_FX) = gb_fxb;
    W(t + TX_COL) = col;
    B(t + TX_KERN) = 1;
    B(t + TX_ALIGN) = (u8)align;
    return t;
}
/* punctuation Melee's SIS encoder lacks (! ? ; [ ] + / % ...) as full-width Shift-JIS codes, same table as ml_sis_expand (menus/ml/ml_core.c): any other unknown byte would swallow its neighbour */
static const char SJ_CHR[] = "!?;()[]+/%#&*@$=<>~|_^{}";
static const unsigned char SJ_LO[] = { 0x49, 0x48, 0x47, 0x69, 0x6A, 0x6D, 0x6E, 0x7B, 0x5E, 0x93, 0x94, 0x95, 0x96, 0x97, 0x90, 0x81, 0x83, 0x84, 0x60, 0x62, 0x51, 0x4F, 0x6F, 0x70 };
static void sis_print(void *t, const char *in)
{
    char out[96];
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
static int gb_big;        /* 1: the next tslot_set text gets the retail box stretch of the row names (S.bw / S.bh): the Friends rows' look (the Standings rows and info box) */
static void tslot_set(struct tslot *s, float x, float y, float font, u32 col, int align, const char *str)
{
    if (s->t && same(s->s, str) && s->col == col && s->tag == gb_tag) return;
    tfree(&s->t);
    copy(s->s, str, 63);
    s->col = col;
    s->tag = gb_tag;
    if (!str[0]) return;
    s->t = text_at(x, y, S.nz, font, col, align);
    if (s->t && gb_big) { F((u8 *)s->t + TX_BW) = S.bw; F((u8 *)s->t + TX_BH) = S.bh; }
    if (s->t) { sis_print(s->t, str); gb_made = 1; }
}

/* original-style layout: x offsets in front of the number column x (S.nx); one text slot per piece (S.X) */
/* x in text units from the number column (18.6 px per unit; the Rooms look measured on the release-387 console frames: host names up to 125 px from 222 px, "NAW" 45 px,
   "Singles" 64 px, the n/16 box 66 px wide): host 4.3 .. region 12.4 .. mode 15.4 (ends 492 px) .. count box centre 21.0 (499 .. 565 px), its text right-aligned inside */
static const float DX_HOST = 4.3f, DX_REG = 11.8f, DX_MODE = 14.5f, DX_CNTBOX = 21.0f;
#define HOST_PX 134u                     /* estimated pixel budget of the host column (region starts 150 px after it) */
static const float ACT_DX[GNB_ACT_N] = { 0.0f, 6.2f, 12.4f, 18.6f };      /* (the text fallback's action row) */
#define X_REG 0u                         /* 9 row slots each: region, mode, count */
#define X_MODE 9u
#define X_CNT 18u
#define X_ACT 27u                        /* four action buttons */
#define X_LOB 0u                         /* the room screen reuses the slots from 0 */
#define X_OPEN 53u                       /* the lobby header's "Open for ..." line */
#define HDR_SMALL_Y 81.0f                /* the lobby header's two small lines (Open for..., the rules' second part): one y and one size, so they share a baseline */
#define HDR_SMALL_FONT 0.027f
#define X_ELO 43u                        /* the Standings info box: games (43), ELO (44) */
#define ST_BOX_L_PX 148                  /* the info box's text span in the 640 x 528 frame (the left edge is the Friends body margin, the right edge the box's right text edge; S.sx is the retail score box's x, not the box's edge) */
#define ST_BOX_R_PX 490
#define ST_NOTE_Y 112                    /* the note over the background's "SUBJECT TABLE" lettering: text y, px of the 640 x 480 layout (lettering centre about 110) */
#define ST_BODY_ROW 11.14f               /* the body line's row (Friends STATUS_ROW[0][0]: one line centred in the box body) */
#define BODY_DX 0.30f                    /* the small left margin of the Friends body lines, units */
#define X_SEASON 40u                     /* the Standings screen's season-days box: three slots (kind ST_KIND frees them with the rest) */
#define SD_BX 549.0f                     /* centre x of the box; number top y, "days" top y, "left" top y (box top about 373, height about 62) "top and left heavy":
                                            cube 1027 capture: the text block sat 7 px left and 3 px high of the black box's centre, so x +5 and y +3 */
#define SD_NUM_Y 424.0f
#define SD_W1_Y 452.0f
#define SD_W2_Y 466.0f
#define X_GRID 21u                       /* the pick grid's cell labels (27: a cell without its icon) */
#define COL_FONT 0.03f
#define MORE_FONT 0.035f                /* the More popup's rows: the size of the empty-list message (0.03 read small) */
#define LIST_FOOT_DY 14.0f             /* the room list's hint line sits this many px below the plate bar (the line crossed the bar's lower edge; 8 -> 14, 'these need to sit a bit lower') */
#define ROW_FONT 0.027f                 /* the room list row: code, name, region, type and count all in ONE size, so they share a baseline and a colour */

static void xs(unsigned i, float x, float y, float font, u32 col, int align, const char *str)
{
    tslot_set(&S.X[i], x, y, font, col, align, str);
}
static void x_free_all(void)
{
    unsigned i;
    for (i = 0; i < X_N; i++) { tfree(&S.X[i].t); S.X[i].s[0] = 0; S.X[i].tag = 0; }
}

/* estimated width in pixels of a host name at the list font: toast_pill.h's one glyph table (the menu font is proportional: narrow letters, wide capitals; within about 8 percent of the console frames) */
static unsigned est_w(const char *s) { return TP_PX_OF(tp_text_gw(s), TP_LIST_PX * 100u); }
/* cut the name to the budget and end it with a dot (never overlaps the region column) */
static void fit_name(char *s, unsigned budget)
{
    unsigned n = 0;
    if (est_w(s) <= budget) return;
    while (s[n]) n++;
    while (n > 1u) {
        s[--n] = 0;
        if (est_w(s) + 7u <= budget) break;
    }
    s[n] = '.'; s[n + 1u] = 0;
}

/* row i: number column into texts[i], title into icons[i] (the retail slots: the retail teardown, fn_8024E1B4, frees them) */
/* the passcode entry of the Rooms look is a panel of its own over the form / list: the rows under it give way */
static int pass_art(void) { return (S.g->scr == GNB_PASS || (S.g->scr == GNB_LIST && S.g->more)) && art_on(); }      /* (the More popup is a panel of the same kind) */
static void row_draw(unsigned i, int force)
{
    char num[GNB_NUM + 1], tit[34], reg[8], mod[10], cnt[12];
    unsigned a;
    u32 col;
    void **tn = (void **)(S.md + MD_TEXTS + 4u * i), **tt = (void **)(S.md + MD_ICONS + 4u * i);
    float y = lin(S.ny0, i, S.dy), tx = S.tx;
    if (S.g->scr == GNB_CREATE || (S.g->scr == GNB_PASS && S.g->p_for == 1u)) tx = S.nx;      /* the Create form has no label column: every row names itself, from the left edge */
    if (S.kind == 3u) { num[0] = 0; tit[0] = 0; a = 0; }                         /* the room screen draws its own texts */
    else if (S.kind == 0u) {
        a = gnb_cols(S.g, i, num, tit, reg, mod, cnt);
        tx = add(S.nx, DX_HOST);
        fit_name(tit, HOST_PX);
        if (i >= evo_rows() || pass_art()) { reg[0] = 0; mod[0] = 0; cnt[0] = 0; tit[0] = 0; }           /* not revealed yet (evo.h) */
        xs(X_REG + i, add(S.nx, DX_REG), y, ROW_FONT, COL[(a & 3u) ? 1 : 0], 0, reg);
        if (same(mod, "Singles")) mod[0] = 0;                                   /* every room plays Singles: no column (a text object less per row: pool, GUI audit GN-01); another mode would show */
        xs(X_MODE + i, add(S.nx, DX_MODE), y, MODE_FONT, COL[(a & 3u) ? 1 : 0], 0, mod);              /* the room type's short name ("Low-Mid" is the widest: 81 px at the list font, 67 px at MODE_FONT, the column has 64 px + the 7 px gap before the count box) */
        {   /* "12 of 16" -> "12/16" (the original list's n/8) */
            char sl[12];
            unsigned k = 0, o = 0;
            while (cnt[k] && o < 10u) { if (cnt[k] == ' ' && cnt[k + 1] == 'o' && cnt[k + 2] == 'f' && cnt[k + 3] == ' ') { sl[o++] = '/'; k += 4u; } else sl[o++] = cnt[k++]; }
            sl[o] = 0;
            xs(X_CNT + i, add(S.nx, DX_CNTBOX), y, ROW_FONT, COL[(a & 3u) ? 1 : 0], 1, sl);      /* centred on its box (the slash was loose and the text sat right of the middle) */
        }
    } else a = gnb_row(S.g, i, num, tit);
    col = COL[a & 3u];
    if (i >= evo_rows() || pass_art()) { num[0] = 0; tit[0] = 0; }             /* not revealed yet (evo.h) */
    if (!force && same(num, S.rnum[i]) && same(tit, S.rtit[i]) && col == S.rcol[i]) return;
    tfree(tn); tfree(tt);
    copy(S.rnum[i], num, GNB_NUM); copy(S.rtit[i], tit, 31); S.rcol[i] = col;
    if (num[0]) { *tn = text_at(S.nx, y, S.nz, S.kind == 0u ? ROW_FONT : NUM_FONT, S.kind == 0u ? COL[(a & 3u) ? 1 : 0] : COL_NUM, 0); if (*tn) gnbev_sis(*tn, "%s", num); }
    if (tit[0]) {
        u8 *t = (u8 *)text_at(tx, y, S.nz, S.kind == 0u ? ROW_FONT : 0.035f, col, 0);
        *tt = t;
        if (t) { F(t + TX_BW) = S.bw; F(t + TX_BH) = S.bh; gnbev_sis(t, "%s", tit); }
    }
}

/* the action row under the list (Create / Join / Random); L/R select, A runs; replaces the key line */
static void action_draw(void)
{
    unsigned i;
    float y = mula(S.ny0, F_HINT_ROWS, S.dy);
    for (i = 0; i < GNB_ACT_N; i++) {
        u32 c = !gnb_act_enabled(S.g, i) ? COL[1] : (S.g->focus && S.g->act_sel == i) ? COL[2] : COL[0];
        xs(X_ACT + i, add(S.nx, ACT_DX[i]), y, COL_FONT, c, 0, gnb_act_label(S.g, i));
    }
}

/* the room screen (original Rooms): header (code left, mode and stage rule right), match slot (two "?" portraits, names, VS, status),
   right panel (Up Next, queue, Lobby n of cap), footer.  Texts only: the retail Event frame stays behind. */
static const float RX_NAME_L = 0.6f, RX_NAME_R = 7.4f, RX_VS = 4.6f, RX_QMARK_L = 1.8f, RX_QMARK_R = 8.6f, RX_PANEL = 14.4f, RX_RIGHT = 21.4f;
static const float RY_NAME = 1.1f, RY_QMARK = 2.1f, RY_VS = 2.6f, RY_STATUS = 6.4f;
static const float RY_PANEL[8] = { 1.0f, 2.1f, 3.0f, 3.9f, 4.8f, 5.7f, 6.8f, 7.7f };
static void room_draw(void)
{
    struct gnb *g = S.g;
    const struct gnb_lobby *v = &g->lob;
    char t[44], c4[6], num[12];
    unsigned i, n, mine;
    float y;
    gnb_code4(v->code, c4);
    t[0] = 0; copy(t, g->rk.on ? "Ranked " : "Room ", 8);               /* (a Grandmaster set has no room code) */
    { unsigned k = 0; while (t[k]) k++; copy(t + k, c4, 4); }
    if (g->tn.on) copy(t, g->tn.name, 18);
    xs(X_LOB + 0u, S.nx, mula(S.ny0, F_TITLE_ROWS, S.dy), TITLE_FONT, COL[0], 0, t);
    { char op[24]; gnb_open_text(g, op); xs(X_OPEN, S.nx, mula(S.ny0, -1.1f, S.dy), HINT_FONT, COL[1], 0, op); }
    { char r1[24], r2[24]; gnb_lobby_rule_parts(g, r1, r2);
      xs(X_LOB + 1u, add(S.nx, RX_RIGHT), mula(S.ny0, F_TITLE_ROWS, S.dy), 0.035f, COL[0], 2, r1);
      xs(X_LOB + 2u, add(S.nx, RX_RIGHT), mula(S.ny0, -1.1f, S.dy), HINT_FONT, COL[1], 2, r2); }
    y = mula(S.ny0, RY_NAME, S.dy);
    xs(X_LOB + 3u, add(S.nx, RX_NAME_L), y, 0.026f, v->side_on[0] ? COL[0] : COL[1], 0, v->side_on[0] ? v->side_name[0] : "Waiting");
    xs(X_LOB + 4u, add(S.nx, RX_NAME_R), y, 0.026f, v->side_on[1] ? COL[0] : COL[1], 0, v->side_on[1] ? v->side_name[1] : "Waiting");
    y = mula(S.ny0, RY_QMARK, S.dy);
    xs(X_LOB + 5u, add(S.nx, RX_QMARK_L), y, 0.09f, COL[1], 0, v->side_on[0] ? " " : "?");
    xs(X_LOB + 6u, add(S.nx, RX_QMARK_R), y, 0.09f, COL[1], 0, v->side_on[1] ? " " : "?");
    xs(X_LOB + 7u, add(S.nx, RX_VS), mula(S.ny0, RY_VS, S.dy), 0.05f, COL[0], 0, "VS");
    {
        char ph[GNB_LINE + 1];
        unsigned a = gnb_panel(g, 1, ph);
        xs(X_LOB + 8u, add(S.nx, RX_NAME_L), mula(S.ny0, RY_STATUS, S.dy), 0.03f, a == 2u ? COL[2] : COL[1], 0, ph);
    }
    xs(X_LOB + 9u, add(S.nx, RX_PANEL), mula(S.ny0, RY_PANEL[0], S.dy), 0.03f, COL[2], 0, "Up next");
    mine = (v->flags & GNB_V_QUEUED) ? v->pos : 0u;
    n = v->nnext;
    for (i = 0; i < 4u; i++) {
        const char *nm = i < n ? v->next[i] : (i == 0u ? "Nobody in the queue" : "");
        xs(X_LOB + 10u + i, add(S.nx, RX_PANEL), mula(S.ny0, RY_PANEL[1u + i], S.dy), 0.026f, (i < n && mine == i + 1u) ? COL[2] : i < n ? COL[0] : COL[1], 0, nm);
    }
    num[0] = 0; copy(num, "Lobby ", 8);
    { unsigned k = 0; char m[8]; unsigned q = v->members; m[0] = 0; while (num[k]) k++; if (q >= 10u) { m[0] = (char)('0' + q / 10u); m[1] = (char)('0' + q % 10u); m[2] = 0; } else { m[0] = (char)('0' + q); m[1] = 0; } copy(num + k, m, 3); }
    {
        char l2[24];
        unsigned k = 0, cap = gnb_lobby_cap(g);
        copy(l2, num, 12);
        while (l2[k]) k++;
        l2[k++] = ' '; l2[k++] = 'o'; l2[k++] = 'f'; l2[k++] = ' ';
        if (cap >= 10u) { l2[k++] = (char)('0' + cap / 10u); } l2[k++] = (char)('0' + cap % 10u); l2[k] = 0;
        xs(X_LOB + 14u, add(S.nx, RX_PANEL), mula(S.ny0, RY_PANEL[5], S.dy), 0.03f, COL[2], 0, l2);
    }
    {
        char you[GNB_LINE + 1];
        gnb_panel(g, 2, you);
        xs(X_LOB + 15u, add(S.nx, RX_PANEL), mula(S.ny0, RY_PANEL[6], S.dy), 0.024f, COL[0], 0, you);
    }
}

/* the detail panel: the retail description box under the list (the Event Match screen has no free column right of the list on a 4:3 picture: Dolphin proof
   gnb-s1-a), four small lines; line 0 lives in the retail description slot (the retail exit frees that slot unconditionally: never NULL there) */
static const float F_DESC[GNB_DESC_LINES] = { 10.70f, 11.27f, 11.84f };
static const float F_LISTSTAT = 9.55f;      /* the list's status line: one row up from the description slot, so it sits inside the list box above its bottom border */
static void join2(char *d, const char *a, const char *b, unsigned lim)
{
    unsigned n = 0, i = 0;
    while (a[i] && n < lim) d[n++] = a[i++];
    if (a[0] && b[0]) { d[n++] = ' '; d[n++] = ' '; d[n++] = ' '; }
    i = 0;
    while (b[i] && n < lim) d[n++] = b[i++];
    d[n] = 0;
}
/* mode 0 the four detail lines; 1 none (the room screen of the Rooms look says it all in its own boxes); 2 only the status message (the Rooms look's list: the columns
   say the rest).  Line 0 stays at least a space: the retail exit frees that slot. */
static void panel_draw(int mode)
{
    unsigned i;
    char p[GNB_PANEL][64], ln[64];
    unsigned at[GNB_PANEL];
    for (i = 0; i < GNB_PANEL; i++) at[i] = gnb_panel(S.g, i, p[i]);
    for (i = 0; i < GNB_DESC_LINES; i++) {
        unsigned a;
        float y = mula(S.ny0, mode == 2 && !i ? F_LISTSTAT : F_DESC[i], S.dy);
        if (mode == 1 || (mode == 2 && i)) { ln[0] = 0; a = 0; }
        else if (mode == 2) {
            copy(ln, p[GNB_PANEL - 1u], 46); a = at[GNB_PANEL - 1u];
            if (!ln[0] && S.g->scr == GNB_LIST && !S.g->more) { gnb_status_text(S.g, ln); a = 1; }      /* no message: what the filters show ("Showing: Europe, any stages") */
        }
        else if (i == 2u && p[GNB_PANEL - 1u][0]) { copy(ln, p[GNB_PANEL - 1u], 60); a = at[GNB_PANEL - 1u]; }      /* a status message takes the last line */
        else { join2(ln, p[2u * i], p[2u * i + 1u], gnb_char_box(S.g) ? 40u : 46u);      /* the Select Character box (call-up only) starts at about 41 characters */ a = at[2u * i] == 3u || at[2u * i + 1u] == 3u ? 3u : (i == 0u ? 2u : at[2u * i] & 1u); }
        if (!evo_chrome()) ln[0] = 0;                                           /* every text after the zoom-in (evo.h) */
        if (i == 0u) {
            void **d = (void **)(S.md + MD_DESC);
            S.pan[0].t = *d;
            if (!ln[0]) { ln[0] = ' '; ln[1] = 0; }
            gb_force = 1; tslot_set(&S.pan[0], S.nx, y, PANEL_FONT, COL[a & 3u], 0, ln); gb_force = 0;
            *d = S.pan[0].t;
            continue;
        }
        if (i == 2u && mode == 0 && p[GNB_PANEL - 1u][0]) {      /* the description line: longer than the panel is wide (about 44 characters, Ditto Dojo has 56) = two slots, split at the last space, the second one row below */
            unsigned k = 0;
            while (ln[k]) k++;
            if (k > 44u) {
                k = 44u;
                { unsigned j = k; while (j && !(ln[j] == ' ' && ln[j - 1u] == '.')) j--; if (j) k = j; else while (k && ln[k] != ' ') k--; }      /* after a sentence when one fits, else at the last space */
                if (k) { ln[k] = 0; copy(S.rest, ln + k + 1u, 44); S.rest_a = a; }
            }
        }
        tslot_set(&S.pan[i], S.nx, y, PANEL_FONT, COL[a & 3u], 0, ln);
    }
    {
        float y2 = mula(S.ny0, 12.41f, S.dy);      /* F_DESC[2] + one row */
        tslot_set(&S.pan[3], S.nx, y2, PANEL_FONT, COL[S.rest_a & 3u], 0, S.rest);
        S.rest[0] = 0;
    }
}

static void badge_draw(void)
{
    char b[16];
    void **n = (void **)(S.md + MD_NAME);
    struct tslot s;
    gnb_badge(S.g, b);
    if (art_on() || !evo_chrome()) b[0] = 0;
    if (!b[0]) { b[0] = ' '; b[1] = 0; }
    s.t = *n; copy(s.s, S.sub.s, 41); s.col = S.sub.col; s.tag = 0;                    /* the badge lives in the retail score box slot (freed unconditionally by the retail exit) */
    gb_force = 1; tslot_set(&s, S.sx, S.sy, 0.03f, COL[0], 2, b); gb_force = 0;
    *n = s.t; copy(S.sub.s, s.s, 41); S.sub.col = s.col;
}

/* the retail right-edge "Select Character" box (joint 4 and its child, MnMaAll) and the "High Score" strip (joint 6, with the type label joint 9): neither means anything before a
   room is joined; our heading text stands where the strip's label was */
static void chrome_draw(void)
{
    char h[16];
    int art = art_on();
    joint_hide(4u, art || !gnb_char_box(S.g));
    joint_hide(6u, 1);
    gnb_head(S.g, h);
    if (art || !evo_chrome()) h[0] = 0;
    tslot_set(&S.head, S.nx, S.sy, 0.03f, COL[2], 0, h);
}

static void cursor_draw(void)
{
    unsigned n = gnb_rows(S.g);
    joint_y(0xBu, lin(ZERO, S.g->page, S.cur_dy));
    joint_hide(0xBu, n == 0u || S.kind == 3u || art_on());                      /* no cursor on a state screen; the Rooms look draws its own */
    joint_hide(3u, (S.g->first == 0u && !art_on()) || (S.kind == 3u && art_on()));          /* the Rooms look shows both arrows in the notch all the time (rooms_pr_1) */              /* up arrow (mnEvent_8024D014) */
    joint_hide(2u, ((unsigned)S.g->first >= gnb_max_first(S.g) && !art_on()) || (S.kind == 3u && art_on()));      /* down arrow */
    S.shown_page = S.g->page; S.shown_first = S.g->first;
}

/* ---- the original Rooms look: retail art by reference (gnb_art.c) under our texts ----
   Layout in pixels of the 640 x 528 Dolphin frame (the EFB; the PR screenshots measured and scaled), turned into text coordinates by the scale measured on the
   release-385 frames (the number column x = 142 px, row 0 = 152 px, 18.6 px per text unit across, 26.8 px per row step) and into world coordinates by y = -text y
   (Dolphin run p1: the boxes land where asked).  The GmTou box draws its rounded border 15 px outside its corner bones and sits 10 px above a text of the same y. */
#define GNB_FOOT_Y 458.0f             /* text y (px of the 640 x 528 frame) of the footer at the Settings height: its ink lands at y 464 (tune from the Dolphin shots) */
static const float PXK = 0.05376f, PYROW = 0.037313f, PX0 = -7.634f, PY0 = -152.0f, BOX_DY = 10.0f;
static float TX(float px) { return mula(S.tx0, px, PXK); }
static float TY(float py) { return mula(S.ty0, py, S.pyk); }
static float art_dz = 0.0f;              /* added to the next boxes' z (the portrait boxes sit behind their "?") */
/* a box centred at text (tx, ty); bw, bh = the corner bones' half sizes in pixels (visible half size - 15); sy scales the whole box vertically (thin bars) */
static void art_box_t(unsigned slot, float tx, float ty, float bw, float bh, float sy, unsigned rgb)
{
    gart_put(slot, GART_BOX, tx, sub(ZERO, mula(ty, BOX_DY, S.pyk)), add(S.nz, art_dz), mula(ZERO, bw, PXK), mula(ZERO, bh, S.pyk), rgb, ONE);
    gart_scale(slot, ONE, sy);
}
static void art_box(unsigned slot, float cx, float cy, float bw, float bh, unsigned rgb) { art_box_t(slot, TX(cx), TY(cy), bw, bh, ONE, rgb); }
static void axs(unsigned i, float px, float py, float font, u32 col, int align, const char *str) { xs(i, TX(px), TY(py), font, col, align, str); }

/* ---- key hints with button badges (keyhint.h; owner GUI pass A1 icons instead of "A:", A3 one footer style, the keys in equal shares) ----
   A token "Hold B: Leave" becomes  Hold (B) Leave: the action text, the letter text of each key with its coloured rounded plate (toast_pill.h) behind it, the prefix text.  The slots of a key:
   its action slot and three of ours (letter 0, letter 1, prefix) from `lb`.  A badge whose letter text cannot be made (the SIS pool guard) falls back to the old text "B: Leave" in the action slot. */
#define X_KLB 57u                        /* the room footer's key k: letters and prefix at 57 + 3 k (57..68) */
#define X_HA 69u                         /* the hint line's action slots 69..72, its letters and prefixes 73 + 3 k (73..84) */
#define X_HLB 73u
#define X_CSB 85u                        /* the costume line's letters and prefix (85..87) */
#define KEV_SLOT(i) (S.X[i])
#define kev_made gb_made
#define kev_tag gb_tag
static void kev_xs(unsigned i, float x, float y, float font, u32 col, int align, const char *str) { xs(i, x, y, font, col, align, str); }
#include "keyhint_ev.h"                  /* key_tok / key_line: the badges' texts */
static float TXI(int px) { return TX(kev_fq(px * 65536)); }      /* an integer pixel column (Q16 -> float by integer arithmetic only: soft-float) */

enum { AB_HEAD, AB_OUTER, AB_INNER, AB_INNER2, AB_LEFT, AB_RIGHT, AB_FOOT, AB_PORT_L, AB_PORT_R, AB_ME, AB_N };
enum { AF_FRAME = AB_N, AF_CUR, AF_LINE, AF_TRACK, AF_BTN0, AF_CNT0 = AF_BTN0 + GNB_ACT_N, AF_SEL = AF_CNT0 + GNB_VIS, AF_END };
enum { AI_L = AF_END, AI_R, AG_PANEL, AG_PANEL2, AG_CUR, AG_GW, AG_CELL0, AI_END = AG_CELL0 + GNB_PICK_N, AG_CHAT = AI_END };    /* the two fighter portraits, the pick grid (gart_icon) */
#define COL_DIM2 0xB4B4B4FFu
#define COL_GO 0x7CE85AFFu
#define COL_GREEN 0x8CF06EFFu

typedef char gnbev_chat_slot_check[AG_CHAT < GART_SLOTS ? 1 : -1];
#define P_BRK 62u                                         /* the yellow cursor bracket under the selected stage and the thin white frame round it (also the OK / Redo boxes): the two slots above the chat's (AG_CHAT = 61, GART_SLOTS = 64).  They were AI_L / AI_R = 28 / 29, which are the dark plates of stage rows 1 and 2: the plates hid the cursor on rows 0 to 2 */
#define P_SEL 63u
typedef char gnbev_prep_cur_slot_check[P_SEL < GART_SLOTS && P_BRK > AG_CHAT ? 1 : -1];
#define X_CHAT 91u                       /* the room chat: 91..93 the strip's three lines; with the picker open 91..94 the four cells and 95 its title (the strip gives way to it) */
#define CHAT_X 321.0f                    /* the strip (inside the header panel, in the middle of it, so the panel is the backdrop; it was below the centre panel, half over its border): centred on the header's middle, */
static const float CHAT_Y[3] = { 45.0f, 57.0f, 69.0f };      /* three lines inside the header box (y 37..99), clear of the room code (ends near x 190) and of the rules at the right (start near x 400) */
#define CHAT_FONT 0.02f
#define CHAT_MAXC 24u                    /* characters per line (about 7 px each at this size: 170 px, x 235..407); a longer line ends in two dots */
/* the picker (it must look EXACTLY like the quick chat window of the character screen): the layout is qc_win.h's, the same code qc_gc.c uses (pitch 20, paddings 12 / 4, column 180, the
   0.84 cut rule, title "Chat: Up" in gold top left, then "Up: ..  Left: .." / "Right: ..  Down: .."), drawn on this canvas: ONE toast plate (toast_pill.h, a text slot with its own rectangle, made BEFORE the
   texts so that they draw over it) shrink-wrapped to the texts and centred on the screen's middle, under the VS panel's status line (y 308) and over the footer box (y 412).  This canvas differs from the
   character screen's in text units only, so the px of qc_win.h are converted by the lobby's own TX / TY (one measured scale) and the glyphs are given the character screen's size and width by two font words. */
#include "../qc/qc_win.h"
#define CQ_CX 321                        /* the plate's centre x, px */
#define CQ_Y0 330                        /* the plate's top, px */
#define CQ_DY 8                          /* the plate text's box top sits 8 px above the y given (Dolphin, p1): the plate is moved down by this */
#define X_CQPLATE 96u                    /* the plate's own slot (a space: the text is only the plate's carrier) */
#define CQ_FONT 0.023f                   /* glyph height: font y; the width is the next word (the character screen draws its glyphs x 0.38 / y 0.5 of the cell) */
#define CQ_FXB 0x3C9CF56Fu               /* font x = 0.01916f */
#define CQ_TDY 11                       /* px from a row's top (qc_win.h QW_ROW_Y) to the y of its text (the glyph top sits this far under the text y, measured in Dolphin) */
#define CQ_TITLE 0xFFEA2FFFu             /* the character screen's header colour (a session is up) */
static float pxf(int v) { union { float f; u32 u; } k; k.u = tp_f2(v, 0); return k.f; }       /* an integer px as a float without float arithmetic */
static void chat_slots_clear(void)
{
    unsigned i;
    gart_hide(AG_CHAT);
    for (i = 0; i < 5u; i++) xs(X_CHAT + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
    xs(X_CQPLATE, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
}
static void art_hide(unsigned from, unsigned to) { while (from < to) gart_hide(from++); }

static void cat_num(char *d, unsigned v)
{
    unsigned k = 0;
    while (d[k]) k++;
    if (v >= 100u) d[k++] = (char)('0' + v / 100u % 10u);
    if (v >= 10u) d[k++] = (char)('0' + v / 10u % 10u);
    d[k++] = (char)('0' + v % 10u);
    d[k] = 0;
}

/* the fighter portraits of the two players on stage: the retail CSS icon (64x56, at its own pixel size) inside a box a little larger
   than the icon; the template quad (2.99 x 2.99 units, gnb_art.c) scaled to 64 x 56 px.  0: no portrait (the slot is empty, the character unknown or Random, or any
   check failed): the box keeps its "?" */
static const float GRID_TX = 9.5f;       /* pick grid: a text centred on a cell sits 9.5 px left of the cell's art (measured in Dolphin: the Random box centre x 465.5 / '?' glyph centre 468 with 12, SHEIK 468 against the cell 465.5) */
/* lobby slot boxes, measured in Dolphin (run22, 640 x 528 frame): the art boxes are drawn at different depths (dz +1 / -1), so the perspective moves them: left box x 106..180 (centre 143), right box 272..342 (centre 307), both y 205..270 (centre 237.5); texts are drawn at depth 0 and need these centres, not PORT_X */
static const float LB_CX[2] = { 143.0f, 307.0f };
static const float LB_QY = 227.5f;      /* the '?' glyph: text y that centres the glyph on y 237.5 (glyph height 30: the text y is the glyph top minus 3) */
static const float LB_VS = 227.5f;      /* VS text x: its centre lands on 225.5, midway between the boxes' inner edges 180 and 272 (gaps 26 / 26) */
static const float PORT_X[2] = { 150.0f, 308.0f }, PORT_Y = 236.5f, PORT_BW = 21.0f, PORT_BH = 17.0f;
static const float ICON_SX = 64.0f * 0.05376f / 2.990234375f, ICON_SYK = 56.0f / 2.990234375f, ICON_DZ = 1.5f;
static int portrait(unsigned side)
{
    unsigned ch = gnb_side_char(S.g, side);
    if (ch >= GART_ICON_N || !gart_icon(AI_L + side, ch, TX(PORT_X[side]), sub(ZERO, mula(TY(PORT_Y), BOX_DY, S.pyk)), add(S.nz, ICON_DZ), ICON_SX, mula(ZERO, S.pyk, ICON_SYK))) {
        gart_hide(AI_L + side);
        return 0;
    }
    return 1;
}

/* room chat: the last three lines "Name: text" under the centre panel (a new line in gold for a few seconds, "The host muted you" in red); with the
   D-Pad picker open the strip gives way to a plate with the four texts of the group as a cross (Up on top, Left and Right at the sides, Down below, the group's name above) */
static void chat_draw(void)
{
    struct gnb *g = S.g;
    char ln[64];
    unsigned i, a;
    if (g->qc.open && evo_chrome()) {
        static const char DIR[4][8] = { "Up: ", "Left: ", "Right: ", "Down: " };
        char hd[24], cell[QC_TXT + 1u], cl[4][44];
        unsigned char *rp[4];
        struct qw_plan pl;
        union { float f; u32 u; } pk;
        int x0, tag0 = gb_tag;
        gart_hide(AG_CHAT);
        pk.f = S.pyk;
        gnb_qc_head(g, hd);
        for (i = 0; i < 4u; i++) {
            gnb_qc_cell(g, i, cell);
            { unsigned k = 0; copy(cl[i], DIR[i], 8); while (cl[i][k]) k++; copy(cl[i] + k, cell[0] ? cell : "-", 43u - k); }
            rp[i] = (unsigned char *)cl[i];
        }
        qw_plan(&pl, (unsigned char *)hd, rp, 1);
        x0 = CQ_CX - pl.w / 2;
        gb_tag = pl.w + 1000;                                                         /* the plate and the texts are made again together when the width changes (their x depends on it) */
        gb_made = 0;
        axs(X_CQPLATE, pxf(x0), pxf(CQ_Y0), 0.03f, COL[0], 0, " ");                   /* first: the texts below are made after it and draw over it */
        if (gb_made && S.X[X_CQPLATE].t) tp_attach_at(S.X[X_CQPLATE].t, 0, CQ_DY * tp_q(pk.u, 16), pl.w * 3523, pl.h * tp_q(pk.u, 16));    /* PXK x 65536 = 3523 */
        gb_fxb = CQ_FXB;
        axs(X_CHAT + 4u, pxf(x0 + pl.x[0]), pxf(CQ_Y0 + QW_ROW_Y(0) + CQ_TDY), CQ_FONT, CQ_TITLE, 0, hd);
        for (i = 0; i < 4u; i++)
            axs(X_CHAT + i, pxf(x0 + pl.x[i & 1u]), pxf(CQ_Y0 + QW_ROW_Y(1 + (int)(i >> 1)) + CQ_TDY), CQ_FONT, COL[0], 0, cl[i]);
        gb_fxb = 0;
        gb_tag = tag0;
        return;
    }
    gart_hide(AG_CHAT);
    axs(X_CHAT + 3u, 0.0f, 0.0f, 0.02f, COL[0], 0, ""); axs(X_CHAT + 4u, 0.0f, 0.0f, 0.02f, COL[0], 0, ""); xs(X_CQPLATE, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
    {   char ck[8];
        gnb_pick_clock(g, ck);                                                       /* a pick turn's countdown owns the header's middle: the chat waits (it is back when the clock ends) */
        for (i = 0; i < GNB_CL_N; i++) {
            a = evo_chrome() && !ck[0] ? gnb_chat_line(g, i, ln) : 0u;
            if (a) {
                unsigned n = 0;
                while (ln[n]) n++;
                if (n > CHAT_MAXC) { ln[CHAT_MAXC - 2u] = '.'; ln[CHAT_MAXC - 1u] = '.'; ln[CHAT_MAXC] = 0; }
            }
            axs(X_CHAT + i, CHAT_X, CHAT_Y[i], CHAT_FONT, a == 1u ? COL[0] : a == 2u ? COL[3] : COL_DIM2, 1, a ? ln : "");
        }
    }
}

/* the room screen as rooms-beta-2's Room scene: header box (Room code left; mode and stage rule right), the centre panel (outer box, dark inner box, the match box with
   two "?" portraits, names, VS and the status; the queue box: Up Next, the queue, Lobby n/cap, you), the footer box with the keys */
static const float QY[3] = { 200.0f, 220.0f, 240.0f };
static void grid_art(void);
static void prep_art(void);              /* the stage prep screen (gnb_prep_ev.h, included after grid_art) */
/* roster rows (precomputed: the overlay is soft-float, no float arithmetic at run time) */
static const float ROS_Y[8] = { 214.0f, 234.0f, 254.0f, 274.0f, 294.0f, 314.0f, 334.0f, 354.0f };
#define ROS_XN 104.0f      /* Room Standings columns (640x480 layout units): Player (left edge) | Status (left edge) | Crowns (right edge) | Streak (right edge) */
#define ROS_XS 296.0f
#define ROS_XC 450.0f
#define ROS_XK 532.0f
#define ROS_HEAD_Y 152.0f    /* y of the title and of the lead block's first line (same baseline); the title's left edge is ROS_XN (the Player column and the name column), the lead block's right edge is ROS_XK (the Streak column) */
#define ROS_LEAD2_Y 168.0f   /* the lead block's second, small line (ends about y 179: the column header row is at 190) */
#define ROS_HEAD_FONT 0.03f  /* the title and the lead line's first line: one size; 'Wobble Pilot and Wobble Pilot' would not fit beside the title, so the two-name form falls back to '2 players' */
#define ROS_HEAD_PX 428u     /* panel text width: ROS_XN 104 .. ROS_XK 532 */
static void room_art(void)
{
    struct gnb *g = S.g;
    const struct gnb_lobby *v = &g->lob;
    char t[44], c4[6];
    unsigned i, n, mine;
    int port[2];
    art_hide(AF_FRAME, AF_END);
    gart_hide(P_BRK); gart_hide(P_SEL);                                              /* (the stage prep's cursor slots, above the chat's) */
    if (g->prep.open && !g->pick_open) { prep_art(); return; }                                       /* a set room's stage prep in Slippi's GameSetup look (strike / pick the stage, the wait between two games): gnb_prep_ev.h draws the whole screen */
    art_box(AB_HEAD, 321.0f, 68.0f, 291.5f, 31.0f, 0x8C8C96FFu);                  /* the header box: y 37..99 (was 37..88): the big line at 48, a gap of about 9, the small line at 77 (0.027 high about 14) and padding below */
    art_box(AB_FOOT, 321.0f, 454.0f, 291.5f, 30.0f, 0x8C8C96FFu);
    gnb_code4(v->code, c4);
    t[0] = 0; copy(t, g->rk.on ? "Ranked " : "Room ", 8);               /* (a Grandmaster set has no room code) */
    { unsigned q = 0; while (t[q]) q++; copy(t + q, c4, 4); }
    if (g->tn.on) copy(t, g->tn.name, 18);                                  /* a tournament match: the tournament's own name, not the room's code */
    axs(X_LOB + 0u, 41.0f, 52.0f, 0.04f, COL[0], 0, t);
    { char r1[24], r2[24]; gnb_lobby_rule_parts(g, r1, r2);                     /* the rules, big (no "Singles"): clear of the room code (ends near x 170) and of the pick clock (centred on 321, ends near 350) */
      axs(X_LOB + 1u, 600.0f, 50.0f, 0.04f, COL[0], 2, r1);
      axs(X_LOB + 2u, 600.0f, HDR_SMALL_Y, HDR_SMALL_FONT, COL_DIM2, 2, r2); }
    { char ck[8]; gnb_pick_clock(g, ck); axs(X_LOB + 18u, 321.0f, 52.0f, 0.04f, COL[0], 1, ck); }     /* the pick turn's countdown, centred in the header (Rooms) */
    { char op[24]; gnb_open_text(g, op); axs(X_OPEN, 41.0f, HDR_SMALL_Y, HDR_SMALL_FONT, COL_DIM2, 0, op); }     /* under the room code, left: at most 20 characters (about 150 px at 0.022), clear of the pick clock at 321 */
    {   /* the footer's keys spread over the box: the line is split at its double spaces, each key centred in an equal share of the box (x 29.5..612.5), the keys as badges (keyhint.h) */
        static const unsigned SL[4] = { X_LOB + 17u, 54u, 55u, 56u };
        key_line(SL, X_KLB, gnb_foot(g), TX(29.5f), 2053909, TY(456.0f), 300u, COL[0]);                  /* the box x 29.5 .. 612.5 px = 583 px = 2053909 Q16 units (3523 a pixel) */
    }
    if (!g->pick_open) { for (i = 0; i < 3u; i++) xs(X_CSB + i, 0.0f, 0.0f, 0.03f, COL[0], 0, ""); }               /* the costume line's badges only live in the grid */
    if (g->pick_open) {                                                             /* the grid popup takes the centre: the room's boxes and texts give way */
        art_hide(AB_OUTER, AB_FOOT); art_hide(AB_PORT_L, AB_N); gart_hide(AI_L); gart_hide(AI_R); chat_slots_clear();
        for (i = 3u; i <= 20u; i++) if (i != 17u && i != 18u && i != 19u) axs(X_LOB + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");      /* 17 is the footer, 18 the pick clock, 19 the costume line: they stay (GUI audit RM-03) */
        {   /* the costume of the cell under the cursor, under the clock: L R picks it in the grid (discoverable) */
            char cs[28];
            unsigned k = 0;
            cs[0] = 0;
            if (g->pick_cur < GNB_CHAR_RANDOM && !g->pick_lock) {
                copy(cs, "L R: Costume ", 20);
                cat_num(cs, (unsigned)g->pick_cos[g->pick_cur] + 1u);
                while (cs[k]) k++;
                cs[k++] = ' '; cs[k++] = 'O'; cs[k++] = 'f'; cs[k++] = ' '; cs[k] = 0;
                cat_num(cs, gnb_costumes(g->pick_cur));
            }
            key_tok(X_LOB + 19u, X_CSB, cs, TX(321.0f), 1, 1, TY(HDR_SMALL_Y), 270u, COL_DIM2);
        }
        for (i = 0; i < GNB_ROSTER_MAX; i++) axs(X_GRID + 3u * GNB_ROSTER_MAX + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
        grid_art();
        return;
    }
    if (g->ros_open) {                                                              /* the Y crowns panel takes the centre like the pick grid: title and streak, a header row, 8 table rows */
        char nm[24], cr[8], st[14];
        unsigned k;
        art_hide(AB_OUTER, AB_FOOT); art_hide(AB_PORT_L, AB_N); gart_hide(AI_L); gart_hide(AI_R); chat_slots_clear();
        for (i = 3u; i <= 20u; i++) if (i != 17u) axs(X_LOB + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");      /* 17 is the footer: it stays */
        for (i = 0; i < GNB_PICK_N; i++) axs(X_GRID + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
        for (i = 0; i < GNB_ROSTER_MAX; i++) axs(X_GRID + 3u * GNB_ROSTER_MAX + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");     /* (the Streak column's slots follow the 27 grid labels' range: 45..52) */
        art_hide(AG_PANEL, AI_END);
        art_dz = 1.6f;
        art_box(AG_PANEL, 321.0f, 263.0f, 231.0f, 117.0f, 0x000000FFu);
        art_box(AG_PANEL2, 321.0f, 263.0f, 231.0f, 117.0f, 0x000000FFu);
        art_dz = 0.0f;
        axs(X_LOB + 3u, ROS_XN, ROS_HEAD_Y, ROS_HEAD_FONT, COL[0], 0, "Room standings");
        if (g->ros_have) {
            char l1[44], l2[44];
            gnb_lead_lines(g, l1, l2, 0);
            if (est_w("Room standings") + 24u + est_w(l1) > ROS_HEAD_PX) gnb_lead_lines(g, l1, l2, 1);        /* the first line must clear the title (font 0.03 for both: estimates at 0.03) */
            axs(X_LOB + 4u, ROS_XK, ROS_HEAD_Y, ROS_HEAD_FONT, l2[0] ? COL[0] : COL_DIM2, 2, l1);
            axs(X_LOB + 10u, ROS_XK, ROS_LEAD2_Y, 0.022f, COL_DIM2, 2, l2);
        }
        axs(X_LOB + 5u, ROS_XN, 190.0f, 0.02f, COL_DIM2, 0, "Player");
        axs(X_LOB + 7u, ROS_XS, 190.0f, 0.02f, COL_DIM2, 0, "Status");
        axs(X_LOB + 6u, ROS_XC, 190.0f, 0.02f, COL_DIM2, 2, "Crowns");
        axs(X_LOB + 9u, ROS_XK, 190.0f, 0.02f, COL_DIM2, 2, "Streak");
        for (i = 0; i < GNB_ROSTER_MAX; i++) {
            k = gnb_roster_row(g, i, nm, cr, st);
            if (k) {                                                                /* k: 1, +1 the viewer (green), +2 a stage player (yellow) */
                u32 c = (k == 2u || k == 4u) ? COL_GO : COL[0];
                char sk[6];
                gnb_roster_streak(g, i, sk);
                fit_name(nm, 184u);                                    /* the standings' size: a long name with " (You)" is cut with a dot before the Status column */
                axs(X_GRID + i, ROS_XN, ROS_Y[i], COL_FONT, c, 0, nm);
                axs(X_GRID + GNB_ROSTER_MAX + i, ROS_XC, ROS_Y[i], COL_FONT, c, 2, cr);
                axs(X_GRID + 2u * GNB_ROSTER_MAX + i, ROS_XS, ROS_Y[i], COL_FONT, st[0] == 'W' ? COL_DIM2 : c, 0, st);
                axs(X_GRID + 3u * GNB_ROSTER_MAX + i, ROS_XK, ROS_Y[i], COL_FONT, sk[0] == '0' ? COL_DIM2 : c, 2, sk);
            }
        }
        if (!g->ros_have) axs(X_LOB + 8u, 321.0f, 260.0f, 0.026f, COL_DIM2, 1, "Loading");
        return;
    }
    art_hide(AG_PANEL, AI_END);
    for (i = 0; i < GNB_PICK_N; i++) axs(X_GRID + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
    for (i = 0; i < GNB_ROSTER_MAX; i++) axs(X_GRID + 3u * GNB_ROSTER_MAX + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
    /* equal gaps: header visible bottom 114, main visible 130.5..392.5, footer visible top 409: 16.5 px above and below (the top used to be 6.5); the panels inside keep their bottoms, their tops move down with it */
    art_box(AB_OUTER, 321.0f, 261.5f, 291.5f, 116.0f, 0xA898C8FFu);
    art_box(AB_INNER, 321.0f, 261.5f, 253.5f, 100.0f, 0x000000FFu);
    art_box(AB_INNER2, 321.0f, 261.5f, 253.5f, 100.0f, 0x000000FFu);
    art_box(AB_LEFT, 229.0f, 261.5f, 149.5f, 92.5f, 0x8C8C96FFu);
    art_box(AB_RIGHT, 488.5f, 261.5f, 75.0f, 92.5f, 0x8C8C96FFu);
    art_dz = 1.0f; art_box(AB_PORT_L, PORT_X[0], PORT_Y, PORT_BW, PORT_BH, 0xC8C8C8FFu);
    art_dz = -1.0f; art_box(AB_PORT_R, PORT_X[1], PORT_Y, PORT_BW, PORT_BH, 0xC8C8C8FFu);
    art_dz = 0.0f;
    port[0] = portrait(0); port[1] = portrait(1);
    axs(X_LOB + 3u, LB_CX[0], 191.0f, 0.026f, v->side_on[0] ? COL[0] : COL_DIM2, 1, v->side_on[0] ? v->side_name[0] : "Waiting");
    axs(X_LOB + 4u, LB_CX[1], 191.0f, 0.026f, v->side_on[1] ? COL[0] : COL_DIM2, 1, v->side_on[1] ? v->side_name[1] : "Waiting");
    axs(X_LOB + 5u, LB_CX[0], LB_QY, 0.06f, COL[1], 1, port[0] ? " " : "?");             /* the "?" glyph: an empty slot or an unknown character */
    axs(X_LOB + 6u, LB_CX[1], LB_QY, 0.06f, COL[1], 1, port[1] ? " " : "?");
    axs(X_LOB + 7u, LB_VS, 238.0f, 0.045f, COL[0], 1, "VS");
    {
        char ph[GNB_LINE + 1], m[44];
        unsigned a = gnb_panel(g, 1, ph), b = gnb_panel(g, GNB_PANEL - 1u, m);
        axs(X_LOB + 8u, 229.0f, 308.0f, 0.028f, a == 2u ? COL_GO : COL[0], 1, ph);
        if (!m[0] && v->streak) { copy(m, "Streak ", 8); cat_num(m, v->streak); b = 0; }
        if (g->qc.open) m[0] = 0;                                                  /* the quick chat picker's title sits right under this line (y 347): the line gives way while the picker is open */
        axs(X_LOB + 16u, 229.0f, 336.0f, 0.022f, b == 3u ? COL[3] : COL_DIM2, 1, m);
    }
    {   /* crowns under BOTH slots, one baseline: "Crowns n" (gold when there are any, dim at 0), a dim "-" for an empty slot */
        char cr[16];
        for (i = 0; i < 2u; i++) {
            cr[0] = 0;
            if (v->side_on[i]) { copy(cr, "Crowns ", 8); cat_num(cr, v->side_crowns[i]); } else copy(cr, "-", 2);
            axs(X_LOB + 19u + i, LB_CX[i], 282.0f, 0.022f, (v->side_on[i] && v->side_crowns[i]) ? COL[0] : COL_DIM2, 1, cr);
        }
    }
    axs(X_LOB + 9u, 417.0f, 172.0f, 0.03f, COL[0], 0, g->tn.on ? "Your opponent" : "Up next");
    mine = (v->flags & GNB_V_QUEUED) ? v->pos : 0u;
    n = g->tn.on ? 0u : v->nnext;
    gart_hide(AB_ME);
    for (i = 0; i < 3u; i++) {
        const char *nm = i < n ? v->next[i] : (n ? "" : (i == 0u ? (g->tn.on ? g->tn.opp : "Nobody queued") : ""));   /* empty queue: one short line inside the box; a tournament match: the named opponent */
        if (i < n && mine == i + 1u) { art_box_t(AB_ME, TX(488.0f), TY(QY[i]), 66.0f, 0.0f, 0.75f, 0x000000FFu); }
        axs(X_LOB + 10u + i, 417.0f, QY[i], 0.026f, (i < n || (g->tn.on && !i)) ? COL[0] : COL_DIM2, 0, nm);
    }
    t[0] = 0; copy(t, "Lobby ", 8); cat_num(t, v->members);
    { unsigned q = 0; while (t[q]) q++; t[q++] = '/'; t[q] = 0; }
    cat_num(t, gnb_lobby_cap(g));
    if (g->tn.on) gnb_tn_clock(g, t);                                       /* while the opponent is not here: the time left to join, in the lobby line */
    if (g->tn.on && !t[0]) { copy(t, "Lobby ", 8); cat_num(t, v->members); { unsigned q = 0; while (t[q]) q++; t[q++] = '/'; t[q] = 0; } cat_num(t, gnb_lobby_cap(g)); }
    axs(X_LOB + 14u, 417.0f, 268.0f, 0.03f, COL[0], 0, t);
    {
        char you[GNB_LINE + 1];
        gnb_panel(g, 2, you);
        if (est_w(you) > 168u) { unsigned k = 0; while (you[k] && you[k] != ' ') k++; if (you[k]) { k++; copy(you, you + k, GNB_LINE); if (you[0] >= 'a' && you[0] <= 'z') you[0] = (char)(you[0] - 32); } }      /* "You: host, not queued" ran past the box's right border: without "You:" it fits */
        axs(X_LOB + 15u, 417.0f, 296.0f, 0.024f, COL[0], 0, you);
    }
    chat_draw();
}

/* the in-room pick (the original Rooms grid popup): a dark panel over the room's centre, the retail character-select icons in their
   own order, 7 per row (the last row 6, its last cell Random "?"), each at its own 64 x 56 px with the patched name, the gold cursor box around the chosen cell.
   Random (26) is the retail "?" icon (gnb_art.c CSS_OFF[26]) like the original; a cell without an icon (any failed check) is a grey box with "?" / its name;
   Sheik shows Zelda's icon with SHEIK under it. */
static const float GRID_X0 = 117.0f, GRID_DX = 68.0f, GRID_Y0 = 168.0f, GRID_DY = 62.0f;
#define DIM_N (AF_END - AF_FRAME)          /* the room's excluded fighters are dimmed by a translucent black plate each: the list screen's art slots AF_FRAME..AF_SEL are free in the room (room_art hides them first) */
static const float DIM_ALPHA = 0.66f;
static const float LOCK_X = 321.0f, LOCK_Y = 222.0f;      /* the locked view (a forced character): the icon's centre, its name 48 px under it, the caption 76 px */
/* an excluded fighter: dimmed by a plate above its icon (z over the icon's 2.5), the strike screen's X over it.  Called AFTER the icon is drawn: the gobjs draw in creation order, a plate made before its icon sat behind it (the dimming never showed) */
static unsigned grid_excl(const struct gnb *g, unsigned i, float px, float py, unsigned nd)
{
    if (gnb_cell_ok(g, i) || nd >= DIM_N) return nd;
    gart_put(AF_FRAME + nd, GART_BOX, TX(px), sub(ZERO, mula(TY(py), BOX_DY, S.pyk)), add(S.nz, 2.7f), mula(ZERO, 17.0f, PXK), mula(ZERO, 13.0f, S.pyk), 0x000000FFu, DIM_ALPHA);
    gart_scale(AF_FRAME + nd, ONE, ONE);
    if (nd < DIM_N / 2u && gart_tex_open()) gart_tex(AF_FRAME + DIM_N / 2u + nd, GT_X, sub(TX(px), mula(ZERO, mula(ZERO, 1.20f, 84.8f), PXK)), sub(ZERO, mula(TY(py), BOX_DY, S.pyk)), add(S.nz, 2.9f), 1.20f, 0.95f, 0xA8A8B200u, 0.8f);      /* the picture is the right half of its canvas (gnb_prep_ev.h PR_QUAD_K: 49.1 px wide and 84.8 px right of the joint per unit of sx) */
    return nd + 1u;
}
static void grid_art(void)
{
    struct gnb *g = S.g;
    unsigned i, r, c, nd = 0;
    float px, py;
    art_dz = 1.6f;
    art_box(AG_PANEL, 321.0f, 263.0f, 231.0f, 117.0f, 0x000000FFu);
    art_box(AG_PANEL2, 321.0f, 263.0f, 231.0f, 117.0f, 0x000000FFu);
    if (g->pick_lock) {                                                             /* a forced character (Ditto Dojo, the player who does not pick): the one fighter, big in the middle, nothing else to choose */
        unsigned f = gnb_forced_char(g);
        for (i = 0; i < GNB_PICK_N; i++) { gart_hide(AG_CELL0 + i); axs(X_GRID + i, 0.0f, 0.0f, 0.03f, COL[0], 0, ""); }
        art_dz = 2.0f;
        art_box(AG_CUR, LOCK_X, LOCK_Y, 21.0f, 17.0f, 0xF0C028FFu);
        if (f == 3u) { art_dz = 2.2f; art_box(AG_GW, LOCK_X, LOCK_Y, 16.0f, 12.0f, 0x4A4A50FFu); } else gart_hide(AG_GW);
        art_dz = 0.0f;
        if (f >= GNB_PICK_N || !(f < GART_ICON_N && gart_icon(AG_CELL0 + f, f, TX(LOCK_X), sub(ZERO, mula(TY(LOCK_Y), BOX_DY, S.pyk)), add(S.nz, 2.5f), ICON_SX, mula(ZERO, S.pyk, ICON_SYK)))) {
            art_dz = 2.2f; art_box(AG_CELL0 + (f < GNB_PICK_N ? f : 0u), LOCK_X, LOCK_Y, 17.0f, 13.0f, 0x6E6E78FFu); art_dz = 0.0f;       /* no icon: the grey box, the name below says who */
        }
        axs(X_GRID + 0u, add(LOCK_X, GRID_TX), add(LOCK_Y, 48.0f), 0.03f, COL[0], 1, f < GNB_PICK_N ? gnb_char_name(f) : "");
        axs(X_GRID + 1u, add(LOCK_X, GRID_TX), add(LOCK_Y, 76.0f), 0.022f, COL_DIM2, 1, "Set for both players");
        return;
    }
    gnb_pick_cell(g->pick_cur, &r, &c);
    art_dz = 2.0f;
    art_box(AG_CUR, lin(GRID_X0, c, GRID_DX), lin(GRID_Y0, r, GRID_DY), 21.0f, 17.0f, 0xF0C028FFu);     /* a gold rim around the 64 x 56 icon */
    gnb_pick_cell(3u, &r, &c);                       /* Mr. Game & Watch (3): a black figure on a transparent icon; a dark grey plate BEHIND it, so the figure and its soft edges
                                                        (translucent palette entries) blend over grey instead of the black panel (a palette recolour left dark fringes) */
    art_dz = 2.2f;
    art_box(AG_GW, lin(GRID_X0, c, GRID_DX), lin(GRID_Y0, r, GRID_DY), 16.0f, 12.0f, 0x4A4A50FFu);
    for (i = 0; i < GNB_PICK_N; i++) {
        gnb_pick_cell(i, &r, &c);
        px = lin(GRID_X0, c, GRID_DX); py = lin(GRID_Y0, r, GRID_DY);
        if (i < GART_ICON_N && gart_icon(AG_CELL0 + i, i, TX(px), sub(ZERO, mula(TY(py), BOX_DY, S.pyk)), add(S.nz, 2.5f), ICON_SX, mula(ZERO, S.pyk, ICON_SYK))) {      /* Sheik (19) shows Zelda's icon (gnb_art.c CSS_OFF[19]) with its name under it */
            if (i == 19u && !gart_sheik_own()) {                                                                                          /* Zelda's icon carries the patched caption ZELDA: a dark strip over it, SHEIK on the strip (GUI audit RM-02) */
                axs(X_GRID + i, add(px, GRID_TX), add(py, 25.0f), 0.022f, gnb_cell_ok(g, i) ? COL[0] : COL[1], 1, gnb_cell_label(i));       /* centred under the portrait, on the patched name plates' baseline (dim when the room excludes it) */
            } else if (!gnb_cell_ok(g, i) && !gart_tex_open()) axs(X_GRID + i, add(px, GRID_TX), sub(py, 4.0f), 0.05f, COL[1], 1, "X");      /* a text mark too: an excluded cell stays plainly locked when the dimming plate could not be made (art slot or heap) */
            else axs(X_GRID + i, px, py, 0.03f, COL[0], 0, "");
            nd = grid_excl(g, i, px, py, nd);
            continue;
        }
        art_dz = 2.2f;
        art_box(AG_CELL0 + i, px, py, 17.0f, 13.0f, 0x6E6E78FFu);
        if (i == GNB_PICK_N - 1u) axs(X_GRID + i, add(px, GRID_TX), sub(py, 2.0f), 0.06f, COL[0], 1, "?");       /* centred in its box (Dolphin: texts centre about 12 px left of the icon's centre, and the glyph sat 14 px high) */
        else axs(X_GRID + i, px, sub(py, 6.0f), 0.026f, gnb_cell_ok(g, i) ? COL[0] : COL[1], 1, gnb_cell_label(i));      /* a cell without an icon (Sheik): the name in capitals at the larger size: build 483 showed "Sheik" at 0.022 garbled ("Sho ik": e and i fall together at that size) */
        nd = grid_excl(g, i, px, py, nd);
    }
    art_dz = 0.0f;
}

#include "gnb_prep_ev.h"

/* the list on the Trophy Gallery frame (rooms-beta-2's Rooms scene: TyMnView's ToyFigurePanel with Rooms' tab art "Online Play | Rooms" and the "Public" side
   label), a grey cursor bar with a red underline on the selected row, the "n/8" boxes, the four round-ended buttons (Trophy Gallery sort plates) */
static const float BTN_X[GNB_ACT_N] = { 63.0f, 204.0f, 346.0f, 487.0f };   /* (cube: Random / Create / Code / More): four plates evenly over the old five's span (6.. 554 px, 124 px plates, steps of 141.3); the labels' centres: the middle of each pill's open part, 5 px left of the plate's centre */
static const float BTN_PX[GNB_ACT_N] = { 6.0f, 147.0f, 289.0f, 430.0f };   /* the plates' left ends (BTN_X - 57) (constants: no float maths in this soft-float overlay) */
static const float LP_CX = 332.0f, LP_CY = 268.0f, LP_BW = 234.0f, LP_BH = 158.0f, LP_SIDE_DX = -266.0f, LP_ARROW_X = 100.0f, LP_ARROW_DY = 152.0f, MINUS_ARROW_DY = -152.0f;
static const float BTN_SX = 1.148f, BTN_SY = 0.95f;      /* 124 px wide (was 0.93 = 100 px for five) */      /* the plate (pill with its round cap, 108 px per unit of scale) is anchored at its left end: shifted so it is centred on its label, 92 px wide like the original's (rooms_pr_1) */
static const float TRACK_CX = 275.0f, TRACK_BW = 275.0f, TRACK_BH = 5.0f;      /* the track under the plates: centre x, corner bones (visible half size 270 x 20 px) */
#define TRACK_RGB 0x3CC030FFu
#define COL_SELTXT 0x101810FFu
static const float BTN_Z[GNB_ACT_N] = { -0.10f, -0.15f, -0.20f, -0.25f };      /* plates: leftmost nearest, all behind the text, above the track (-0.4) */
#define PLATE_OFF 0x2E8A24FFu
#define PLATE_FONT 0.029f                /* the four short labels (Random, Create, Code, More) fit the 124 px pill's open part at the full size */
static void list_art(void)
{
    struct gnb *g = S.g;
    unsigned i, rows = gnb_rows(g);
    float y;
    art_hide(AB_HEAD, AB_N);
    art_hide(AI_L, AI_END);
#ifdef GNB_FRAME_PROBE
    gart_put(AF_FRAME, GART_FRAME, ZERO, ZERO, ZERO, 0.5f, 0.5f, 0, ONE);           /* layout probe (never shipped): the whole Trophy Gallery panel at half size */
#else
    gart_put(AF_FRAME, GART_FRAME, ZERO, ZERO, ZERO, ONE, ONE, 0, ONE);
#endif
    /* the list panel: ToyFigurePanel joint 7 (the trophy list holder) moved to the list, its box joint 8 (black, green rounded border, the notch with the scroll
       arrows on the left) stretched over the rows, the side label joint 13 ("PUBLIC") at the panel's left edge (Dolphin probe fp: the panel's top band and right bar
       are the frame of joint 1, the rest is this box) */
    gart_part_pos(AF_FRAME, 7u, TX(LP_CX), sub(ZERO, mula(TY(LP_CY), BOX_DY, S.pyk)));
    gart_part_hide(AF_FRAME, 8u);                                                     /* the retail opaque black box is not drawn: a green frame box and a translucent dark one of the same size (the Tournament look's boxes) let the globe show through */
    art_dz = -0.2f; art_box(AB_OUTER, LP_CX, LP_CY, LP_BW, LP_BH, TRACK_RGB);
    art_dz = -0.15f; art_box(AB_INNER, LP_CX, LP_CY, 231.0f, 155.0f, 0x0A0A14FFu); art_dz = 0.0f;
    gart_part_pos(AF_FRAME, 13u, mula(ZERO, LP_SIDE_DX, PXK), ZERO);
    joint_xy(1u, TX(LP_ARROW_X), sub(ZERO, mula(TY(LP_CY), BOX_DY, S.pyk)));          /* the retail Event scroll arrows into the panel's notch */
    joint_xy(3u, ZERO, mula(ZERO, LP_ARROW_DY, S.pyk));
    joint_xy(2u, ZERO, mula(ZERO, MINUS_ARROW_DY, S.pyk));
    if (!gart_frame_labels()) {
        axs(X_ACT + 5u, 120.0f, 40.0f, 0.035f, COL[0], 0, "Online Play");
        axs(X_ACT + 6u, 330.0f, 40.0f, 0.035f, COL[0], 0, "Rooms");
    }
    if (rows && S.kind != 1u && g->scr != GNB_PASS && !g->more) {                           /* the cursor: a grey bar and its red underline on the selected row */
        y = lin(S.ny0, g->page, S.dy);
        art_box_t(AF_CUR, TX(346.0f), y, 206.5f, 0.0f, 0.85f, 0xD0D0D8FFu);
        art_box_t(AF_LINE, TX(346.0f), add(y, mula(ZERO, 14.0f, S.pyk)), 206.5f, 0.0f, 0.12f, 0xE01818FFu);
    } else { gart_hide(AF_CUR); gart_hide(AF_LINE); }
    for (i = 0; i < GNB_VIS; i++) {                                              /* the "n/8" box of each listed room */
        if (S.kind == 0u && g->scr != GNB_PASS && (unsigned)g->first + i < g->n && !g->more) art_box_t(AF_CNT0 + i, add(S.nx, DX_CNTBOX), lin(S.ny0, i, S.dy), 18.0f, 0.0f, 0.8f, 0x58585EFFu);
        else gart_hide(AF_CNT0 + i);
    }
    axs(X_ACT + 7u, 440.0f, 425.0f, 0.026f, COL[0], 1, "");                      /* the notch box under the list is not drawn (rooms_pr_1) and the row shows the room's code: no detail text */
    if (g->scr == GNB_LIST && !g->more) { art_dz = -0.4f; art_box_t(AF_TRACK, TX(TRACK_CX), TY(471.0f), TRACK_BW, TRACK_BH, ONE, TRACK_RGB); art_dz = 0.0f; }     /* the green track the plates lie on (rooms_pr_1) */
    else gart_hide(AF_TRACK);
    gart_hide(AF_SEL);
    for (i = GNB_ACT_N; i-- > 0;) {      /* right to left: each plate's dark body reaches left over its neighbour's pill, so the left plate is made last (and nearer, BTN_Z) and shows whole */
        int sel = g->scr == GNB_LIST && g->focus && g->act_sel == i, on = gnb_act_enabled(g, i);
        if (g->scr != GNB_LIST || g->more) { gart_hide(AF_BTN0 + i); axs(X_ACT + i, BTN_X[i], 471.0f, COL_FONT, COL[0], 1, ""); continue; }
        gart_put(AF_BTN0 + i, GART_BUTTON, TX(BTN_PX[i]), sub(ZERO, mula(TY(471.0f), BOX_DY, S.pyk)), add(S.nz, BTN_Z[i]), BTN_SX, BTN_SY, sel ? 0xFFFFFFFFu : PLATE_OFF, ONE);      /* the selected plate: the plate itself drawn white */
        axs(X_ACT + i, BTN_X[i], 471.0f, PLATE_FONT, sel ? COL[0] : on ? COL_GREEN : COL[1], 1, gnb_act_label(g, i));      /* PLATE_FONT: the longest label (Create) fits inside the pill, clear of its cap */
    }
}

/* the passcode entry: a rounded plate centred over the form, the title, four digit cells evenly spaced, the one under the cursor gold; the keys are in the footer only */
static void pass_draw(void)
{
    static const float CX[4] = { 228.0f, 290.0f, 352.0f, 414.0f };       /* the cells (sprite space) */
    static const float DXS[4] = { 224.8f, 289.0f, 353.0f, 418.0f };      /* the digits (text space scales about x 321 differently: cells step 78 px, digits 75, measured in the frame dump) */
    struct gnb *g = S.g;
    char ti[GNB_LINE + 1], dg[6], one[2];
    unsigned i, pos;
    if (!pass_art()) {
        for (i = 0; i < 5u; i++) xs(X_CHAT + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
        return;
    }
    if (g->scr == GNB_LIST) {                                                   /* the More popup: the passcode plate's look, shrink-wrapped to its four rows ("Hide full rooms: Yes" is the widest, about 218 px at the larger font,: the rows read small) */
        static const float RY[GNB_MORE_N] = { 244.0f, 278.0f, 312.0f, 346.0f };
        char row[26];
        art_dz = 1.6f;
        art_box(AG_PANEL, 321.0f, 287.0f, 126.0f, 82.0f, 0x000000FFu);
        art_box(AG_PANEL2, 321.0f, 287.0f, 126.0f, 82.0f, 0x000000FFu);
        art_dz = 0.0f;
        axs(X_CHAT, 321.0f, 208.0f, 0.034f, COL[0], 1, "More");
        for (i = 0; i < GNB_MORE_N; i++) axs(X_CHAT + 1u + i, 210.0f, RY[i], MORE_FONT, i == g->more_row ? COL[0] : COL_DIM2, 0, gnb_more_label(g, i, row));
        return;
    }
    pos = gnb_pass_view(g, ti, dg);
    art_dz = 1.6f;
    art_box(AG_PANEL, 321.0f, 270.0f, 135.0f, 60.0f, 0x000000FFu);
    art_box(AG_PANEL2, 321.0f, 270.0f, 135.0f, 60.0f, 0x000000FFu);
    art_dz = 1.2f;
    for (i = 0; i < 4u; i++) art_box(AG_CELL0 + i, CX[i], 285.0f, 9.0f, 13.0f, i == pos ? 0xE0A818FFu : 0x3A3A46FFu);
    art_dz = 0.0f;
    axs(X_CHAT, 321.0f, 207.0f, 0.034f, COL[0], 1, ti);
    one[1] = 0;
    for (i = 0; i < 4u; i++) { one[0] = dg[i]; axs(X_CHAT + 1u + i, DXS[i], 277.0f, 0.06f, COL[0], 1, one); }
}

/* which look this layout gets: 0 Event Match, 1 the Rooms look */
static int art_on(void)
{
    if (GNB_EVENT_LOOK || !S.art) return 0;
    if (S.kind == 3u) return (S.art & (1u << GART_BOX)) != 0;
    return (S.art & ART_NEED) == ART_NEED;
}

/* ---- the Grandmaster standings (stb.h) on the Event look: rank in the number column, the name in the title slot, rating
   (right-aligned) and tier in two slots of ours; the selected row's detail in the description box, your own line at its bottom, the example-data tag and the
   page in the score box, "Updated <day>" where the High Score label was ---- */
static const float DX_RATE_L = 13.1f;      /* left edge of the tier text (px 385 of the frame: its own cell, 16 px right of the old 369 so the longest tier "Grandmaster" ends at 511, inside the 534 px edge) */
#define ST_NAME_PX 120u                  /* the name's pixel budget by est_w (the glyph table reads about 20 % under the stretched row font: 120 = about 143 px drawn, 11 px clear of the tier column) */
#define ST_KIND 4u                       /* S.kind of the standings screen (the X slots are freed when it changes) */
#define ST_RATE_CX 176.5f                /* the rating's own cell centre, px of the 640 x 528 frame (measured on the Dolphin frames; the cell is between the marker column's line and the name column's) */
static const float ST_ROW_DY = 0.075f;   /* one baseline for rating, name and tier: the Friends rows' offset (frd_ev.c ROW_DY_NAME; a text is placed by its top), so the text is centred in the row */
static void st_big(void *t) { F((u8 *)t + TX_BW) = S.bw; F((u8 *)t + TX_BH) = S.bh; }      /* the retail box stretch of the row names */
static void st_row(unsigned i, int force)
{
    char rk[8], nm[16], ra[8], ti[12], tit[34];
    unsigned a;
    u32 col;
    void **tn = (void **)(S.md + MD_TEXTS + 4u * i), **tt = (void **)(S.md + MD_ICONS + 4u * i);
    float y = mula(lin(S.ny0, i, S.dy), S.dy, ST_ROW_DY);
    const char *st = stb_state_text(S.sd);
    a = stb_cols(S.sd, i, rk, nm, ra, ti);
    if (!a) { rk[0] = 0; ra[0] = 0; ti[0] = 0; copy(tit, (i == 2u && st && S.sd->net != STB_N_LOADING) ? st : "", 32); col = COL[1]; }     /* Loading: the panel line says it (as Friends) */
    else { copy(tit, nm, 12); fit_name(tit, ST_NAME_PX); col = COL[(a - 1u) == 2u ? 2 : (a - 1u) == 1u ? 1 : 0]; }
    if (i >= evo_rows()) { rk[0] = 0; ra[0] = 0; ti[0] = 0; tit[0] = 0; }    /* not revealed yet (evo.h) */
    if (a) copy(rk, ra, 6);                                                   /* the number column shows the rating: the position is in the info box below */
    gb_big = 1;
    xs(X_REG + i, add(S.nx, DX_RATE_L), y, COL_FONT, col, 0, ti);            /* the tier alone, one left edge for every row, the name's look */
    gb_big = 0;
    if (!force && same(rk, S.rnum[i]) && same(tit, S.rtit[i]) && col == S.rcol[i]) return;
    tfree(tn); tfree(tt);
    copy(S.rnum[i], rk, GNB_NUM); copy(S.rtit[i], tit, 31); S.rcol[i] = col;
    if (rk[0]) {
        *tn = text_at(TX(ST_RATE_CX), y, S.nz, COL_FONT, a == 3u ? COL[2] : COL_NUM, 1);      /* centred in its own cell */
        if (*tn) sis_print(*tn, rk);
    }
    if (tit[0]) {
        u8 *t = (u8 *)text_at(S.tx, y, S.nz, COL_FONT, col, 0);
        *tt = t;
        if (t) { st_big(t); sis_print(t, tit); }
    }
}
/* the tier icons in the marker column: one piece per row, Project Slippi's rank badge (a colour texture, drawn untinted by gart_rank_icon) on the Trophy Gallery
   plate's quad (the art keeps a 1/32 transparent margin), placed like the Rooms look's pieces (pixel column MARK_PX of the 640 x 528 frame, the row's text y); the retail circle on the selected row stays under it.
   ONE call site draws the icon of a row (st_tier_mark): centred in the icon cell, true aspect on a 4:3 TV (24 px wide x 26.4 px tall in the 640 x 528 frame: the EFB is squeezed 528 / 480), the Friends
   discs' row step and centring (GART_CIRC_* of gnb_art.h); a newer icon is swapped in this function only. */
static const float MARK_PX = 118.0f, BDG_DY = 13.5f;
#define ST_ICON_PX 26                       /* icon height, px of the frame (the width is 1.013 x: GART_RI_SX; measured plate geometry, gnb_art.h) */
static void st_tier_mark(unsigned i, unsigned sh)
{
    float y = lin(S.ny0, i, mula(ZERO, GART_CIRC_STEPK, S.dy)), x = add(TX(MARK_PX), GART_RI_X(0.365f, ST_ICON_PX)), yy = sub(ZERO, mula(y, BDG_DY, S.pyk)), z = add(S.nz, ONE);
    gart_rank_icon(i, sh, 0u, x, yy, z, GART_RI_SX(ST_ICON_PX), GART_RI_SY(ST_ICON_PX));
}
static void st_badges(void)
{
    unsigned i;
    if (!S.bdg) return;
    for (i = 0; i < GNB_VIS; i++) {
        unsigned long rgb;
        unsigned sh = stb_mark(S.sd, i, &rgb);
        if (sh == STB_SH_NONE) { gart_hide(i); continue; }
        (void)rgb;
        st_tier_mark(i, sh);
    }
}
static void st_cursor(void)
{
    struct stb *d = S.sd;
    unsigned n = stb_rows(d);
    joint_y(0xBu, lin(ZERO, d->sel, S.cur_dy));
    joint_hide(0xBu, n == 0u);
    joint_hide(3u, d->page == 0u || n == 0u);                                   /* up arrow: a page above */
    joint_hide(2u, (unsigned)d->page + 1u >= stb_pages(d) || n == 0u);          /* down arrow: a page below */
    S.shown_sel = d->sel; S.shown_pg = d->page;
}
static void st_draw(int force)
{
    struct stb *d = S.sd;
    unsigned i;
    char b[48];
    if (S.kind != ST_KIND || force) { x_free_all(); S.kind = ST_KIND; force = 1; }
    for (i = 0; i < GNB_VIS; i++) st_row(i, force);
    {   /* the info box (no example / update wording on the console): top row = the selected player's display name (left) and tier title (right) in white, bottom row in grey =
           "Position: n" (left), games (middle), "ELO n" (right).  Line 0 (the retail description slot, never NULL) only carries "Loading standings". */
        char tl[STB_TOP_MAX + 1u], tr[16], bl[STB_BOTTOM_MAX + 1u], bm[16], br[16];
        float y0 = mula(S.ny0, F_DESC[0], S.dy), y1 = mula(S.ny0, ST_BODY_ROW, S.dy);
        void **dd = (void **)(S.md + MD_DESC);
        int w1, w2, w3, g, cx;
        stb_top(d, tl, tr); stb_bottom(d, bl, bm, br);
        if (!evo_chrome()) { tl[0] = 0; tr[0] = 0; bl[0] = 0; bm[0] = 0; br[0] = 0; }
        b[0] = 0;
        if (d->net == STB_N_LOADING && evo_chrome()) copy(b, "Loading standings", 40);          /* the loading line where Friends has it */
        if (!b[0]) { b[0] = ' '; b[1] = 0; }
        S.pan[0].t = *dd;
        tslot_set(&S.pan[0], S.nx, y0, PANEL_FONT, COL[1], 0, b);
        *dd = S.pan[0].t;
        /* the body line (the Friends box's look): white, the row names' font and retail stretch, the small left margin of the Friends body; "Position", the games and the ELO sit
           at the left edge, in the middle and at the right edge with EQUAL gaps between them (the widths by the one glyph table, est_w) */
        w1 = (int)est_w(bl); w2 = (int)est_w(bm); w3 = (int)est_w(br);
        g = (ST_BOX_R_PX - ST_BOX_L_PX - w1 - w2 - w3) / 2;
        cx = ST_BOX_L_PX + w1 + g + w2 / 2;
        gb_big = 1;
        tslot_set(&S.pan[1], add(S.nx, BODY_DX), y1, COL_FONT, COL[0], 0, bl);
        tslot_set(&S.pan[2], S.nx, y1, COL_FONT, COL[0], 0, "");
        xs(X_ELO, TXI(cx), y1, COL_FONT, COL[0], 1, bm);
        xs(X_ELO + 1u, TXI(ST_BOX_R_PX), y1, COL_FONT, COL[0], 2, br);
        {   /* the score box slot (retail, freed by the retail exit: never NULL) carries the title, right aligned */
            void **nn = (void **)(S.md + MD_NAME);
            struct tslot t;
            if (!tr[0]) { tr[0] = ' '; tr[1] = 0; }
            t.t = *nn; copy(t.s, S.sub.s, 41); t.col = S.sub.col; t.tag = 0;
            gb_force = 1; tslot_set(&t, TXI(ST_BOX_R_PX), S.sy, COL_FONT, COL[0], 2, tr); gb_force = 0;
            *nn = t.t; copy(S.sub.s, t.s, 41); S.sub.col = t.col;
        }
        tslot_set(&S.head, add(S.nx, BODY_DX), S.sy, COL_FONT, COL[0], 0, tl);
        gb_big = 0;
    }
    {   /* X: Me while not on the board: the note stands centred, white, over the background's "SUBJECT TABLE" lettering (about y 110 of 480, x 165..490); that retail lettering DObj is
           hidden (subject_hide) while the note shows and shown again when it ends; slot X_CHAT (Standings has no chat strip) */
        const char *nt = evo_chrome() ? stb_note(d) : 0;
        if (nt) {
            subject_hide();
            gb_big = 1;
            axs(X_CHAT, pxf(320), pxf(ST_NOTE_Y), COL_FONT, COL[0], 1, nt);
            gb_big = 0;
        } else {
            subject_show();
            xs(X_CHAT, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
        }
    }
    joint_hide(4u, 1);                                                          /* no "Select Character" box */
    joint_hide(6u, 1);                                                          /* no High Score strip: our heading stands there */
    st_cursor();
    st_badges();
    tslot_set(&S.title, S.nx, mula(S.ny0, F_TITLE_ROWS, S.dy), TITLE_FONT, COL[2], 0, "");        /* no big title under the header: the header tab reads "Grandmaster" (ml_glue.c tab_event) */
    tslot_set(&S.hint, add(S.nx, HINT_DX), mula(S.ny0, F_HINT_ROWS, S.dy), HINT_FONT, COL[1], 0, "");                 /* (the hint line is the key line below: badges, one footer style) */
    { static const unsigned HA[4] = { X_HA, X_HA + 1u, X_HA + 2u, X_HA + 3u }; key_line(HA, X_HLB, evo_chrome() ? stb_hint(d) : "", add(S.nx, FOOT_DX), FOOT_WQ, mula(S.ny0, F_HINT_ROWS, S.dy), 240u, COL[1]); }      /* the shared footer span (Friends / Game Night / Settings): Start Reload, X Me, B Back spread evenly */
    {   /* the small box at the bottom right of the info panel (about 62 x 62 px, centre 547 / 404 in the 640 x 480 layout units): days left of the season, three centred lines: the number
           (big), "days" / "day", "left" (small).  Constants only (soft-float).  The server sends the number (the console has no clock); unknown = empty box. */
        char dn[4], d1[8], d2[8];
        unsigned on = evo_chrome() && stb_season_box(d, dn, d1, d2);
        if (!on) { dn[0] = 0; d1[0] = 0; d2[0] = 0; }
        axs(X_SEASON + 0u, SD_BX, SD_NUM_Y, 0.045f, COL[0], 1, dn);
        axs(X_SEASON + 1u, SD_BX, SD_W1_Y, 0.022f, COL_DIM2, 1, d1);
        axs(X_SEASON + 2u, SD_BX, SD_W2_Y, 0.022f, COL_DIM2, 1, d2);
    }
    d->dirty = 0;
}

static void draw_all(int force)
{
    if (S.sd) { st_draw(force); return; }
    {
    unsigned i;
    struct gnb *g = S.g;
    int art;
    {
        unsigned k = gnb_layout(g);
        if (k != S.kind || force) { x_free_all(); S.kind = k; force = 1; }
    }
    art = art_on();
    for (i = 0; i < GNB_VIS; i++) row_draw(i, force);
    if (art) {
        gart_menu_panel(0);
        (void)gart_bg(1);                                           /* Tournament's animated globe background behind the room screen and behind the list, whose panel is translucent dark now */
        if (S.kind != 3u) { gart_hide(P_BRK); gart_hide(P_SEL); }                  /* (the stage prep's cursor slots 62 / 63 sit above the lists' art ranges: nothing of that screen stays on the list, the Create form or the passcode screen) */
        if (S.kind == 3u) room_art(); else { list_art(); pass_draw(); }
    } else {
        if (S.art) { gart_hide_all(); gart_menu_panel(1); (void)gart_bg(0); }
        if (S.kind == 3u) room_draw();
    }
    if (art) { }                                                                /* the list's buttons are drawn by list_art */
    else if (g->scr == GNB_LIST) action_draw();
    else for (i = 0; i < GNB_ACT_N; i++) xs(X_ACT + i, S.nx, S.ny0, COL_FONT, COL[0], 0, "");
    panel_draw(!art ? 0 : (S.kind == 3u || g->scr == GNB_PASS) ? 1 : 2);      /* the Create room form's summary block is gone: every value is in a row */
    badge_draw();
    chrome_draw();
    cursor_draw();
    tslot_set(&S.title, S.nx, mula(S.ny0, F_TITLE_ROWS, S.dy), TITLE_FONT, COL[2], 0, S.kind == 3u || art || !evo_chrome() ? "" : gnb_title(g));
    tslot_set(&S.hint, add(S.nx, HINT_DX), mula(S.ny0, F_HINT_ROWS, S.dy), HINT_FONT, COL[1], 0, "");
    {   /* ONE footer style: the Settings family's span and badges; the forms, the passcode and the More popup stand at the Settings height (y 464), the list under its plates (the track fills y 464..478) */
        static const unsigned HA[4] = { X_HA, X_HA + 1u, X_HA + 2u, X_HA + 3u };
        if (art && S.kind != 3u) {
            const char *ft = !evo_chrome() ? "" : (g->scr == GNB_LIST && !g->more) ? "A: Select  L R: Region  B: Back" : gnb_hint(g);
            key_line(HA, X_HLB, ft, add(S.nx, FOOT_DX), FOOT_WQ, g->scr == GNB_LIST && !g->more ? add(mula(S.ny0, F_HINT_ROWS, S.dy), mula(ZERO, LIST_FOOT_DY, S.pyk)) : TY(GNB_FOOT_Y), 240u, COL[1]);
        } else key_line(HA, X_HLB, (g->scr == GNB_LIST && !g->more) || (S.kind == 3u && art) || !evo_chrome() ? "" : gnb_hint(g), add(S.nx, HINT_DX), HINT_WQ, mula(S.ny0, F_HINT_ROWS, S.dy), 240u, COL[1]);
    }
    g->dirty = 0;
    }
}

static void sound(void)
{
    static const int ID[5] = { -1, 0, 1, 2, 3 };
    unsigned s;
    if (S.sd) { s = S.sd->sfx; S.sd->sfx = 0; }
    else { s = S.g->sfx; S.g->sfx = 0; }
    if (s && s < 5u) AUDIO(ID[s]);
}

/* ---- the procs ---- */
static void ev_think(void *gobj)
{
    (void)gobj;
    if (!S.want_exit || S.state != GNBEV_UP) return;
    if (S.md) {   /* our extra texts first (the retail slots are freed by the retail teardown); never NULL in the two slots the retail exit frees unconditionally */
        unsigned i;
        for (i = 1; i < 4u; i++) tfree(&S.pan[i].t);
        tfree(&S.title.t); tfree(&S.hint.t); tfree(&S.head.t); x_free_all();
        gb_force = 1;
        if (!W((u32)S.md + MD_DESC)) W((u32)S.md + MD_DESC) = (u32)text_at(S.dx_desc, S.dy_desc, S.nz, 0.03f, COL[0], 0);
        if (!W((u32)S.md + MD_NAME)) W((u32)S.md + MD_NAME) = (u32)text_at(S.sx, S.sy, S.nz, 0.03f, COL[0], 0);
        gb_force = 0;
        for (i = 0; i < GNB_VIS; i++) {                                         /* marks: none of ours */
            u32 go = W((u32)S.md + MD_GOBJS + 4u * i);
            if (go) { GOBJ_FREE((void *)go); W((u32)S.md + MD_GOBJS + 4u * i) = 0; }
        }
    }
    subject_show();
    gart_close();                                                                /* our pieces, the main menu panel drawn again, the archives freed */
    S.art = 0;
    S.state = GNBEV_CLOSING;
    B(MENU_FLOW + 0x11u) = 0;                                                    /* entering_menu, as the retail B handler */
    MN_GOTO((int)S.ret_kind, (u16)S.ret_row, 3);                                 /* frees THIS gobj (the current proc's), builds the screen we came from, adds its think */
    S.think = 0; S.scr = 0; S.md = 0;
    S.state = GNBEV_CLOSED;
}

static void capture(void)
{
    u8 *t0 = (u8 *)W((u32)S.md + MD_TEXTS), *t1 = (u8 *)W((u32)S.md + MD_TEXTS + 4u), *i0 = (u8 *)W((u32)S.md + MD_ICONS);
    u8 *d = (u8 *)W((u32)S.md + MD_DESC), *n = (u8 *)W((u32)S.md + MD_NAME);
    u8 *ja = (u8 *)joint(0xAu), *jc = (u8 *)joint(0xCu);
    S.nx = t0 ? F(t0 + TX_X) : -14.0f; S.ny0 = t0 ? F(t0 + TX_Y) : -8.0f; S.nz = t0 ? F(t0 + TX_Z) : 17.0f;
    S.dy = (t0 && t1) ? sub(F(t1 + TX_Y), F(t0 + TX_Y)) : 1.5f;
    S.tx = i0 ? F(i0 + TX_X) : add(S.nx, 4.8f);
    S.bw = i0 ? F(i0 + TX_BW) : 364.68f; S.bh = i0 ? F(i0 + TX_BH) : 38.39f;
    S.dx_desc = d ? F(d + TX_X) : -9.5f; S.dy_desc = d ? F(d + TX_Y) : 8.0f;
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
    if (S.state != GNBEV_LAUNCH) return;
    AUDIO(1);
    EV_ENTER(0, 0);                                                              /* cur_menu 7, the art, the screen (retail rows of events 0..8 for one frame of nothing: replaced below before any draw) */
    GOBJ_FREE(gobj);                                                             /* the old menu think, as the retail 1P think does after mnEvent_8024E838 */
    S.orig = 0;
    S.scr = (void *)W(EV_SCREEN);
    S.think = find_proc(EV_THINK, &cb);
    if (S.think && cb) *cb = (u32)ev_think;
    if (!S.scr || !S.think) {                                                    /* cannot take the screen over: go straight back (never leave the retail think running) */
        S.err = S.think ? 3u : 2u;
        if (S.think && cb) { S.state = GNBEV_UP; S.want_exit = 1; S.md = S.scr ? (u8 *)W((u32)S.scr + 0x2Cu) : 0; }
        else S.state = GNBEV_FAILED;
        return;
    }
    S.md = (u8 *)W((u32)S.scr + 0x2Cu);
    capture();
    S.bdg = S.sd ? (unsigned)gart_badges_open() : 0u;                          /* the standings' badge pieces (the retail circle stays when the plate is not recognised) */
    S.art = (GNB_EVENT_LOOK || S.sd) ? 0u : gart_open();       /* the standings always keep the Event look */                                  /* the retail art of the Rooms look, read from the disc (0: the Event look stays) */
    if (!GNB_EVENT_LOOK && !S.sd && (S.art & ART_NEED) != ART_NEED) {                /* a piece missing: one more read of both archives (a momentary failure must not cost the whole look), the art is built before the first frame is drawn either way */
        gart_close();
        S.art = gart_open();
    }
#ifdef GNB_PROBE
    {   /* layout probe (SG_EXTRA_CFLAGS=-DGNB_PROBE, never shipped): the retail screen untouched plus markers at known coordinates */
        static const float PX[3] = { -15.0f, 0.0f, 15.0f }, PY[3] = { -10.0f, 0.0f, 10.0f };
        static const char *const LB[9] = { "+-15 -10", "+0 -10", "+15 -10", "+-15 0", "+0 0", "+15 0", "+-15 10", "+0 10", "+15 10" };
        unsigned k;
        for (k = 0; k < 9u; k++) { void *t = text_at(PX[k % 3u], PY[k / 3u], S.nz, 0.03f, COL[3], 0); if (t) gnbev_sis(t, "%s", LB[k]); }
        S.state = GNBEV_UP; S.built = 1;
        return;
    }
#endif
    for (i = 0; i < GNB_VIS; i++) { S.rnum[i][0] = 1; S.rtit[i][0] = 1; }      /* never equal to a real string: force the first draw */
    evo_prep(S.scr, GNB_VIS);                                                    /* the shared open (evo.h): retail marks and texts out, empty portrait, the reveal starts */
    S.pan[0].t = 0; S.sub.t = 0; S.sub.s[0] = 1;
    S.state = GNBEV_UP;
    S.built = 1;
    draw_all(1);
}

/* ---- API ---- */
static int ev_open(struct gnb *g, struct stb *sd);
int gnbev_open(struct gnb *g) { return ev_open(g, 0); }
int gnbev_open_st(struct stb *sd) { return ev_open(0, sd); }
static int ev_open(struct gnb *g, struct stb *sd)
{
    u32 kind = B(MENU_FLOW), *cb = 0;
    void *go;
    if (S.state != GNBEV_IDLE && S.state != GNBEV_CLOSED && S.state != GNBEV_FAILED) return -1;
    zero(&S, (unsigned)sizeof S);
    S.g = g; S.sd = sd;
#ifdef GNB_TEST_REFUSE
    {   /* DOLPHIN PROOF ONLY (SG_EXTRA_CFLAGS=-DGNB_TEST_REFUSE=N, never shipped): the first N opens of the Game Night screen per overlay visit are refused as when no menu think is found (gn_glue's retry and short message) */
        static unsigned test_refused;
        if (g && test_refused < (unsigned)(GNB_TEST_REFUSE)) { test_refused++; S.err = 1; S.state = GNBEV_FAILED; return -1; }
    }
#endif
    if (kind >= 0x22u || kind == EV_KIND || !W(TAB + kind * 20u + 0x10u)) { S.err = 1; S.state = GNBEV_FAILED; return -1; }
    go = find_proc(W(TAB + kind * 20u + 0x10u), &cb);
    if ((!go || !cb) && kind == 0u) go = find_proc(W(TAB + 1u * 20u + 0x10u), &cb); /* The Main Menu reached by Back from Slippi's screen runs its think through the 1-P kind's slot (ml_glue.c: ml_think_1p, MENU_FLOW kind 0 meanwhile): the kind 0 think is then in no proc and the Event screen was refused for ever */
    if (!go || !cb) { S.err = 1; S.state = GNBEV_FAILED; return -1; }
    S.ret_kind = kind; S.ret_row = H(MENU_FLOW + 2u);
    S.orig = go;
    S.state = GNBEV_LAUNCH;
    *cb = (u32)ev_launch;                                                         /* the next proc pass runs the launcher in proc context */
    return 0;
}

void gnbev_frame(void)
{
    if (S.state != GNBEV_UP || S.want_exit) return;
    gart_tick();
    if (art_on()) gart_menu_panel(0);                                           /* every frame until it holds: the retail panel's own think draws its zoom-in through another callback for about 50 frames, and draw_all runs only when something changed */
#ifdef GNB_PROBE
    S.g->dirty = 0; S.g->sfx = 0;
    return;
#endif
    if (S.sd) {
        int rv;
        if (S.shown_pg != S.sd->page) evo_page();                               /* a page turn: quick replay */
        rv = evo_frame(stb_rows(S.sd));
        if (S.sd->dirty || rv) draw_all(0);
        else if (S.shown_sel != S.sd->sel || S.shown_pg != S.sd->page) { st_draw(0); }
        sound();
        return;
    }
    if (gnb_layout(S.g) != S.kind) evo_skip();                                  /* another screen of the browser: all at once */
    else if (S.shown_first != S.g->first) evo_page();
    if (evo_frame(gnb_rows(S.g))) S.g->dirty = 1;
    if (S.g->dirty) draw_all(0);
    else if (S.shown_page != S.g->page || S.shown_first != S.g->first) { if (art_on()) draw_all(0); else cursor_draw(); }      /* the Rooms look's cursor is a box of ours */
    if (S.g->prep.open && !S.g->pick_open && art_on()) prep_anim();              /* the stage prep screen's two animations (gnb_prep_ev.h) */
    sound();
}

void gnbev_close(void) { if (S.state == GNBEV_UP) { sound(); S.want_exit = 1; } }
int gnbev_state(void) { return S.state; }
unsigned gnbev_err(void) { return S.err; }
void gnbev_reset(void) { if (S.state == GNBEV_CLOSED || S.state == GNBEV_FAILED) S.state = GNBEV_IDLE; }

void gnbev_abort(void)
{
    u32 *cb = 0;
    unsigned i;
    if (S.state == GNBEV_LAUNCH && S.orig && find_proc((u32)ev_launch, &cb) && cb) *cb = W(TAB + S.ret_kind * 20u + 0x10u);      /* the menu think gets its own callback back */
    if (S.state == GNBEV_UP || S.state == GNBEV_CLOSING) {
        if (find_proc((u32)ev_think, &cb) && cb) *cb = NOP_PROC;
        for (i = 1; i < 4u; i++) tfree(&S.pan[i].t);
        tfree(&S.title.t); tfree(&S.hint.t); tfree(&S.head.t); x_free_all();
        subject_show();
        gart_close();
    }
    S.state = GNBEV_IDLE;
}
#endif
