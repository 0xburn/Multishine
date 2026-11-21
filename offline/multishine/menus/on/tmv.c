/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* tmv: see tmv.h.  The pure state machine first (also built by the host test), then the console side (TMV_HOST not defined). */
#include "tmv.h"

unsigned tmv_step(struct tmv_m *m, const struct tmv_world *w)
{
    unsigned *phase = &m->phase, *frames = &m->frames, *err = &m->err;
    unsigned in_t = w->major == TMV_SCENE_MAJOR && !w->loading;
    switch (*phase) {
    case TMV_IDLE:
        if (!w->req) return TMV_A_NONE;
        *phase = TMV_SETUP; *frames = 0; m->settle = 0; *err = TMV_E_NONE;
        return TMV_A_NONE;
    case TMV_SETUP:
        if (!w->req) { *phase = TMV_IDLE; return TMV_A_ABORT; }
        if (++*frames > TMV_WAIT_FRAMES) { *phase = TMV_FAIL; *err = TMV_E_TIMEOUT; return TMV_A_ABORT; }
        if (in_t && w->minor == TMV_SETUP_MINOR && *frames > 2u) { *phase = TMV_BRACKET; *frames = 0; return TMV_A_SETUP_GO; }
        return TMV_A_NONE;
    case TMV_BRACKET:
        if (!w->req) { *phase = TMV_IDLE; return TMV_A_ABORT; }
        if (++*frames > TMV_WAIT_FRAMES) { *phase = TMV_FAIL; *err = TMV_E_TIMEOUT; return TMV_A_ABORT; }
        if (!in_t || w->minor != TMV_BRACKET_MINOR) { if (in_t && w->minor == TMV_SETUP_MINOR && *frames > 600u) { *phase = TMV_FAIL; *err = TMV_E_SCENE; return TMV_A_ABORT; } return TMV_A_NONE; }
        if (w->cur_option == 0x1Du) { if (++m->settle >= TMV_SETTLE) { *phase = TMV_APPLY; *frames = 0; m->settle = 0; } } else m->settle = 0;
        return TMV_A_NONE;
    case TMV_APPLY:
        *phase = TMV_HOLD; *frames = 0;
        return TMV_A_APPLY;
    case TMV_HOLD:
        if (!w->req || !in_t) { *phase = TMV_IDLE; return TMV_A_ABORT; }
        if (w->leave_key) { *phase = TMV_LEAVE; return TMV_A_LEAVE; }
        if (w->page_key) return TMV_A_PAGE;
        if (w->want_applied > w->applied) return TMV_A_APPLY;
        return TMV_A_HOLD_PIN;
    case TMV_LEAVE:
        if (!in_t) *phase = TMV_IDLE;
        return TMV_A_NONE;
    default:
        if (!w->req) { *phase = TMV_IDLE; *frames = 0; *err = TMV_E_NONE; }
        return TMV_A_NONE;
    }
}

#ifndef TMV_HOST
/* ---- the console side -------------------------------------------------------------------------------------------------------------------------------------------------------- */
#include "gnt_tmb.h"
#include "gnt_tmb.c"
#include "on_bhold.h"
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#ifndef TMV_DATA
#define TMV_DATA __attribute__((section(".data")))
#endif
#define W(a) (*(volatile u32 *)(a))
#define B8(a) (*(volatile u8 *)(a))
#define TM_BASE 0x804771C4u                          /* TmData (0x574) */
#define BRK_BASE 0x80473AB8u                         /* BracketEntry[64], 0xDC each */
#define BRK_N 64u
#define PAD0 0x804C1FACu                             /* HSD_PadMasterStatus[0]: button, last_button, trigger, repeat, release (u32 each) */
#define SCENE_W 0x80479D30u                          /* [major, pending, prev, minor] */
#define LOADING_W 0x80479D64u
#define GM_SETSTATE ((void (*)(int))0x801A42A0u)     /* gm_SetNextGameModeStateId */
#define GM_GO ((void (*)(void))0x801A4B60u)          /* gm_801A4B60 */
#define GM_AFTER ((void (*)(int))0x801A42F8u)        /* gm_ChangeGameModeAfterCurrentScene */
#define TM_PLACE ((void (*)(void *, int, int))0x8018AA74u)    /* fn_8018AA74(jobj, box, slot): the plate's position on its leaf (retail layout), the same call the bracket build makes */
#define TM_FINISH ((void (*)(void))0x80192938u)      /* fn_80192938: the setup's own finish (defaults, shuffle) */
#define GM_MENU_MODE 1                               /* GM_MENU: the stock Tourney exit */
#define PB_A 0x0100u
#define PB_B 0x0200u
#define PB_X 0x0400u
#define PB_Y 0x0800u
#define PB_START 0x1000u

