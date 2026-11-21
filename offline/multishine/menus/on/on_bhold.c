/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* on_bhold: hold B for 1.0 s (time based) to leave the CSS, with the hint and the progress bar (on_bhold.h).  Original work.
   Soft-float build: no FPU instruction in this file; floats are data (bit patterns) and the two calls that take float or variadic
   arguments go through on_bhold_sis.S.  Host test: -DBH_HOST builds only the pure part (tests/bh_test.c). */
#include "on_bhold.h"
#include "../ms_fmt.h"

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef BH_DATA                                          /* the flat image has no .bss: state lives.data (the host test defines it empty) */
#define BH_DATA __attribute__((section(".data")))
#endif

struct bh_status bh_st BH_DATA = {{[BHS_MAGIC] = BHS_MAGIC_WORD}};
#define ST bh_st.w

/* ---- pure part --------------------------------------------------------------------------------------------------- */
#if !BH_QUIET || defined(BH_HOST)                       /* the hold-B picture's arithmetic: not in the image while the picture is off (size budget); the host test keeps it */
void bh_view_of(unsigned int holding, unsigned int elapsed, unsigned int lim, struct bh_view *v)
{
    unsigned int q, pm;
    if (!lim) lim = BH_HOLD_TICKS;
    if (elapsed > lim) elapsed = lim;
    if (!holding) elapsed = 0;
    q = lim / 1000u; if (!q) q = 1u;
    pm = elapsed / q; if (pm > 1000u) pm = 1000u;        /* permille of the hold, from the clock only (frame rate independent) */
    v->holding = holding != 0;
    v->fill_units = (BH_BAR_UNITS * pm) / 1000u;
    q = lim / 10u; if (!q) q = 1u;
    v->tenths_left = (lim - elapsed + q - 1u) / q;       /* 0.1 s steps, rounded up: 10 at the start, 0 at the exit */
    if (v->tenths_left > 10u) v->tenths_left = 10u;
}

unsigned int bh_i2f(unsigned int n)
{
    unsigned int e = 23;
    if (!n) return 0;
    while (n >= (1u << 24)) n >>= 1;                     /* (callers stay far below 2^24) */
    while (!(n & (1u << 23))) { n <<= 1; e--; }
    return ((127u + e) << 23) | (n & 0x7FFFFFu);
}

#endif

unsigned int bh_step(struct bh_state *s, const struct bh_in *in, struct bh_view *v)
{
    unsigned int act = BH_DO_NOTHING, held, lim = in->lim ? in->lim : BH_HOLD_TICKS, el = 0;
    v->holding = 0; v->fill_units = 0; v->tenths_left = 10u;
    if (!in->css || in->scene != s->scene) {
        if (s->have) act |= BH_DO_FORGET;                /* the old scene's text heap is gone: never free those */
        s->have = 0; s->prev_held = 0; s->peak = 0; s->exited = 0; s->last_tenths = ~0u; s->t0 = 0; s->armed = 0; s->b_prev = 0; s->rel = 0;
        s->scene = in->scene;
        ST[BHS_SHOWN] = 0; ST[BHS_HELD] = 0; ST[BHS_FILL] = 0;
        if (!in->css) return act;
        ST[BHS_VISITS]++;
    }
    ST[BHS_CSS_FRAMES]++;
    held = in->held > BH_GUARD_FRAMES ? BH_GUARD_FRAMES : in->held;
    ST[BHS_HELD] = held;
    if (in->exitkind != 0) {                             /* exit started, or the connect-code keyboard is open: draw nothing */
        if (s->have) act |= BH_DO_FREE;
        s->have = 0; s->last_tenths = ~0u;
        ST[BHS_SHOWN] = 0;
        if (in->exitkind == 2u && held != 0 && !s->exited) {   /* Melee's backstop compare left before the clock did */
            s->exited = 1; ST[BHS_EXITS]++; ST[BHS_EXIT_HELD] = held; ST[BHS_LAST_PEAK] = held;
        } else if (in->exitkind != 2u) ST[BHS_NAME_ENTRY]++;
        return act;
    }
    if (held == 0 && s->prev_held != 0) { ST[BHS_CANCELS]++; ST[BHS_LAST_PEAK] = s->peak; s->peak = 0; }
    if (held > s->peak) s->peak = held;
    ST[BHS_PEAK] = s->peak;
    s->prev_held = held;                                 /* (Melee's counter: statistics only, the exit does not depend on it) */
    /* the exit clock follows the REAL B button (in->b, the pad word), not Melee's counter.
       B must be down continuously; a release resets; B already down when the CSS opens arms only after one release (as Melee's own gate). */
    /* a release shorter than 1/16 s does not count ("hold B feels twice as slow"; a Bluetooth adapter or the test bridge drops the pad for a frame or two
       now and then, and each drop started the hold over): the clock keeps running through such a gap */
    if (!in->b && s->armed && s->b_prev) {
        if (!s->rel) { s->rel = 1; s->t_rel = in->now; }
        if (in->now - s->t_rel >= lim / 16u) { s->b_prev = 0; s->rel = 0; }
    } else if (!in->b) { s->armed = 1; s->b_prev = 0; s->rel = 0; }
    else if (s->armed && !s->b_prev) { s->b_prev = 1; s->rel = 0; s->t0 = in->now; }   /* B went down: the clock starts */
    else if (!s->armed) s->b_prev = 0;
    else s->rel = 0;                                     /* B back within the grace: the same hold */
    {   unsigned int bd = (in->b || s->rel) && s->armed && s->b_prev;
        if (bd) el = in->now - s->t0;                    /* wrap safe */
#if !BH_QUIET || defined(BH_HOST)
        bh_view_of(bd, el, lim, v);
#endif
        if (bd && in->b && el >= lim && !s->exited) {     /* (leave only on a frame with B really down: never during a dropout) */             /* B held for the whole time: leave (the caller writes the exit byte) */
            s->exited = 1; ST[BHS_EXITS]++; ST[BHS_EXIT_HELD] = held; ST[BHS_LAST_PEAK] = held;
            act |= BH_DO_EXIT;
        }
    }
    if (!s->have) {
        if (in->sis_free < 1024u) { ST[BHS_TEXT_SKIP]++; return act; }   /* an allocation that does not fit panics: never try */
        s->have = 1; s->last_tenths = ~0u;
        act |= BH_DO_MAKE;
    }
    ST[BHS_SHOWN] = 1 + v->holding;
    ST[BHS_FILL] = v->fill_units;
    ST[BHS_TENTHS] = v->tenths_left;
    act |= BH_DO_UPDATE;                                 /* the bar follows the counter every frame */
    {   u32 key = v->holding ? v->tenths_left : 0x100u;
        if (key != s->last_tenths) { s->last_tenths = key; act |= BH_DO_LABEL; }
    }
    return act;
}

/* S19: the rollback-off notice (pure part) */
unsigned int bh_note_step(struct bh_note *n, unsigned int game, unsigned int rb_off, unsigned int sis_free)
{
    if (!game) {                                         /* the text died with its scene: forget it, a new match may show it again */
        unsigned int act = n->have ? BH_NOTE_FORGET : BH_NOTE_NOTHING;
        n->shown = 0; n->frames = 0; n->have = 0;
        return act;
    }
    if (n->have) {
        if (++n->frames >= BH_NOTE_FRAMES) { n->have = 0; return BH_NOTE_HIDE; }
        return BH_NOTE_NOTHING;
    }
    if (!rb_off || n->shown) return BH_NOTE_NOTHING;
    n->shown = 1;                                        /* once per match, also when the text cannot be made */
    if (sis_free < 1024u) { ST[BHS_NOTE_SKIP]++; return BH_NOTE_NOTHING; }   /* an allocation that does not fit panics: never try */
    return BH_NOTE_MAKE;
}
void bh_note_made(struct bh_note *n, int ok)
{
    n->have = ok != 0;
    n->frames = 0;
    if (ok) ST[BHS_NOTE_SHOWN]++; else ST[BHS_NOTE_SKIP]++;
}

