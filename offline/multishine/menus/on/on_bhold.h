/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * on_bhold: "hold B for 1 s to leave the character select screen", made obvious.  Original work.
 *
 * Retail Melee already has a hold-B exit on every CSS ( mnCharSel_CursorThink, lines 2398-2490): while a
 * player's B is held, that cursor's u16 counter (CSSCursorData +0xA) counts up and, once it exceeds 30 (0.5 s), the CSS sets its exit
 * byte mnCharSel_804D6CF6 = 2 and returns to the previous menu.  Conditions: any cursor (VS: each port; 1P / online: the one cursor),
 * coin held or placed or no coin at all, B released at least once since the CSS opened (mnCharSel_804D6CF3 bit of that port), and the
 * counter is reset to 0 the frame B is released.  Slippi's online CSS keeps that code (no injection in 0x802602A0..0x80262648), and its
 * menu load sends CleanupConnections (BA) when the CSS is left (OnMenuLoad.asm), which our backend answers by cancelling the search
 * (on_backend.c od_cleanup): leaving by B is the existing back-out path.
 *
 * This module changes one number and adds the picture:
 *   - the two compares `cmplwi r0, 0x1E` (0x2800001E at 0x802603FC, 1P branch, and 0x802606F4, multi-player branch) become
 *     `cmplwi r0, 179` (0x280000B3): Melee's own exit is only a backstop at the 180th consecutive frame; the real exit is time based (BH_HOLD_TICKS, 1.0 s), see below;
 *   - on any CSS (scene kind GS_CSS = 8, gm_804D6720->scene_kind) a small box at the top centre reads "Hold B to exit"; while a B hold
 *     is counting it reads "Exiting in 2.4 s" and a bar under it fills from the left in proportion to the counter; releasing B resets
 *     Melee's counter, the bar disappears and the text returns to "Hold B to exit".  Nothing is drawn on the connect-code keyboard
 *     (mnCharSel_804D6CF6 != 0) or after the exit started.
 * The counter shown is Melee's own (the largest of the CSS's cursors), so the picture and the exit cannot disagree.
 */
#ifndef ON_BHOLD_H
#define ON_BHOLD_H

/* The hold is TIME based: 1.0 s of GameCube time base (40.5 MHz; the Wii value is bus / 4, as sg_entry.c), the same length as
   every hold B / hold Z in the product (frd_hold.h FRD_HOLD_TICKS).  Melee's own counter counts frames and the CSS does not run at a fixed 60 f/s on the console (a 70-frame
   press did not leave, a 150-frame one did, release 458), so the exit is decided here from the clock: bh_tick writes the exit byte when B has been counted for 1.0 s.
   Melee's compare stays only as a backstop (BH_GUARD_FRAMES, 3 s at 60 f/s) for a tick that does not run. */
#define BH_HOLD_TICKS 40500000u                          /* 1.0 s of time base at the GameCube's 40.5 MHz (same value as FRD_HOLD_TICKS / SG_HOLD_B_TICKS) */
#define BH_GUARD_FRAMES 180u                             /* frames of continuous B at which Melee's own compare exits anyway (backstop) */
#define BH_QUIET 1                                       /*: no "Hold B to exit" box or bar, the exit looks like stock Melee (0 draws them) */
#define BH_SITE0 0x802603FCu                             /* mnCharSel_CursorThink, 1P-mode branch: cmplwi r0, 0x1E */
#define BH_SITE1 0x802606F4u                             /* mnCharSel_CursorThink, multi-player branch: cmplwi r0, 0x1E */
#define BH_WORD_RETAIL 0x2800001Eu                       /* cmplwi r0, 30 (exit on the 31st frame) */
#define BH_WORD_OURS (0x28000000u | (BH_GUARD_FRAMES - 1u)) /* cmplwi r0, 179 (backstop: exit on the 180th frame) */

/* layout in px of the 640x480 frame (CSS camera mapping measured for the chat rows, offline/chat/gc/chat_layout.h: 9.45 px per world
   unit, origin at (320, 265); text units: font_size 0.1 -> 1 unit = 1/9.45*10 px) */