/* the request and the data, in RAM words the hub entry (or a harness) fills; tmv_in is read-only for us */
volatile u32 tmv_st[TMV_W_N] TMV_DATA = { TMV_MAGIC };
struct tmb_pack tmv_in TMV_DATA;
static unsigned char tmv_hs[0xBA8 + 4 + 0x60 + 4] TMV_DATA;     /* + the r13 block of the online codeset (0x60 bytes + a magic word, ml_glue.c tn_restore / tn_unrestore) */
struct tmv_pub tmv_pub TMV_DATA = { tmv_st, &tmv_in, (unsigned)sizeof(struct tmb_pack), tmv_hs };
static struct tmv_m M TMV_DATA;
static struct tmb_state S TMV_DATA;
static u32 g_applied TMV_DATA, g_x_last TMV_DATA, g_y_last TMV_DATA;

static void tm_setup_go(void)
{
    u8 *tm = (u8 *)TM_BASE;
    u32 i;
    W(TM_BASE + 0x04) = 0;                           /* match_type: 0 = Tournament (1v1) */
    W(TM_BASE + 0x08) = 2;                           /* combatants */
    W(TM_BASE + 0x0C) = tmv_in.size_idx;             /* entrants: the size index */
    W(TM_BASE + 0x10) = 0;                           /* hmn_cpu_count: no humans, the retail bracket never plays by itself here */
    W(TM_BASE + 0x14) = 0;                           /* stage_selection_type */
    W(TM_BASE + 0x18) = 5;                           /* cpu_level */
    TM_FINISH();                                     /* x2E, x2F, the x37[] defaults and the position shuffle */
    for (i = 0; i < 64u; i++) {                      /* our entrants, positions in leaf order (the k-th occupied leaf holds entrant k) */
        u8 *e = tm + 0x37u + i * 0x12u;
        if (i < tmv_in.nent) {
            e[0] = tmv_in.ent[i].kind; e[1] = tmv_in.ent[i].level; e[2] = tmv_in.ent[i].handicap; e[3] = tmv_in.ent[i].ch; e[7] = tmv_in.ent[i].costume;
            e[9] = (u8)(tmv_in.ent[i].label >> 8); e[10] = (u8)tmv_in.ent[i].label;
        }
        e[0xD] = (u8)i;
    }
    GM_SETSTATE(1);                                  /* the setup's own "Yes" */
    GM_GO();
}

static u32 tm_boxes(struct tmb_box *box)             /* the box table as the retail scene just built it: successor and the initial occupancy, no retail data carried */
{
    u32 n = 0, i;
    for (i = 0; i < BRK_N; i++) {
        u32 b = BRK_BASE + i * 0xDCu;
        if (!B8(b + 0)) break;
        box[n].nxt = B8(b + 5); box[n].slot = B8(b + 6);
        box[n].occ0 = B8(b + 0x2Cu + 4u) != 0; box[n].occ1 = B8(b + 0x2Cu + 0x2Cu + 4u) != 0;
        n++;
    }
    return n;
}

extern void tm_a514(int count);                      /* on_bhold_sis.S: fn_8018A514(count, 4.5f) */
int bh_sis_size(void *text, int idx, const u32 *sxy_bits);

