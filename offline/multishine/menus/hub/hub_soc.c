/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * hub_soc: the hub overlay's menu-time social glue.  CONSOLE ONLY, in ms/hub.ovl (never resident):
 *   - the MENU TICK: in Melee's menus (major scene 1) once a second PRESENCE_SET state 2 with the toast block (client_net/socc.c) on the hub client's wire v2 session;
 *   - the TOASTS (sign/toast.c model): a Mano a mano challenge with its shrinking line, friend notices, drawn as SisLib texts on the menu's front canvas at the top edge;
 *     Z accepts: INVITE_RESPOND, then the Direct search of the challenger through the existing path (the hand-off block of the resident hub client: gn_state
 *     MNC_GN_DIRECT + the code; the character screen's lock-in (on_backend.c od_find) searches it), the scene goes to Slippi's online character screen exactly like the
 *     Multishine rows do (ml_glue.c go_mode);  a GO (our own challenge was accepted) starts the same way;
 *   - on the online character screen (major 8): the challenger's search status ("Not answered" / "Accepted") from TICKET_POLL ext bit 2 (mnc.c, MNC_POLL_INVITE), and
 *     after a match of OUR server the post-fight prompt "Z: Add <name> as a friend" (FIGHT_FRIEND), once per match (bit 1 of the resident od.mid_ok marks it shown),
 *     only when the game end (BD) of that match was seen (bit 2) and tst_fight_allowed says so (Grandmaster / Mano a mano / Game Night, a human, not a guest), drawn
 *     right of the player panel (never over the header), not when our own setting 'friend requests after a fight' is off;
 *   - on a Game Night set's character screen (the call-up hand-off, or after the set) a PRESENCE_SET every 5 s keeps the room member alive (the room is not polled
 *     there; the server's RoomsHost.seen).
 * It runs from a GObj proc of its own (game thread, the menu's proc pass), created when the overlay is entered in such a scene and neutralised (callback = a retail blr)
 * when the overlay is left; its texts die with the scene (never freed after a scene change).  Toasts never show in a match, on Slippi's own online submenu (kind 8),
 * the Event screen (kind 7, the Game Night / Friends screens) or while a page of ours holds the menu (the input cooldown is set: the sign overlay, the stub pages);
 * the menu tick pauses there too (the sign overlay drives the same client then).  Soft-float C (no FPU instruction except hub_sis.S).
 */
#include "hub_ovl.h"
#include "socc.h"
#include "toast.h"
#define TP_FILL_MENU 1                        /* menu toasts: toast_pill.h's menu profile (near black, nearly opaque) */
#include "toast_pill.h"                                   /* the rounded plate behind the toasts and the CSS line */
#include "hub_conn.h"                                     /* toasts wait while a session is connected (hold Z = Slippi's disconnect; host test hub/tests/hub_conn_test.c) */
#include "hub_rank.h"                                   /* the Rank block of OUR Grandmaster character screen (tier text, rating, placement line; host test hub/tests/hub_rank_test.c) */
#include "../on/on_bhold.h"                               /* co_policy / co_delay_step / co_hold_step: the CSS chips (host-tested in on/tests/bh_test.c) */
#include "keyhint.h"                                      /* button badges for the key of the second line ("Z: Play", owner GUI pass) */

extern const struct hub_env *hub_E;
int hs_sis_add(void *t, const unsigned *xy, const char *fmt, const char *s);    /* hub_sis.S */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define W(a) (*(volatile u32 *)(a))
#define H(a) (*(volatile u16 *)(a))
#define B(a) (*(volatile u8 *)(a))
#include "hub_icon.h"                                     /* the Slippi rank icon of the Rank block (Project Slippi rank badges, GPL-3.0; hub.ovl only) */

/* ---- retail addresses (NTSC 1.02, config/GALE01/symbols.txt) ---- */
#define SCENE 0x80479D30u                /* [major, pending, prev, minor] */
#define LOADING 0x80479D64u              /* 0 while a scene runs */
#define MENU_FLOW 0x804A04F0u            /* +0 cur_menu, +0x11 entering_menu */
#define MENU_COOLDOWN 0x804D6BC8u        /* the menu input cooldown: our pages and the sign overlay hold it non-zero */
#define MENU_CANVAS_FRONT 0x804D6BB4u    /* SisLib canvas id of the front menu camera */
#define PAD_MASTER 0x804C1FACu           /* HSD_PadMasterStatus[4], 0x44 bytes: +0 the held buttons (Z 0x10) */
#define PAD_COPY 0x804C20BCu             /* HSD_PadCopyStatus[4], 0x44 bytes: +8 trigger, +0x41 err */
#define PLINK_HEAD 0x804D782Cu
#define GOBJ_INIT 0x804CE380u
#define NOP_PROC 0x80081540u             /* a retail blr */
#define SIS_CANVAS_FIRST 0x804D797Cu     /* CSS: first text canvas entry (gx link +0xE, priority +0xF), as on_bhold.c */
#define R13_SWITCH_MENU_FLAG (-0x4AD8)   /* the u16 the retail menu thinks set to 5 when a screen is left for a scene */
#define R13_ONLINE_MODE (-0x5060)        /* Slippi: the online mode byte */
#define ONLINE_SCENE 8
#define PAD_Z 0x10u
typedef void *(*fn_text_new)(int, int);
#define TEXT_NEW ((fn_text_new)0x803A6754u)
#define TEXT_FREE ((void (*)(void *))0x803A5CC4u)
#define AUDIO ((void (*)(int))0x80024030u)            /* 0 back, 1 forward, 2 move, 3 refusal */
#define GOBJ_CREATE ((void *(*)(int, int, int))0x803901F0u)
#define GOBJ_PROC ((void *(*)(void *, void *, int))0x8038FD54u)
#define MN_PENDING ((void (*)(int))0x80229860u)       /* mn_80229860: leave the menu scene to that major scene */
#define CSS_STORE_PORT ((void (*)(int))0x801677E8u)   /* gm_801677E8: the port that picks on the character screen */

#define TICK_MS 5000u                    /* the menu tick (eased from 1 s; the first one goes at once on entry and right after a HELLO) */
#define KEEP_MS 5000u                    /* Game Night character screen: one PRESENCE_SET (state online, the reply ignored) this often keeps the room member alive */
#define HELLO_GAP_MS 15000u
#define MSG_SETTLE_MS 3000u        /* a fetch marks the message shown on the server: ask only when the screen has been steady for 3 s (not just entered, not just left a page of ours), so the toast is not lost to a scene change */
enum { OP_NONE, OP_TICK, OP_RESP, OP_FRIEND, OP_FIGHT, OP_PROF, OP_HELLO, OP_KEEP, OP_WHOAMI, OP_MSGF, OP_RANK, OP_FCHK };

/* text geometry as float bit patterns (compile-time constants: no FPU code) */
static const union { float f[16]; u32 u[16]; } K = {{
    -1.52f, -11.35f, 17.0f / 0.028f, 3.3f / 0.028f,     /* 0-3 menu backdrop x, y, box w, h (box units = size / font): TOP RIGHT, plate right edge 28 px from the screen edge, top 28 px down; half the screen wide (25-50 %; was 27.2 units from x -11.72) */
    -0.92f, -10.95f, -10.0f, -8.80f,                   /* 4-7 line x, line 0 y, line 1 y, bar y (the same shift: +1.88 right, +1.55 down; 18.86 px a unit on the menu canvas) */
    0.028f, 17.0f, 0.32f / 0.028f, 0.0f,                /* 8-11 font, z, bar box h, zero */
    -11.0f, 18.5f, 43.0f / 0.1f, 1.6f / 0.1f }};        /* 12-15 CSS line x, y, box w, h (font 0.1 as on_bhold): the free area right of the player panel, below
                                                           Slippi's "Press START to search" (screen px 216..622, y 440; world = (px - 320 | 265) / 9.45); never over the header / BACK strip */
/* shrink-wrap: the plate is as wide as the longer line, right edge fixed at the top right (15.48 units); n = characters of that line,
   n = the line's width in 0.56 unit steps at font 0.028.  The width is measured with toast_pill.h's ONE glyph table
   (HS_N), not counted in characters.  0.62 units padding left and right.  [n] plate x, line x (compile-time floats: no FPU code) */
static const union { float f[45]; u32 u[45]; } KX = {{ 14.240f, 13.680f, 13.120f, 12.560f, 12.000f, 11.440f, 10.880f, 10.320f, 9.760f, 9.200f, 8.640f, 8.080f, 7.520f, 6.960f, 6.400f, 5.840f, 5.280f, 4.720f, 4.160f, 3.600f, 3.040f, 2.480f, 1.920f, 1.360f, 0.800f, 0.240f, -0.320f, -0.880f, -1.440f, -2.000f, -2.560f, -3.120f, -3.680f, -4.240f, -4.800f, -5.360f, -5.920f, -6.480f, -7.040f, -7.600f, -8.160f, -8.720f, -9.280f, -9.840f, -10.400f }};
static const union { float f[45]; u32 u[45]; } KL = {{ 14.860f, 14.300f, 13.740f, 13.180f, 12.620f, 12.060f, 11.500f, 10.940f, 10.380f, 9.820f, 9.260f, 8.700f, 8.140f, 7.580f, 7.020f, 6.460f, 5.900f, 5.340f, 4.780f, 4.220f, 3.660f, 3.100f, 2.540f, 1.980f, 1.420f, 0.860f, 0.300f, -0.260f, -0.820f, -1.380f, -1.940f, -2.500f, -3.060f, -3.620f, -4.180f, -4.740f, -5.300f, -5.860f, -6.420f, -6.980f, -7.540f, -8.100f, -8.660f, -9.220f, -9.780f }};
static const union { float f[1]; u32 u[1]; } KH1 = {{ 1.70f / 0.028f }};    /* the plate's box height for a one-line toast (two lines: K.u[3], 3.3 units); 1.70: the text sits in the middle */
static const union { float f[3]; u32 u[3]; } KC = {{ 0.1f, 0.5f, 0.38f }};    /* CSS font, scale y, scale x (on_bhold.c kt) */

static struct {
    void *go;
    unsigned scene;
    socc S;
    struct tst T;
    u8 lastp, op, hello, suppress, port, css_kind, prof_asked, fight_on, fchk, dret, n, n_drawn;   /* n / n_drawn: characters of the longer toast line now / when the plate was sized; fchk: the question before the post-fight prompt: 1 wanted, 2 asked; dret: this character screen was entered by a Mano a mano hand-off (toast accept / accepted challenge): keep MNC_GN_DIRECT so the menu entry returns to the hub's Mano a mano row */
    void *zt[2];                                                                 /* the CSS toast's key hint: the Z badge letter, the action word */

    u32 next_tick, hello_at, quiet, ping_at, prev_ms, dw0, dgive;                    /* dw0 / dgive: a Mano a mano search began waiting / the give-up notice began (css_frame) */
    u8 msg_want, msg_more, msg_last, msg_idle;                                  /* friend messages: a fetch is wanted / another page may follow / the unread count of the last tick / the count at the last finished fetch (a count the server keeps because the website has not read it is not asked again) */
    void *bd, *l0, *l1, *bar, *css;
    struct socc_rank rk;                                                         /* OUR Grandmaster rank (RANKED_STATUS) for the character screen's Rank block */
    u8 rk_try, rk_ok, rk_re;                                                     /* rk_re: a match was played since the block was drawn: ask again (games left / rating stayed old until the screen was left and entered) */
    u8 rk_mid[8];                                                                /* the match id the block's numbers belong to */
    u32 rk_at;
    void *cht, *cht2, *chi[3], *cht3;                                                                /* the CSS chips' two plain white lines (delay; Grandmaster: region) */
    u32 chs;                                                                     /* what it shows (value key); 0 = nothing drawn */
    struct co_hold chh;                                                          /* hold R (region) */
    void *yt[2];                                                                 /* the "R Standings" hint of the Grandmaster screen: the key letter, the word */
    void *hz[4];                                                                 /* VARIANT Z: the "Z Friends" hint of the Main Menu: badge letter, word, hold bar */
    struct frd_hold hzh;                                                         /* VARIANT Z: hold Z on the Main Menu */
    char hz_s[48];                                                               /* VARIANT Z: the hint's text as drawn */
    u8 y_go;                                                                     /* R was pressed: the screen is being left for Standings */
    void *rt[4];                                                                 /* the block's texts: label, disc, tier, rating or placement line */
    void *kb[3];                                                                 /* the second line's key badges: letter 0, letter 1 (plates behind them), the Hold prefix */
    char s0[TST_LINE + 1], s1[TST_LINE + 1];
    int line;
    u32 ticks, toasts;
    u32 eb_s[2];                                                                 /* the CSS error box: the text scale words (0x24 / 0x28) as Slippi made them, kept while the box is enlarged */
    u8 eb_on;
    u32 eb_x, eb_y;                                                              /* the struct's own x / y position (+0x0 / +0x4, y grows downward) as Slippi made them */
} S;

/* RAM proof words (Dolphin runs read them through hub_st[HB_ADDR] + the symbol's offset in the image, hub_ovl_pack.py --stats "hs_proof_off"):
   [0] 'HSOC'  [1] menu tick replies | toasts drawn << 16  [2] toast kind | busy << 4 | raised action << 8 | op in flight << 16 | suppressed << 24
   [3] last finished op | its socc err << 8 | its server status << 16 | 1 << 24 once a Mano a mano hand-off was armed (go_direct) */
volatile unsigned hs_proof[6];       /* [4] HSD_PadMasterStatus[0].button, [5] the toast's hold: ports with Z held | on << 8 | permille << 16 (hold-Z proof) */
static void zero(void *p, unsigned n) { u8 *b = (u8 *)p; while (n--) *b++ = 0; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void copy(char *d, const char *s, unsigned max) { unsigned i = 0; while (s[i] && i < max) { d[i] = s[i]; i++; } d[i] = 0; }
static u32 r13(void) { u32 v; __asm__ volatile("mr %0,13" : "=r"(v)); return v; }
static u32 now_ms(void) { return hub_E->clock_ms(); }
static struct od *O(void) { return hub_E->o; }

/* integer -> float bits (no FPU) */
static u32 i2f(u32 v)
{
    u32 e = 31, m;
    if (!v) return 0;
    while (!(v & (1u << e))) e--;
    m = e > 23u ? v >> (e - 23u) : v << (23u - e);
    return ((e + 127u) << 23) | (m & 0x7FFFFFu);
}

/* Melee's SIS encoder lacks some punctuation: full-width Shift-JIS codes (the table of ml_core.c ml_sis_expand / gnb_ev.c sis_print) */
static const char SJ_CHR[] = "!?;()[]+/%#&*@$=<>~|_^{}";
static const u8 SJ_LO[] = { 0x49, 0x48, 0x47, 0x69, 0x6A, 0x6D, 0x6E, 0x7B, 0x5E, 0x93, 0x94, 0x95, 0x96, 0x97, 0x90, 0x81, 0x83, 0x84, 0x60, 0x62, 0x51, 0x4F, 0x6F, 0x70 };
static void sis(void *t, const char *in)
{
    char out[100];
    unsigned n = 0, i, j;
    u32 xy[2];
    for (i = 0; in[i] && n + 3u < sizeof out; i++) {
        int k = -1;
        for (j = 0; j < sizeof SJ_CHR - 1u; j++) if (SJ_CHR[j] == in[i]) k = (int)j;
        if (k < 0) out[n++] = in[i]; else { out[n++] = (char)0x81; out[n++] = (char)SJ_LO[k]; }
    }
    out[n] = 0;
    xy[0] = 0; xy[1] = 0;
    hs_sis_add(t, xy, "%s", out[0] ? out : " ");
}

/* ---- menu texts (front canvas) ---- */
static void *mtext(u32 x, u32 y, u32 w, u32 h, u32 bg, u32 fg, const char *s)
{
    u32 *t = (u32 *)TEXT_NEW(0, B(MENU_CANVAS_FRONT));
    if (!t) return 0;
    t[0] = x; t[1] = y; t[2] = K.u[9];
    t[3] = w; t[4] = h;
    t[0x24 / 4] = K.u[8]; t[0x28 / 4] = K.u[8];
    t[0x2C / 4] = bg; t[0x30 / 4] = fg;
    ((u8 *)t)[0x49] = 1; ((u8 *)t)[0x4A] = 0;
    sis(t, s);
    return t;
}
static void tfree(void **p) { if (*p) TEXT_FREE(*p); *p = 0; }
static void kb_free(void) { tfree(&S.kb[0]); tfree(&S.kb[1]); tfree(&S.kb[2]); }
static void toast_hide(void) { kb_free(); tfree(&S.l1); tfree(&S.l0); tfree(&S.bar); tfree(&S.bd); S.s0[0] = 0; S.s1[0] = 0; S.line = -2; }

/* the second line "Z: Play" as (Z) Play: the key's letter on its plate, then the action (keyhint.h).  Font 0.028 as every toast text; the pair starts at the line's x.  1 = drawn, 0 = not a key line or a
   text could not be made (the caller draws the old text) */
#define TK_FONT 280u
static void *kb_text(int x_q, int y_q, u32 fg, const char *s, unsigned fe, unsigned align)
{
    void *t = mtext(tp_f2(x_q, 16), tp_f2(y_q, 16), 0, 0, 0, fg, s);
    if (!t) return 0;
    ((u32 *)t)[0x24 / 4] = tp_f2((int)(fe * 6554u / 1000u), 16); ((u32 *)t)[0x28 / 4] = ((u32 *)t)[0x24 / 4];
    ((u8 *)t)[0x4A] = (u8)align;
    return t;
}
static int kb_line(const char *b, int n)
{
    struct kh_seg g;
    struct kh_pair p;
    unsigned i, pl = 0;
    int x0 = tp_q(KL.u[n], 16), y0 = tp_q(K.u[6], 16);
    if (!kh_parse(b, &g)) return 0;
    kh_pair_build(&g, TK_FONT, &p);
    for (i = 0; i < p.n; i++) {
        const struct kh_item *it = &p.it[i];
        void *t;
        if (it->kind == KH_I_PLATE) {
            u32 ic = kh_ink_rgb(it->key);
            t = kb_text(x0 + it->x, y0 + it->dy, (ic << 8) | 0xFFu, it->s, it->fe4, 1);
            if (!t) { kb_free(); tfree(&S.l1); return 0; }
            kh_attach(t, it);
            S.kb[pl++] = t;
        } else if (it->kind == KH_I_PRE) {
            S.kb[2] = kb_text(x0 + it->x, y0, 0xFFD040FFu, it->s, TK_FONT, 0);
            if (!S.kb[2]) { kb_free(); tfree(&S.l1); return 0; }
        } else {
            S.l1 = kb_text(x0 + it->x, y0, 0xFFD040FFu, it->s, TK_FONT, 0);
            if (!S.l1) { kb_free(); return 0; }
        }
    }
    return 1;
}
/* the width of a line in steps of 0.56 units at font 0.028 (a lower case letter there is 9.3 px = 1.06 steps: tp_text_gw is in 1/100 letters) */
static u32 hs_n(const char *s) { return (tp_text_gw(s) * 106u + 9999u) / 10000u; }
/* steps (of 0.56 units) the key pair of a second line takes: the badges and the Hold word count as their width */
static u32 kb_chars(const char *b)
{
    struct kh_seg g;
    struct kh_pair p;
    if (!kh_parse(b, &g)) return 0;
    kh_pair_build(&g, TK_FONT, &p);
    return (u32)(p.w + 36699) / 36700u;
}

static void toast_draw(u32 now)
{
    char a[TST_LINE + 1], b[TST_LINE + 1];
    int line;
    if (!tst_on(&S.T) || S.suppress) { if (S.bd) toast_hide(); return; }
    line = tst_text(&S.T, now, a, b);
    {   /* the width of the longer line: plate box = n x 20.0 + 44 box units (0.56 and 1.24 units / 0.028), clamped to the table */
        u32 la = hs_n(a), lb = hs_n(b);
        { u32 kc = kb_chars(b); if (kc) lb = kc; }                                  /* a key line: its badges' width, not the "Z: " it replaces */
        S.n = (u8)(la > lb ? la : lb);
        if (S.n > 44u) S.n = 44u;
    }
    if (S.bd) ((u32 *)S.bd)[4] = b[0] ? K.u[3] : KH1.u[0];                         /* one line: a one-line plate (shrink-wrapped in height too) */
    if (S.bd && S.n != S.n_drawn) {                                                  /* a new text of another width: the plate and the lines move */
        ((u32 *)S.bd)[0] = KX.u[S.n]; ((u32 *)S.bd)[3] = i2f((u32)S.n * 2000u / 100u + 44u);
        if (S.l0) ((u32 *)S.l0)[0] = KL.u[S.n];
        if (S.l1) ((u32 *)S.l1)[0] = KL.u[S.n];
        if (S.kb[0] || S.kb[2]) S.s1[0] = 1;                                         /* the badges are placed from the line's x: made again below */
        if (S.bar) ((u32 *)S.bar)[0] = KL.u[S.n];
        S.n_drawn = S.n;
    }
    if (!S.bd) {
        S.bd = mtext(KX.u[S.n], K.u[1], i2f((u32)S.n * 2000u / 100u + 44u), K.u[3], 0, 0xFFFFFFFFu, " ");
        S.n_drawn = S.n;
        if (S.bd) tp_attach(S.bd);                                              /* the rounded plate (toast_pill.h) instead of the renderer's rectangle */
        S.s0[0] = 1; S.s1[0] = 1;                                               /* never equal: the lines are made below */
        S.toasts++;
    }
    if (!same(a, S.s0)) { tfree(&S.l0); S.l0 = mtext(KL.u[S.n], K.u[5], 0, 0, 0, 0xFFFFFFFFu, a); copy(S.s0, a, TST_LINE); }
    if (!same(b, S.s1)) { kb_free(); tfree(&S.l1); if (b[0] && !kb_line(b, S.n)) S.l1 = mtext(KL.u[S.n], K.u[6], 0, 0, 0, 0xFFD040FFu, b); copy(S.s1, b, TST_LINE); }
    if (line < 0) { tfree(&S.bar); }
    else {
        u32 w = i2f((u32)line * ((u32)S.n * 2000u / 100u) / 1000u);           /* the text's width at the full window */
        if (!S.bar) S.bar = mtext(KL.u[S.n], K.u[7], w, K.u[10], 0xFFC820FFu, 0, " ");
        else ((u32 *)S.bar)[3] = w;
    }
    S.line = line;
}

/* ---- CSS text (on_bhold.c technique: a text canvas entry's GX link and priority, restored right after the creation). DRAW ORDER (mnCharSel_80262.. in the decomp): the one CSS camera draws the GX
   links 0 .. 4 in that order; the background and panels are link 1, the P1 token and the character models link 2 (priority 0x80), the HAND cursor link 3 (0x80), the "press start" model link 4.  The first version
   put our texts and the rank icon on link 4 at 255 ("above every CSS model"), so they covered the hand and the token.  Now link 2 below priority 0x80: over the panels (link 1), under
   the tokens (same link, drawn later) and under the hand (link 3).  The icon is at CSS_PRIO_ICON, under the texts. ---- */
#define CSS_LINK 2u
#define CSS_PRIO 0x70u
#define CSS_PRIO_ICON 0x10u
static void *ctext_at(const char *s, u32 x, u32 y, u32 w, u32 h, u32 fg, u32 bg, u32 sy, u32 sx)
{
    u32 *t, ce = W(SIS_CANVAS_FIRST);
    u8 ol = 0, op = 0;
    int ok = ce >= 0x80000000u && ce < 0x81800000u;
    if (ok) { ol = B(ce + 0xE); op = B(ce + 0xF); B(ce + 0xE) = CSS_LINK; B(ce + 0xF) = CSS_PRIO; }
    t = (u32 *)TEXT_NEW(0, 0);
    if (ok) { B(ce + 0xE) = ol; B(ce + 0xF) = op; }
    if (!t) return 0;
#ifndef CSS_TEXT_Z
#define CSS_TEXT_Z 0x00000000u                                                       /* z 0.0: the plane of the CSS panels; the C stick pans the camera and texts at z 5.0 moved only a quarter as far as the background (parallax) */
#endif
    t[0] = x; t[1] = y; t[2] = CSS_TEXT_Z;
    t[3] = w; t[4] = h;
    t[0x24 / 4] = KC.u[0]; t[0x28 / 4] = KC.u[0];
    t[0x2C / 4] = bg; t[0x30 / 4] = fg;
    t[0x34 / 4] = sx; t[0x38 / 4] = sy;
    ((u8 *)t)[0x49] = 1; ((u8 *)t)[0x4A] = 0;
    sis(t, s);
    return t;
}
/* the character screen's momentary messages are TOASTS: white text on the rounded dark plate (toast_pill.h), top right of the frame (right edge 28 px from it, top 32 px:).
   The text's origin sits 8 px inside the plate (plate 24 px high, 5 px above the origin); the width comes from the one glyph table (tp_text_gw, 6.6 px a letter at the CSS font 0.1 x 0.38: an ESTIMATE, not yet
   seen on a screen: Dolphin shots 'so_css' still to take).  Replaces the yellow strip of the old line.  World x = (px - 320) / 9.45, y = (px - 265) / 9.45 (on_bhold.c). */
/* CSS TOAST GEOMETRY ('the text sits below the plate, always').  The plate stays where it was: top CT_TOP_PX, CT_H_PX high.  The text object's y is its TOP, and its glyphs
   are centred CT_GLYPH_PX below that y at this font and scale (0.1 x 0.5 x 0.38: measured from the delay chip rows, whose L / R icon plates are centred 22.9 px below their letter's y, 1.5 px above the row's y,
   so 24.4 px; the old code assumed the glyphs near the y, the plate 5 px above it and 19 below: the text sat about 17 px low).  So the text's y is the plate's centre minus CT_GLYPH_PX and the plate
   hangs CT_OFF_PX below that y.  Host check: tests/test_css_toast_geom.py. */
#define CT_TOP_PX 32
#define CT_H_PX 24
#define CT_GLYPH_PX 24
#define CT_TEXT_PX (CT_TOP_PX + CT_H_PX / 2 - CT_GLYPH_PX)                      /* 20 */
#define CT_OFF_PX (CT_TOP_PX - CT_TEXT_PX)                                      /* 12: the plate's top below the text's y */
static const union { float f[1]; u32 u[1]; } KT = {{ ((float)CT_TEXT_PX - 265.0f) / 9.45f }};
#define CT_PX 6935                                                              /* one px of the 640 x 528 frame in Q16 text units (65536 / 9.45; chip_x / CHIP_PW use the same) */
static void *ctext(const char *s)
{
    u32 w_px = tp_text_gw(s) * 76u / 1000u + 16u;                                  /* plate width: the text + 8 px each side */
    void *t = ctext_at(s, tp_f2(((int)(566u - w_px + 8u) - 320) * 6935, 16), KT.u[0], 0, 0, 0xFFFFFFFFu, 0, KC.u[1], KC.u[2]);
    if (t) tp_attach_at(t, -8 * CT_PX, CT_OFF_PX * CT_PX, (int)w_px * CT_PX, CT_H_PX * CT_PX);          /* tp_attach_at takes Q16 TEXT units (9.45 px each), not px: the plate was 9.45 times too big, a dark slab over the top right of the screen */
    return t;
}

/* ---- the Rank block of OUR Grandmaster character screen: "Rank" label, tier disc, tier name, rating with one decimal / placement and games left.  Slippi's pinned asm draws
   no such block, so this is ours: texts only, the disc is a glyph of the menu font in the tier's colour (docs/data/grandmaster_tiers.json, the Standings discs), nothing of Slippi's. ---- */
static const union { float f[12]; u32 u[12]; } KR = {{
    -11.0f, 7.9f,  10.0f / 0.1f, 1.6f / 0.1f,           /* 0-3 label x, y, box w, h */
    -11.0f, 11.0f, 3.0f / 0.1f, 2.0f / 0.1f,            /* 4-7 disc x, y, box w, h */
    -3.9f, 10.7f, -3.9f, 13.2f }};                       /* 8-11 tier x, y; rating x, y (right of the badge, as Slippi's ranked screen: badge left, tier name and rating beside it) */
static const union { float f[5]; u32 u[5]; } KI = {{ -19.46f, -14.12f, 0.0f, 2.61f, 2.51f }};   /* MEASURED (TyMnView joint 18): the plate's quad runs x -5.78 .. 5.88, y +-1.355; u is piecewise (hub_tex), only x 3.24 .. 5.88 (2.64 wide) maps the texture undistorted: the badge sits there, so the joint x is the badge's left edge -11 minus 3.24 x 2.61, the width 2.64 x 2.61 = 6.9 units, the height 2.71 x 2.51 = 6.8 units: sx / sy = 1.04, the aspect measured on cube 1094 (hexagon w/h 0.79 at 0.9275, target 0.886); 1.3 x the first version, centred between the tier and rating lines, the text 6 px right of the hexagon */    /* the icon (as Slippi's ranked screen): LEFT of the tier name and rating, under the label, at the label's x; about 38 px wide and 42 px tall (80 % of the 48 x 53 of build 1082; the 64 px texture is only ever scaled DOWN, so smaller stays crisp; 1.1 taller than wide in frame pixels: round on a 4:3 TV, as the Standings icons), centred on the same lines; left x, top y (y up), z, scale x, y of the sort plate's quad (about 46 x 22 px per unit scale).  Not distorted: the quad is 1.1 taller than wide in frame pixels ON PURPOSE, the 640 x 528 frame is shown at 4:3 (pixels 1.1 wider), and the texture is square (the art is letterboxed in it, Slippi's own badges are 1.2: 1) */
static const union { float f[4]; u32 u[4]; } KS = {{ 0.5f, 0.38f, 0.62f, 0.50f }};   /* label / rating scale y, x; disc and tier scale y, x */
/* THE one call site of the tier icon (left of the tier name; the box x KR[4], y KR[5], about 2.4 units wide).  Returns the icon's object, or 0 = nothing drawn: the text then moves to the label's x.
   Dolphin the menu font has no filled-circle glyph (Shift-JIS 0x819C drew nothing), so there is NO icon until our Slippi-badge helper lands (other contributors, GPL-3.0 badges);
   it replaces this body and nothing else: S.rk.tier is the tier byte, col the tier colour.
   STATE the Slippi rank icons exist as colour textures (sign/stb_rank_tex.h, 64 x 64 CMPR: STB_RI_B; ids STB_SH_T1.. = tier byte 0.., STB_SH_GM, STB_SH_PLACEMENT; helper gart_rank_icon in sign/gnb_art.h) but the
   helper lives in sign.ovl, which is not loaded here: DONE, hub_icon.h carries the same plate technique for ONE 64 px icon. */
static void *rank_icon(u32 col)
{
    unsigned id = S.rk.tier == SOCC_TIER_GM ? 6u : S.rk.tier == SOCC_TIER_PLACEMENT ? 7u : S.rk.tier;      /* stb.h STB_SH_*: T1.. = the tier byte, GM, placement "?" */
    (void)col;
    return hi_make(id, KI.u[0], KI.u[1], KI.u[2], KI.u[3], KI.u[4], (int)CSS_LINK, (int)CSS_PRIO_ICON);      /* link 2, under our texts, under the P1 token and the hand (priority 1 on link 4 was still drawn after the hand: links are drawn in order) */
}
static void rank_draw(void)
{
    char tier[HR_TIER_MAX], line[HR_RATE_MAX + 8];
    unsigned long rgb;
    u32 col;
    if (S.rt[0] || !hr_block(&S.rk, tier, line, &rgb)) return;
    col = ((u32)rgb << 8) | 0xFFu;
    S.rt[0] = ctext_at("Rank", KR.u[0], KR.u[1], KR.u[2], KR.u[3], 0xB4B4B4FFu, 0, KS.u[0], KS.u[1]);
    S.rt[1] = rank_icon(col);
    S.rt[2] = ctext_at(tier, KR.u[8], KR.u[9], KR.u[2], KR.u[3], 0xFFFFFFFFu, 0, KS.u[2], KS.u[3]);
    S.rt[3] = ctext_at(line, KR.u[10], KR.u[11], KR.u[2], KR.u[3], 0xC8C8C8FFu, 0, KS.u[0], KS.u[1]);
}

/* ---- "R Standings" on OUR Grandmaster character screen: the Standings left the Multishine list, this is the way in.  At the top RIGHT of the header band, level with Slippi's
   "Playing:" line, the word ending at about x 505 of the 640 frame, just before the arrow of the BACK button (x 520): a key badge (the rounded plate of keyhint.h / toast_pill.h in the R colour, grey, the
   letter dark) and the word "Standings" beside it.  Shown, and R read, only while no search or session is live, no character is locked in and no room hand-off is armed.  R (the digital bit) leaves the screen like a hold B (the
   exit byte, "back") with the hand-off state STAND: the menu entry (ml_glue.c ml_enter) opens Standings over the hub and its Back re-enters this screen (go_mode 0).  The state is cleared here when the menu
   scene's overlay starts (hs_enter). ---- */
#define CSS_EXITKIND_ADDR 0x804D6CF6u
#define DW_MAX_MS 60000u               /* a Mano a mano search waits this long for the other player */
#define DW_NOTICE_MS 2500u             /* the give-up line stays this long before the character screen is left */
#define PAD_STAND PAD_Z                     /* Standings key: Z (R is now the CSS chips' key, tap = delay up, hold 1 s = region; Y is the retail costume key, so Standings had moved to R on).  Z is Slippi's hold-to-disconnect only while connected, and the hint is hidden then (st_idle) and while a toast (hold Z accepts) is up */
static const union { float f[7]; u32 u[7]; } KY = {{ 11.0f, 12.6f, -24.0f, 6.0f / 0.1f, -24.15f, 0.4f, 0.304f }};   /* x of the badge's centre, x of the word (66 px wide at this font: it ends at x 505), y of the word's line, the word's box width, y of the letter (its glyph centre on the word's), the letter's scale y, x (0.8 of the word's, keyhint.h) */
#define KY_PLATE 131072                     /* the badge plate: 2.0 text units (about 19 px) square, Q16 */
#define KY_PLATE_DY 93061                   /* its top from the letter's text y, Q16: the plate centre on the letter's glyph centre */
static int gn_css(const struct od *o);
static int st_idle(const struct od *o)
{
    return B(r13() + (u32)R13_ONLINE_MODE) == 0u && (o->phase == OD_IDLE || o->phase == OD_ERR) && !o->started && !gn_css(o) && !tst_on(&S.T)
           ;        /* shown after a character is locked in too: hidden only while searching / paired (o->started, phase).  Earlier: no character locked in: on this screen picking a character locks in (the search starts at START) */
}
static void stand_draw(const struct od *o)
{
    int i, y = 0;
    if (S.y_go) return;
    if (!st_idle(o)) { tfree(&S.yt[0]); tfree(&S.yt[1]); return; }
    if (!S.yt[0]) {
        S.yt[0] = ctext_at("Z", KY.u[0], KY.u[4], KR.u[2], KR.u[3], (kh_ink_rgb(KH_Z) << 8) | 0xFFu, 0, KY.u[5], KY.u[6]);
        if (S.yt[0]) { ((u8 *)S.yt[0])[0x4A] = 1; tp_attach_rgb_round(S.yt[0], kh_plate_rgb(KH_Z), -(KY_PLATE / 2), KY_PLATE_DY, KY_PLATE, KY_PLATE, 50u); }      /* centred on x, the plate behind it */
    }
    if (!S.yt[1]) S.yt[1] = ctext_at("Standings", KY.u[1], KY.u[2], KY.u[3], KR.u[3], 0xFFFFFFFFu, 0, KS.u[0], KS.u[1]);
    for (i = 0; i < 4; i++) if (B(PAD_COPY + (u32)i * 0x44u + 0x41u) == 0u && (W(PAD_COPY + (u32)i * 0x44u + 8u) & PAD_STAND)) y = 1;      /* newly pressed, only ports with a pad */
    if (y && o->m.gn_magic == MNC_GN_MAGIC) {
        ((struct od *)o)->m.gn_state = MNC_GN_STAND;
        B(CSS_EXITKIND_ADDR) = 2u;
        S.y_go = 1;
        tfree(&S.yt[0]); tfree(&S.yt[1]);
    }
}

/* ---- the GObj proc lookup (gnb_ev.c find_proc) ---- */
static u32 *find_cb(u32 cb)
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
                if (W((u32)pr + 0x14u) == cb) return (u32 *)(pr + 0x14);
                pr = (u8 *)W((u32)pr);
            }
            go = (u8 *)W((u32)go + 0x08u);
        }
    }
    return 0;
}