#define BH_BOX_X_PX 222
#define BH_BOX_Y_PX 30
#define BH_BOX_W_PX 196
#define BH_BOX_H_PX 22
#define BH_TEXT_DX_PX 10
#define BH_TEXT_DY_PX (-9)
#define BH_BAR_X_PX 226
#define BH_BAR_Y_PX 54
#define BH_BAR_W_PX 188
#define BH_BAR_H_PX 6
#define BH_BAR_UNITS 199u                                /* BH_BAR_W_PX in text units: 188 * 10 / 9.45 = 198.9 */

struct bh_view {                                         /* what to show for a counter value (pure, host-tested) */
    unsigned int holding;                                /* 1 while the counter runs */
    unsigned int fill_units;                             /* bar fill in text units, 0..BH_BAR_UNITS */
    unsigned int tenths_left;                            /* time left in 0.1 s, rounded up: 30 at the first frame, 0 at the exit */
};
void bh_view_of(unsigned int holding, unsigned int elapsed, unsigned int lim, struct bh_view *v);   /* elapsed / lim in time-base ticks */
unsigned int bh_i2f(unsigned int n);                     /* IEEE single bits of the integer n (< 2^24), no FPU */

/* RAM proof block (symbol bh_st, image .json bh_status_addr) */
enum { BHS_MAGIC, BHS_PATCHED, BHS_PATCH_BAD, BHS_CSS_FRAMES, BHS_HELD, BHS_PEAK, BHS_LAST_PEAK, BHS_CANCELS, BHS_EXITS,
       BHS_EXIT_HELD, BHS_TEXT_NEW, BHS_TEXT_FREE, BHS_TEXT_SKIP, BHS_SHOWN, BHS_FILL, BHS_TENTHS, BHS_NAME_ENTRY, BHS_VISITS,
       BHS_SIS_MIN, BHS_NOTE_STATE, BHS_NOTE_SHOWN, BHS_NOTE_SKIP, BHS_PING, BHS_TMV_ST, BHS_TMV_IN, BHS_N };   /* S19: the rollback-off notice (bh_note_step); BHS_PING = the ping hint's value on screen, ms, 0 = none (a lab readout, nothing reads it) */
#define BHS_MAGIC_WORD 0x42484C44u                       /* 'BHLD' */
struct bh_status { volatile unsigned int w[BHS_N]; };
extern struct bh_status bh_st;

/* one step of the screen logic on sampled inputs (host-tested; the console fills it from RAM in bh_tick) */
struct bh_in {
    unsigned int css;                                    /* a CSS scene is running */
    unsigned int scene;                                  /* identity of the scene instance (a change = new CSS, old texts gone) */
    unsigned int exitkind;                               /* mnCharSel_804D6CF6 (0 = the CSS runs its cursors) */
    unsigned int held;                                   /* the largest hold counter of the CSS's cursors */
    unsigned int b;                                      /* B is down on a CSS cursor's port (pad word 0x0200): the exit clock follows this */
    unsigned int sis_free;                             /* largest free block of the text heap */
    unsigned int now;                                    /* 32-bit time base (mftb), wrap safe */
    unsigned int lim;                                    /* ticks of the hold (0 = BH_HOLD_TICKS); the Wii passes bus / 4 */
};
enum { BH_DO_NOTHING, BH_DO_FORGET = 1, BH_DO_FREE = 2, BH_DO_MAKE = 4, BH_DO_UPDATE = 8, BH_DO_LABEL = 16, BH_DO_EXIT = 32 /* write mnCharSel_804D6CF6 = 2: B was counted for the whole hold time */ };
struct bh_state { unsigned int have, scene, prev_held, peak, exited, last_tenths, t0, armed, b_prev, rel, t_rel; };   /* rel / t_rel: a release in progress and when it began (BH_REL_GRACE) */
unsigned int bh_step(struct bh_state *s, const struct bh_in *in, struct bh_view *v);

void bh_tick(void);                                      /* console: once per rendered frame (s12_gc.c s3_tick, ON_BUILD) */