/* ---- names over the plates (the retail plates are icons only): one SIS text per plate, drawn with the engine's text maker (on_bhold.c text_make), positions from the plate's layout coordinates ---- */
#ifndef TMV_NO_NAMES
#define TMV_TXT_MAX 64u
#define TMV_LEAF_Y 350                                /* the lowest leaf of the tree after the lift (retail: about 414), names go under it */
#define TMV_TOP_Y 70                                  /* the tree's top never goes above this */
#ifndef NAME_DY
#define NAME_DY 24                                    /* pixels below the plate's leaf point (calibrated in Dolphin) */
#endif
static void *tm_txt[TMV_TXT_MAX] TMV_DATA;
static u32 tm_i2f(u32 n)                             /* a positive integer as float bits (the engine is soft-float; on_bhold.c bh_i2f is compiled out while the hold-B picture is off) */
{
    u32 e = 23u;
    if (!n) return 0;
    while (n >= (1u << 24)) { n >>= 1; e++; }
    while (n < (1u << 23)) { n <<= 1; e--; }
    return ((e + 127u) << 23) | (n & 0x7FFFFFu);
}
static u32 tm_f(int n, int sh)                       /* the float n / 2^sh as bits (n any int, soft-float safe) */
{
    u32 neg = n < 0, a = neg ? (u32)(-n) : (u32)n;
    if (!a) return 0;
    return (tm_i2f(a) - ((u32)sh << 23)) | (neg ? 0x80000000u : 0u);
}
/* a name text on the Tourney scene's own SIS canvas (pixel space like the retail panel texts, e.g. fn_8018ECA8 at x 143 / 514, y 87): created WITHOUT text_make's CSS link override */
static void *tm_text(int px, int py, u32 fx, u32 fy, const char *s, int half)
{
    void *t = TEXT_NEW(0, (int)W(0x804D663C));                       /* the retail Tourney text canvas (lbl_804D663C, :2377), pixel space */
    u32 xy[2];
    if (!t) { bh_st.w[BHS_TEXT_SKIP]++; return 0; }
    bh_st.w[BHS_TEXT_NEW]++;
    put(t, 0x24, fx); put(t, 0x28, fy);                               /* HSD_Text.font_size (retail: 0.054945 x 0.08) */
    put(t, 0x34, 0x3F800000u); put(t, 0x38, 0x3F800000u);             /* x34: scale 1.0 (the CSS maker sets these too) */
    ((u8 *)t)[0x49] = 1;                                              /* default_kerning */
    ((u8 *)t)[0x4A] = 1;                                              /* default_alignment: centred on the subtext x */
    /* calibrated in Dolphin (calibration mode, dbg 2): at this font size the subtext position is pixel = 320 + 0.420 u, 250 + 0.612 v (480 space, centred), so convert the wanted pixel */
    xy[0] = tm_f(((px - 320) * 1000) / (half ? 280 : 420), 0); xy[1] = tm_f(((py - 250) * 1000) / (half ? 408 : 612), 0);    /* the factors scale with the font size: 0.75x -> 0.420 / 0.612, 0.5x -> two thirds of that */
    bh_sis_add(t, xy, "%s", s);
    xy[0] = xy[1] = 0x3F800000u;
    bh_sis_size(t, 0, xy);                                            /* the subtext size, as the retail text setup does (fn_80198C60: 1.5 x 1.5) */
    return t;
}
static void tm_names(void)
{
    static const u32 szx[4] = { 0x3CE10E02u, 0x3D28CA82u, 0x3D610E02u, 0x3DA8CA82u }, szy[4] = { 0x3D23D70Au, 0x3D75C28Fu, 0x3DA3D70Au, 0x3DF5C28Fu };   /* retail font_size x 0.5 .75 1 1.5 */
    u32 i, bi, sl, k = 0, pw = 8u, fs = 1u;
    while (pw < tmv_in.nent) pw <<= 1;                               /* the cup size as a power of two: the leaf spacing is about 600 / pw pixels */
    if (pw > 8u) fs = 0u;                                            /* 16 and up: the half size font (about 20 px for a 5 letter name) */
    for (i = 0; i < TMV_TXT_MAX; i++) tm_txt[i] = 0;                 /* retail's rebuild (fn_8019B458 -> HSD_SisLib_803A5E70) already freed every SIS text and made a new canvas: never free ours, only forget them */
    if (tmv_in.dbg == 2u) {                                             /* calibration: four labels at known text coordinates (x, y) */
        static const int cx[4] = { 80, 320, 560, 320 }, cy[4] = { 100, 240, 400, 420 };
        static const char *cn[4] = { "A 80 100", "B 320 240", "C 560 400", "D 320 420" };
        for (k = 0; k < 4u; k++) tm_txt[k] = tm_text(cx[k], cy[k], szx[1], szy[1], cn[k], 0);
        return;
    }
    for (bi = 0; bi < BRK_N && k < TMV_TXT_MAX; bi++) {
        u32 b = BRK_BASE + bi * 0xDCu;
        if (!B8(b + 0)) break;
        for (sl = 0; sl < 2u && k < TMV_TXT_MAX; sl++) {
            u32 s = b + 0x2Cu + sl * 0x2Cu, e = B8(s + 0x24);
            const char *nm;
            if (!B8(s + 4) || e >= 64u) continue;
            nm = tmv_in.name[e];
            if (!nm[0] || sis_largest() < 3072u) continue;
            if (pw > 32u && !tmv_in.dbg) continue;                   /* 64: the leaves are about 9 px apart, no room for a name; the plates and the match list carry it */
            {   /* the plate's leaf from the box layout (retail fn_8018AA74 / SetRegularCoords with x3 = 1): x = left + slot * width, y = the box bottom (top + height; the top for a mirrored box) */
                int bx = (int)W(b + 0x0C) + (int)sl * (int)W(b + 0x14);
                int by = (int)W(b + 0x10) + (B8(b + 2) ? 0 : (int)W(b + 0x18));
                int dy = NAME_DY + (pw > 8u ? (int)sl * 11 : 0);    /* 16 and 32: every other name one line lower.  By the box's slot (left up, right down), so neighbours always alternate; the leaf index rounded from the pixel x put SW002 / SW008 on one line */
                u32 f = tmv_in.dbg ? (k & 3u) : fs ? 1u : 0u;
                tm_txt[k] = tm_text(bx, by + dy, szx[f], szy[f], nm, f == 0u);
            }
            k++;
        }
    }
}
#endif