/* ---- in-match ping hint, pure part (on_bhold.h) ----------------------------------------------------------------- */
static unsigned int bp_median(const unsigned int *r, unsigned int n)
{
    unsigned int a, b, c;
    if (n == 1u) return r[0];
    if (n == 2u) return (r[0] + r[1]) / 2u;
    a = r[0]; b = r[1]; c = r[2];
    if (a > b) { unsigned int t = a; a = b; b = t; }     /* a <= b */
    if (b > c) b = c;                                    /* b = min(max(a,b), c) */
    return a > b ? a : b;                                /* max(min(a,b), min(max(a,b), c)) */
}

unsigned int bh_ping_step(struct bh_ping *p, unsigned int online, unsigned int acks, unsigned int rtt, unsigned int var, unsigned int now, unsigned int sec, bh_derive_fn derive)
{
    unsigned int m;
    if (!online) {
        unsigned int was = p->shown != 0;
        p->have = 0; p->n = 0; p->i = 0; p->shown = 0;
        return was ? BP_RESET : BP_NONE;
    }
    if (!p->have) { p->have = 1; p->acks = acks; p->rtt = rtt; p->var = var; return BP_NONE; }   /* the baseline of this match */
    if (acks == p->acks + 1u) {                          /* exactly one acknowledgement since the last tick: its raw round trip */
        unsigned int r = derive(p->rtt, p->var, rtt, var);
        if (r > BH_PING_MAX) r = BH_PING_MAX;
        if (r) { p->ring[p->i] = r; p->i = (p->i + 1u) % 3u; if (p->n < 3u) p->n++; }
    }
    p->acks = acks; p->rtt = rtt; p->var = var;
    if (p->n < 3u) return BP_NONE;                       /* nothing shown before 3 samples (about 1.5 s): never ENet's 500 ms start value, never the first handshake acks (Dolphin pair run: 8 / 52 / 63 ms from 1 or 2 early samples) */
    m = bp_median(p->ring, p->n);
    if (m == p->shown || (p->shown && now - p->last_t < sec)) return BP_NONE;   /* a changed value waits for the 1 s tick; the first value shows at once */
    p->shown = m; p->last_t = now;
    return BP_SET;
}

unsigned int bh_ping_make(unsigned int passes, unsigned int on)
{
    if (passes == 0u || passes > BH_PING_MAKE_PASSES) return BPM_NEVER;
    return on ? BPM_MAKE : BPM_WAIT;
}

#ifdef MS_FLASH_BLANK
unsigned int fb_step(struct fb_state *s, unsigned int now, unsigned int lim, unsigned int sc, unsigned int loading)
{
    unsigned int major = sc >> 24, pend = (sc >> 16) & 0xFFu;
    if (!lim) lim = 40500000u;
    if (s->armed) {
        if (now - s->t0 >= lim + lim / 2u || (major != s->major0 && !loading)) {   /* hard failsafe 1.5 s, or the new scene runs */
            s->armed = 0;
            s->off_count++;
            return FB_BLACK_OFF;
        }
        return FB_NONE;
    }
    if (pend != 8u) { s->latch = 0; return FB_NONE; }   /* the online scene is not pending: ready for the next leave */
    if (major == 8u || s->latch) return FB_NONE;        /* already online, or this leave was handled (the black timed out while the scene still loads) */
    s->armed = 1; s->latch = 1; s->t0 = now; s->major0 = major;
    s->on_count++;
    return FB_BLACK_ON;
}
#endif



#ifndef BH_HOST
/* ---- Melee (NTSC 1.02) addresses, config/GALE01/symbols.txt -------------------------------------- */
#define W(a) (*(volatile u32 *)(a))
#define H(a) (*(volatile u16 *)(a))
#define B8(a) (*(volatile u8 *)(a))
#define SCENE_WORD W(0x80479D30)                         /* [major, pending, prev, minor] */
#define LOADING W(0x80479D64)                            /* 0 while a scene runs */
#define SCENE_INFO W(0x804D6720)                         /* gm_804D6720: GameSceneInfo* of the running minor scene, +0 scene_kind */
#define GS_CSS 8u
#define CSS_NCURSORS B8(0x804D6CF5)                      /* mnCharSel_804D6CF5: 1 (1P modes, Slippi online) or 4 (VS) cursors */
#define CSS_EXITKIND B8(0x804D6CF6)                      /* mnCharSel_804D6CF6: != 0 -> leaving (2 = back) or the code keyboard */
#define CSS_CURSORS 0x804A0BC0u                          /* mnCharSel_804A0BC0[4]: CSSCursorData* {gobj, u8 port, .., u16 xA hold} */
#define SIS_FREE_HEAD W(0x804D7970)                      /* sislib free_head: {next, data, size} blocks */
#define SIS_CANVAS_FIRST W(0x804D797C)                   /* first text canvas entry: gx link +0xE, priority +0xF */
#define DCFLUSH ((void (*)(void *, u32))0x8034480C)
typedef void *(*fn_text_new)(int font, int canvas);
typedef void (*fn_text_free)(void *t);
#define TEXT_NEW ((fn_text_new)0x803A6754)               /* HSD_SisLib_803A6754(font, canvas) */
#define TEXT_FREE ((fn_text_free)0x803A5CC4)             /* HSD_SisLib_803A5CC4 */
int bh_sis_add(void *text, const u32 *xy_bits, const char *fmt, const char *s);       /* on_bhold_sis.S -> HSD_SisLib_803A6B98 */
int bh_sis_set(void *text, int idx, const char *fmt, int a, int b);                   /* on_bhold_sis.S -> HSD_SisLib_803A70A0 */

static int ptr_ok(u32 p) { return p >= 0x80000000u && p < 0x81800000u; }

/* px -> CSS world / text units (compile-time folded: data only) */
#define BH_PXW 9.45f
#define BH_WX(px) ((float)((px) - 320) / BH_PXW)
#define BH_WY(px) ((float)((px) - 265) / BH_PXW)
#define BH_U(px) ((float)(px) * 10.0f / BH_PXW)
static const union { float f[16]; u32 u[16]; } kt = {{
    BH_WX(BH_BOX_X_PX), BH_WY(BH_BOX_Y_PX), BH_U(BH_BOX_W_PX), BH_U(BH_BOX_H_PX), BH_U(BH_TEXT_DX_PX), BH_U(BH_TEXT_DY_PX),
    BH_WX(BH_BAR_X_PX), BH_WY(BH_BAR_Y_PX), BH_U(BH_BAR_H_PX), 0.1f, 5.0f, 0.5f, 0.38f}};
enum { K_BX, K_BY, K_BW, K_BH, K_DX, K_DY, K_RX, K_RY, K_RH, K_FONT, K_Z, K_SCALE, K_XSCALE };

struct bh_g { struct bh_state s; void *lab, *trk, *bar; u32 patched_once; };
static struct bh_g g BH_DATA;

static u32 sis_largest(void)
{
    u32 b = SIS_FREE_HEAD, m = 0, n = 0;
    while (ptr_ok(b) && n++ < 256) {
        u32 sz = W(b + 8);
        if (sz > m) m = sz;
        b = W(b);
    }
    return m;
}

static void put(void *t, u32 off, u32 bits) { *(volatile u32 *)((u8 *)t + off) = bits; }