#ifdef MS_FLASH_BLANK
/* DIAG EXPERIMENT (MANIFEST option flashblank=on, lab builds only, default off = image unchanged): leaving the main menu for Slippi's online scene (the pending
   major scene byte becomes 8 while the running major is not 8) blanks the screen (VISetBlack(1) + VIFlush) so the last Event Match frame of the hub is not shown while
   the scene loads.  Safe by construction: the engine tick lifts the black when the running major scene differs from the one that armed it and nothing is loading, and
   at the latest after FB_MAX_TICKS_NUM/DEN of a second (time base, wrap safe) whatever happens.  Pure step, host-tested. */
enum { FB_NONE = 0, FB_BLACK_ON = 1, FB_BLACK_OFF = 2 };
struct fb_state { unsigned int armed, latch, t0, major0, on_count, off_count; };
/* sc = [major, pending, prev, minor]; lim = ticks of 1.0 s (bus / 4); the black lasts 1.5 s at most */
unsigned int fb_step(struct fb_state *s, unsigned int now, unsigned int lim, unsigned int sc, unsigned int loading);
#endif

/* S19: in a match where the engine turned rollback off for memory (s12_gc.c s19_rb_off: the store did not fit the HSD heap,
   or Melee's own use made the engine free it), a one-line notice "Rollback off: low memory, delay only" shows at the top left for
   BH_NOTE_FRAMES (5 s), once per match.  Pure step (host-tested) + the console tick. */
#define BH_NOTE_FRAMES 300u
#define BH_NOTE_TEXT "Rollback off: low memory, delay only"   /* letters, space, colon, comma: glyphs the SIS line format has */
struct bh_note { unsigned int shown, frames, have; };
enum { BH_NOTE_NOTHING = 0, BH_NOTE_MAKE = 1, BH_NOTE_HIDE = 2, BH_NOTE_FORGET = 4 };
unsigned int bh_note_step(struct bh_note *n, unsigned int game, unsigned int rb_off, unsigned int sis_free);
void bh_note_made(struct bh_note *n, int ok);           /* after BH_NOTE_MAKE: whether the text exists */
void bh_note_tick(void);                                 /* console: once per rendered frame, next to bh_tick */

/* In-match ping hint: "Ping: 42 ms", the round trip to the OPPONENT, small, next to Slippi's "Delay: Nf" text, only in an online match and only while
   Settings > Game > Show frame delay (the Debug Lines bit, the same switch as the delay text) is On.  DISPLAY ONLY: it reads the engine's ENet peer record (acknowledgement count, smoothed
   rtt / variance) and writes nothing back; it never touches synced state, the wire, or the match rules record.
   Source: ENet's smoothed rtt starts at 500 ms and needs ~15 acknowledgements (8 s) to settle, so the raw round trip of every single acknowledgement is derived with the pure
   s7_rtt_derive (the DELAY_AUTO method, error <= 3 ms) and the MEDIAN of the last 3 raw samples is shown (a lone spike does not move it); the text changes at most once a second
   and only when the value differs.  No float, no allocation per frame (one text per match, re-printed with HSD_SisLib_803A70A0 on a change). */
#define BH_PING_MAX 999u
struct bh_ping { unsigned int have, acks, rtt, var, ring[3], n, i, last_t, shown; };
enum { BP_NONE = 0, BP_SET = 1, BP_RESET = 2 };
typedef unsigned int (*bh_derive_fn)(unsigned int rtt0, unsigned int var0, unsigned int rtt1, unsigned int var1);
/* one tick: online = a peer is connected in a match; acks / rtt / var = the peer's rx_acks, rtt, rtt_var; now / sec = time base and ticks of 1 s.  BP_SET: p->shown holds the new value (1..999);
   BP_RESET: nothing to show (offline, or the peer went away: hide); BP_NONE: unchanged. */