static u8 g_page TMV_DATA, g_two TMV_DATA, g_page_set TMV_DATA;   /* page 0 winners, 1 losers; g_two: the rebuild is running, the real pending flags follow */
static u32 g_nbox TMV_DATA;

/* write the slot state S into the entries (all boxes pending for the rebuild, so the scene places every plate on its leaf: the retail placement only computes pending boxes) and start the rebuild */
static void tm_commit(u32 nb)
{
    u32 i, sl;
    {   /* ("lift everything up so the bottom doesn't run off screen"): the retail tree hangs low because it has no names under its leaves.  The whole layout moves up until its
           lowest leaf is at TMV_LEAF_Y, never higher than TMV_TOP_Y for its top.  Measured from the boxes each time, so a table that is already lifted does not move again */
        int lo = -100000, hi = 100000, d;
        for (i = 0; i < nb; i++) {
            u32 b = BRK_BASE + i * 0xDCu;
            int t = (int)W(b + 0x10), e = t + (int)W(b + 0x18);
            if (!B8(b + 0)) continue;
            if (e > lo) lo = e;
            if (t > lo) lo = t;
            if (t < hi) hi = t;
            if (e < hi) hi = e;
        }
        d = lo - TMV_LEAF_Y;
        if (hi - d < TMV_TOP_Y) d = hi - TMV_TOP_Y;
        if (lo > -100000 && d > 0)
            for (i = 0; i < nb; i++) {
                u32 b = BRK_BASE + i * 0xDCu;
                if (B8(b + 0)) W(b + 0x10) = (u32)((int)W(b + 0x10) - d);
            }
    }
    for (i = 0; i < nb; i++) {
        u32 b = BRK_BASE + i * 0xDCu;
        B8(b + 1) = 1;
        for (sl = 0; sl < 2u; sl++) {
            u32 s = b + 0x2Cu + sl * 0x2Cu;
            const struct tmb_slot *d = &S.s[i][sl];
            B8(s + 4) = d->occ; B8(s + 0x20) = d->place; B8(s + 0x21) = d->ch; B8(s + 0x22) = d->kind; B8(s + 0x23) = d->costume;
            B8(s + 0x24) = d->ent; B8(s + 0x25) = d->level; B8(s + 0x26) = d->handicap; B8(s + 0x28) = (u8)(d->label >> 8); B8(s + 0x29) = (u8)d->label;
        }
    }
    g_nbox = nb;
    W(TM_BASE + 0x24) = tmv_in.nres ? (u32)tmv_in.nres - 1u : 0u;    /* x24: matches played; the rebuild adds one */
    B8(TM_BASE + 0x2C) = 0;                                            /* x2C: no fresh build (the rebuild keeps the slots we wrote) */
    W(TM_BASE + 0x00) = 0x13u;                                        /* cur_option: rebuild lines and plates from the entries, no animation */
    g_two = 1;
    g_applied = tmv_in.ver;
    tmv_st[TMV_W_APPLIED] = tmv_in.ver;
}