static __attribute__((unused)) void *text_make(u32 x, u32 y, u32 w, u32 h, u32 bg, u32 fg, const char *s)
{
    void *t;
    u32 ce = SIS_CANVAS_FIRST, xy[2];
    u8 ol = 0, op = 0;
    int ok = ptr_ok(ce);
    /* drawn in the last GX link the CSS camera renders, at the top priority (as the chat rows, chat_layout.h LAY_GX_LINK/PRIO):
       above every CSS model; the canvas entry is restored right after the creation */
    if (ok) { ol = B8(ce + 0xE); op = B8(ce + 0xF); B8(ce + 0xE) = 4; B8(ce + 0xF) = 255; }
    t = TEXT_NEW(0, 0);
    if (ok) { B8(ce + 0xE) = ol; B8(ce + 0xF) = op; }
    if (!t) { ST[BHS_TEXT_SKIP]++; return 0; }
    ST[BHS_TEXT_NEW]++;
    put(t, 0x00, x); put(t, 0x04, y); put(t, 0x08, kt.u[K_Z]);
    put(t, 0x0C, w); put(t, 0x10, h);                    /* backdrop quad = box x font_size */
    put(t, 0x24, kt.u[K_FONT]); put(t, 0x28, kt.u[K_FONT]);
    put(t, 0x2C, bg); put(t, 0x30, fg);
    put(t, 0x34, kt.u[K_XSCALE]); put(t, 0x38, kt.u[K_SCALE]);
    ((u8 *)t)[0x49] = 1;                                 /* kerning as Slippi's CSS text */
    ((u8 *)t)[0x4A] = 0;                                 /* left aligned */
    xy[0] = kt.u[K_DX]; xy[1] = kt.u[K_DY];
    bh_sis_add(t, xy, "%s", s);                          /* a subtext also allocates the buffer the renderer requires */
    return t;
}

static __attribute__((unused)) void text_free(void **t)
{
    if (*t) { TEXT_FREE(*t); ST[BHS_TEXT_FREE]++; }
    *t = 0;
}

static void patch_site(u32 a, u32 bit)
{
    u32 w = W(a);
    if (w == BH_WORD_RETAIL) {
        W(a) = BH_WORD_OURS;
        DCFLUSH((void *)a, 4);
        __asm__ volatile("icbi 0,%0; sync; isync" : : "r"(a) : "memory");
        w = W(a);
    }
    if (w == BH_WORD_OURS) ST[BHS_PATCHED] |= bit;
    else ST[BHS_PATCH_BAD] = w;                          /* not Melee 1.02's word: leave it, show the hint anyway (retail timing) */
}

static u32 tbl(void)
{
    u32 t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}

static u32 held_now(void)
{
    u32 n = CSS_NCURSORS, i, m = 0;
    if (n != 1u && n != 4u) return 0;
    for (i = 0; i < n; i++) {
        u32 c = W(CSS_CURSORS + 4u * i);
        if (!ptr_ok(c) || !ptr_ok(W(c)) || B8(c + 4) != i) continue;
        { u32 h = H(c + 0xA); if (h <= BH_GUARD_FRAMES + 1u && h > m) m = h; }
    }
    return m;
}

/* B (0x0200) down on a port that controls a CSS cursor: HSD_PadMasterStatus[port].button (0x804C1FAC + 0x44 * port, the held-buttons word the hold-Z toast
   and the rollback combo read: hub_soc.c z_held), only ports with a pad (err byte +0x41 == 0).  Read only, no allocation.  Ports come from the cursors' own
   port byte (CSSCursorData +4), the same cursors held_now walks; if none is valid, port 0..3 all count (never a dead exit). */
#define PADM 0x804C1FACu
#define PAD_B 0x0200u
static u32 b_down(void)
{
    u32 n = CSS_NCURSORS, i, any = 0;
    if (n != 1u && n != 4u) n = 0;
    for (i = 0; i < n; i++) {
        u32 c = W(CSS_CURSORS + 4u * i), p;
        if (!ptr_ok(c) || !ptr_ok(W(c))) continue;
        p = B8(c + 4);
        if (p >= 4u) continue;
        any = 1;
        if (B8(PADM + p * 0x44u + 0x41u) == 0u && (W(PADM + p * 0x44u) & PAD_B)) return 1;
    }
    if (!any)
        for (i = 0; i < 4u; i++) if (B8(PADM + i * 0x44u + 0x41u) == 0u && (W(PADM + i * 0x44u) & PAD_B)) return 1;
    return 0;
}

#if defined(MS_RB_READOUT) || defined(MS_HUB_DIAG)
#include "ml_set.h"                                       /* struct ms_settings (dbgl) */
/* HUB DIAG (lab builds: hubdiag=on, or any rbread build): two compact state lines on the online character screen, drawn from the first character-screen pass and REMADE every 4th pass (a SIS text is a
   snapshot of its string): "UP ss cc r1 r2 Fffff" (the hang-locator ring, on_backend.c) and "B D S W I H E L P G M K R | Y.. Q.. A.. Z.. X.. U.. T.. V.." (the result report and the hub loader: BD seen / sent /
   skip reason / idle, hub state / error / loads / kept BD+BA, gn state, mode, report kind, rearms; loader: last reason, idle detail, attempts, ticks skipped gave-up / loading, pack table state, tries, flags
   1 gave up 2 loaded 4 BD kept 8 BA kept).  No frame counter and no other readout needed; it only waits for a text that fits. */
extern u32 ms_bc_get(u32 i) __attribute__((weak));       /* on_backend.c: the hang locator words */
extern char gns_bc_txt[40] __attribute__((weak));        /* ... and its text */
extern u32 ms_bd_diag(u32 i) __attribute__((weak));      /* ... the result-report / hub loader state words */
#ifdef MS_RB_READOUT
#define HD_ROW0 7u                                      /* under the rollback readout's lines */
#else
#define HD_ROW0 0u
#endif
static struct { u32 scene, n; void *t[3]; char l[2][56]; } hd BH_DATA;
#ifdef MS_RB_READOUT
static const union { float f[6]; u32 u[6]; } hk = {{ BH_WX(24), BH_U(440), BH_U(20), BH_WY(66 + 22 * 9), BH_WY(66 + 22 * 8), BH_WY(66 + 22 * 11) }};     /* x, w, h, UP row 9, B row 8, B2 row 11 */
#else
static const union { float f[6]; u32 u[6]; } hk = {{ BH_WX(24), BH_U(440), BH_U(20), BH_WY(66), BH_WY(88), BH_WY(110) }};                            /* x, w, h, UP row 0, B row 1, B2 row 2 */
#endif
extern const struct ms_settings *ons_cur(void) __attribute__((weak));   /* on_set.c: Settings > Diagnostics > Debug Lines (dbgl, default Off) gates these yellow lines like every other lab line */
static void hd_tick(u32 css, u32 scene)
{
    static const char kk[22] = { 'D', 'S', 'W', 'I', 'H', 'E', 'L', 'P', 'G', 'M', 'K', 'R', 'Y', 'Q', 'A', 'Z', 'X', 'U', 'T', 'V', 'S', 'N' };
    char *p, *e;
    u32 i, v, k, lo, hi;
#ifndef MS_HUB_DIAG
    return;                                              /*: the B / UP / L hub lines are gone from the readout builds (they showed before a match); only an explicit hubdiag=on build draws them */
#endif
    if (!css || scene != hd.scene) { hd.scene = scene; hd.t[0] = hd.t[1] = hd.t[2] = 0; hd.n = 0; if (!css) return; }
    if (!ms_bc_get || !ms_bd_diag) return;
    if (!ons_cur || !ons_cur()->dbgl) {                  /* Debug Lines Off: no lab text on the character screen while the console waits for the network (it was always on in hubdiag builds) */
        if (hd.t[0] || hd.t[1] || hd.t[2]) { text_free(&hd.t[0]); text_free(&hd.t[1]); text_free(&hd.t[2]); }
        return;
    }
    if (++hd.n > 90u && (hd.n & 3u) != 0u) return;       /* every pass for the first 90 passes of the screen, every 4th after that */
    (void)ms_bc_get(9u);
    text_free(&hd.t[0]); text_free(&hd.t[1]); text_free(&hd.t[2]);        /* free first: the pool is small (the old B line never drew: its gate looked at the pool before anything was given back) */
    if (sis_largest() >= 3072u) hd.t[0] = text_make(hk.u[0], hk.u[3], hk.u[1], hk.u[2], 0x000000D0u, 0xFFE040FFu, gns_bc_txt);
    for (k = 0; k < 2u; k++) {
        lo = k ? 12u : 0u; hi = k ? 22u : 12u;
        p = hd.l[k]; e = p + sizeof hd.l[0] - 1;
        *p++ = k ? 'L' : 'B';
        for (i = lo; i < hi; i++) {
            v = ms_bd_diag(i);
            if (p < e) *p++ = ' ';
            if (p < e) *p++ = kk[i];
            p = ms_fmt_ue(p, e, v, 10u);
        }
        *p = 0;
        if (sis_largest() >= 3072u) hd.t[1 + k] = text_make(hk.u[0], hk.u[4 + k], hk.u[1], hk.u[2], 0x000000D0u, 0xFFE040FFu, hd.l[k]);
    }
}
#define HD_TICK(c, s) hd_tick((c), (s))
#else
#define HD_TICK(c, s) ((void)0)
#endif
#ifdef MS_RB_READOUT
/* B0 console readout (LAB ONLY, on_bhold.h): six lines (B2: + two timing lines) at the top of the first CSS after a match, from s12_gc.c rbr_w[] (made once per CSS
   scene, never freed here: they die with the scene like every CSS text; the line buffers stay.data because the SIS line keeps the format
   arguments' text).  Digits, upper-case letters and spaces only (glyphs the SIS line format has). */