unsigned int bh_ping_step(struct bh_ping *p, unsigned int online, unsigned int acks, unsigned int rtt, unsigned int var, unsigned int now, unsigned int sec, bh_derive_fn derive);
void bh_ping_tick(unsigned int active, unsigned int acks, unsigned int rtt, unsigned int var);   /* console: once per rendered frame (s12_gc.c, next to bh_note_tick) */
/* WHEN the text is made (review H2).  The rollback restore puts back gobj_alloc_data, the GObj sbss lists and the HSD_ObjAllocData pool headers: a GObj made AFTER the frame a
   rollback restores goes back on the free list and off the p_link / camera lists (the next GObj of the re-run reuses its cell while the ping line still points at it).  Made after the
   third sample (match frame ~90, bh_test), a rollback could restore a frame before it.  Restores never go before START_SYNC_FRAME (s7_core.h S7_FREEZE_TAG 78): both sides send neutral
   pads below it, so no prediction there can miss.  So the text is made, hidden, in the first BH_PING_MAKE_PASSES passes of the match scene (minus the largest delay, 15, still far below
): every capture then holds it, and a restore can neither free nor duplicate it.  Too late in the match (no peer in time, setting read late): no text this match, never later.
   bh_ping_make(passes, on): passes = match-scene passes so far including this one; on = a peer is connected and Show ping is on.  BPM_MAKE = make it now, BPM_WAIT = not yet,
   BPM_NEVER = the window is over. */
#define BH_PING_MAKE_PASSES 30u
enum { BPM_WAIT = 0, BPM_MAKE = 1, BPM_NEVER = 2 };
unsigned int bh_ping_make(unsigned int passes, unsigned int on);

/* B0 console readout (LAB ONLY, MANIFEST option rbread -> -DMS_RB_READOUT): s12_gc.c fills
   rbr_w[] when a match ends, on_bhold.c rbr_tick (called by bh_tick) prints three lines at the top of the next CSS:
     "RB M<match> G<ground kind> S<store K> <HSD|ALM|OFF> Z<mem state> O<cell_overflow> L<lost_ranges> R<rollbacks>"
     "PK C<cells> X<extras ring> J<journal> E<events> A<mirror high water K>"
     "HSD <free at the store decision K> <minimum K> ALM <free K> <minimum K>"
     "DP K<deepest rollback> H <rollbacks of depth 0..7+, 8 numbers> N<empty match-scene passes skipped>"
     "T1 N<depth-1 restores> S<step 1 us> U<undo us> R<restore us> P<H plan us> H<H stream us>"   (B2, averages per restore)
     "T2 N<depth-2 restores> S<step 1 us> U<undo us> R<restore us>"
     "PM B<psc busy|ps busy<<1> S<cache state 0 idle 1 cached 2 none 3 filling> A<blocks held> F<freed> N<not listed> T<bounded-wait timeouts> M<served from a moved block> D<fell back to disc> X<invalid fill> C<frees with blocks>"
       = Stadium cache state at the MATCH END (before psc_free);  "PS ..." the same words after psc_free ran in the CSS, + "K<psc_free calls since the match end>"
     "FT M<longest frame ms> F<its match frame> N<frames >= 100 ms> L<frames >= 500 ms> K<Stadium request kind near it: 0 none 1 cache 2 moved block 3 disc 4 cache unusable 5 vanilla> W<longest ps_wait ms>"
   Peaks are latched every 16 match frames and published only for a match that captured. */
enum { RBR_MATCH, RBR_STAGE, RBR_STORE, RBR_MEM, RBR_CELLS, RBR_XRING, RBR_JRING, RBR_EVTS, RBR_OVF, RBR_LOST, RBR_ARENA_HI,
       RBR_HSD0, RBR_HSDMIN, RBR_ALLM0, RBR_ALLMMIN, RBR_RB, RBR_MAXK, RBR_H0, RBR_EMPTY = RBR_H0 + 8, RBR_PLACE,
       RBR_T1N, RBR_T1S, RBR_T1U, RBR_T1R, RBR_TP, RBR_TH, RBR_T2N, RBR_T2S, RBR_T2U, RBR_T2R,
       RBR_PM0, RBR_PS0 = RBR_PM0 + 10, RBR_PSCALLS = RBR_PS0 + 10,
       RBR_FT_MS, RBR_FT_F, RBR_FT_N, RBR_FT_K, RBR_N };   /* CELLS .. ARENA_HI are one run (copied as a block); PLACE 0 OFF, 1 HSD, 2 ALM */