/* ---- the network: one socc request at a time, only while the hub client is idle ---- */
int od_cfg_poll(unsigned now);                                                   /* on_backend.c: the card read of the setup (retried) */
static int net_ok(void)
{
    struct od *o = O();
    return o->cfg_ok && o->net_up && o->srv_ip;
}
static int guest_id(void);
static int client_free(void)
{
    struct od *o = O();
    return net_ok() && o->m.state != MNC_BUSY && (o->phase == OD_IDLE || o->phase == OD_UP || o->phase == OD_ERR) && !o->rep_started;
}
static int session_ok(u32 now)
{
    mnc *m = &O()->m;
    return m->have_hello && mw2_ready(&m->w, now) && m->w.dev_sess;
}
static int start(u32 now, int op)
{
    int r = -1;
    socc_init(&S.S, &O()->m);
    hub_E->ob_arm();                                                            /* menu-thread datagrams are queued for the network slice (s12_gc.c od_send) */
    switch (op) {
    case OP_TICK: r = socc_tick(&S.S, now, SOCC_PRES_MENUS); break;
    case OP_RESP: r = socc_invite_respond(&S.S, now, S.T.cur.id, 1); break;
    case OP_FRIEND: r = socc_friend_answer(&S.S, now, S.T.cur.code, 1); break;
    case OP_FIGHT: r = socc_fight_friend(&S.S, now, S.T.cur.mid); break;
    case OP_FCHK: r = socc_fight_check(&S.S, now, O()->mid); break;
    case OP_PROF: r = socc_prof_get(&S.S, now); break;
    case OP_RANK: r = socc_rank_me(&S.S, now); break;
    case OP_MSGF: r = socc_msg_fetch(&S.S, now, 0, 1); break;                /* only show: nothing is marked read, each message comes once (the server tracks it) */
    case OP_KEEP: r = socc_tick(&S.S, now, SOCC_PRES_ONLINE); break;
    default: break;
    }
    S.op = r ? (u8)OP_NONE : (u8)op;
    return r;
}