extern u32 rbr_w[RBR_N];
#ifdef MS_RB_RUNTIME
extern u32 rbr_enable;                                   /* s12_gc.c: rbread=rt builds draw only while the Options flag is on */
#define RBR_LIVE() (rbr_enable)
#else
#define RBR_LIVE() 1u
#endif
#ifdef MM_FTSTATS
#define RR_LINES 21u                                    /* + the long-pass lines SW / S1 / S2  + the Wii IO line (WII_PERFORMANCE_STUDY_2026-11-04) */
#else
#define RR_LINES 11u
#endif
/* The readout lines are built (and uploaded to the server, LAB_DIAG) without being drawn: our reads them on the server.  -DMM_RB_SHOW=1 draws them on the character screen again. */
#ifdef MM_RB_SHOW
#define RR_TEXT text_make
#else
#define RR_TEXT(...) ((void)kr.u[0])
#endif
static struct { u32 scene, made, psmade; char l[RR_LINES][56]; } rr BH_DATA;
static u32 rr_gen BH_DATA;                              /* counts the readouts made (the lab upload, on_backend.c on_ld_step, sends each one once) */
/* the readout's text for the hub overlay's LAB_DIAG upload: *gen = readouts made so far, *n = lines; the buffer is rr.l (56-byte stride), the text stays.data after the scene ends, so the sender may start it later, at the next idle moment */
static u32 rr_sent BH_DATA;                              /* the readout number the overlay has finished uploading (the overlay forgets at every reload, this stays) */
const char *bh_diag_lines(u32 *gen, u32 *n)
{
    if (*n == 0xFFFFFFFFu) { rr_sent = rr_gen; *gen = 0; return rr.l[0]; }       /* the overlay marks the readout as sent */
    *gen = rr_gen != rr_sent ? rr_gen : 0u; *n = RR_LINES;
    return rr.l[0];
}
#ifdef MM_FTSTATS
static const union { float f[24]; u32 u[24]; } kr = {{  BH_WX(24), BH_U(440), BH_U(20), BH_WY(66), BH_WY(88), BH_WY(110), BH_WY(132), BH_WY(154), BH_WY(176), BH_WY(198), BH_WY(220), BH_WY(242), BH_WY(264), BH_WY(286), BH_WY(308), BH_WY(330), BH_WY(352), BH_WY(374), BH_WY(396), BH_WY(418), BH_WY(440), BH_WY(462), BH_WY(484), BH_WY(506) }};   /* x, w, h, y of each line (data only; 3 + RR_LINES words) */
#else
static const union { float f[14]; u32 u[14]; } kr = {{ BH_WX(24), BH_U(440), BH_U(20), BH_WY(66), BH_WY(88), BH_WY(110), BH_WY(132), BH_WY(154), BH_WY(176), BH_WY(198), BH_WY(220), BH_WY(242), BH_WY(264), BH_WY(286) }};   /* x, w, h, y of each line (data only) */
#endif
/* the lines as one format table: plain bytes are copied, RF(i) prints rbr_w[i] in decimal,
   RFK(i) the same in KB (>> 10), RF_PLACE prints OFF / HSD / ALM, '\n' ends a line */
#define RF(i) ((unsigned char)(0x80u | (i)))
#define RFK(i) ((unsigned char)(0xC0u | (i)))
#define RF_PLACE ((unsigned char)0xFFu)
static const unsigned char rbf[] = { 'R', 'B', ' ', 'M', RF(RBR_MATCH), ' ', 'G', RF(RBR_STAGE), ' ', 'S', RFK(RBR_STORE), 'K', ' ', RF_PLACE, ' ', 'Z', RF(RBR_MEM), ' ', 'O', RF(RBR_OVF), ' ', 'L', RF(RBR_LOST), ' ', 'R', RF(RBR_RB), '\n', 'P', 'K', ' ', 'C', RF(RBR_CELLS), ' ', 'X', RF(RBR_XRING), ' ', 'J', RF(RBR_JRING), ' ', 'E', RF(RBR_EVTS), ' ', 'A', RFK(RBR_ARENA_HI), 'K', '\n', 'H', 'S', 'D', ' ', RFK(RBR_HSD0), ' ', RFK(RBR_HSDMIN), 'K', ' ', 'A', 'L', 'M', ' ', RFK(RBR_ALLM0), ' ', RFK(RBR_ALLMMIN), 'K', '\n', 'D', 'P', ' ', 'K', RF(RBR_MAXK), ' ', 'H', ' ', RF(RBR_H0), ' ', RF(RBR_H0 + 1), ' ', RF(RBR_H0 + 2), ' ', RF(RBR_H0 + 3), ' ', RF(RBR_H0 + 4), ' ', RF(RBR_H0 + 5), ' ', RF(RBR_H0 + 6), ' ', RF(RBR_H0 + 7), ' ', 'N', RF(RBR_EMPTY), '\n',
    'T', '1', ' ', 'N', RF(RBR_T1N), ' ', 'S', RF(RBR_T1S), ' ', 'U', RF(RBR_T1U), ' ', 'R', RF(RBR_T1R), ' ', 'P', RF(RBR_TP), ' ', 'H', RF(RBR_TH), '\n',
    'T', '2', ' ', 'N', RF(RBR_T2N), ' ', 'S', RF(RBR_T2S), ' ', 'U', RF(RBR_T2U), ' ', 'R', RF(RBR_T2R), 0 };
static const char rbpl[3][4] = { "OFF", "HSD", "ALM" };
static void rr_psl(u32 li, char t, const u32 *w, int withk)   /* "PM|PS B S A F N T M D X C [K]": the Stadium cache words of on_bhold.h, plain decimals */
{
    static const char kk[11] = { 'B', 'S', 'A', 'F', 'N', 'T', 'M', 'D', 'X', 'C', 'K' };
    char *p = rr.l[li], *e = p + sizeof rr.l[0] - 1;
    u32 i, v;
    *p++ = 'P'; *p++ = t;
    for (i = 0; i < 11u; i++) {
        if (i == 10u && !withk) break;
        v = i == 10u ? rbr_w[RBR_PSCALLS] : w[i];
        if (p < e) *p++ = ' ';
        if (p < e) *p++ = kk[i];
        p = ms_fmt_ue(p, e, v, 10u);
    }
    *p = 0;
    RR_TEXT(kr.u[0], kr.u[3 + li], kr.u[1], kr.u[2], 0x000000D0u, 0xFFE040FFu, rr.l[li]);
}
#ifdef MM_FTSTATS                                         /* : the frame period line "FS" and the message line "MM" (lab readout) */
extern u32 rbr_mm[8];
extern u32 rbr_xs[8];                                    /* the EXI cost line XE */
extern u32 rbr_sp[64];
extern u32 rbr_pr[16];                                   /* PR line: peer address, port, mean / min / max round trip */                                   /* : the long passes {frame, interval, A, B, C, Z, K, R, S, Y} x (4 first + the longest), [50] count >= 60 ms, [51] count >= 500 ms */
#if defined(MS_MATCH_MSG) && defined(HUB_OVL_RES)
#include "gns_ovl.h"
extern volatile u32 gns_st[];
#endif
static void rr_kv(u32 li, const char *pre, const char *keys, const u32 *v, u32 n)
{
    char *p = rr.l[li], *e = p + sizeof rr.l[0] - 1;
    u32 i, x;
    while (*pre && p < e) *p++ = *pre++;
    for (i = 0; i < n; i++) {
        x = v[i];
        if (p < e) *p++ = ' ';
        if (p < e) *p++ = keys[i];
        p = ms_fmt_ue(p, e, x, 10u);
    }
    *p = 0;
    RR_TEXT(kr.u[0], kr.u[3 + li], kr.u[1], kr.u[2], 0x000000D0u, 0xFFE040FFu, rr.l[li]);
}
/* NT line: the peer connection of an engine session that ended (s12_gc.c end_session): address, port, datagrams sent / received, CONNECTs dropped by the allow-list or the peer check, connects tried.
   Marks the readout as new so the overlay uploads it when the search is over (a failed pairing has no match readout). */