/* CSS OPTION CHIP (to-do 67): the online DELAY changeable on the online CSS, as one pill under Slippi's status list (the mock 01_dojo.png), Slippi's own wording "Delay: 3f".
   Keys: X / Y are Melee's costume keys on the CSS, Z is Slippi's hold-to-disconnect (HandleInputsOnCSS.asm), so the free keys L and R are used:
   L steps the delay down, R up (1..15, no wrap).  The value is the word the Settings menu writes (s3_cfg[3] = ENGINE_DELAY, read by both backends at the next match setup, so Slippi's in-match
   "Delay: Nf" line shows the same number) and the Settings record (ons_change_delay).  Modes: see co_policy below.  The REGION chip of the mocks is not built on the console yet: the
   server pairs by the account's region (a website setting) and the wire has no per-search region. */
/* ad_step: Auto, 1f.. 15f */
#if defined(__has_include) && __has_include("../../delay_auto/auto_delay_tab.h")
#include "../../delay_auto/auto_delay_tab.h"           /* staged build tree (offline/s17/net/...) */
#else
#include "../../../delay_auto/auto_delay_tab.h"        /* the source tree (offline/multishine/menus/...) */
#endif
#define CO_DELAY_MIN 1u
#define CO_DELAY_MAX 15u
#define CO_PAD_R 0x20u
#define CO_PAD_L 0x40u
/* THE L / R BUTTON ICONS of the chip lines: small rounded plates with the letter in them, px of the 640 x 480 frame.  Line 1: [L][R] Delay 4f; line 2 (Grandmaster): Hold [R] Region (Auto).
   Icon: CO_ICON_W x CO_ICON_H, 3 px between the two icons, 5 px between an icon and the text after it.  "Hold" is 27 px wide at the CSS font (tp_text_gw 400 x 6.6 px / 100) + 2 px seen in Dolphin, 4 px before its icon. */
#define CO_X_PX 219                                       /* left edge of both lines (the free band right of the player's card, under the status list) */
#define CO_Y1_PX 418                                      /* line 1 text origin y */
#define CO_Y2_PX 432                                      /* line 2 text origin y */
#define CO_ICON_W 14
#define CO_ICON_H 13
#define CO_ICON_GAP 3
#define CO_ICON_TGAP 5
#define CO_HOLD_W 29
#define CO_HOLD_GAP 4
#define CO_L1_LX CO_X_PX                                  /* line 1: L icon left edge */
#define CO_L1_RX (CO_L1_LX + CO_ICON_W + CO_ICON_GAP)     /* R icon left edge (236) */
#define CO_L1_TX (CO_L1_RX + CO_ICON_W + CO_ICON_TGAP)    /* "Delay Nf" text x (255) */
#define CO_L2_RX (CO_X_PX + CO_HOLD_W + CO_HOLD_GAP)      /* line 2: R icon left edge (252) */
#define CO_L2_TX (CO_L2_RX + CO_ICON_W + CO_ICON_TGAP)    /* "Region (..)" text x (271) */
#define CO_ICON_RGB 0xD8DAE2u                             /* plate: light grey */
#define CO_ICON_INK 0x15171Fu                             /* letter: near black */
/* WHICH CHIPS A MODE SHOWS.  The inputs are the engine's raw facts: gn_state MNC_GN_* (1 armed, 2 search = a Game Night or a Grandmaster set hand-off,
   4 = Mano a mano answer), rank_hand = that hand-off belongs to a Grandmaster set, online_byte = Slippi's mode byte (0 ranked = Grandmaster, 1 unranked = Dojo / Slippi Unranked, 2 direct = Mano a mano /
   Slippi Direct, 3+ Teams / Party / not online).  DELAY chip: Dojo, Grandmaster, Mano a mano, the Slippi modes; NOT Game Night, NOT offline / Teams / Party.  REGION chip: Grandmaster only.  One flag per chip,
   so a later mode is one line here.  Nothing is "locked" any more: a chip is shown (and changes) or not shown.  The delay is only ever changed on the CSS, before the search starts. */