/* leave for Slippi's online character screen in Unranked mode (no code keyboard; ml_glue.c go_mode); the lock-in's B4 searches the armed code in Direct mode */
static int data_files_missing(void)                                              /* the same probe as br_glue.c data_files_present: the SDK's name -> entry lookup (FST in RAM; the card pack hooks it) */
{
    return ((int (*)(const char *))0x8033796Cu)("slpCSS.dat") < 0;           /* DVDConvertPathToEntrynum: Slippi's online screen asserts without this file and the console freezes for good */
}
static int go_direct(const char *code, int port)
{
    mnc *m = &O()->m;
    if (m->gn_magic != MNC_GN_MAGIC) return 0;                                  /* an older resident: no hand-off block */
    if (data_files_missing()) return 0;                                         /* never enter the online screen without its data files */
    copy(m->gn_room, code, sizeof m->gn_room - 1u);
    m->gn_state = MNC_GN_DIRECT;
    hs_proof[3] |= 1u << 24;
    B(MENU_FLOW + 0x11u) = 1;
    H(MENU_COOLDOWN) = 5;
    *(volatile u16 *)(r13() + (u32)R13_SWITCH_MENU_FLAG) = 5;
    CSS_STORE_PORT(port);
    *(volatile u8 *)(r13() + (u32)R13_ONLINE_MODE) = 1;
    AUDIO(1);
    MN_PENDING(ONLINE_SCENE);
    toast_hide();
    S.suppress = 1;
    return 1;
}
static int first_port(void)
{
    int i;
    if (S.lastp && B(PAD_COPY + (u32)(S.lastp - 1u) * 0x44u + 0x41u) == 0u) return S.lastp - 1;   /* the pad the player is using: the bridge's adapter holds port 1 and counts as connected, so 'first connected' put a Friends invite on P1 while the pad was on port 2 */
    for (i = 0; i < 4; i++) if (B(PAD_COPY + (u32)i * 0x44u + 0x41u) == 0u) return i;
    return 0;
}
static void note_port(void)
{
    int i;
    for (i = 0; i < 4; i++) if (B(PAD_COPY + (u32)i * 0x44u + 0x41u) == 0u && W(PAD_COPY + (u32)i * 0x44u + 8u)) { S.lastp = (u8)(i + 1); return; }   /* S.lastp = port + 1, 0 = none yet (hs_proc) */
}