void bh_nt_note(const u32 *v)
{
    u32 nv[9];
    nv[0] = v[0] >> 24; nv[1] = (v[0] >> 16) & 255u; nv[2] = (v[0] >> 8) & 255u; nv[3] = v[0] & 255u; nv[4] = v[1];
    nv[5] = v[2]; nv[6] = v[3]; nv[7] = v[4] + v[5]; nv[8] = v[6];
    rr_kv(17, "NT", "ABCDPTRLO", nv, 9u);
    rr_gen++;
}
#endif
static void rbr_tick(u32 css, u32 scene)
{
    const unsigned char *f = rbf;
    char *p, *e;
    u32 li = 0, c, v;
#ifdef MM_FTSTATS
    if (!css && (scene >> 24) == 1u) css = 1u;              /*  cube run (option 1): also on the menu screens, so Game Night's room screen shows it (no Slippi CSS there) */
#endif
    if (!css || scene != rr.scene) { rr.scene = scene; rr.made = 0; rr.psmade = 0; }
    if (css && rr.made && !rr.psmade && rbr_w[RBR_PSCALLS] && sis_largest() >= 3072u) { rr.psmade = 1; rr_psl(7, 'S', &rbr_w[RBR_PS0], 1); }   /* after psc_free ran in this CSS (never shown if it hangs: then PM tells the state before it) */
    if (!css || rr.made || !RBR_LIVE() || !rbr_w[RBR_MATCH] || sis_largest() < 3072u) return;
    rr.made = 1; rr_gen++;
    for (p = rr.l[0], e = p + sizeof rr.l[0] - 1;; f++) {
        c = *f;
        if (c == '\n' || !c) {
            *p = 0;
            RR_TEXT(kr.u[0], kr.u[3 + li], kr.u[1], kr.u[2], 0x000000D0u, 0xFFE040FFu, rr.l[li]);
            if (!c || ++li >= 6u) break;
            p = rr.l[li]; e = p + sizeof rr.l[0] - 1;
        } else if (c == RF_PLACE) {
            const char *s = rbpl[rbr_w[RBR_PLACE] < 3u ? rbr_w[RBR_PLACE] : 0u];
            while (*s && p < e) *p++ = *s++;
        } else if (c & 0x80u) {
            v = rbr_w[c & 0x3Fu];
            if (c & 0x40u) v >>= 10;
            p = ms_fmt_ue(p, e, v, 10u);
        } else if (p < e) *p++ = (char)c;
    }
    {   char *q = rr.l[10], *e = q + sizeof rr.l[0] - 1;
        static const char kk[6] = { 'M', 'F', 'N', 'L', 'K', 'W' };
        u32 vv[6], i, v;
        vv[0] = rbr_w[RBR_FT_MS]; vv[1] = rbr_w[RBR_FT_F]; vv[2] = rbr_w[RBR_FT_N] & 0xFFFFu; vv[3] = rbr_w[RBR_FT_N] >> 16; vv[4] = rbr_w[RBR_FT_K] & 0xFFu; vv[5] = rbr_w[RBR_FT_K] >> 8;
        *q++ = 'F'; *q++ = 'T';
        for (i = 0; i < 6u; i++) {
            v = vv[i];
            if (q < e) *q++ = ' ';
            if (q < e) *q++ = kk[i];
            q = ms_fmt_ue(q, e, v, 10u);
        }
        *q = 0;
        RR_TEXT(kr.u[0], kr.u[3 + 10], kr.u[1], kr.u[2], 0x000000D0u, 0xFFE040FFu, rr.l[10]);
    }
#ifdef MM_FTSTATS
    rr_kv(11, "SW", "FDABCZKRSY", &rbr_sp[48], 10u);
    rr_kv(9, "NW", "OI", &rbr_sp[58], 2u);               /* the longest pass: udp datagrams the Wii sent (O) and received (I) during it; both 0 = the network path was silent, I 0 only = nothing arrived */     /* : the longest pass: match frame, interval (0.1 ms); A rest of the previous tick body, B outside our code, C start of this body; Z B0, K captures, R restores inside it; S stalled B0 calls, Y time-sync ones */
    if (rbr_sp[60] > 0u) rr_kv(12, "S1", "FDABCZKRSY", &rbr_sp[0], 10u);
    if (rbr_sp[60] > 1u) rr_kv(13, "S2", "FDABCZKRSY", &rbr_sp[12], 10u);
    {   /* Wii only: the vBBA's published status block (kernel words, nothing on a GameCube): send queue sent / dropped / peak, wait ms average / max, udp out / in */
        volatile u32 *sb = (volatile u32 *)0xD31BE000u;
        u32 sq[7], qi;
        for (qi = 0; qi < 7u; qi++) { sq[qi] = 0; __asm__ volatile("" ::: "memory"); }   /* (no memset in the overlay) */
        if (W(0x800000F8u) > 200000000u && sb[126] == 0x56425854u && (sb[127] >> 24) == 0xD3u) {
            volatile u32 *e = (volatile u32 *)sb[127];
            sq[0] = e[37]; sq[1] = e[31]; sq[2] = e[29]; sq[3] = e[33] ? e[34] / e[33] : 0u; sq[4] = e[35]; sq[5] = sb[12]; sq[6] = sb[13];
        }
        rr_kv(15, "SQ", "SDPAMUR", sq, 7u);
    }
    {   extern u32 rbr_io[8];                        /* IO (Wii only, WII_PERFORMANCE_STUDY_2026-11-04 part 2; all 0 on a GameCube): L sendto completions >= 50 ms in the match, V average sendto us, J ARM main-loop passes >= 5 ms, K longest pass ms since boot, N passes (J K N need the NIN_MS_PASSHIST kernel) */
        rr_kv(20, "IO", "LVJKN", rbr_io, 5u);
    }
    {   u32 pv[8];                                    /* PR: peer address as four numbers, port, mean / min / max ENet round trip in ms over the match (0 = no peer) */
        pv[0] = rbr_pr[0] >> 24; pv[1] = (rbr_pr[0] >> 16) & 255u; pv[2] = (rbr_pr[0] >> 8) & 255u; pv[3] = rbr_pr[0] & 255u; pv[4] = rbr_pr[1];
        pv[5] = rbr_pr[2]; pv[6] = rbr_pr[3]; pv[7] = rbr_pr[4];
        rr_kv(16, "PR", "ABCDPRNX", pv, 8u);
    }
    { u32 dv[9]; u32 i9; for (i9 = 0; i9 < 6u; i9++) dv[i9] = rbr_pr[6 + i9]; dv[6] = rbr_pr[12]; dv[7] = rbr_pr[13]; dv[8] = rbr_pr[14]; rr_kv(18, "DS", "FOZRCL", dv, 6u); rr_kv(19, "DJ", "JDM", dv + 6, 3u); }        /* the ASM's desync detection (s12_gc.c rbr_ds): our frame when it fired, ODB frame, finalized frame, the remote entry's frame and checksum, our checksum for that frame (all 0 = no desync was detected) */          /* why the in-match ping line is missing: passes P, flags F (1 on, 2 peer, 4 setting), tried K, make verdict M, SIS largest L, created C, shown S, samples N */
    rr_kv(14, "XE", "NTAXDIQS", rbr_xs, 8u);          /* EXI transactions since match frame 3: N count, T total time (0.1 ms), A average and X longest (us), D time in x_delay (0.1 ms), I imm, Q dma, S select counts */
    rr_kv(8, "FS", "AHPXSN", rbr_mm, 6u);                /* frame period of the match in 0.1 ms: A mean, H median, P p99, X max; S frames >= 20 ms; N frames */
#if defined(MS_MATCH_MSG) && defined(HUB_OVL_RES)
    {   u32 m[10];                                       /* the message check (gns.ovl, gns_ovl.h GS_MM_*): P checks sent, R replies, S plates shown, B refused, L lost; A / C average / worst call, D / E average / worst plate draw, T worst check, in microseconds */
        m[0] = gns_st[GS_MM_POLLS]; m[1] = gns_st[GS_MM_RX]; m[2] = gns_st[GS_MM_SHOWN]; m[3] = gns_st[GS_MM_BAD]; m[4] = gns_st[GS_MM_LOST];
        m[5] = gns_st[GS_MM_CN] ? gns_st[GS_MM_CSUM] / gns_st[GS_MM_CN] / 40u : 0u; m[6] = gns_st[GS_MM_CMAX] / 40u;
        m[7] = gns_st[GS_MM_DN] ? gns_st[GS_MM_DSUM] / gns_st[GS_MM_DN] / 40u : 0u; m[8] = gns_st[GS_MM_DMAX] / 40u; m[9] = gns_st[GS_MM_PMAX] / 40u;
        rr_kv(9, "MM", "PRSBLACDET", m, 10u);
    }
#endif
#endif
    rr_psl(6, 'M', &rbr_w[RBR_PM0], 0);                  /* Stadium cache state at the match end */
}
#define RBR_TICK(c, s) rbr_tick((c), (s))
#elif defined(MM_FC)
/* FRAME COST (test build RELEASE_TEST=fc, fc_log.h): the readout words of the last two matches (s12_gc.c fc_end, two slots) as FC_LINES hex lines for the
   hub overlay's LAB_DIAG upload (hub_res.c HUB_LD_FN = bh_diag_lines), the oldest first; a slot is formatted when it is handed out.  Clean games too. */