static void tm_apply_wb(void)
{
    struct tmb_box box[TMB_BOXES];
    u32 nb, rc;
    if (g_page_set) { tm_a514((int)tmv_in.size_idx); g_page_set = 0; }    /* back from the losers page: the retail table of this size again */
    nb = tm_boxes(box);
    if (!nb) { tmv_st[TMV_W_ERR] = TMV_E_BUILD; return; }
    rc = tmb_build(&S, box, nb, tmv_in.ent, tmv_in.nent, tmv_in.res, tmv_in.nres);
    if (rc) { tmv_st[TMV_W_ERR] = TMV_E_BUILD; tmv_st[TMV_W_APPLIED] = 0x100u | rc; return; }
    tm_commit(nb);
}

static void tm_apply_lb(void)
{
    struct tmb_lbox lb[TMB_BOXES];
    u32 n = tmb_lb_table(lb, tmv_in.nent), i, sl;
    if (!n) { tmv_st[TMV_W_ERR] = TMV_E_BUILD; return; }
    for (i = 0; i < sizeof S; i++) ((volatile u8 *)&S)[i] = 0;
    S.nbox = (u8)n;
    for (i = 0; i < BRK_N; i++) {                                    /* the generated table into the entries (the fields fn_8018A514 copies from a retail record) */
        u32 b = BRK_BASE + i * 0xDCu;
        if (i >= n) { B8(b + 0) = 0; B8(b + 1) = 0; continue; }
        B8(b + 0) = 1; B8(b + 2) = 0; B8(b + 3) = 1; B8(b + 4) = 0; B8(b + 5) = lb[i].nxt; B8(b + 6) = lb[i].slot;
        W(b + 0x0C) = (u32)(int)lb[i].left; W(b + 0x10) = (u32)(int)lb[i].top; W(b + 0x14) = (u32)(int)lb[i].width; W(b + 0x18) = (u32)(int)lb[i].height;
        W(b + 0x1C) = 0x40900000u;                                   /* line thickness 4.5 */
        B8(b + 0x20) = 0xFF; B8(b + 0x21) = 0xFF; B8(b + 0x22) = 0x00; B8(b + 0x23) = 0xFF;      /* yellow lines, as the retail records */
        B8(b + 0x24) = 0xFF; B8(b + 0x25) = 0xFF; B8(b + 0x26) = 0x00; B8(b + 0x27) = 0xFF; B8(b + 0x28) = 0x00;
        for (sl = 0; sl < 4u; sl++) { u32 s = b + 0x2Cu + sl * 0x2Cu; B8(s + 6) = 0; B8(s + 0x26) = 9; B8(s + 4) = 0; }   /* x32, x52 = 9, x30 as fn_8018A514 leaves a fresh slot */
        S.pending[i] = tmv_in.lbx[i];
        for (sl = 0; sl < 2u; sl++) {
            u32 v = tmv_in.lbs[i][sl];
            struct tmb_slot *d = &S.s[i][sl];
            d->place = 3; d->kind = 3; d->label = 0x3E7u;
            if (v < tmv_in.nent) {
                d->occ = 1; d->place = tmv_in.lbp[i][sl]; d->ch = tmv_in.ent[v].ch; d->kind = tmv_in.ent[v].kind; d->costume = tmv_in.ent[v].costume;
                d->ent = (u8)v; d->level = tmv_in.ent[v].level; d->handicap = tmv_in.ent[v].handicap; d->label = tmv_in.ent[v].label;
            }
        }
    }
    tm_commit(n);
}