static void op_done(u32 now)
{
    socc *s = &S.S;
    int ok = s->state == SOCC_DONE;
    u8 op = S.op;
    unsigned i;
    S.op = OP_NONE;
    hs_proof[3] = (hs_proof[3] & 0xFF000000u) | (u32)op | ((u32)(s->err & 0xFF) << 8) | ((u32)(s->status & 0xFF) << 16);
    switch (op) {
    case OP_TICK:
        if (!ok) { if (s->err == SOCC_E_NOSESSION) S.hello_at = now; return; }
        S.ticks++;
        O()->unread = s->tick.msgs; O()->unread_ms = now | 1u; O()->srv_ok_ms = now | 1u;                       /* the Main Menu's Friends row and bar (ml_glue.c, ms_net_cost 100) */
        O()->news_req = s->tick.requests; O()->news_inv = (s->tick.invite || (s->tick.has & 1u)) ? 1u : 0u;   /* the Friends panel of the Main Menu (ms_net_cost 105) */
        O()->news_online = s->tick.online_ok ? (u8)((s->tick.online > 254u ? 254u : s->tick.online) + 1u) : 0u;                         /* friends online (ms_net_cost 106); the number only, the list loads when the Friends list opens */
        if (s->tick.has & 1u) tst_invite(&S.T, now, s->tick.id, s->tick.from, s->tick.name, s->tick.ms_left);
        for (i = s->tick.nn; i > 0; i--)                                        /* newest first on the wire: the oldest is queued first */
        {
            const struct socc_notice *nt = &s->tick.n[i - 1u];
            if (nt->kind == SOCC_N_REQUEST || nt->kind == SOCC_N_ACCEPTED) tst_notice(&S.T, now, nt->kind == SOCC_N_REQUEST ? TST_K_REQUEST : TST_K_ACCEPTED, nt->code, nt->name);
            else if (nt->kind == SOCC_N_DIRECT) tst_note(&S.T, now, TST_K_DIRECT, nt->code, nt->name, 0);
            else tst_note(&S.T, now, TST_K_ROOM, nt->code, nt->name, nt->kind == SOCC_N_ROOM_JOIN);
        }
        if ((s->tick.has & 2u) && s->tick.go[0]) go_direct(s->tick.go, first_port());   /* our challenge was accepted: search them */
        S.msg_last = s->tick.msgs;
        if (!s->tick.msgs) S.msg_idle = 0;
        else if (s->tick.msgs != S.msg_idle) S.msg_want = 1;                    /* the unread count changed: ask for what is new (the console reads nothing; the unread ring stays until the website reads or deletes) */
        return;
    case OP_MSGF:
        if (!ok) { S.msg_want = S.msg_more = 0; if (s->err == SOCC_E_NOSESSION) S.hello_at = now; return; }
        for (i = 0; i < s->nm; i++) {
            tst_msg(&S.T, now, s->msg[i].from, s->msg[i].name, s->msg[i].text);   /* "Name: text", once */
        }
        if (s->nm == SOCC_MSG_PAGE) S.msg_more = 1;                             /* a full page: more may wait */
        else { S.msg_want = S.msg_more = 0; S.msg_idle = S.msg_last; }
        return;
    case OP_RESP:
        if (ok && s->peer[0]) { if (go_direct(s->peer, S.port)) tst_done(&S.T, now, 1, 0); else tst_done(&S.T, now, 0, data_files_missing() ? "Multishine system files not set up: check network" : "Couldn't start the match"); }
        else tst_done(&S.T, now, 0, s->status == SOCC_ST_INVITE_GONE || s->status == SOCC_ST_NOT_FOUND ? "The challenge is over" : "Couldn't accept the challenge");
        return;
    case OP_FRIEND:
        if (ok) tst_done_friends(&S.T, now);                                    /* "You and <name> are friends" (3 s) */
        else tst_done(&S.T, now, 0, "Couldn't answer the request");
        return;
    case OP_FIGHT:
        tst_done(&S.T, now, ok, !ok ? (s->status == SOCC_ST_RATE_LIMITED ? "Too many requests, wait a while" : "Couldn't send the request")
                                     : s->result == 1u ? "You are friends now" : s->result == 2u ? "Already friends" : s->result == 4u ? "Sandbag players can't be friends" : "Friend request sent");
        return;
    case OP_RANK:
        if (ok && s->rk.have) { S.rk = s->rk; S.rk_ok = 1; S.rk_re = 0; tfree(&S.rt[0]); tfree(&S.rt[1]); tfree(&S.rt[2]); tfree(&S.rt[3]); { int i; for (i = 0; i < 8; i++) S.rk_mid[i] = O()->mid[i]; } }
        else if (!ok && s->err == SOCC_E_NOSESSION) S.hello_at = now;
        return;
    case OP_FCHK: {                                                             /* the server's answer about the opponent of THIS match: only 0 shows the prompt (already friends, a request open, blocked, anything unclear or no answer: nothing) */
        struct od *o = O();
        S.fchk = 0;
        o->mid_ok |= 2;
        if (ok && s->result == 0u && S.fight_on && (o->mid_ok & 1)) tst_fight(&S.T, now, o->mid, o->opp_name);
        return;
    }
    case OP_PROF:
        S.fight_on = (u8)(!ok || s->prof.fight);                                 /* unknown: show it (the server filters the other side's setting anyway) */
        return;
    case OP_KEEP:                                                               /* only the datagram mattered (the server's room member is seen) */
        if (!ok && s->err == SOCC_E_NOSESSION) S.hello_at = now;
        return;
    default:
        return;
    }
}

static u32 tb_get(void)
{
    u32 t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}
static void nc_add(int i, u32 dt)       /* NET COST probe: the frame thread's time in this class (time base ticks) */
{
    struct od *o = O();
    o->nc_n[i]++; o->nc_tot[i] += dt;
    if (dt > o->nc_max[i]) o->nc_max[i] = dt;
}
static void net_step_run(u32 now, int may_tick);
static void net_step(u32 now, int may_tick)
{
    u32 t0 = tb_get();
    net_step_run(now, may_tick);
    nc_add(1, tb_get() - t0);
}
static void net_step_run(u32 now, int may_tick)
{
    mnc *m = &O()->m;
    if (!O()->cfg_ok) { (void)od_cfg_poll(now); }                               /* the setup read belongs to the overlay's tick, not to the START timing: retried (1 per s, soft budget lifted after a pause, on_card.c) until the card layer lets it through */
    if (S.op == OP_HELLO) {
        int st = m->state;
        if (st == MNC_BUSY) { (void)mnc_poll(m, now); return; }
        S.op = OP_NONE;
        if (st != MNC_DONE) { S.hello_at = now + HELLO_GAP_MS; O()->hello_fail_ms = now | 1u; }
        else { O()->hello_fail_ms = 0; O()->srv_ok_ms = now | 1u; S.next_tick = now; }   /* a fresh session: the first tick at once */
        return;
    }
    if (S.op == OP_WHOAMI) {                                                    /* Settings > Network status: one WHOAMI = a real answer and our public address (mnc.c fills whoami_ip) */
        int st = m->state;
        if (st == MNC_BUSY) { (void)mnc_poll(m, now); return; }
        S.op = OP_NONE;
        if (st == MNC_DONE) O()->srv_ok_ms = now | 1u;
        return;
    }
    if ((O()->on_bk & 2u) && !O()->srv_ip) return;                              /* the fallback lookup of the patched name runs in od_tick (the network slice): nothing goes to the server until it ends */
    if (S.op != OP_NONE) { if (socc_poll(&S.S, now) != SOCC_BUSY) op_done(now); return; }
    if (!client_free()) return;
    if (((O()->net_ask_ms && (u32)(now - O()->net_ask_ms) < 3000u) || (!O()->srv_ok_ms && guest_id())) && (u32)(now - S.ping_at) <= 0x7FFFFFFFu) {   /* a Settings page is up, or a GUEST console whose server has not answered once since boot (a guest sends no HELLO: the
                                                                                   offline guard on_backend.c srv_never_heard needs a first answer): one WHOAMI per 10 s, only here (idle client: no search, report or update).  The first answer ends the boot probe */
        S.ping_at = now + ((O()->net_ask_ms && (u32)(now - O()->net_ask_ms) < 3000u) ? 3000u : 10000u);   /* a Settings page waits for the answer: 3 s, so Reconnect shows the result soon */
        hub_E->ob_arm();
        if (!m->have_hello && guest_id() && !O()->srv_ok_ms) {                  /* the guest's boot probe is a HELLO until the first one is answered (was always a WHOAMI): the Slippi row is closed until a HELLO says
                                                                                   whether Slippi matchmaking is allowed (on_backend.c ms_slippi_mm_off, fail closed), and a guest whose title check did not run (link up late, check
                                                                                   failed) would never get one.  One Ed25519 verify, once; after it the probe is the WHOAMI again and stops at the first answer as before */
            if (mnc_hello(m, now) == 0) S.op = OP_HELLO;
            return;
        }
        if (mnc_whoami(m, now) == 0) S.op = OP_WHOAMI;
        return;
    }
    if (guest_id()) return;                                                     /* a GUEST console (no linked account) has no presence, no toasts, no friends.  session_ok is false for it for ever (no device
                                                                                   session), so this step used to send a HELLO again at every tick, and each answer costs an Ed25519 verify on this thread (0.25 to 0.4 s on the
                                                                                   Gekko; no session key to take the cheap MAC path): the menus ran at 26 fps online on the cube */
    if (!session_ok(now)) {
        if (!may_tick && !S.T.act) return;
        if ((u32)(now - S.hello_at) > 0x7FFFFFFFu) return;                      /* not yet */
        hub_E->ob_arm();
        if (mnc_hello(m, now) == 0) S.op = OP_HELLO; else { S.hello_at = now + HELLO_GAP_MS; O()->hello_fail_ms = now | 1u; }
        return;
    }
    if (!(m->srv_caps & SOCC_CAP_TOASTS)) return;                               /* a server without toasts (or without --social): nothing to ask */
    if (S.T.act == TST_A_INVITE && data_files_missing()) { tst_done(&S.T, now, 0, "Multishine system files not set up: check network"); return; }       /* never accept a match this console cannot enter (the server would tell the challenger it was accepted and the screen would do nothing) */
    if (S.T.act == TST_A_INVITE) { if (start(now, OP_RESP)) tst_done(&S.T, now, 0, "Couldn't accept the challenge"); return; }
    if (S.T.act == TST_A_FRIEND) { if (start(now, OP_FRIEND)) tst_done(&S.T, now, 0, "Couldn't answer the request"); return; }
    if (S.T.act == TST_A_DIRECT) { if (go_direct(S.T.cur.code, first_port())) tst_done(&S.T, now, 1, 0); else tst_done(&S.T, now, 0, "Multishine system files not set up: check network"); return; }          /* Slippi Direct: the same hand-off as an accepted challenge */
    if (S.T.act == TST_A_ROOM) {                                                                                           /* Game Night room: the menu glue opens the browser in it */
        const char *c = S.T.cur.code;
        unsigned v = 0, k;
        for (k = 0; c[k] >= '0' && c[k] <= '9' && k < 4u; k++) v = v * 10u + (unsigned)(c[k] - '0');
        if (k == 4u && !c[4]) { O()->room_note = (unsigned short)(v + 1u); tst_done(&S.T, now, 1, 0); }
        else tst_done(&S.T, now, 0, "Couldn't open the room");
        return;
    }
    if (S.T.act == TST_A_FIGHT) { if (start(now, OP_FIGHT)) tst_done(&S.T, now, 0, "Couldn't send the request"); return; }
    if ((S.msg_want || S.msg_more) && may_tick == 1 && S.T.qn == 0u && S.T.cur.kind == TST_K_NONE && (u32)(now - S.quiet) >= MSG_SETTLE_MS) {          /* friend messages: one fetch at a time, the next page and the ack only after the toasts are through */
        S.msg_want = S.msg_more = 0;
        if (m->srv_caps & SOCC_CAP_MESSAGES) { (void)start(now, OP_MSGF); return; }
    }
    if (S.fchk == 1u) { S.fchk = 2; if (start(now, OP_FCHK)) { S.fchk = 0; O()->mid_ok |= 2; } return; }
    if (S.prof_asked == 1u) { S.prof_asked = 2; (void)start(now, OP_PROF); return; }
    if (may_tick == 3) { S.rk_try++; S.rk_at = now + 2000u; (void)start(now, OP_RANK); return; }                    /* OUR Grandmaster character screen: the rank for the Rank block, up to 3 tries 2 s apart */
    if (may_tick == 2 && (u32)(now - S.next_tick) < 0x80000000u) { S.next_tick = now + KEEP_MS; (void)start(now, OP_KEEP); return; }
    if (may_tick == 1 && (u32)(now - S.next_tick) < 0x80000000u) { S.next_tick = now + TICK_MS; (void)start(now, OP_TICK); }
}