#if defined(MM_DZ)
#error "MM_FC and MM_DZ are separate test builds: both define bh_diag_lines"
#endif
#include "fc_log.h"
extern u32 fc_slot[2][FC_W];
extern u32 fc_pend[2];
static char fc_txt[FC_LINES][56] BH_DATA;
static u32 fc_cur BH_DATA, fc_fmt BH_DATA;
const char *bh_diag_lines(u32 *gen, u32 *n)
{
    u32 k;
    if (*n == 0xFFFFFFFFu) {                                     /* the overlay marks the readout it was given as sent */
        if (fc_cur < 2u && fc_pend[fc_cur] == fc_fmt) fc_pend[fc_cur] = 0;
        *gen = 0; return fc_txt[0];
    }
    k = fc_pend[0] && (!fc_pend[1] || fc_pend[0] > fc_pend[1]) ? 0u : fc_pend[1] ? 1u : 2u;     /* the NEWEST readout first (our powers off about 30 s after the game: the one just played must go first) */
    *n = FC_LINES;
    if (k == 2u) { fc_cur = 2u; *gen = 0; return fc_txt[0]; }
    if (fc_fmt != fc_pend[k]) { fc_lines(fc_txt, fc_slot[k]); fc_fmt = fc_pend[k]; }
    fc_cur = k;
    *gen = fc_pend[k] & 0xFFu ? fc_pend[k] & 0xFFu : 1u;
    return fc_txt[0];
}
#define RBR_TICK(c, s) ((void)0)
#else
#define RBR_TICK(c, s) ((void)0)
#endif

#ifdef HUB_OVL_RES
extern void hub_res_tick(void);                          /* on_backend.c / hub_res.c: load or free the hub overlay for this scene */
#endif
#ifdef MS_FLASH_BLANK
static struct fb_state fbs BH_DATA;
static void fb_tick(void)
{
    u32 bus = W(0x800000F8), act;
    act = fb_step(&fbs, tbl(), bus >= 80000000u && bus <= 300000000u ? bus / 4u : 40500000u, SCENE_WORD, LOADING);
    if (act == FB_NONE) return;
    ((void (*)(int)) 0x80350100)(act == FB_BLACK_ON);     /* VISetBlack (the same call as ml_glue.c soft_reset) */
    ((void (*)(void)) 0x8034FF78)();                      /* VIFlush: the VI shadow registers take it at the next retrace */
}
#define FB_TICK() fb_tick()
#else
#define FB_TICK() ((void)0)
#endif


/* The CSS chips (delay, region) are NOT in the engine any more: they run in the hub overlay (hub/hub_soc.c chip_frame); the pure logic stays in on_bhold.h. */

#include "tmv.c"                                        /* the retail Tourney scene as a view of a server bracket (tmv.h): idle unless tmv_st[TMV_W_REQ] is set */

void bh_tick(void)
{
    struct bh_in in;
    struct bh_view v;
    u32 act, info;
#ifdef HUB_OVL_RES
    hub_res_tick();                                      /* the first hub entry of every engine pass, outside the network lock */
#endif
    tmv_tick();
    FB_TICK();                                           /* flashblank=on (diag): black while the hub hands over to the online scene */
    if (!g.patched_once) { g.patched_once = 1; patch_site(BH_SITE0, 1u); patch_site(BH_SITE1, 2u); }
    info = SCENE_INFO;
    in.css = LOADING == 0 && ptr_ok(info) && B8(info) == GS_CSS;
    in.scene = SCENE_WORD;
    in.exitkind = in.css ? CSS_EXITKIND : 0;
    in.held = in.css ? held_now() : 0;                  /* also after the exit started: the counter stays at its last value */
#ifdef MS_QCHAT
    { extern int qc_win_open(void); in.b = in.css && !qc_win_open() ? b_down() : 0; }   /* QUICK CHAT: while the chat window is open, B closes it (menus/qc/qc_gc.c); no hold-B exit then */
#else
    in.b = in.css ? b_down() : 0;                       /* the exit clock's input: the real B button */
#endif
    in.sis_free = in.css && !in.exitkind && !g.s.have ? sis_largest() : 0x10000u;
    in.now = tbl();
    {   u32 bus = W(0x800000F8);                         /* time base = bus clock / 4 (40.5 MHz GameCube, 60.75 MHz Wii); an implausible word keeps the GameCube value */
        in.lim = bus >= 80000000u && bus <= 300000000u ? bus / 4u : BH_HOLD_TICKS;
    }
    RBR_TICK(in.css, in.scene);                          /* B0 readout (lab builds only) */
    HD_TICK(in.css, in.scene);                           /* hub diag lines (lab builds only) */
    if (in.css && !g.s.have && (!ST[BHS_SIS_MIN] || in.sis_free < ST[BHS_SIS_MIN])) ST[BHS_SIS_MIN] = in.sis_free;
    act = bh_step(&g.s, &in, &v);
    if (act & BH_DO_EXIT) CSS_EXITKIND = 2;              /* the CSS's back exit, exactly the byte Melee's own hold-B block stores */
#if !BH_QUIET                                          /* the "Hold B to exit" picture is off (BH_QUIET): its text calls, strings and label code are left out of the image */
    if (act & BH_DO_FORGET) { g.lab = 0; g.trk = 0; g.bar = 0; }
    if (act & BH_DO_FREE) { text_free(&g.bar); text_free(&g.trk); text_free(&g.lab); }
    if ((act & BH_DO_MAKE) && !BH_QUIET) {
        g.lab = text_make(kt.u[K_BX], kt.u[K_BY], kt.u[K_BW], kt.u[K_BH], 0x000000D0u, 0xFFFFFFFFu, "Hold B to exit");
        g.trk = text_make(kt.u[K_RX], kt.u[K_RY], bh_i2f(BH_BAR_UNITS), kt.u[K_RH], 0x404040E0u, 0x00000000u, " ");
        g.bar = text_make(kt.u[K_RX], kt.u[K_RY], 0, kt.u[K_RH], 0xFFC820FFu, 0x00000000u, " ");
    }
    if ((act & BH_DO_LABEL) && g.lab) {
        if (v.holding) bh_sis_set(g.lab, 0, "Exiting in %d.%d s", (int)(v.tenths_left / 10u), (int)(v.tenths_left % 10u));
        else bh_sis_set(g.lab, 0, "Hold B to exit", 0, 0);
    }
    if (act & BH_DO_UPDATE) {
        if (g.trk) ((u8 *)g.trk)[0x4D] = v.holding ? 0 : 1;          /* hidden flag */
        if (g.bar) { put(g.bar, 0x0C, bh_i2f(v.fill_units)); ((u8 *)g.bar)[0x4D] = v.fill_units ? 0 : 1; }
    }
#endif
}