static void tm_apply(void)
{
    if (g_page && tmv_in.lb_ok) tm_apply_lb(); else { g_page = 0; tm_apply_wb(); }
}

void tmv_tick(void)
{
    struct tmv_world w;
    u32 act, tr, x;
    if (!bh_st.w[BHS_TMV_ST]) { bh_st.w[BHS_TMV_ST] = (u32)tmv_st; bh_st.w[BHS_TMV_IN] = (u32)&tmv_in; }     /* where the hub entry / harness finds the request word and the data */
    w.major = W(SCENE_W) >> 24; w.minor = W(SCENE_W) & 0xFFu; w.loading = W(LOADING_W);
    w.cur_option = W(TM_BASE);
    w.req = tmv_st[TMV_W_REQ] != 0u;
    tr = W(PAD0 + 8u); x = (tr & (PB_X | PB_B)) != 0u;                 /* B leaves like everywhere else (rule); X stays */
    w.leave_key = x && !g_x_last; g_x_last = x;
    x = (tr & PB_Y) != 0u; w.page_key = x && !g_y_last; g_y_last = x;
    if (g_two && W(TM_BASE) != 0x13u) {                               /* the rebuild ran: now the real pending flags */
        u32 i;
        for (i = 0; i < g_nbox; i++) B8(BRK_BASE + i * 0xDCu + 1) = S.pending[i];
        g_two = 0;
#ifndef TMV_NO_NAMES
        tm_names();                                                   /* after the rebuild: the new canvas (lbl_804D663C) exists now */
#endif
    }
    w.want_applied = tmv_in.ver; w.applied = g_applied;
    act = tmv_step(&M, &w);
    tmv_st[TMV_W_PHASE] = M.phase; tmv_st[TMV_W_FRAMES] = M.frames; tmv_st[TMV_W_ERR] = M.err;
    switch (act) {
    case TMV_A_SETUP_GO: g_page = 0; g_page_set = 0; if (tmv_in.nent && tmv_in.size_idx <= 8u) tm_setup_go(); else { tmv_st[TMV_W_ERR] = TMV_E_SIZE; M.phase = TMV_FAIL; } break;
    case TMV_A_APPLY: tm_apply(); break;
    case TMV_A_PAGE: if (tmv_in.lb_ok && !g_two) { g_page ^= 1u; if (!g_page) g_page_set = 1; tm_apply(); } break;       /* the other page (a switch is refused while a rebuild runs) */
    case TMV_A_HOLD_PIN: { u32 co = W(TM_BASE); if (co >= 0x14u && co <= 0x1Eu) W(TM_BASE) = 0x30u;
        { u32 bi, sl;
            for (bi = 0; bi < BRK_N; bi++) {                  /* the zoom state started to slide the pending box's plates before we pinned: put them back on their leaves (retail's own placement) */
                u32 b = BRK_BASE + bi * 0xDCu;
                if (!B8(b + 0) || !B8(b + 1)) continue;
                for (sl = 0; sl < 2u; sl++) {
                    u32 s = b + 0x2Cu + sl * 0x2Cu, g = W(s);
                    if (B8(s + 4) && g >= 0x80000000u && g < 0x81800000u && W(g + 0x28)) TM_PLACE((void *)W(g + 0x28), (int)bi, (int)sl);
                }
            } } }          /* 0x30 is a state nothing handles: fn_8019A71C (0x13..0x1A), fn_8019A86C (0x1B..0x1E, the panels) and fn_8019AF50 (only 0x1F) leave it alone and fn_8018B090 has no case for it, so the whole-bracket overview stays; the retail 0x14 advances to the zoom and fade by itself */
        W(PAD0 + 8u) &= ~(PB_A | PB_B | PB_START | PB_X); W(PAD0) &= ~(PB_A | PB_B | PB_START | PB_X); break;   /* the scene's own confirm keys do nothing in the view */
    case TMV_A_LEAVE: tmv_st[TMV_W_LEAVE] = 1; GM_AFTER(GM_MENU_MODE); GM_GO(); break;
    case TMV_A_ABORT: tmv_st[TMV_W_REQ] = 0; break;
    default: break;
    }
}
#endif /* TMV_HOST */