static int z_port(void)
{
    int i;
    for (i = 0; i < 4; i++) if (W(PAD_COPY + (u32)i * 0x44u + 8u) & PAD_Z) return i;
    return -1;
}

/* Z held down on any port (HSD_PadMasterStatus[port].button, 0x804C1FAC + 0x44 * port, the held buttons: the word the cloader and the rollback combo read; the copy's +0 is not the held word): the friend request toast needs a HOLD of FRD_HOLD_MS */
static int z_held(void)
{
    int i;
    for (i = 0; i < 4; i++) if (B(PAD_MASTER + (u32)i * 0x44u + 0x41u) == 0u && (W(PAD_MASTER + (u32)i * 0x44u) & PAD_Z)) return 1;   /* only ports with a pad (err 0) */
    return 0;
}

static void sound(void)
{
    u8 s = S.T.sfx;
    S.T.sfx = TST_SFX_NONE;
    if (s == TST_SFX_SHOW) AUDIO(S.T.cur.kind == TST_K_INVITE ? 1 : 2);
    else if (s == TST_SFX_OK) AUDIO(1);
    else if (s == TST_SFX_DENY) AUDIO(3);
}

/* main menu scene */
/* TEST TOAST (Settings > Diagnostics, lab builds): a toast of the chosen kind through the real model and draw path, answered locally: the hold-Z accept shows "You and <name> are friends"
   without a word to the server (our sees every look and the hold timing on the console) */
static void test_toast(u32 now)
{
    struct od *o = O();
    u8 k = o->test_toast;
    if (k) {
        o->test_toast = 0;
        if (k == 1u) tst_notice(&S.T, now, TST_K_REQUEST, "TEST@1", "Test Friend");
        else if (k == 2u) tst_invite(&S.T, now, 0x7E57ul, "TEST@1", "Test Friend", 30000u);
        else if (k == 4u) tst_note(&S.T, now, TST_K_DIRECT, "TEST#1", "Test Friend", 0);
        else if (k == 5u) tst_note(&S.T, now, TST_K_ROOM, "TEST@1", "Test Friend", 0);
        else if (k == 6u) tst_note(&S.T, now, TST_K_ROOM, "TEST@1", "Test Friend", 1);
        else tst_notice(&S.T, now, TST_K_ACCEPTED, "TEST@1", "Test Friend");
    }
    if (S.T.act && (same(S.T.cur.code, "TEST@1") || same(S.T.cur.code, "TEST#1"))) {                              /* an action raised on a TEST toast: answered here, never sent */
        if (S.T.act == TST_A_FRIEND) tst_done_friends(&S.T, now);
        else tst_done(&S.T, now, 1, "Test invite accepted");
    }
}
/* ---- VARIANT Z (test build to compare with Friends as a More row): Friends is no Main Menu row; on the Main Menu HOLDING Z for the one hold length (FRD_HOLD_MS, 1.0 s, time based)
   opens it.  Top right of the Main Menu (where a toast stands; a toast hides it and keeps Z for itself: hold Z accepts) a key badge in the Standings hint's style (keyhint.h plate in the Z colour, the
   letter in the ink colour) and the word "Friends"; a thin bar under it fills along the hold.  The hold sends the menu glue a request through the toast-room word (od.room_note = HZ_OPEN, taken and cleared
   by ms_net_cost 101, ml_glue.c ml_frame), which opens the Friends screen like the old row (same online gate).  HZ_HOLD 0 makes it a tap. ---- */
#define HZ_IN_HUB 0                    /* 0 since the MENU overlay draws the hint and takes the Z tap (ml_glue.c mz_frame); this code stays for reference */
#define HZ_HOLD 0                      /* 0: a TAP (press) of Z opens Friends (after the Wii test of 1289); 1: hold Z for FRD_HOLD_MS */
#define HZ_OPEN 0xFFFFu                /* od.room_note sentinel (rooms are code + 1 <= 10000): open Friends (ml_glue.c ML_ROOM_FRIENDS) */
static const union { float f[4]; u32 u[4]; } KZH = {{ 13.6f, -12.05f, 11.0f, -10.65f }};  /* the hint's RIGHT edge, its line y (centred on the logo emblem
                                                                                            at the top left: logo rows 14..39 of the 528-row frame), the bar's x and y */