/* The rollback-off notice keeps its rectangle and its fill. cube check: the toasts' near-black fill is invisible on Final Destination's dark starfield; a mid slate blue reads on dark and on bright stages. */
#define BH_PLATE 0x2C4690F0u
/* The two frozen-stage texts (match notice, stage select label) look and feel like the toasts: the rounded plate of toast_pill.h in its default fill (0C1428), white text, the in-match
   toast's plate (gns_ovl.c TS_*): 34 px high, 11 px pad left and right, the line y unchanged; the notice sits in the toast's TOP RIGHT slot, row 0.  The plate is laid out round the MEASURED line (HSD_Text.current_width, +0x70). */
#include "../sign/toast_pill.h"
#define BH_PILL_DY 82500                                  /* plate top below the box top (TS_PLATE_DY), Q16 units (6000 = 1 px) */
#define BH_PILL_H 210000                                  /* plate height (TS_PLATE_H) */
#define BH_PILL_PAD 66000                                 /* 11 px left and right */
#define BH_PILL_RIGHT 1769472                             /* the slot's right edge, +27.0 units from the centre (TS_RIGHT_Q) */
#define BH_PILL_TOP 1398000                               /* the first row's top, 21.3 units above the centre (TS_TOP_Q) */
extern u32 bh_note_pool[];
static void bh_pill(void *t, int right)
{
    u32 cw = W((u32)t + 0x70u);
    int fx, lx, w, ox;
    if (cw - 1u >= 0x7F7FFFFFu) return;                   /* not measured yet (the first draw has not run) */
    fx = tp_q(W((u32)t + 0x24u), 16);
    lx = (tp_q(bh_note_pool[0], 4) * fx) >> 4;            /* the line's left inset in plate units */
    ox = lx - BH_PILL_PAD;
    w = ((tp_q(cw, 4) * fx) >> 4) - lx + 2 * BH_PILL_PAD;
    tp_attach_at(t, ox, BH_PILL_DY, w, BH_PILL_H);
    if (right) { put(t, 0x00, tp_f2(BH_PILL_RIGHT - w - ox, 16)); put(t, 0x04, tp_f2(-(BH_PILL_TOP + BH_PILL_DY), 16)); }
}
/* S19: the rollback-off notice (console part) */
void *bh_note_create(const char *line);
void bh_note_fit(void *t);                               /* on_bhold_sis.S: box width = the text's measured width (HSD_Text.current_width) + the left inset */
extern u32 bh_note_pool[];                               /* on_bhold_sis.S: the line pools; word 1 = pool 0's line y */
void *bh_ping_create(const char *line);                  /* on_bhold_sis.S: the same, with the delay text's own parameters (right aligned at x 270, y 207, size 0.33, alpha 0x80) */                  /* on_bhold_sis.S: own HUD camera + canvas + one-line text */
extern u32 s19_rb_off;                                   /* s12_gc.c: nonzero while rollback is off for memory in this match */
extern u32 ms_frozen_kind;                               /* s12_gc.c fd_step: 1 = Frozen Final Destination, 2 = Frozen Pokemon Stadium (the notice says it) */
static struct { struct bh_note n; void *t; } nt BH_DATA;
void *bh_sss_create(const char *line, u32 font);          /* on_bhold_sis.S: bh_note_create's text on a camera made from the menu camera (the stage select has no HUD camera) */
extern u32 ms_sss_kind;                                   /* s12_gc.c fd_step: on the stage select, 1 = the hovered icon is Final Destination and FD is frozen, 2 = Pokemon Stadium and frozen, else 0 */
/* Stage select: the word "Frozen" on the dark plate while the hovered icon is a frozen stage.  One text per visit of the stage select, made when first needed
   and hidden / shown with the hover; it dies with the scene (the same rule as the match notice).  Position: menu camera units, 11.25 px each, origin (320, 297) px: lower left, in the circle under the stage name (the name art ends about y 420). */
static struct { void *t; } ss BH_DATA;
void bh_sss_tick(void)
{
    static const union { float f; u32 u; } sx = { -24.9f }, sy = { 12.34f };      /* px (40, 450): the plate's top (the box top + BH_PILL_DY) */
    u32 sc = SCENE_WORD, k;
    if (LOADING || (sc >> 24) != 2u || (sc & 0xFFu) != 1u) { ss.t = 0; return; }
    k = ms_sss_kind;
    if (k && !ss.t && sis_largest() >= 1024u) {
        ss.t = bh_sss_create("Frozen", 0);                /* font 0: the menu's own */
        if (ss.t) {
            put(ss.t, 0x00, sx.u); put(ss.t, 0x04, sy.u); put(ss.t, 0x0C, 0x43480000u); put(ss.t, 0x10, 0x41F00000u);   /* box 200 x 30 until the first draw has measured the text */
            tp_attach(ss.t);                           /* the toast's rounded plate in its default fill (the box until bh_pill has the measured line) */
        }
    }
    if (ss.t) {
        ((u8 *)ss.t)[0x4D] = k ? 0 : 1;                   /* HSD_Text.hidden */
        bh_pill(ss.t, 0);
    }
}
void bh_note_tick(void)
{
    u32 sc = SCENE_WORD, game = LOADING == 0 && ((sc >> 24) == 8u || (sc >> 24) == 2u) && (sc & 0xFFu) == 2u, act, want = s19_rb_off | ms_frozen_kind;   /* match scene: online (major 8) or offline Vs Melee (major 2) */
    ST[BHS_NOTE_STATE] = s19_rb_off;
    act = bh_note_step(&nt.n, game, want, game && want && !nt.n.shown ? sis_largest() : 0x10000u);
    if (act & BH_NOTE_FORGET) nt.t = 0;
    if ((act & BH_NOTE_HIDE) && nt.t) { ((u8 *)nt.t)[0x4D] = 1; nt.t = 0; }   /* HSD_Text.hidden */
    if (nt.t && s19_rb_off && W((u32)nt.t + 0x70u) - 1u < 0x7F7FFFFFu) bh_note_fit(nt.t);   /* the rollback-off notice (old rectangle): the width the renderer measured (the end of the line after the last frame's draw, positive finite): the dark box follows the text on any console and aspect */
    if (nt.t && !s19_rb_off) bh_pill(nt.t, 1);               /* the frozen-stage notice: the toast's plate, top right */
    if (act & BH_NOTE_MAKE) {
        /* pos = top left of the box, below the match timer at the left edge (the self-test overlay's place);
           dark translucent box (bg_color) behind the line.  S19d: the line carries the engine's numbers (s19_rb_info: free KB,
           largest cell KB, KB needed, state), so a console run tells WHY rollback went off without a log (the first real Stadium match had none).
           The box is wider for it (the text scale is the same). */
        static const union { float f; u32 u; } px = { -28.6f }, py = { -15.0f }, bw = { 430.0f }, bh = { 30.0f }, fw = { 340.0f };
        static char line[72];
        {
            extern u32 s19_rb_info[4];
            const char *s = s19_rb_off ? BH_NOTE_TEXT : "Frozen";   /*: the word alone is enough (was "Frozen Final Destination" / "Frozen Pokemon Stadium") */     /* rollback off outranks the frozen-stage line (one notice a match) */
            char *p = line, *e = line + sizeof line - 1;
            u32 k;
            while (*s && p < e) *p++ = *s++;
            for (k = 0; s19_rb_off && k < 4 && p + 8 < e; k++) {               /* " F1234K L1100K N1456K S3": free, largest, needed, state */
                u32 v = s19_rb_info[k], d = 1000000u;
                int any = 0;
                *p++ = ' '; *p++ = "FLNS"[k];
                for (; d; d /= 10u) { u32 c = (v / d) % 10u; if (c || any || d == 1u) { *p++ = (char)('0' + c); any = 1; } }
                if (k < 3) *p++ = 'K';
            }
            *p = 0;
        }
        if (s19_rb_off) {                                /* rollback off: the old rectangle, top left under the timer, glyphs centred by the line y -8.0 (pool 0 is the toasts' too, put back at once) */
            u32 y0 = bh_note_pool[1];
            bh_note_pool[1] = 0xC1000000u;
            nt.t = bh_note_create(line);
            bh_note_pool[1] = y0;
        } else nt.t = bh_note_create(line);
        if (nt.t) {
            put(nt.t, 0x00, px.u); put(nt.t, 0x04, py.u); put(nt.t, 0x0C, s19_rb_off ? bw.u : fw.u); put(nt.t, 0x10, bh.u);
            if (s19_rb_off) put(nt.t, 0x2C, BH_PLATE); else tp_attach(nt.t);   /* frozen: the toast's rounded plate (the box until bh_pill has the measured line) */
            put(nt.t, 0x30, 0xFFE040FFu);                /* text colour: yellow */
        }
        bh_note_made(&nt.n, nt.t != 0);
    }
}