#define CO_P_DELAY 1u
#define CO_P_REGION 2u
static __inline__ unsigned int co_policy(unsigned int online_byte, unsigned int gn_state, unsigned int rank_hand)
{
    if (gn_state == 1u || gn_state == 2u) return rank_hand ? CO_P_DELAY | CO_P_REGION : 0u;   /* a set hand-off: Grandmaster has both, a Game Night room has none (its room has the rules) */
    if (gn_state == 4u) return CO_P_DELAY;                                                   /* Mano a mano answer (direct) */
    if (online_byte == 0u) return CO_P_DELAY | CO_P_REGION;                                   /* Grandmaster queue */
    return online_byte <= 2u ? CO_P_DELAY : 0u;                                              /* Dojo / Unranked, Direct; Teams / Party / offline: nothing */
}
/* REGION values (one source of truth: the ACCOUNT's region on the server, ms_region.CODES index; Settings > Online region writes the same, socc_prof_set_acct).  Chip order, Auto first:
   0 Auto (= the account's own region, whatever the website says), 1..7 NAE NAW EU LA AS OC NAC (= ms_region index + 1; NAC appended).  Cycles with wrap. */
#define CO_REGION_N 8u
/* Auto, NAW, NAC, NAE, EU, LA, AS, OC (west to east; the values stay the wire order +1: 1 NAE, 2 NAW .. 6 OC, 7 NAC) */
static __inline__ unsigned int co_region_step(unsigned int cur) { static const unsigned char nx[8] = { 2, 3, 7, 4, 5, 6, 0, 1 }; return cur < 8u ? nx[cur] : 0u; }

/* HOLD R for the region: R is the delay-up key; where the REGION chip exists a TAP of R (released before lim) still steps the delay up (on release), and HOLDING R for the standard
   hold time (lim = 1 s of time base, the same as hold-B-leave / hold-Z-accept) cycles the region once; the hold must be released before it can fire again.  Hold L is not used. */
struct co_hold { unsigned int t0, s; };                  /* s: 0 idle, 1 down, 2 fired */
enum { CO_EV_NONE = 0, CO_EV_TAP = 1, CO_EV_HOLD = 2 };

/* single-call-site helpers: inline in the header so the engine carries no stand-alone copy, and the host test sees the same code */
static __inline__ unsigned int co_delay_step(unsigned int cur, int dir)
{
    return ad_step(cur, dir, 0);                          /* Auto (0) sits below 1f, no wrap */
}
/* the delay chip's text: a stored setting "Delay 3f"; Auto is "Delay Auto", and "Delay Auto (2f)" only with the frames this console PLAYS against the
   paired opponent (`played`: the match block's delay byte while its connection state is 4, what the ASM latches at the game start; 0 = no opponent yet).
   The internet estimate (the Auto word) is never shown: on a Wii it read 6f on a LAN that played 2.  t: 16 chars at least; returns the end (no terminator). */
static __inline__ char *co_delay_text(char *t, unsigned int d, unsigned int played)
{
    static const char a[] = "Delay Auto (";
    unsigned int i, n = d ? 6u : played - 1u < CO_DELAY_MAX ? 12u : 10u;
    for (i = 0; i < n; i++) *t++ = a[i];
    if (!d) { d = played; if (n == 10u) return t; }
    if (d >= 10u) *t++ = (char)('0' + d / 10u % 10u);
    *t++ = (char)('0' + d % 10u);
    *t++ = 'f';
    if (n == 12u) *t++ = ')';
    return t;
}
static __inline__ int co_dir(unsigned int pressed)
{
    return (int)((pressed & CO_PAD_R) != 0u) - (int)((pressed & CO_PAD_L) != 0u);   /* both = 0 */
}
static __inline__ unsigned int co_hold_step(struct co_hold *h, unsigned int down, unsigned int now, unsigned int lim)
{
    unsigned int ev = CO_EV_NONE;
    if (!down) { if (h->s == 1u) ev = CO_EV_TAP; h->s = 0u; }
    else if (!h->s) { h->s = 1u; h->t0 = now; }
    else if (h->s == 1u && now - h->t0 >= lim) { h->s = 2u; ev = CO_EV_HOLD; }
    return ev;
}

#endif