#define HZ_MAXW_Q (20 * 65536)         /* the widest the hint may be (Q16 units): from the logo's right side to the right edge; wider: the font shrinks (never at today's worst case, 'Friends (99 online, 99 requests)' about 17.5 units) */
#define HZ_FRESH_MS 45000u             /* the counts are those of the last menu tick reply and are shown while it is younger than this (on_backend.c ms_net_cost 105 / 106: the same rule as the Main Menu's Friends panel) */
#ifndef HZ_BAR
#define HZ_BAR 0                       /* the hold bar: OFF */
#endif
#define HZ_BAR_W 164u                  /* the bar's full width in box units (4.6 units / font 0.028) */
static void hz_free(void) { tfree(&S.hz[0]); tfree(&S.hz[1]); tfree(&S.hz[2]); tfree(&S.hz[3]); }
static char *hz_num(char *d, unsigned v) { char b[4]; int n = 0; if (v > 99u) v = 99u; do { b[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *d++ = b[--n]; return d; }
static char *hz_put(char *d, const char *s) { while (*s) *d++ = *s++; return d; }
/* "Z: Friends", "Z: Friends (6 online)", "Z: Friends (6 online, 1 request)", "Z: Friends (2 requests)": the counts of the Main Menu's Friends panel (variant A), only while fresh, only the non-zero ones */
static void hz_text(u32 now, char *o)
{
    const struct od *d = O();
    unsigned on = 0, rq = 0;
    char *p = hz_put(o, "Z: Friends");
    (void)d;
#ifdef HZ_TEST                         /* LAB ONLY (REC_XCF=-DHZ_TEST): cycle the five looks every 4 s for the Dolphin close-ups; never in a release */
    { static const unsigned char tc[5][2] = { { 0, 0 }, { 6, 0 }, { 6, 1 }, { 0, 1 }, { 99, 99 } }; unsigned k = (now / 4000u) % 5u; on = tc[k][0]; rq = tc[k][1]; }
#else
    if (d->unread_ms && (u32)(now - d->unread_ms) < HZ_FRESH_MS) { on = d->news_online ? (unsigned)d->news_online - 1u : 0u; rq = d->news_req; }
#endif
    if (on || rq) {
        p = hz_put(p, " (");
        if (on) { p = hz_num(p, on); p = hz_put(p, " online"); }
        if (on && rq) p = hz_put(p, ", ");
        if (rq) { p = hz_num(p, rq); p = hz_put(p, rq == 1u ? " request" : " requests"); }
        *p++ = ')';
    }
    *p = 0;
}
static void hz_draw(u32 now)
{
    struct kh_seg g;
    struct kh_pair p;
    char s[48];
    unsigned i, n = 0, fe = TK_FONT;
    int x0, y0 = tp_q(KZH.u[1], 16);
    hz_text(now, s);
    if (S.hz[0] && same(s, S.hz_s)) return;                                     /* drawn and the same text: nothing to do (a new count: made again) */
    hz_free();
    copy(S.hz_s, s, sizeof S.hz_s - 1u);
    if (!kh_parse(s, &g)) return;
    kh_pair_build(&g, fe, &p);
    if (p.w > HZ_MAXW_Q) { fe = (unsigned)((u32)fe * (u32)(HZ_MAXW_Q / 256) / (u32)(p.w / 256)); kh_pair_build(&g, fe, &p); }
    x0 = tp_q(KZH.u[0], 16) - p.w - p.w / 12;                                 /* right aligned: the width is the glyph table's estimate (within about 8 percent), so 1/12 of it more margin */
    for (i = 0; i < p.n && n < 3u; i++) {
        const struct kh_item *it = &p.it[i];
        void *t;
        if (it->kind == KH_I_PLATE) {
            t = kb_text(x0 + it->x, y0 + it->dy, (kh_ink_rgb(it->key) << 8) | 0xFFu, it->s, it->fe4, 1);
            if (t) kh_attach(t, it);
        } else t = kb_text(x0 + it->x, y0, 0xFFFFFFFFu, it->s, fe, 0);
        if (!t) { hz_free(); return; }
        S.hz[n++] = t;
    }
}
static void hz_frame(u32 now, int on)
{
    int fire, held = z_held();
    if (!on) { if (S.hz[0]) hz_free(); S.hz_s[0] = 0; frd_hold_note(&S.hzh, held); return; }
    hz_draw(now);
#if HZ_HOLD
    fire = frd_hold_step(&S.hzh, now, held, FRD_HOLD_MS);
    if (HZ_BAR && S.hzh.on && S.hzh.pm && !fire) {
        u32 w = i2f((u32)S.hzh.pm * HZ_BAR_W / 1000u);
        if (!S.hz[3]) S.hz[3] = mtext(KZH.u[2], KZH.u[3], w, K.u[10], 0xFFC820FFu, 0, " ");     /* the toast bar's colour and height */
        else ((u32 *)S.hz[3])[3] = w;
    } else tfree(&S.hz[3]);
#else
    fire = held && !S.hzh.prev; S.hzh.prev = (unsigned char)(held != 0);
#endif
    if (fire) { O()->room_note = (unsigned short)HZ_OPEN; tfree(&S.hz[3]); }
}

static void menu_frame(u32 now)
{
    test_toast(now);
    u8 kind = B(MENU_FLOW);
    int zp, quiet = (H(MENU_COOLDOWN) != 0 || kind == 7u || kind == 8u) && !(O()->gn_lobby & 1u);   /* our pages hold the menu (the sign overlay keeps the cooldown non-zero): no toasts, except the Game Night room lobby */
    if (kind != 7u && kind != 8u) O()->gn_lobby &= (u8)~5u;                    /* not on one of our pages: no room lobby (a scene change left it set) */
    if (quiet) S.quiet = now;                                                   /* paused while a page of ours holds the menu, and 0.5 s after */
    S.suppress = (u8)(quiet || (u32)(now - S.quiet) < 500u);
    net_step(now, !S.suppress || (O()->gn_lobby & 4u));                      /* the Friends screen (gn_lobby bit 2) keeps the tick: an invite accepted late arrives as a 'go' */
    if (hc_connected(O()->phase)) S.suppress = 1;                               /* connected: Z (tap or hold) on a toast would be Slippi's hold-Z disconnect; the toast waits (hub_conn.h), the tick above went on */
    zp = S.suppress ? -1 : z_port();
    if (zp >= 0 && tst_on(&S.T)) S.port = (u8)zp;
    if (S.suppress && S.prev_ms) tst_hold(&S.T, (u32)(now - S.prev_ms));       /* a message toast waits while it cannot be drawn: the server counts it shown once fetched */
    S.prev_ms = now;
    tst_frame(&S.T, now, zp >= 0, S.suppress ? 0 : z_held());
    {   /* bit 1: a toast that waits for Z is up; the room lobby then leaves Z to it (sg_entry.c lobby_pad) */
        u8 k = S.T.cur.kind, ask = !S.suppress && tst_on(&S.T) && (k == TST_K_INVITE || k == TST_K_REQUEST || k == TST_K_DIRECT || k == TST_K_ROOM || k == TST_K_FIGHT);
        O()->gn_lobby = (u8)((O()->gn_lobby & ~2u) | (ask ? 2u : 0u));
    }
    if (!S.suppress) sound(); else S.T.sfx = TST_SFX_NONE;
    toast_draw(now);
    O()->gn_lobby = (u8)((O()->gn_lobby & ~8u) | (tst_on(&S.T) && !S.suppress ? 8u : 0u));   /* bit 3: a toast is on screen (the menu overlay hides its "(Z) Friends" hint, ml_glue.c mz_frame) */
    if (HZ_IN_HUB) hz_frame(now, kind == 0u && !quiet && !tst_on(&S.T) && !hc_connected(O()->phase));     /* VARIANT Z: the Main Menu only, no toast (it owns Z), not while our pages hold the menu.  `quiet` of THIS frame, not S.suppress:
                                                                                                 suppress also holds for 0.5 s after the overlay's entry and after every page (the toasts' settle time), which made the hint appear about
                                                                                                 half a second after the Main Menu */
}

static int guest(const struct od *o)                                            /* od_glue.c od_guest: an all-zero device id = no linked account */
{
    unsigned i, any = 0;
    if (!o->cfg) return 1;
    for (i = 0; i < 8u; i++) any |= o->cfg->dev[i];
    return !any;
}
static int guest_id(void) { return guest(O()); }

/* the console is on the character screen of a Game Night set (the call-up's hand-off, or the set just played): the room is not polled from here */
static int gn_css(const struct od *o)
{
    return o->m.gn_magic == MNC_GN_MAGIC && o->m.gn_state != MNC_GN_NONE && o->m.gn_state != MNC_GN_DIRECT;
}

/* ---- the room pick carries over to Slippi's character screen (the player never touches it) ------------------------------------------------------------------
   A call-up hand-off (MNC_GN_ARMED) with a pick in gn_room[17] (character + 1) / gn_room[18] (costume): this runs every frame on the online character screen (the resident tick does not:
   Dolphin run 904).  After a few frames it writes the pick where the screen keeps it (the slot's character byte at 0x803F0E0A, which the screen turns into its "a character is selected"
   flag every frame; the selection record *(0x804D6CB0) + index * 0x24: character +0x70, costume +0x73) and puts START into the controlling port's newly-pressed word (HSD_PadCopyStatus + 8)
   for a few frames: the screen's own START handling locks in (B5) and searches the room (B4: od_find, ARMED -> SEARCH).  The screen is blacked out (VISetBlack) from the first write until
   the scene leaves it (timeout), so the player sees no character screen.  A WATCH hand-off (MNC_GN_WATCH) is blacked out the same way.  Constants from third_party/slippi-ssbm-asm
   HandleInputsOnCSS.asm (FN_TX_LOCK_IN, CHECK for START at 0x80263258) and the decomp (mnCharSel_*). */
#define GP_PICK 17u
#define GP_COST 18u
#define GP_COVER 16u                   /* gn_room[16] = 1: the cover is on (the resident engine lifts it); gn_room[15] = 0xC5: this engine can */
#define GP_CAP 15u
#define GP_CAP_MAGIC 0xC5u
#define GP_WAIT 10u
#define GP_START 4u
#define GP_BLACK_MAX 900u
static struct { u16 n; u8 black, cancel; } GP;
static int hs_black(int on)
{
    volatile u8 *r = (volatile u8 *)O()->m.gn_room;
    if (on && r[GP_CAP] != GP_CAP_MAGIC) return 0;                       /* an older engine would not lift it again */
    ((void (*)(int))0x80350100u)(on);                                    /* VISetBlack */
    ((void (*)(void))0x8034FF78u)();                                     /* VIFlush: the shadow registers take it at the next retrace */
    r[GP_COVER] = (u8)(on != 0);
    return 1;
}
static void hs_pick(void)
{
    struct od *o = O();
    volatile u8 *r = (volatile u8 *)o->m.gn_room;
    u32 ch, co, base, idx, port;
    int watch = o->m.gn_magic == MNC_GN_MAGIC && o->m.gn_state == MNC_GN_WATCH;
    int arm = o->m.gn_magic == MNC_GN_MAGIC && o->m.gn_state == MNC_GN_ARMED && r[GP_PICK];
    if (watch) {
        if (!GP.black && ++GP.n > 3u) GP.black = (u8)hs_black(1);
        if (GP.black && ++GP.n > GP_BLACK_MAX) { hs_black(0); GP.black = 0; GP.n = 0; }
        return;
    }
    if (!arm) {
        if (GP.black && ++GP.n > GP_BLACK_MAX) { hs_black(0); GP.black = 0; }
        if (!GP.black) { GP.n = 0; GP.cancel = 0; }
        return;
    }
    /* (no cancel valve any more: the cube's pad reports phantom buttons (BlueRetro), which cancelled the bypass and left the character screen up; the resident engine lifts the cover for certain) */
    if (!GP.black && ++GP.n > 1u && GP.n < GP_WAIT) GP.black = (u8)hs_black(1);        /* cover from the first frames: the screen is never shown while the pick is written */
    if (++GP.n < GP_WAIT) return;
    ch = (u32)r[GP_PICK] - 1u; co = (u32)r[GP_COST] & 15u;
    if (ch >= 26u) { ch = now_ms() % 26u; co = 0; }                                  /* Random: any of the 26 (the server's view keeps Random) */
    base = W(0x804D6CB0u); idx = B(0x804D6598u); port = B(0x804D6CF0u) & 3u;
    if (base < 0x80000000u || base >= 0x81800000u || (base & 3u) || idx > 3u) return;
    B(0x803F0E0Au) = (u8)ch;
    B(base + idx * 0x24u + 0x70u) = (u8)ch;
    B(base + idx * 0x24u + 0x73u) = (u8)co;
    if (!GP.black) { GP.black = (u8)hs_black(1); GP.n = GP_WAIT; }
    if (GP.n >= GP_WAIT + 2u && GP.n < 600u && ((GP.n - GP_WAIT) % 3u) == 2u) W(PAD_COPY + port * 0x44u + 8u) |= 0x1000u;      /* START every third frame until the first one is taken (B4 flips ARMED to SEARCH and this stops): no timing luck needed */
}
#ifdef MNC_RKSET
/* A GRANDMASTER SET WAS PAIRED while the player waits on the ranked character screen (od_glue.c od_rk_hold: the hand-off block says MNC_GN_RKSET): the set's strikes, bans and picks are drawn in the menu scene (sign overlay),
   so this screen is left at once.  The character and costume the player locked in for game 1 are read from the screen's selection record (the one hs_pick writes) into the hand-off block's pick bytes (the sign overlay
   hands them back for game 1), the screen goes black, and a few frames later the same exit byte as the Game Night leaves use (0x804D6CF6 = 2, "back") takes the scene to the menu.  The menu entry redirects to the
   Game Night row / browser, which finds the state RKSET and opens the set (ml_glue.c ml_enter, sg_entry.c gn_handoff).   14.2. */
#define RK_EXIT_AT 12u                 /* frames of black before the exit byte goes in */
#define CSS_EXITKIND_ADDR 0x804D6CF6u
static u16 RK_n;
static void hs_rk_hold(void)
{
    struct od *o = O();
    volatile u8 *r = (volatile u8 *)o->m.gn_room;
    u32 base, idx;
    if (!(o->m.gn_magic == MNC_GN_MAGIC && o->m.gn_state == MNC_GN_RKSET)) { RK_n = 0; return; }
    if (RK_n == 0) {
        base = W(0x804D6CB0u); idx = B(0x804D6598u);
        if (base >= 0x80000000u && base < 0x81800000u && !(base & 3u) && idx <= 3u && B(base + idx * 0x24u + 0x70u) < 26u) {
            r[GP_PICK] = (u8)(B(base + idx * 0x24u + 0x70u) + 1u);
            r[GP_COST] = (u8)(B(base + idx * 0x24u + 0x73u) & 15u);
        }
        if (!GP.black) GP.black = (u8)hs_black(1);
        GP.n = GP_WAIT;
    }
    if (RK_n < 0xFFFFu) RK_n++;
    if (RK_n > RK_EXIT_AT && B(CSS_EXITKIND_ADDR) == 0u) B(CSS_EXITKIND_ADDR) = 2u;
}
#endif
#ifdef MNC_RKSET
/* A GAME OF A GRANDMASTER SET WAS ARMED AND ITS CONNECTION IS GONE (cube "Found an opponent, but couldn't connect to them", then Z, then hold B: the menu entry reopened the set's screen from the leftover SEARCH
   hand-off, the Event Match frame stayed empty and the console went black for good).  The lock-in turned the hand-off into SEARCH and od_rk_go started the peer (phase PEER at once); a client that is IDLE or ERR
   in that state for a short while means the connection failed or was cancelled, and no game is going to report: the hand-off is released as "left" (the menu entry then neither reopens the set nor the room, the
   player lands where the hold B of any other character screen goes).  The set itself is the server's to time out.  A game that was played ends in RKSET (the report), never here. */
#define RK_DEAD_FRAMES 20u
static u16 RK_dead;
static u8 RK_armed;                      /* this character screen saw the game armed (ARMED + the set flag): only then can a SEARCH that dies be a failed connect (the screen of a game that was played, back without its report, never saw it) */
static void hs_rk_dead(void)
{
    struct od *o = O();
    volatile u8 *r = (volatile u8 *)o->m.gn_room;
    unsigned i;
    if (o->m.gn_magic == MNC_GN_MAGIC && o->m.gn_state == MNC_GN_ARMED && !r[0] && (r[MNC_RK_FLAGS_AT] & MNC_RK_FLAG)) RK_armed = 1;
    if (o->m.gn_magic == MNC_GN_MAGIC && o->m.gn_state == MNC_GN_SEARCH && !r[0] && (r[MNC_RK_FLAGS_AT] & MNC_RK_FLAG) && (o->phase == OD_PEER || o->phase == OD_CONNECT)) RK_armed = 1;      /* (this overlay loaded after the lock-in: the connect itself is seen) */
    if (!RK_armed || !(o->m.gn_magic == MNC_GN_MAGIC && o->m.gn_state == MNC_GN_SEARCH && !r[0] && (r[MNC_RK_FLAGS_AT] & MNC_RK_FLAG) && (o->phase == OD_IDLE || o->phase == OD_ERR))) { RK_dead = 0; return; }
    if (++RK_dead < RK_DEAD_FRAMES) return;
    RK_dead = 0; RK_armed = 0;
    for (i = 0; i < 19u; i++) if (i != GP_CAP && i != GP_COVER) r[i] = 0;
    r[MNC_RK_FLAGS_AT] &= 0x80u;                                         /* (the engine's watch capability bit stays) */
    o->m.gn_state = MNC_GN_LEFT;                                         /* ml_enter: acknowledged, no return to a set or a room */
}
#endif
/* A FRESH online character screen (entered from the menus after the previous one was left with hold B, CSSData.pending_scene_change 2 = "back") shows the character of the last visit as
   selected: Melee copies the saved VS selection into the screen and the cursor starts without a coin, so B can not pick it up and the player is stuck with the old character.  Start like the very first visit instead: no icon (0x803F0E0A = 0x19, the byte the screen turns into "a character is selected" every frame), no
   character in the selection record.  Only for pending 2: a screen left for a match (1) or the code keyboard (4) keeps what the player picked; a Game Night hand-off writes its own pick. */
static void hs_fresh_css(void)
{
    struct od *o = O();
    u32 base = W(0x804D6CB0u), idx = B(0x804D6598u);
    if (o->m.gn_magic == MNC_GN_MAGIC && (o->m.gn_state == MNC_GN_ARMED || o->m.gn_state == MNC_GN_WATCH || o->m.gn_state == MNC_GN_ADDFR || o->m.gn_state == MNC_GN_ADDFR_DONE)) return;      /* */
    if (base < 0x80000000u || base >= 0x81800000u || (base & 3u) || idx > 3u) return;
    if (B(base + 3u) != 2u || B(0x803F0E0Au) >= 0x19u) return;
    B(0x803F0E0Au) = 0x19u;
    B(base + idx * 0x24u + 0x70u) = 26u;
    B(base + idx * 0x24u + 0x73u) = 0u;
}
/* the scene left the character screen: the picture comes back */
static void hs_pick_end(void) { if (GP.black) { hs_black(0); GP.black = 0; } GP.n = 0; }

/* ---- the key hint of the CSS toast ("Add Nigzel?" showed with no key shown): the toast's second line is drawn on the same row at the right of the question as a key badge and the action
   word, the style of "R Standings" (a rounded plate in the key's colour, the letter in the ink colour of keyhint.h, then the word).  "Z: Add friend" -> (Z) Add friend; a line without the key prefix
   ("Sending...") is the word alone.  Row y and scales are the toast text's (K.u[13], KC); badge x 10.0, word x 11.6 (past the longest question of 25 characters). ---- */
static const union { float f[5]; u32 u[5]; } KZ = {{ 10.0f, 11.6f, 18.35f, 18.5f, 20.0f / 0.1f }};     /* badge centre x, word x, letter y (the glyph centre on the word's: -0.15 as Standings), word y */
static void css_hint_free(void) { tfree(&S.zt[0]); tfree(&S.zt[1]); }
static void css_hint(const char *b)
{
    const char *w = b;
    if (!b[0]) { if (S.zt[0] || S.zt[1]) css_hint_free(); S.s1[0] = 0; return; }
    if (same(b, S.s1) && S.zt[1]) return;
    css_hint_free();
    copy(S.s1, b, TST_LINE);
    if (b[0] == 'Z' && b[1] == ':' && b[2] == ' ') {
        w = b + 3;
        S.zt[0] = ctext_at("Z", KZ.u[0], KZ.u[2], KR.u[2], KR.u[3], (kh_ink_rgb(KH_Z) << 8) | 0xFFu, 0, KY.u[5], KY.u[6]);
        if (S.zt[0]) { ((u8 *)S.zt[0])[0x4A] = 1; tp_attach_rgb_round(S.zt[0], kh_plate_rgb(KH_Z), -(KY_PLATE / 2), KY_PLATE_DY, KY_PLATE, KY_PLATE, 50u); }
    }
    S.zt[1] = ctext_at(w, w == b ? KZ.u[0] : KZ.u[1], KZ.u[3], KZ.u[4], KR.u[3], 0xFFFFFFFFu, 0, KS.u[0], KS.u[1]);
}

/* ---- the CSS chips: ONE pill under Slippi's status list.  Delay: L down, R up (1..15) in Dojo / Unranked, Direct / Mano a mano and Grandmaster; Grandmaster
   adds the region: a TAP of R is delay up (on release), HOLDING R for 1.0 s cycles Auto / NAW / NAC / NAE / EU / LA / AS / OC.  Game Night, Teams / Party: nothing (co_policy).  The delay word is the engine's s3_cfg[3]
   (the same word Settings > Game > Online delay writes; read once per match at B3), saved through the Settings record; the region value lives in the resident (hub_env.ch_region) and rides the next
   ranked ticket (mnc_region_chip, TICKET_CREATE ext bit 1).  Runs only on the CSS, before the search. ---- */
static const union { float f[4]; u32 u[4]; } KD = {{ (219.0f - 320.0f) / 9.45f, (418.0f - 265.0f) / 9.45f, (432.0f - 265.0f) / 9.45f, 200.0f * 10.0f / 9.45f }};   /* plain white text, no plate: x, line 1 y (px 418), line 2 y (px 432), box width */
static const char chip_rg[CO_REGION_N][5] = {"Auto", "NAE", "NAW", "EU", "LA", "AS", "OC", "NAC"};      /* chip order = ms_region.CODES + 1 */
static char *chip_ap(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static void chip_free(void) { tfree(&S.cht); tfree(&S.cht2); tfree(&S.cht3); tfree(&S.chi[0]); tfree(&S.chi[1]); tfree(&S.chi[2]); }
static u32 chip_x(int px) { return tp_f2((px - 320) * 6935, 16); }                       /* px of the frame -> text x (world units, 9.45 px each) */
static u32 chip_y(int px, int dq) { return tp_f2((px - 265) * 6935 + dq, 16); }            /* dq: Q16 units added */
/* an L / R button icon: the letter on a rounded light plate (Standings badge technique: the letter centred, the plate behind it); left edge x_px, line origin y_px */
#define CHIP_PW (CO_ICON_W * 6935)
#define CHIP_PH (CO_ICON_H * 6935)
static void *chip_icon(u32 k, int x_px, int y_px)
{
    void *t = ctext_at(kh_letter(k), chip_x(x_px + CO_ICON_W / 2), chip_y(y_px, -9830), KR.u[2], KR.u[3], (CO_ICON_INK << 8) | 0xFFu, 0, KY.u[5], KY.u[6]);
    if (t) { ((u8 *)t)[0x4A] = 1; tp_attach_rgb_round(t, CO_ICON_RGB, -(CHIP_PW / 2), 158597 - CHIP_PH / 2, CHIP_PW, CHIP_PH, 32u); }
    return t;
}
static void chip_frame(const struct od *o, u32 now)
{
    const struct hub_env *e = hub_E;
    int port = (int)(signed char)B(0x804D6CF0u), dir;
    u32 gs, pol, d, pr, key, aw, sw;
    if (e->ch_magic != HUB_CH_MAGIC || !e->ch_delay || !e->ch_chg || !e->ch_region) return;
    mnc_region_chip = *e->ch_region;
    gs = o->m.gn_magic == MNC_GN_MAGIC ? (u32)o->m.gn_state : 0u;
    pol = B(CSS_EXITKIND_ADDR) ? 0u : co_policy(B(r13() + (u32)R13_ONLINE_MODE), gs, (u32)(gs && !o->m.gn_room[0] && (o->m.gn_room[MNC_RK_FLAGS_AT] & MNC_RK_FLAG) != 0u));
    if (!(pol & CO_P_DELAY)) { chip_free(); S.chs = 0; S.chh.s = 0; return; }
    sw = *e->ch_delay & AD_SW;                                                    /* the Settings "Auto delay" switch is On: a chip change is for the current opponent only (nothing stored; the engine drops it at a new opponent) */
    d = *e->ch_delay & 0xFFu;
    if (port >= 0 && port < 4) {
        dir = co_dir(W(PAD_COPY + 0x44u * (u32)port + 8u));
        if (pol & CO_P_REGION) {                                                  /* R: tap = delay up, hold = region; L alone is immediate */
            u32 ev = co_hold_step(&S.chh, (W(PAD_MASTER + 0x44u * (u32)port) & CO_PAD_R) != 0u, now, 1000u);
            dir = dir < 0 ? -1 : (int)(ev == CO_EV_TAP);
            if (ev == CO_EV_HOLD) { *e->ch_region = (u8)co_region_step(*e->ch_region); mnc_region_chip = *e->ch_region; }
        }
        pr = co_delay_step(d, dir);
        if (!sw && !pr) pr = d;                                                   /* Auto delay Off: the chip stays on 1f..15f (the switch in Settings is the way back to Auto) */
        if (pr != d) { d = pr; *e->ch_delay = d | sw; if (!sw) (void)e->ch_chg(d, 0u); }
    }
    aw = 0u;                                                                      /* Auto: the frames played against the PAIRED opponent only */
    if (d == AD_AUTO) {
        u32 css = W(0x80005614u), ms;                                             /* Slippi's CSS data table -> the match block (CSSDT_MSRB_ADDR): connection state 4 = paired, byte 9 = MSRB_DELAY_FRAMES */
        if (css >= 0x80000000u && css < 0x81800000u && (ms = W(css)) >= 0x80000000u && ms < 0x81800000u && B(ms) == 4u) aw = B(ms + 9u);
    }
    key = d | ((u32)*e->ch_region << 8) | ((pol & CO_P_REGION) << 12) | 0x10000u | ((aw & 0xFFu) << 20);
    if (S.chs != key || !S.cht) {
        char t[40], *p;
        chip_free();
        *co_delay_text(t, d, aw) = 0;                                             /* "Delay 3f", "Delay Auto", "Delay Auto (2f)" (on_bhold.h, host-tested) */
        S.chi[0] = chip_icon(KH_L, CO_L1_LX, CO_Y1_PX);
        S.chi[1] = chip_icon(KH_R, CO_L1_RX, CO_Y1_PX);
        S.cht = ctext_at(t, chip_x(CO_L1_TX), chip_y(CO_Y1_PX, 0), KD.u[3], KR.u[3], 0xFFFFFFFFu, 0, KS.u[0], KS.u[1]);
        if (pol & CO_P_REGION) {
            S.cht3 = ctext_at("Hold", chip_x(CO_X_PX), chip_y(CO_Y2_PX, 0), KD.u[3], KR.u[3], 0xFFFFFFFFu, 0, KS.u[0], KS.u[1]);
            S.chi[2] = chip_icon(KH_R, CO_L2_RX, CO_Y2_PX);
            p = chip_ap(t, "Region (");
            p = chip_ap(p, chip_rg[*e->ch_region % CO_REGION_N]);
            *p++ = ')'; *p = 0;
            S.cht2 = ctext_at(t, chip_x(CO_L2_TX), chip_y(CO_Y2_PX, 0), KD.u[3], KR.u[3], 0xFFFFFFFFu, 0, KS.u[0], KS.u[1]);
        }
        S.chs = S.cht ? key : 0u;
    }
}

/* Slippi's online character screen: the challenger's status, the post-fight prompt (only after a recorded match of ours against a human: tst_fight_allowed) */
/* The CSS error box (unreadable on a CRT).  Slippi draws it itself: its CSS text struct (CSSDT_BUF_ADDR 0x80005614 -> +8) holds the header and the four error lines at size 0.4, 30 letters
   to a line (LoadCSSText.asm).  While the connection state of the match block is 5 (ERROR_ENCOUNTERED) the struct's two scale words (+0x24 / +0x28, the same words our own texts set) are raised by
   ERRBOX_PCT percent and put back when the state leaves 5, so nothing else on the screen changes.  Any miss (pointer outside RAM, scale word not a sane float) does nothing. */
/* ERRBOX_TEST (a -D, Dolphin proof only) forces the online-direct error state for a few hundred frames */
#ifndef ERRBOX_PCT
#define ERRBOX_PCT 125u
#endif
#ifndef ERRBOX_SHIFT_PX
#define ERRBOX_SHIFT_PX 15                                                           /* px to the left of the unscaled left edge (the scale grows about the screen centre, so the lines would creep right off a CRT) */
#endif
#ifndef ERRBOX_SHIFT_Y_PX
#define ERRBOX_SHIFT_Y_PX (((int)ERRBOX_PCT - 100) * (309 - 240) / 100 + 3)          /* px up: the scale also pushed the footer ("Press Z to clear error", unscaled y about 309) down by (s - 1) * (309 - 240) px onto the curved bottom panel (top about y 349); lift the whole box so the footer is back at its old place */
#endif
static void err_box(void)
{
    u32 css = W(0x80005614u), ms, ts, st, k;
    if (css < 0x80000000u || css >= 0x81800000u) goto off;
    ms = W(css), ts = W(css + 8u);
    if (ms < 0x80000000u || ms >= 0x81800000u || ts < 0x80000000u || ts >= 0x81800000u) goto off;
#ifdef ERRBOX_TEST                                                            /* Dolphin proof only: put the online-direct state machine into its error state for a few hundred frames (the engine then fills the match block with the text) */
    { static u32 n; struct od *o = O(); n++; if (o && n >= 900u && n < 1700u) { o->phase = OD_ERR; o->err = OD_E_NOCONN; } }
#endif
    st = B(ms);
    if (st != 5u) goto off;
    if (!S.eb_on) {
        S.eb_s[0] = W(ts + 0x24u); S.eb_s[1] = W(ts + 0x28u); S.eb_x = W(ts); S.eb_y = W(ts + 4u);
        k = (S.eb_s[0] >> 23) & 0xFFu;
        if (k < 100u || k > 140u || S.eb_s[0] != S.eb_s[1] || ((S.eb_y >> 23) & 0xFFu) > 140u || ((S.eb_x >> 23) & 0xFFu) > 140u) return;          /* not the 0.0x floats of the CSS text struct: leave it alone */
        S.eb_on = 1;
    }
    W(ts + 0x24u) = tp_f2(tp_q(S.eb_s[0], 16) * (int)ERRBOX_PCT / 100, 16);
    W(ts + 0x28u) = W(ts + 0x24u);
    /* left edge: x0 * s about the centre; put the struct x so that the left edge ends ERRBOX_SHIFT_PX left of where it was: dx = -((s - 1) * (x0 - 320) + shift) px, x0 = 385 */
    W(ts) = tp_f2(tp_q(S.eb_x, 16) - (int)(((int)ERRBOX_PCT - 100) * 65 / 100 + ERRBOX_SHIFT_PX) * 6935, 16);
    W(ts + 4u) = tp_f2(tp_q(S.eb_y, 16) - ERRBOX_SHIFT_Y_PX * 6935, 16);        /* whole box up (y word next to x, same units: chip_y) */
    return;
off:
    if (S.eb_on) {
        S.eb_on = 0;
        if (css >= 0x80000000u && css < 0x81800000u) { ts = W(css + 8u); if (ts >= 0x80000000u && ts < 0x81800000u) { W(ts + 0x24u) = S.eb_s[0]; W(ts + 0x28u) = S.eb_s[1]; W(ts) = S.eb_x; W(ts + 4u) = S.eb_y; } }
    }
}
static void css_frame(u32 now)
{
    struct od *o = O();
    int zp, cn;
    const char *want = 0;
    char a[TST_LINE + 1], b[TST_LINE + 1];
    hs_pick();
    chip_frame(o, now);
    err_box();
    if (S.dret && o->m.gn_magic == MNC_GN_MAGIC && o->m.gn_state == MNC_GN_NONE) o->m.gn_state = MNC_GN_DIRECT;   /* the lock-in's B4 spent the hand-off (on_backend.c: state NONE, code cleared); the mark stays so the NEXT menu entry (ml_glue.c ml_enter, before hs_enter drops it) knows where the player came from (backing out landed in Slippi's submenu) */
#ifdef MNC_RKSET
    hs_rk_hold();
    hs_rk_dead();
#endif
    if (!(o->phase == OD_FIND && o->mode == MNC_MODE_DIRECT)) S.dw0 = 0;
    if (o->phase == OD_FIND && o->mode == MNC_MODE_DIRECT) {
        want = mnc_invite_status == 1u ? "Accepted: they are searching you now" : mnc_invite_status == 2u ? "Not answered" : 0;
        if (S.dret) {                                                                /* a Mano a mano search of an invite: wait at most DW_MAX_MS for the other player, then say so and back out (before, an invite accepted late left the acceptor here for ever) */
            static char wt[40];
            u32 el, sec;
            if (!S.dw0) S.dw0 = now | 1u;
            el = now - S.dw0;
            if (S.dgive) {
                want = "No answer: back to the menu";
                if ((u32)(now - S.dgive) > DW_NOTICE_MS) { od_cancel(o, now); if (B(CSS_EXITKIND_ADDR) == 0u) B(CSS_EXITKIND_ADDR) = 2u; want = 0; }   /* the screen is being left: the line goes this frame (freed below), nothing of it reaches the exit */
            } else if (el > DW_MAX_MS) S.dgive = now | 1u;
            else if (!want) {
                sec = (DW_MAX_MS - el) / 1000u;
                copy(wt, "Waiting for the other player... ", 33); wt[32] = 0;
                { unsigned n = 32u; if (sec >= 10u) wt[n++] = (char)('0' + sec / 10u); wt[n++] = (char)('0' + sec % 10u); wt[n++] = 's'; wt[n] = 0; }
                want = wt;
            }
        }
    }
    else if ((o->mid_ok & 7) == 5 && (o->phase == OD_UP || o->phase == OD_IDLE || o->phase == OD_ERR)) {   /* bit 0 paired, bit 2 the game end (BD) of that match seen, bit 1 prompt done */
        if (!tst_fight_allowed(o->mode, 1, o->opp_code, guest(o))) o->mid_ok |= 2;  /* Dojo, the lab bot, a guest: never */
        else if (!S.prof_asked) S.prof_asked = 1;                                /* our own setting first */
        else if (S.prof_asked == 2u && S.op != OP_PROF) {
            if (!S.fight_on) o->mid_ok |= 2;
            else if (!S.fchk) S.fchk = 1;                                        /* ask the server first (net_step), the prompt comes with the answer and is marked done then (fail closed) */
        }
    }
    {   /* OUR Grandmaster (the online mode byte 0 is only ever our own Ranked row: Slippi's Unranked / Direct are modes 1 / 2, Slippi's real Ranked does not exist on the console): ask for the rank once the client is idle */
        int idle = o->phase == OD_IDLE || o->phase == OD_UP || o->phase == OD_ERR;
        int rk_new = 0, rk_want, i;
        for (i = 0; i < 8; i++) if (o->mid[i] != S.rk_mid[i]) rk_new = 1;
        if (S.rk_ok && !S.rk_re && rk_new && idle && gn_css(o)) { S.rk_re = 1; S.rk_try = 0; S.rk_at = now + 4000u; }      /* the report of the finished match lands within a few seconds */
        rk_want = (!S.rk_ok || S.rk_re) && S.rk_try < 3u && idle && !guest(o) && B(r13() + (u32)R13_ONLINE_MODE) == 0u && (u32)(now - S.rk_at) < 0x80000000u;
        net_step(now, (gn_css(o) || (o->mid_ok & 4)) && !tst_on(&S.T) && idle ? 2 : rk_want ? 3 : 0);   /* (bit 2: a match of ours ended and this screen is idle again: the keep-alive says Online, so the server does not keep showing this console In match for 30 minutes and refuse a rematch invite to it) */
        if (S.rk_ok && B(r13() + (u32)R13_ONLINE_MODE) == 0u) rank_draw();
        if (B(r13() + (u32)R13_ONLINE_MODE) == 0u) stand_draw(o);
    }
    cn = hc_connected(o->phase) && (S.T.cur.kind == TST_K_INVITE || S.T.cur.kind == TST_K_REQUEST || S.T.cur.kind == TST_K_DIRECT || S.T.cur.kind == TST_K_ROOM);   /* connected: a Z-accept toast waits (hold Z = Slippi's disconnect); the post-fight prompt is not one of them */
    zp = cn ? -1 : z_port();
    tst_frame(&S.T, now, zp >= 0, S.suppress || cn ? 0 : z_held());
    if (cn) S.T.sfx = TST_SFX_NONE; else sound();
    b[0] = 0;
    if (!want && o->tm_note) want = o->tm_note == 1u ? "Opponent found on Slippi" : o->tm_note == 2u ? "Opponent found on Multishine" : o->tm_note == 3u ? "Slippi not available. Searching Multishine." : 0;   /* TIERED MATCHMAKING: the network of the opponent, or why Slippi was skipped (the texts equal tmm_core.h tmm_note_text, checked by bridge/run_tests.sh) */
    tst_status(&S.T, want);                                                          /* the momentary messages above ("Accepted: ...", "Not answered", "No answer: ...", the live "Waiting ... 49s") are the toast model's sticky STATUS line: a refreshed number is not a new toast, no sound */
    want = tst_status_text(&S.T)[0] ? tst_status_text(&S.T) : 0;                     /* (a real toast on screen hides it) */
    if (tst_on(&S.T) && !cn) {
        (void)tst_text(&S.T, now, a, b);
        want = a;
    }
    css_hint(want ? b : "");
    if (!want) { if (S.css) { TEXT_FREE(S.css); S.css = 0; S.s0[0] = 0; } return; }
    if (S.css && same(want, S.s0)) return;
    if (S.css) TEXT_FREE(S.css);
    S.css = ctext(want);
    copy(S.s0, want, TST_LINE);
}

static void hs_proc(void *gobj)
{
    u32 sc = W(SCENE), now, t0 = tb_get();
    (void)gobj;
    if (W(LOADING) || !hub_E) return;
    now = now_ms();
    note_port();
    if ((sc >> 24) == 1u) menu_frame(now);
    else if ((sc >> 24) == 8u && (sc & 0xFFu) == 0u) css_frame(now);
    else hs_pick_end();
    nc_add(0, tb_get() - t0);
    hs_proof[4] = W(PAD_MASTER);
    { u32 zm = 0; int zi; for (zi = 0; zi < 4; zi++) if (W(PAD_MASTER + (u32)zi * 0x44u) & PAD_Z) zm |= 1u << zi; hs_proof[5] = zm | ((u32)S.T.hold.on << 8) | ((u32)S.T.hold.pm << 16); }
    hs_proof[1] = (S.ticks & 0xFFFFu) | (S.toasts << 16);
    hs_proof[2] = (u32)S.T.cur.kind | ((u32)S.T.cur.busy << 4) | ((u32)S.T.act << 8) | ((u32)S.op << 16) | ((u32)S.suppress << 24);
}

/* the overlay was entered (hub_enter, game thread, outside the network lock): a proc of ours in the menu scene or the online character screen */
void hs_enter(void)
{
    u32 sc = W(SCENE), major = sc >> 24;
    zero(&S, sizeof S);
#ifdef MNC_RKSET
    RK_dead = 0; RK_armed = 0;
#endif
    hs_proof[0] = 0x48534F43u;                                                  /* 'HSOC' */
    S.line = -2; S.fight_on = 1;
    S.next_tick = now_ms(); S.hello_at = now_ms();
    S.quiet = now_ms();
    tst_init(&S.T);
    if (major == 1u) {
        mnc *m = &O()->m;
        if (m->gn_magic == MNC_GN_MAGIC && m->gn_state == MNC_GN_DIRECT) { m->gn_state = MNC_GN_NONE; m->gn_room[0] = 0; }   /* back in the menus: an unused Mano a mano hand-off is dropped */
        if (m->gn_magic == MNC_GN_MAGIC && m->gn_state == MNC_GN_STAND) m->gn_state = MNC_GN_NONE;      /* the menu entry has read the Standings mark (ml_enter runs before this overlay) */
        O()->gn_lobby &= (u8)~8u;                                              /* no toast of ours on screen yet (the menu overlay's Z hint may show) */
    } else if (!(major == 8u && (sc & 0xFFu) == 0u)) return;
    else { hs_fresh_css(); S.dret = (u8)(O()->m.gn_magic == MNC_GN_MAGIC && O()->m.gn_state == MNC_GN_DIRECT); }
    /* priority 0x10, not the 0x80 of Slippi's own menu objects: Slippi's online submenu think skips ALL input while word 8 of its object (the next object of the link) is non-zero.
       After the character select the menu scene is built first and this overlay loads a moment later: an object of ours at the same priority sat behind the submenu's and the
       submenu was dead (B, stick and D-pad ignored) after every Slippi character screen (Dolphin, slippi_css_exit.hscn). */
    S.go = GOBJ_CREATE(0, 1, 0x10);
    if (S.go) GOBJ_PROC(S.go, (void *)hs_proc, 0);
    S.scene = sc;
}

/* the overlay is left (scene change, hub_free): nothing of ours may stay reachable from a retail proc; the texts died with the scene (never freed here) */
void hs_leave(void)
{
    u32 *cb = find_cb((u32)hs_proc);
    if (cb) *cb = NOP_PROC;
    if (S.op == OP_HELLO || S.op == OP_WHOAMI) S.op = OP_NONE;
    S.go = 0; S.bd = S.l0 = S.l1 = S.bar = S.css = 0; S.kb[0] = S.kb[1] = S.kb[2] = 0; S.rt[0] = S.rt[1] = S.rt[2] = S.rt[3] = 0; S.yt[0] = S.yt[1] = 0; S.zt[0] = S.zt[1] = 0; S.y_go = 0; S.hz[0] = S.hz[1] = S.hz[2] = S.hz[3] = 0;
}