/* In-match ping hint, console part (on_bhold.h).  One text per match made with the notice's own HUD camera (bh_note_create); the number is re-printed only when the pure step says it changed.
   Position / size: next to the "Delay: Nf" text (Slippi's HUD canvas, bottom right).  A failure to make the text is final for this match (never retried per frame). */
#include "ml_set.h"                                       /* struct ms_settings (dbgl) */
extern unsigned int s7_rtt_derive(unsigned int, unsigned int, unsigned int, unsigned int);
extern const struct ms_settings *ons_cur(void) __attribute__((weak));
extern void ml_hud_apply(void) __attribute__((weak));     /* ml_glue.c (resident menu layer): absent in builds without it */
static struct { struct bh_ping p; void *t; u32 tried, passes; } pg BH_DATA;
#ifdef MM_FTSTATS
u32 pg_dbg[8] BH_DATA;                                         /* PG readout line (why the ping line is missing): passes, flags (on, active, setting), tried, make verdict, SIS largest at the try, created, shown, samples */
#endif
void bh_ping_tick(u32 active, u32 acks, u32 rtt, u32 var)
{
    u32 sc = SCENE_WORD, game = LOADING == 0 && (sc >> 24) == 8u && (sc & 0xFFu) == 2u, on, bus, act;
    on = game && active && ons_cur && ons_cur()->pingon;   /* Settings > Game > Show ping */
    if (!game) { pg.t = 0; pg.tried = 0; pg.passes = 0; } /* the text died with its scene */
    else if (pg.passes < 0xFFFFu) pg.passes++;
    if (ml_hud_apply && pg.passes <= 1u) ml_hud_apply();  /* the HUD marks (Delay text, its place, the Ping line's place, Raise HUD) from the settings, every frame outside the match and on its first pass: self-healing, whatever reset them since the menu (ml_glue.c ml_hud_apply; Slippi route, tester) */
    if (game && !pg.tried) {                             /* review H2: made hidden at the START of the match, before any frame a rollback can restore (on_bhold.h bh_ping_make) */
        u32 mk = bh_ping_make(pg.passes, game && ons_cur && ons_cur()->pingon);      /* made hidden in the window whenever the setting is on: in a Game Night room the peer connection is not up yet in the first 30 passes, the line is shown later */
        u32 sl = sis_largest(), fits = sl >= 1024u;
        if (mk == BPM_MAKE && fits) pg.tried = 1;        /* one try per match, inside the window: while the text heap is too small the next early pass tries again */
        else if (mk == BPM_NEVER) pg.tried = 1;
#ifdef MM_FTSTATS                                       /* the PG words are read by nothing since the DS line took their place: test builds only, the release engine is 96 bytes over its limit with them */
        if (game) {                                      /* only the match's own passes write the PG words (the menu passes overwrote them after the match) */
            if (pg.passes == 1u) pg_dbg[3] = 0;          /* M: 0x10 per early pass that waited or found no room, low nibble = the verdict (0 wait, 1 make, 2 never) */
            if (mk == BPM_WAIT || !fits) pg_dbg[3] += 0x10u; else pg_dbg[3] = (pg_dbg[3] & ~0xFu) | mk;
            if (pg.passes <= 2u || mk != BPM_WAIT) pg_dbg[4] = sl;     /* L: the text heap's largest block at the first pass and at the try */
        }
#endif
        if (mk == BPM_MAKE && fits) {  /* an allocation that does not fit panics: never try */
            pg.t = bh_ping_create("Ping: 000 ms");       /* the longest line it will ever hold: updates never reallocate; same text parameters as Slippi's Delay text, nothing else is set (position, size, colour and shadow all come from the delay text's own values) */
            if (pg.t) ((u8 *)pg.t)[0x4D] = 1;            /* HSD_Text.hidden until the third sample */
        }
    }
    bus = W(0x800000F8);
#ifdef MM_FTSTATS
    if (game) {                                          /* the PG line's words (readout builds print them) */
        pg_dbg[0] = pg.passes; pg_dbg[1] = (on ? 1u : 0u) | (active ? 2u : 0u) | ((ons_cur && ons_cur()->pingon) ? 4u : 0u); pg_dbg[2] = (pg_dbg[2] & ~1u) | pg.tried;
        if (pg.passes == 1u) pg_dbg[2] = pg.tried | (ons_cur ? 0x10u : 0u) | ((ons_cur && ons_cur()->pingon) ? 0x20u : 0u) | (active ? 0x40u : 0u);      /* the first pass of the match scene: setting reachable (0x10), Show ping on (0x20), a peer (0x40) */
        pg_dbg[5] = pg.t != 0; pg_dbg[6] = pg.p.shown; pg_dbg[7] = pg.p.n | (pg.p.have << 8);
    }
#endif
    act = bh_ping_step(&pg.p, on, acks, rtt, var, tbl(), bus >= 80000000u && bus <= 300000000u ? bus / 4u : BH_HOLD_TICKS, s7_rtt_derive);
    if (act == BP_RESET) { ST[BHS_PING] = 0; if (pg.t) ((u8 *)pg.t)[0x4D] = 1; return; }   /* HSD_Text.hidden */
    if (act != BP_SET) return;
    ST[BHS_PING] = pg.p.shown;
    if (pg.t) { ((u8 *)pg.t)[0x4D] = 0; bh_sis_set(pg.t, 0, "Ping: %d ms", (int)pg.p.shown, 0); }
}

/* the Game Night watcher's in-match toast (gns_res.c -> ms/gns.ovl w_toast): the text maker only.  The overlay owns the line, its timing, the rounded plate and the
   6-texts-per-scene limit (a hidden text lives until its scene ends); the engine keeps the part that needs its own symbols: the same in-match text as the rollback notice (bh_note_create), never
   when the SIS heap cannot take it (an allocation that does not fit panics).  Was bh_toast, (the toast moved to the overlay: this is the smaller half). */
void *bh_toast_make(const char *line)
{
    return sis_largest() < 1024u ? 0 : bh_note_create(line);
}
#endif
