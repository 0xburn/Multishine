/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* gns_ovl: the entry table and glue of the Game Night match-time block ms/gns.ovl (gns_ovl.h).  The uploader itself is offline/client_net/gnrec.c (compiled in unchanged);
 * this file adds: the forwards for the four crypto services mw2.c calls by name (the block has no import relocations), the AllM-resident state, the reply check, and the
 * two libc routines the compiler may emit calls to.  Strict C99, no libc, no heap.  All state is in this block's bss (zero at every load). */
#include "gns_ovl.h"
#include "gnrec.h"
#include "mw2.h"
#include "gnw.h"
#include "gnzk.h"
#ifdef GNS_HOST
#define TP_HOST 1
#define KH_HOST 1
#endif
#include "toast_pill.h"
#include "keyhint.h"

typedef char gns_buf_size_check[sizeof(((gnrec *)0)->buf) == GNS_BUF_BYTES ? 1 : -1];
const struct gns_env *gns_E;
static gnrec R;
static struct gns_proof P;
static mnc_io IO;
static gnw W;                                               /* the watcher (Watch Last Game): ring, descriptor, counters */
static struct gns_wproof WP;
static gnzk ZK;
#ifdef MS_MATCH_MSG                                         /* friend messages in the match: the state is below, next to the plate */
#ifndef MM_POLL_MS
#define MM_POLL_MS 8000u                                    /* time between two checks (the cost numbers use 5 s and 10 s: the same code) */
#endif
#define MM_OP 0x2Fu                                         /* SOCC_OP_MSGFETCH */
#define MM_QN 4u                                            /* messages per reply (ms_social.MSG_FETCH_MAX) */
#define MM_LINE 44u                                         /* characters of "Name: text" on the plate: 44 x 11.2 px + 22 px padding = 515 of the 640 px (a longer line is cut at a space) */
static struct gns_mmproof MP;
static struct {
    volatile unsigned char on;                              /* set by w_msg (menu thread) every pass; read by the check in step (game thread) */
    unsigned char ph, armed, vis, made;                      /* ph 1 = a request is in flight */
    unsigned next_ms, sent_ms, ctr, until_ms, sc, qn, qi;
    void *t;
    char q[MM_QN][MM_LINE + 1];
    unsigned last_tb;
} MM;
static int mm_rx(const unsigned char *d, unsigned n, unsigned now_ms);
static void mm_poll(unsigned now_ms);
#endif
#define GNS_HIST_TAGS 512u                                  /* the uploader's history for on-demand streaming, own AND opponent pads (16 B a tag): 8 KB; a longer game switches to full streaming (gnrec overflow) */
static unsigned char HIST[GNS_HIST_TAGS * 16u];

#ifndef GNS_HOST                                       /* the host test links the real mw2_crypto.c */
void mw2_sha512(const mw2_u8 *msg, mw2_u32 n, mw2_u8 out[64]) { gns_E->sha512(msg, n, out); }
void mw2_hmac(const mw2_u8 key[32], const mw2_u8 *msg, mw2_u32 n, mw2_u8 out[32]) { gns_E->hmac(key, msg, n, out); }
int mw2_ed25519_verify(const mw2_u8 pub[32], const mw2_u8 *msg, mw2_u32 n, const mw2_u8 sig[64]) { return gns_E->ed_verify(pub, msg, n, sig); }
int mw2_ct_equal(const mw2_u8 *a, const mw2_u8 *b, mw2_u32 n) { return gns_E->ct_equal(a, b, n); }
#endif

static int io_send(void *ctx, const mnc_u8 *p, mnc_u32 len) { int rc = gns_E->send(ctx, p, len); WP.sends++; WP.send_rc = (unsigned int) rc; return rc; }
static int io_recv(void *ctx, mnc_u8 *buf, mnc_u32 cap) { (void)ctx; (void)buf; (void)cap; return 0; }

static volatile unsigned *zd_keep;                             /* the Zelda no-transform patch (below) */
static unsigned zd_set(int on);
static unsigned zd_wanted(void);
static void pr_decide(void);

static int gns_enter(const struct gns_env *e)
{
    if (e->abi != GNS_ABI || e->sz_view != (unsigned int)sizeof(struct gns_view) || e->sz_mw2 != (unsigned int)sizeof(mw2) || e->sz_gnrec_buf != GNS_BUF_BYTES) return -1;
    if (!e->zd_keep) return -1;
    gns_E = e;
    zd_keep = e->zd_keep;
    zd_set(0);                                                 /* a patch left by a block that was dropped without leave (the resident normally undid it already) */
#ifdef GNS_ZELDA_LAB
    zd_set(GNS_ZELDA_LAB & 1u);                                /* LAB: the set-level apply (bit 0 = match 0) */
#else
    zd_set(zd_wanted());                                       /* the room type (Low-Mid Fun) says so: the same call for a console and a PC player */
    pr_decide();                                               /* ... and the server is told whether it is on (via the hub's poll): it starts the match only when both consoles said yes */
#endif
    IO.send = io_send; IO.recv = io_recv; IO.ctx = e->ctx;
    gnrec_init(&R);
    gnw_init(&W);
    gnzk_init(&ZK);
    return 0;
}

/* ---- per-match code patch: Zelda's down-B must not transform (Low-Mid Fun room type) ----
   `blr` over the first instruction of the only two entry points of the Zelda -> Sheik transform, ftZd_SpecialLw_Enter (ground) and ftZd_SpecialAirLw_Enter (air); down-B is then a no-op.  Written by THIS block
   (no resident code for the write), applied when the block is entered (set level) and at every match start (gns_arm decides again), undone at the end of the match (gns_close), when the uploader stops
   (gns_leave) and before the next decision.  Not game state: rollback, checksums and the replay are unaffected as long as every participant applies the same patch.  The original words are read from RAM at
   patch time (none is shipped) and kept in the RESIDENT gns_st (env->zd_keep, 0 = not patched), not in this block: the resident puts them back by itself whenever it drops the block without calling leave
   (the online scene ended, the block was found overwritten, gns_res.c gns_zd_restore), and a block entered again finds a stale patch there and undoes it first.  The guard refuses an address that does not
   look like the start of a function in this DOL (the word before it is not a blr) or that something else already hooked (a branch, or our blr).  The cache is flushed per word (dcbst; sync; icbi; sync;
   isync).  Returns 1 patched, 2 refused (nothing written), 0 off. */
#define ZD_BLR 0x4E800020u
#define ZD_N 2u
static volatile unsigned *zd_keep;
static unsigned char zd_refused;
#ifdef GNS_HOST
unsigned gns_zd_fake[ZD_N * 2u];                           /* host test: [2i] = the word before the entry, [2i + 1] = the entry */
#define ZD_PTR(i) (&gns_zd_fake[2u * (i) + 1u])
#else
static const unsigned zd_at[ZD_N] = { 0x8013AEE0u, 0x8013AFA4u };      /* NTSC-U 1.02 (GALE01): ftZd_SpecialLw_Enter, ftZd_SpecialAirLw_Enter; the same two in gns_res.c */
#define ZD_PTR(i) ((volatile unsigned *)zd_at[i])
#endif
static void zd_put(volatile unsigned *p, unsigned w)
{
    *p = w;
#ifndef GNS_HOST
    __asm__ volatile("dcbst 0,%0 ; sync ; icbi 0,%0 ; sync ; isync" : : "r"(p) : "memory");
#endif
}
static unsigned zd_set(int on)
{
    unsigned i;
    if (!zd_keep) return 0u;
    if (on) {
        if (zd_keep[0] || zd_keep[1]) return 1u;
        for (i = 0; i < ZD_N; i++) {
            unsigned w = *ZD_PTR(i);
            if (ZD_PTR(i)[-1] != ZD_BLR || w == ZD_BLR || (w >> 26) == 18u) { zd_refused = 1u; return 2u; }     /* not a function start of this DOL / already hooked: write nothing */
        }
        for (i = 0; i < ZD_N; i++) { zd_keep[i] = *ZD_PTR(i); zd_put(ZD_PTR(i), ZD_BLR); }
        zd_refused = 0u;
        return 1u;
    }
    for (i = 0; i < ZD_N; i++) if (zd_keep[i]) { zd_put(ZD_PTR(i), zd_keep[i]); zd_keep[i] = 0; }
    zd_refused = 0u;
    return 0u;
}
/* the room type's flag: a resident byte the resident keeps (gns_res.c gns_zwant: the PLAY hand-off carries GNS_RF_ZELDA for a Low-Mid Fun room), so the block needs no knowledge of room types */
static unsigned zd_wanted(void) { return (gns_E && gns_E->zd_want && *gns_E->zd_want) ? 1u : 0u; }
#ifdef GNS_HOST
unsigned gns_zd_host(int on) { return zd_set(on); }
#endif

/* ---- the patch state: proof.zd = 1 the patch is on, 2 the guard refused it (0 not wanted).  The RESIDENT turns it into the hand-off flag bits GNS_RF_ZON / GNS_RF_ZERR, and the hub client's
   TICKET_POLL carries it to the server (mnc.c, ext bit 6): this block does NOT send it itself, because the hub's request in flight is matched against the session counter (mnc.c mw2_open(... m->w.ctr)) and any
   other seal on the session in that window makes the hub drop its own reply (found in the first Dolphin run: the second console's find timed out 120 fields after the block was entered). ---- */
static void pr_decide(void)
{
    unsigned st = 0;
    if (zd_wanted()) st = (zd_keep && zd_keep[0] && zd_keep[1] && !zd_refused) ? 1u : 2u;
    P.zd = st;
}

static void gns_leave(void) { zd_set(0); P.zd = 0; R.state = GNREC_OFF; P.state = 0; W.state = GNW_OFF; }

static int gns_arm(unsigned int set_id, unsigned int game, const unsigned char *sel, unsigned int sel_len, int frame)
{
#ifdef GNS_ZELDA_LAB                                           /* LAB proof: bit n of the mask = match n of this boot (n = `game`) runs with the patch; no upload (the Slippi-route rig has no Game Night room). The room type (piece B) replaces the mask. */
    (void)set_id; (void)sel; (void)sel_len; (void)frame;
    (void)zd_set(((GNS_ZELDA_LAB >> (game & 31u)) & 1u) != 0u);
    return 0;
#else
    zd_set(zd_wanted());                                       /* every match decides again from the room type (Low-Mid Fun = on) */
    pr_decide();
#endif
    gnrec_arm(&R, (mnc_u32)set_id, game, sel, sel_len, (mnc_s32)frame);
    gnrec_set_ondemand(&R, HIST, GNS_HIST_TAGS);                 /* idle (heartbeats) while the server says nobody watches and it understands that; streams in full otherwise */
    P.state = 1; P.why = 0; P.set_id = set_id; P.game = game;
    return 0;
}

static void sync_proof(void)
{
    P.sent = R.st_sent; P.rollback = R.st_rollback; P.notready = R.st_notready; P.replies = R.st_replies;
    if (R.state == GNREC_DEAD) { P.state = 2; P.why = R.st_dead_why; }
}

static int gns_step(const struct gns_view *v, unsigned int now_ms)
{
    struct gnrec_rings g;
    int rc;
#ifdef MS_MATCH_MSG
    if (MM.on) mm_poll((unsigned)now_ms);                       /* the message check rides on the uploader's thread and lock (one seal at a time on the session): */
#endif
    if (R.state != GNREC_ON) { if (R.state == GNREC_DEAD) sync_proof(); return 0; }
    g.mask = v->mask; g.ltag = v->ltag; g.lpad = v->lpad; g.lnewest = v->lnewest; g.rtag = v->rtag; g.rpad = v->rpad; g.rlatest = v->rlatest;
    g.hframe = v->hframe; g.hcks = v->hcks; g.hmask = v->hmask; g.fin = v->fin;
    rc = gnrec_step(&R, gns_E->w, &IO, &g, (mnc_s32)v->frame, v->in_rb, (mnc_u32)now_ms);
    sync_proof();
    return rc > 0;
}

/* a server datagram that is a GN_REC reply (0x6D 0x02 0xBF status sid ctr echo ... MAC): checked with the session (mw2_open refreshes its liveness), then fed to the
   uploader's watchdog; anything that does not verify is counted and dropped.  Returns 1 = consumed (the resident must not queue it for the hub client). */
static int gns_rx(const unsigned char *d, unsigned int n, unsigned int now_ms)
{
    mw2_u8 st = 0;
    mw2_u32 blen = 0, ctr;
    int rc;
    WP.rx_n++; WP.rx_op = n >= 3 ? d[2] : 0u;
#ifdef MS_MATCH_MSG
    if (n >= 3u && d[0] == 0x6D && d[2] == (MM_OP | 0x80u)) return mm_rx(d, n, now_ms);        /* the message check's reply (friend messages in the match) */
#endif
    if (n >= MW2_HDR && d[0] == 0x6D && d[2] >= (GNW_OP_SPEC | 0x80u) && d[2] <= (GNW_OP_RANGE | 0x80u)) return gnw_rx(&W, gns_E->w, d, (mnc_u32) n, (mnc_u32) now_ms);     /* the watcher's replies and feed */
    if (n < MW2_HDR + MW2_MAC || d[0] != 0x6D || d[2] != (GNREC_OP | 0x80u)) return 0;
    ctr = ((mw2_u32)d[8] << 24) | ((mw2_u32)d[9] << 16) | ((mw2_u32)d[10] << 8) | d[11];
    rc = mw2_open(gns_E->w, (mw2_u32)now_ms, d, n, (mw2_u8)GNREC_OP, ctr, &st, &blen);
    if (rc == 2) { gnrec_kill(&R, 3u); sync_proof(); return 1; }          /* the server forgot our session: uploads are useless until the next arm */
    if (rc != 1) { P.bad_rx++; return 1; }
    if (st == 0 && blen >= 4u) P.merged = ((mnc_u32)d[MW2_HDR] << 24) | ((mnc_u32)d[MW2_HDR + 1] << 16) | ((mnc_u32)d[MW2_HDR + 2] << 8) | d[MW2_HDR + 3];
    if (st) P.refused = st;
    gnrec_reply2(&R, st, P.merged, (st == 0 && blen >= 5u) ? d[MW2_HDR + 4] : 0u);
    sync_proof();
    return 1;
}

static void wsync(void)
{
    WP.state = W.state; WP.why = W.why; WP.have = W.have; WP.frames = W.frames; WP.feed = W.st_feed; WP.range = W.st_range; WP.stalls = W.st_stall; WP.fed = W.st_fed;
    WP.cmp = W.st_cmp; WP.cmp_bad = W.st_cmp_bad; WP.first_bad = W.st_first_bad; WP.leave = W.st_leave; WP.bad = W.st_bad;
}
static int w_begin(unsigned int now_ms) { int rc = gnw_begin(&W, gns_E->w, &IO, (mnc_u32)now_ms); wsync(); return rc; }
static int w_begin_live(unsigned int now_ms) { int rc = gnw_begin_live(&W, gns_E->w, &IO, (mnc_u32)now_ms); wsync(); return rc; }
static int w_begin_friend(unsigned int now_ms, const char *code) { int rc = gnw_begin_friend(&W, gns_E->w, &IO, (mnc_u32)now_ms, code); wsync(); return rc; }
static int w_called(void) { return W.called; }
static int w_decline(unsigned int now_ms) { int rc = gnw_decline(&W, gns_E->w, &IO, (mnc_u32)now_ms); wsync(); return rc; }
static void w_leave(void) { gnw_leave(&W); }
static int w_zstep(int z, unsigned int now_ms) { return gnzk_step(&ZK, z, (unsigned int)now_ms); }
/* the WATCHER applies the same per-match patches as the players, from the room-type block the descriptor carries (gnw.c rt_*): Low-Mid Fun = the Zelda no-transform patch, written when the descriptor
   is in (READY: the replay's first Zelda down-B is seconds away) and put back when the watch ends or fails (and by leave / the resident's restore, as for a player).  An older server sends no block: nothing is
   written (a Low-Mid replay would then diverge at the first blocked down-B and the watcher leaves on its checksum compare, as for any lost sync).  The Wild And Crazy items hint (rt_items) is applied to the
   watched block itself (gnw.c gnw_b3: frequency 4, every item switch bit, as the players' S7R_ITEMS_ALL), no code patch. */
static void zd_watch(void)
{
    if (W.state >= GNW_READY && W.state <= GNW_RUN) { if (W.rt_have && (W.rt_patches & GNW_RTP_ZELDA)) zd_set(1); }
    else if (W.state == GNW_DONE || W.state == GNW_FAIL) zd_set(0);
}
static int w_tick(unsigned int now_ms) { int rc = gnw_tick(&W, gns_E->w, &IO, (mnc_u32)now_ms); wsync(); zd_watch(); return rc; }
static int w_css(unsigned char *msrb) { return gnw_css(&W, msrb); }
static int w_b0(const unsigned char *txb, unsigned char *rxb) { int rc = gnw_b0(&W, txb, rxb); wsync(); return rc; }
static int w_b3(unsigned char *msrb, int markers) { return gnw_b3(&W, msrb, markers); }
static int w_sels(const unsigned char **s0, const unsigned char **s1)
{
    if (!(W.desc_flags & GNW_DF_SEL) || W.sel_n[0] != GNW_SEL_LEN || W.sel_n[1] != GNW_SEL_LEN || W.state < GNW_READY || W.state > GNW_RUN) return 0;
    *s0 = W.sel[0]; *s1 = W.sel[1];
    return 1;
}
static void w_names(const char **n0, const char **n1) { *n0 = W.names[0]; *n1 = W.names[1]; }

static int gns_close(unsigned int now_ms) { int rc;
#ifndef GNS_ZELDA_LAB_KEEP                                    /* LAB (drop-restore proof): the patch stays on after the game result, so the resident's restore at the scene end is what the proof sees */
    zd_set(0);
#endif
    rc = gnrec_close(&R, gns_E->w, &IO, (mnc_u32)now_ms); sync_proof(); return rc; }
static void gns_kill(unsigned int why) { gnrec_kill(&R, why); sync_proof(); }

/* ---- the in-match hint toast: "Hold B: Leave" / "Hold Z: Accept  Double Z: Decline" of the watcher ----
   The resident (gns_res.c) calls w_toast once per pass with the line it wants (0 = none).  The text itself is made by the engine's HUD text recipe (create = on_bhold_sis.S
   bh_note_create behind a SIS pool guard, passed in by the resident: this block has no import relocations); it lives until its scene ends, so a hidden toast is kept and shown again
   (at most TS_MAKE_MAX different lines are made per scene).  The plate behind it is toast_pill.h (rounded, translucent).  A hint (not sticky) is shown for TP_ON_TIME_MS after the last
   button press on any pad, and when it first appears; a prompt that waits for an answer (sticky: the call-up) stays.  All state is in the bss (zero at every load). */
#ifndef GNS_HOST
#define TS_ITM 6u                                          /* texts of one row: "Hold" (badge) "Accept" "Double" (badge) "Decline" */
#define TS_MAKE_MAX 12u                                    /* texts per scene: the hint 3 + the call-up 6, and room to remake a row once (each text has its own HUD canvas: the SIS guard of bh_toast_make still applies) */
/* Placement: the in-match toasts stack in the TOP RIGHT corner.  The plate's right edge sits TS_RIGHT_Q from the screen centre and the first row's top TS_TOP_Q above it
   (HUD text units, Q16; 1 unit = about 10.9 px of the 640 x 528 frame: a 28 px right margin, a 32 px top margin), the next row is a plate plus an 8 px gap lower.  A toast that is hidden gives
   its row to the next one (the rows close up). */
#define TS_RIGHT_Q 1769472                                 /* +27.0 units: plate right edge */
#define TS_TOP_Q 1398000                                   /* 21.3 units above the centre: plate top, 32 px from the top edge (box y = -(this + TS_PLATE_DY)); measured in Dolphin: 6000 Q16 = 1 px */
#define TS_STEP_Q 252500                                   /* plate height (34 px) + gap (8 px) */
#define TS_BOX_H 30.0f
#define TS_PLATE_DX (-30000)                               /* the plate from the box's top left, Q16 units: the line sits 6 px right of the box and 22 px down, its capitals are 15 px high: 11 px pad left and right, 9 above and below */
#define TS_PLATE_DY 82500
#define TS_PLATE_H 210000
#define TS_PLATE_W0 133700                                 /* plate width = TS_PLATE_W0 + characters x TS_PLATE_WC: 22 px pad + 11.2 px a character */
#define TS_PLATE_WC 67960
#define TS_BTN_MASK 0x1F7F0000u                            /* a button of the controller's SI report word (INBUFH: Start Y X B A | 1 L R Z Up Down Right Left; top two bits = error) */
#define TS_SLOTS 2u                                        /* slot 0 = the hint, slot 1 = the call-up prompt (below the hint) */
static struct { void *t[TS_ITM]; const char *mline; unsigned n, w, row; } TSL[TS_SLOTS];
static struct { unsigned sc, made, last_in, have; } TS;
static const char ts_hint[] = "Hold B: Leave";
/* The PHYSICAL pad: the SDK's own copy of the last SI poll (SI.c InputBuffer[4][2] = INBUFH / INBUFL a port, 0x804A7F78 in the NTSC 1.02 DOL, plain RAM: no register is read, so no RDST bit is cleared;
   cube 505).  NOT Melee's raw pad queue (HSD_PadLibData): a replay injects the recorded players' pads into that queue before this runs, a game with real players never went idle.  A
   console whose SDK keeps no copy (Nintendont's patched PADRead leaves the buffer at zero) falls back to the queue. */
static unsigned ts_pressed(void)
{
    const volatile unsigned *ib = (const volatile unsigned *)0x804A7F78u;
    unsigned ch, any = 0;
    for (ch = 0; ch < 4u; ch++) {
        unsigned h = ib[2u * ch];
        any |= h | ib[2u * ch + 1u];
        if (!(h & 0xC0000000u) && (h & TS_BTN_MASK)) return 1;
    }
    if (!any) {
        const unsigned char *lib = (const unsigned char *)0x804C1F78u;      /* HSD_PadLibData: qnum @0, qwrite @2, queue @8 */
        const unsigned char *q = (const unsigned char *)*(const unsigned int *)(lib + 8u);
        unsigned qn = lib[0], qw = lib[2];
        if (!q || !qn) return 0;
        q += 0x30u * (qw ? qw - 1u : qn - 1u);
        for (ch = 0; ch < 4u; ch++, q += 12) if (q[10] == 0u && ((((unsigned)q[0] << 8) | q[1]) & 0x1F7Fu)) return 1;
    }
    return 0;
}
static unsigned ts_bits(float f) { union { float f; unsigned u; } c; c.f = f; return c.u; }
static void ts_put(void *t, unsigned off, unsigned bits) { *(volatile unsigned int *)((unsigned char *)t + off) = bits; }
static unsigned ts_len(const char *l) { unsigned n = 0; while (l[n]) n++; return n; }
static int ts_same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void ts_hide(unsigned k) { unsigned i; for (i = 0; i < TSL[k].n; i++) ((unsigned char *)TSL[k].t[i])[0x4D] = 1u; }
/* One row = several texts: a line "Hold B: Leave" / "Hold Z: Accept  Double Z: Decline" is split at its double spaces (keyhint.h kh_split / kh_parse); a token that starts with a key
   ([Hold ][Double ]<KEY>:) becomes "Hold" + the key's badge (the letter in the key's ink on a plate in the GameCube colour, keyhint.h) + "Leave"; any other token stays one plain text.
   The first text carries the outer translucent plate of the whole row (made first = drawn first), a badge's plate is its letter text's own.  Every x is Q16 HUD units from the row's
   plate left edge; the HUD font is 11.2 px a character (TS_PLATE_WC).  The row's texts are hidden, shown and moved together. */
#define TS_PADL 60000                                      /* plate left edge to the first glyph: 10 px nominal (the glyph begins about 1 px later: measured in Dolphin) */
#define TS_PADR 78000                                      /* the last glyph to the plate right edge: 13 px nominal (the HUD font is proportional: the estimate of a word is off by up to 5 px) */
#define TS_LX 36000                                        /* the line starts this far right of its text's x */
#define TS_BADGE_H 156000                                  /* the badge's plate: 26 px high, a square for a one letter key */
#define TS_BADGE_PAD 84000                                 /* a longer key name (Start): 14 px more than its letters */
#define TS_GC 183000                                       /* the glyph centre below the text's y: 30.5 px */
#define TS_GAP_PB 42000                                    /* the prefix word to its badge: 7 px (a word is estimated, not measured) */
#define TS_GAP_BA 36000                                    /* the badge to its action: 6 px */
#define TS_GAP_TOK (2 * TS_PLATE_WC)                       /* two spaces between tokens */
struct ts_it { char s[KH_TOK]; unsigned char key, kind; int x, w; };      /* kind 0 = text, 1 = badge letter */
static unsigned ts_cpy(char *d, const char *s) { unsigned n = 0; while (s[n] && n < KH_TOK - 1u) { d[n] = s[n]; n++; } d[n] = 0; return n; }
static unsigned ts_items(const char *line, struct ts_it *it, int *total)
{
    char k[KH_MAXTOK][KH_TOK];
    unsigned nt = kh_split(line, k), i, n = 0, j;
    int x = 0;
    for (i = 0; i < nt; i++) {
        struct kh_seg g;
        if (i) x += TS_GAP_TOK;
        if (!kh_parse(k[i], &g)) {
            if (n >= TS_ITM) break;
            it[n].kind = 0; it[n].key = 0; it[n].x = x; it[n].w = (int)ts_cpy(it[n].s, k[i]) * TS_PLATE_WC; x += it[n].w; n++;
            continue;
        }
        if (g.pre) {
            if (n >= TS_ITM) break;
            it[n].kind = 0; it[n].key = 0; it[n].x = x; it[n].w = (int)ts_cpy(it[n].s, g.pre == KH_P_HOLD ? "Hold" : "Double") * TS_PLATE_WC; x += it[n].w + (g.pre == KH_P_HOLD ? TS_GAP_PB : TS_GAP_PB + 24000); n++;   /* "Double" is drawn about 4 px wider than its estimate */
        }
        for (j = 0; j < g.n; j++) {
            unsigned l;
            int w;
            if (n >= TS_ITM) break;
            l = ts_cpy(it[n].s, kh_letter(g.key[j]));
            w = l > 1u ? (int)l * TS_PLATE_WC + TS_BADGE_PAD : TS_BADGE_H;
            it[n].kind = 1; it[n].key = g.key[j]; it[n].x = x; it[n].w = w; x += w + (j + 1u < g.n ? TS_GAP_PB : TS_GAP_BA); n++;
        }
        if (n >= TS_ITM) break;
        it[n].kind = 0; it[n].key = 0; it[n].x = x; it[n].w = (int)ts_cpy(it[n].s, g.act) * TS_PLATE_WC; x += it[n].w; n++;
    }
    *total = x;
    return n;
}
/* one row of the stack: make the texts of `line` (or reuse the hidden ones), put them in their row, show or hide them */
static void ts_row(unsigned k, const char *line, void *(*create)(const char *), int vis, unsigned row)
{
    unsigned i;
    if (!line) { ts_hide(k); return; }
    if (!(TSL[k].n && TSL[k].mline && ts_same(TSL[k].mline, line))) {       /* another line (or none yet): hide the old texts, make the new ones */
        struct ts_it it[TS_ITM];
        int total, w, tx;
        unsigned n;
        ts_hide(k); TSL[k].n = 0; TSL[k].mline = 0;
        if (!create) return;
        n = ts_items(line, it, &total);
        if (!n || TS.made + n > TS_MAKE_MAX) return;                       /* all or none of a row */
        w = total + TS_PADL + TS_PADR;
        for (i = 0; i < n; i++) {
            void *t;
            TS.made++;
            t = create(it[i].s);
            if (!t) { TSL[k].n = i; ts_hide(k); TSL[k].n = 0; return; }
            TSL[k].t[i] = t;
            if (it[i].kind) {                                              /* the letter centred on its plate: the plate from the text's box (x + TS_LX is the letter's left) */
                unsigned ink = kh_ink_rgb(it[i].key);
                int lw = (int)ts_len(it[i].s) * TS_PLATE_WC;
                tx = TS_RIGHT_Q - w + (TS_PADL - TS_LX) + it[i].x + (it[i].w - lw) / 2;
                ts_put(t, 0x30, (ink << 8) | 0xFFu);
                ts_put(t, 0x00, tp_f2(tx, 16));
                tp_attach_rgb_at(t, kh_plate_rgb(it[i].key), TS_LX + lw / 2 - it[i].w / 2, TS_GC - TS_BADGE_H / 2, it[i].w, TS_BADGE_H);
            } else {
                tx = TS_RIGHT_Q - w + (TS_PADL - TS_LX) + it[i].x;
                ts_put(t, 0x00, tp_f2(tx, 16));
                if (!i) tp_attach_at(t, -(TS_PADL - TS_LX), TS_PLATE_DY, w, TS_PLATE_H);
            }
            ts_put(t, 0x0C, tp_f2((int)((((unsigned)ts_len(it[i].s) * 112u + 220u) * 6554u) / 10u), 16)); ts_put(t, 0x10, ts_bits(TS_BOX_H));   /* the box (alignment only) */
        }
        TSL[k].n = n; TSL[k].mline = line; TSL[k].w = (unsigned)w; TSL[k].row = 0xFFu;
    }
    if (!TSL[k].n) return;
    if (TSL[k].row != row) { TSL[k].row = row; for (i = 0; i < TSL[k].n; i++) ts_put(TSL[k].t[i], 0x04, tp_f2(-(TS_TOP_Q + TS_PLATE_DY) + (int)row * TS_STEP_Q, 16)); }
    for (i = 0; i < TSL[k].n; i++) ((unsigned char *)TSL[k].t[i])[0x4D] = vis ? 0u : 1u;
}
static void w_toast(const char *line, void *(*create)(const char *), unsigned now_ms, unsigned sc, unsigned sticky)
{
    int hv;
    unsigned k;
#ifdef GNS_TOAST_DEMO                                      /* lab proof only: every other 8 s the call-up prompt (below the hint) instead of the leave hint alone */
    static const char demo[] = "Hold Z: Accept  Double Z: Decline";
    if (line && ((now_ms >> 13) & 1u)) { line = demo; sticky = 1u; }
#endif
    if (TS.sc != sc) { for (k = 0; k < TS_SLOTS; k++) { TSL[k].n = 0; TSL[k].mline = 0; } TS.made = 0; TS.sc = sc; }   /* the texts died with their scene */
    if (!line) {
#ifdef GNS_TOAST_DBG
        P.bad_rx++;
#endif
        for (k = 0; k < TS_SLOTS; k++) ts_hide(k);
        TS.have = 0; return;
    }
    if (!TS.have || ts_pressed()) { TS.have = 1; TS.last_in = now_ms; }      /* the first call of a toast counts as an input: the hint is seen once at the start */
    hv = tp_visible(now_ms, TS.last_in, TP_ON_TIME_MS, 0);
#ifdef GNS_TOAST_DBG                                       /* lab proof only: the uploader's proof words are idle in a watch session, the resident copies them into gns_* every pass */
    P.merged = TS.last_in; P.refused = now_ms; P.rollback = (unsigned)hv | (TS.have << 1) | (sticky << 2); P.replies++;
    if (ts_pressed()) P.notready++;
#endif
    if (sticky) {                                          /* a prompt that waits for an answer: the hint above it (timed), the prompt in the next row (always) */
        ts_row(0, ts_hint, create, hv, 0);
        ts_row(1, line, create, 1, hv ? 1u : 0u);
    } else {
        ts_row(0, line, create, hv, 0);
        ts_hide(1);
    }
}
#else
static void w_toast(const char *line, void *(*create)(const char *), unsigned now_ms, unsigned sc, unsigned sticky) { (void)line; (void)create; (void)now_ms; (void)sc; (void)sticky; }
#endif

#ifdef MS_MATCH_MSG
/* ---- friend messages DURING the match (prototype behind MS_MATCH_MSG, default off) ----
   The resident calls w_msg once per menu-thread pass.  While `on` (a Game Night / Unranked / Direct match, never Grandmaster, never a watcher) the block asks the server for new friend messages
   with ONE small datagram every MM_POLL_MS (MSG_FETCH, flag 1 = only show: the same request the menus use, socc.c socc_msg_fetch) on the session it already has, takes the reply in rx, and shows each
   message as "Name: text" on ONE plate in the toast style for MM_SHOW_MS.  The plate is one HUD text per scene (made by the engine's text maker with a blank line) whose line is RE-PRINTED for every message
   with Melee's own HSD_SisLib_803A70A0 (the call the CSS hold-B label uses): no engine step is needed for the re-print, the block calls the retail function by its address (a variadic call, "%s" and the
   text as an argument, so a '%' in a message is only a '%').  Nothing is read or marked read: the website keeps the message unread. */
#ifndef MM_SHOW_MS
#define MM_SHOW_MS 4000u                                   /* one plate stays this long */
#endif
#ifndef MM_FIRST_MS
#define MM_FIRST_MS 2000u                                  /* the first check after the match starts */
#endif
#define MM_WAIT_MS 3000u                                   /* a request without a reply this long is forgotten (the next one goes at the normal time) */
static unsigned __attribute__((unused)) mm_tb(void)
{
#if defined(MM_PROBE) && !defined(GNS_HOST)
    unsigned t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
#else
    return 0;
#endif
}
#ifdef MM_PROBE                                            /* lab cost probe: tick counters (40.5 MHz on a GameCube), the gap between two passes in 1/8 ms */
static unsigned short MMH[256];
static void mm_sum_add(unsigned *n, unsigned *sum, unsigned *mx, unsigned dt) { (*n)++; *sum += dt; if (dt > *mx) *mx = dt; }
static void mm_per(unsigned dt_ticks)
{
    unsigned b = dt_ticks / 5063u, i, acc = 0, n;
    if (b > 255u) b = 255u;
    MMH[b]++; MP.per_n++;
    if (b > MP.per_max) MP.per_max = b;
    if (!(MP.per_n & 15u)) {
        n = MP.per_n;
        MP.per_p50 = MP.per_p99 = 0;
        for (i = 0; i < 256u; i++) {
            acc += MMH[i];
            if (!MP.per_p50 && acc * 2u >= n) MP.per_p50 = i;
            if (acc * 100u >= n * 99u) { MP.per_p99 = i; break; }
        }
    }
}
#define MM_PT_BEGIN unsigned mm_t0 = mm_tb()
#define MM_PT_END mm_sum_add(&MP.pn, &MP.psum, &MP.pmax, mm_tb() - mm_t0)
#else
#define MM_PT_BEGIN (void)0
#define MM_PT_END (void)0
#endif
static unsigned mm_g32(const unsigned char *p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }
/* "Name: text" into d, at most MM_LINE characters, cut at a space when longer */
static void mm_line(char *d, const char *name, unsigned nl, const char *text, unsigned tl)
{
    char tmp[12 + 2 + 60 + 1];
    unsigned n = 0, i;
    for (i = 0; i < nl; i++) tmp[n++] = name[i];
    tmp[n++] = ':'; tmp[n++] = ' ';
    for (i = 0; i < tl; i++) tmp[n++] = text[i];
    if (n > MM_LINE) {                                     /* cut at the last space inside the plate's width (a hard cut when the text has none past the name); no "...": the HUD font drew three dots as ". N" */
        unsigned c = MM_LINE;
        while (c > nl + 4u && tmp[c] != ' ') c--;
        n = c > nl + 4u ? c : MM_LINE;
    }
    for (i = 0; i < n; i++) d[i] = tmp[i];
    d[n] = 0;
}
/* the reply body of MSG_FETCH: count, then per message id u32 | from p8 | name p12 | age u16 | text p60 (socc_parse_msgs); 0 = good */
static int mm_parse(const unsigned char *b, unsigned n)
{
    unsigned cnt, k, i = 1, qn = 0;
    char line[MM_QN][MM_LINE + 1];
    if (n < 1u || n > 400u) return -1;
    cnt = b[0];
    if (cnt > MM_QN) return -1;
    for (k = 0; k < cnt; k++) {
        unsigned fl, nl, tl, j, ni;
        if (i + 4u + 1u > n) return -1;
        MP.last_id = mm_g32(b + i); i += 4u;
        fl = b[i]; if (fl > 8u || i + 1u + fl >= n) return -1; i += 1u + fl;
        nl = b[i]; if (nl > 12u || i + 1u + nl + 2u + 1u > n) return -1;
        ni = i + 1u;
        for (j = 0; j < nl; j++) if (b[ni + j] < 0x20u || b[ni + j] > 0x7Eu) return -1;
        i += 1u + nl + 2u;
        tl = b[i]; if (tl > 60u || tl == 0u || i + 1u + tl > n) return -1;
        for (j = 0; j < tl; j++) if (b[i + 1u + j] < 0x20u || b[i + 1u + j] > 0x7Eu) return -1;
        mm_line(line[qn], (const char *)(b + ni), nl, (const char *)(b + i + 1u), tl);
        i += 1u + tl;
        qn++;
    }
    if (i != n) return -1;
    for (k = 0; k < qn; k++) { for (i = 0; i < MM_LINE + 1u; i++) MM.q[k][i] = line[k][i]; }
    MM.qn = qn; MM.qi = 0;
    return 0;
}
/* a datagram with the MSG_FETCH reply op: 1 = ours and consumed (verified or counted bad), 0 = not ours (the hub client's) */
static int mm_rx(const unsigned char *d, unsigned n, unsigned now_ms)
{
    mw2_u8 st = 0;
    mw2_u32 blen = 0;
    int rc;
    MM_PT_BEGIN;
    if (MM.ph != 1u || n < MW2_HDR + MW2_MAC || mm_g32(d + 8) != MM.ctr) return 0;
    MP.rx++; MP.rxb += n;
    rc = mw2_open(gns_E->w, (mw2_u32)now_ms, d, (mw2_u32)n, (mw2_u8)MM_OP, MM.ctr, &st, &blen);
    MM.ph = 0; MM.next_ms = now_ms + MM_POLL_MS;
    if (rc != 1 || st != 0 || mm_parse(d + MW2_HDR, blen)) { MP.bad++; MM_PT_END; return 1; }
    if (!MM.qn) MP.rx_empty++;
    MM_PT_END;
    return 1;
}
static void mm_poll(unsigned now_ms)
{
    static mw2_u8 buf[MW2_HDR + 5u + MW2_MAC];
    mw2_u32 n;
    if (MM.ph == 1u) { if (now_ms - MM.sent_ms > MM_WAIT_MS) { MM.ph = 0; MP.lost++; MM.next_ms = now_ms + MM_POLL_MS; } return; }
    if (!MM.armed) { MM.armed = 1; MM.next_ms = now_ms + MM_FIRST_MS; return; }
    if ((int)(now_ms - MM.next_ms) < 0) return;
    if (MM.qi < MM.qn || MM.vis) return;                  /* what was fetched is still being shown: ask again after it */
    if (!gns_E->w || !mw2_ready(gns_E->w, (mw2_u32)now_ms)) { MP.nosess++; MM.next_ms = now_ms + 1000u; return; }
    {
        MM_PT_BEGIN;
        buf[MW2_HDR] = 0; buf[MW2_HDR + 1] = 0; buf[MW2_HDR + 2] = 0; buf[MW2_HDR + 3] = 0; buf[MW2_HDR + 4] = 1u;      /* after = 0, flags = 1 (only show) */
        n = mw2_seal(gns_E->w, buf, (mw2_u8)MM_OP, 5u);
        MM.ctr = gns_E->w->ctr; MM.sent_ms = now_ms; MM.ph = 1u;
        MP.polls++; MP.txb += n;
        if (gns_E->send(gns_E->ctx, buf, (unsigned)n) < 0) { MM.ph = 0; MM.next_ms = now_ms + MM_POLL_MS; MP.lost++; }
        MM_PT_END;
    }
}
#ifndef GNS_HOST
#define MM_SIS_SET ((int (*)(void *, int, const char *, ...))0x803A70A0u)      /* HSD_SisLib_803A70A0(text, subtext index, format, ...): replaces a subtext's contents */
static const char mm_sj_chr[] = "!?;()[]+/%#&*@$=<>~|_^{}";                    /* Melee's SIS encoder lacks some punctuation: full-width Shift-JIS (hub_soc.c sis, ml_core.c ml_sis_expand) */
static const unsigned char mm_sj_lo[] = { 0x49, 0x48, 0x47, 0x69, 0x6A, 0x6D, 0x6E, 0x7B, 0x5E, 0x93, 0x94, 0x95, 0x96, 0x97, 0x90, 0x81, 0x83, 0x84, 0x60, 0x62, 0x51, 0x4F, 0x6F, 0x70 };
#ifdef MM_PROBE
static void mm_draw_probe(void *gobj) { unsigned t0 = mm_tb(); tp_draw(gobj); mm_sum_add(&MP.dn, &MP.dsum, &MP.dmax, mm_tb() - t0); }
#define MM_CB mm_draw_probe
#else
#define MM_CB tp_draw
#endif
static void mm_show(const char *line, void *(*create)(const char *))
{
    char out[MM_LINE * 2u + 2u];
    unsigned n = 0, i, j, len = ts_len(line), w;
    int tx;
    if (!MM.t) {
        if (!create || MM.made) return;
        MM.made = 1u;
        MM.t = create(" ");                               /* a blank line: the real text is printed below (a format or special code in a message never reaches the creator) */
        if (!MM.t) return;
        ts_put(MM.t, 0x04, tp_f2(-(TS_TOP_Q + TS_PLATE_DY), 16));
        ts_put(MM.t, 0x10, ts_bits(TS_BOX_H));
    }
    for (i = 0; i < len; i++) {
        int k = -1;
        for (j = 0; j < sizeof mm_sj_chr - 1u; j++) if (mm_sj_chr[j] == line[i]) k = (int)j;
        if (k < 0) out[n++] = line[i]; else { out[n++] = (char)0x81; out[n++] = (char)mm_sj_lo[k]; }
    }
    out[n] = 0;
    (void)MM_SIS_SET(MM.t, 0, "%s", out);
    w = len * (unsigned)TS_PLATE_WC + (unsigned)(TS_PADL + TS_PADR);
    tx = TS_RIGHT_Q - (int)w + (TS_PADL - TS_LX);
    ts_put(MM.t, 0x00, tp_f2(tx, 16));
    ts_put(MM.t, 0x0C, tp_f2((int)((((unsigned)len * 168u + 330u) * 6554u) / 10u), 16));    /* the box (alignment only) is wider than the text on purpose: a line of 46 characters lost its last one, 44 is the cap now */
    tp_attach_at(MM.t, -(TS_PADL - TS_LX), TS_PLATE_DY, (int)w, TS_PLATE_H);
    ((unsigned *)MM.t)[0x58 / 4] = (unsigned)MM_CB;
    ((unsigned char *)MM.t)[0x4D] = 0u;
}
static void mm_hide(void) { if (MM.t) ((unsigned char *)MM.t)[0x4D] = 1u; MM.vis = 0; }
static void mm_plate(unsigned now_ms, void *(*create)(const char *))
{
    if (MM.vis && (int)(now_ms - MM.until_ms) >= 0) mm_hide();
    if (!MM.vis && MM.qi < MM.qn) {
        mm_show(MM.q[MM.qi], create);
        MM.qi++;
        if (MM.t) { MM.vis = 1u; MM.until_ms = now_ms + MM_SHOW_MS; MP.shown++; }
    }
}
#endif /* !GNS_HOST */
static void w_msg(unsigned int now_ms, void *(*create)(const char *), unsigned int sc, unsigned int on)
{
#ifdef MM_PROBE
    unsigned t0 = mm_tb();
#endif
    if (MM.sc != sc) { MM.sc = sc; MM.t = 0; MM.made = 0; MM.vis = 0; MM.armed = 0; MM.ph = 0; MM.qn = MM.qi = 0; }    /* the text died with its scene; a request in flight is forgotten */
    MM.on = (unsigned char)on;
    if (!on) {
#ifndef GNS_HOST
        if (MM.vis) mm_hide();
#endif
        MM.armed = 0; MM.last_tb = 0; MM.vis = 0; MM.ph = 0;
        MP.state = 0;
        return;
    }
#ifndef GNS_HOST
    mm_plate(now_ms, create);
#else
    (void)create;                                         /* the host test has no HUD: a message counts as shown for MM_SHOW_MS */
    if (MM.vis && (int)(now_ms - MM.until_ms) >= 0) MM.vis = 0;
    if (!MM.vis && MM.qi < MM.qn) { MM.qi++; MM.vis = 1u; MM.until_ms = now_ms + MM_SHOW_MS; MP.shown++; }
#endif
    MP.state = (MM.ph ? 1u : 0u) | (MM.vis ? 2u : 0u) | (MM.made ? 4u : 0u) | (on ? 8u : 0u);
#ifdef MM_PROBE
    {
        unsigned t1 = mm_tb();
        mm_sum_add(&MP.cn, &MP.csum, &MP.cmax, t1 - t0);
        if (MM.last_tb) mm_per(t1 - MM.last_tb);
        MM.last_tb = t1;
    }
#endif
}
#endif /* MS_MATCH_MSG */

/* FRIENDS ADD (moved here from the resident gns_res.c,, with the same logic: the resident engine had no room left for the tab art, the two backup buffers and the scan).
   THE HELP TEXT AND THE TOP-LEFT TAB ("Friends" / "Name Entry", and "the other player gets a friend invite, nobody enters my code").  Both live in the loaded game files: the
   description is SIS entry 87 of SdSlChr (HandleDescriptionText.asm), the first tab is the "VS. Mode" strip of MnExtAll (168 x 28 I4).  For this hand-off only, the loaded copies are rewritten in place before
   the keyboard opens and put back when it closes, so Mano a mano keeps Slippi's text and tab.  Nothing of the game's own bytes is kept in this file: the two places are found by a fingerprint of 40 of their bytes
   (a rolling hash over MEM1, half a megabyte per pass, confirmed by a second hash; the screen is black meanwhile), the original bytes are copied into gns_E->ad_buf at patch time and written back from there.  The new
   text is built here (SIS: 0x20 YY glyph, 0x1A space, 0x03 line break, 0x0C ff cb 00 .. 0x0D the yellow span, the 13 format bytes in front are the old string's own); the new tab is our Friends strip (frd_tab_art.h). */
#include "frd_tab_art.h"
#define GNS_AD_TXT_OLD 178u
#define GNS_AD_B 0x9E3779B1u
static unsigned int ad_scan, ad_hit_t[2], ad_hit_v[2];
static unsigned char ad_nt, ad_nv, ad_scanned, ad_patched;
#define AD_BK_T(i) (gns_E->ad_buf + (i) * GNS_AD_TXT_OLD)
#define AD_BK_V(i) (gns_E->ad_buf + 2u * GNS_AD_TXT_OLD + (i) * (unsigned int)FRD_TAB_BYTES)
static unsigned int ad_fnv(const volatile unsigned char *p)
{
    unsigned int h = 0x811C9DC5u, k;
    for (k = 0; k < 40u; k++) h = (h ^ p[k]) * 0x01000193u;
    return h;
}
static void ad_text_scan(void)
{
    unsigned int a = ad_scan ? ad_scan : 0x80400000u, e = a + 0x80000u, h = 0, bp = 1, k;
    if (e > 0x81700000u) e = 0x81700000u;
    for (k = 0; k < 39u; k++) bp *= GNS_AD_B;
    for (k = 0; k < 40u; k++) h = h * GNS_AD_B + ((const volatile unsigned char *)a)[k];
    for (; a < e; a++) {
        const volatile unsigned char *p = (const volatile unsigned char *)a;
        if (h == 0x5FE2F893u && ad_fnv(p) == 0x1E998F12u && ad_nt < 2u && a < 0x8176A000u) ad_hit_t[ad_nt++] = a;
        else if (h == 0x9CCC9A52u && ad_fnv(p) == 0x63979733u && ad_nv < 2u && a < 0x8176A000u) ad_hit_v[ad_nv++] = a - 168u;
        h = (h - p[0] * bp) * GNS_AD_B + p[40];
    }
    ad_scan = e;
    if (e >= 0x81700000u) ad_scanned = 1;
}
static unsigned int ad_enc(unsigned char *o, const char *t)
{
    unsigned int n = 0;
    for (; *t; t++) {
        char c = *t;
        if (c == ' ') o[n++] = 0x1A;
        else if (c == '|') o[n++] = 0x03;
        else if (c == '{') { o[n++] = 0x0C; o[n++] = 0xFF; o[n++] = 0xCB; o[n++] = 0x00; }
        else if (c == '}') o[n++] = 0x0D;
        else { o[n++] = 0x20; o[n++] = c == '\'' ? 0xF3 : c == '.' ? 0xE7 : (c >= 'a' ? (unsigned char)(36 + c - 'a') : (unsigned char)(10 + c - 'A')); }
    }
    return n;
}
static void ad_flush(volatile unsigned char *p, unsigned int n)
{
#ifndef GNS_HOST
    unsigned int k;
    for (k = 0; k < n; k += 32u) __asm__ volatile("dcbst 0,%0; sync" : : "r"(p + k) : "memory");
#else
    (void)p; (void)n;
#endif
}
static void ad_patch(void)
{
    unsigned int i, k, n;
    for (i = 0; i < ad_nt; i++) {
        volatile unsigned char *p = (volatile unsigned char *)ad_hit_t[i];
        unsigned char buf[GNS_AD_TXT_OLD];
        for (k = 0; k < GNS_AD_TXT_OLD; k++) AD_BK_T(i)[k] = p[k];
        for (k = 0; k < 13u; k++) buf[k] = p[k];
        n = 13u + ad_enc(buf + 13, "Enter your {friend's connect code} above.|They get a friend request.");
        buf[n++] = 0x19; buf[n++] = 0x0F; buf[n++] = 0x00;
        for (k = 0; k < n; k++) p[k] = buf[k];
        ad_flush(p, n);
    }
    for (i = 0; i < ad_nv; i++) {
        volatile unsigned char *p = (volatile unsigned char *)ad_hit_v[i];
        for (k = 0; k < FRD_TAB_BYTES; k++) { AD_BK_V(i)[k] = p[k]; p[k] = frd_tab_friends[k]; }
        ad_flush(p, FRD_TAB_BYTES);
    }
    ad_patched = (unsigned char)(ad_nt || ad_nv);
}
/* THE SHOWN SUGGESTION IS THE CODE (cube with the field showing the prefilled suggestion, START then A on Confirm did nothing; Slippi commits a suggestion only on Z, OnConfirmButtonHandler refuses an
   empty committed count).  For the Friends hand-off the committed count is set to the suggestion's length once the cursor is on Confirm, exactly the field Z autofill writes first (CheckTriggersAndZ.asm:
   ACB_COMMITTED_CHAR_COUNT = ACRXB_SUGGESTION_LEN); the letters are already in the field's buffer.  The ACB / ACXB addresses come from the data table of Slippi's CheckAutofill injection at 0x8023CCA4 (+0xC the ACB;
   ACB +6 the ACXB): every address is range checked, anything unexpected writes nothing.  Mano a mano never calls this (the resident calls ad_prep only in the Friends hand-off). */
#define AD_INJ 0x8023CCA4u
static unsigned char ad_cdone;
static int ad_okp(unsigned int a) { return a >= 0x80400000u && a < 0x81800000u; }
static void ad_commit(void)
{
#ifndef GNS_HOST
    unsigned int hk = *(volatile unsigned int *)AD_INJ, tgt, acb, acxb;
    volatile unsigned char *b;
    if (ad_cdone || *(volatile unsigned short *)0x804A04F2u != 57u || (hk & 0xFC000003u) != 0x48000000u) return;      /* only once the cursor is on Confirm (Menu_FlowData hovered option 57: START, or the stick): a suggestion committed at once made B delete it one letter at a time */
    hk &= 0x03FFFFFCu;
    if (hk & 0x02000000u) hk |= 0xFC000000u;
    tgt = AD_INJ + hk;
    if (!ad_okp(tgt) || *(volatile unsigned int *)(tgt + 4u) != 0x4E800021u) return;
    acb = *(volatile unsigned int *)(tgt + 0xCu);
    if (!ad_okp(acb)) return;
    b = (volatile unsigned char *)acb;
    acxb = ((unsigned int)b[6] << 24) | ((unsigned int)b[7] << 16) | ((unsigned int)b[8] << 8) | (unsigned int)b[9];
    if (!ad_okp(acxb) || !b[5]) return;                                    /* b[5]: the one-shot fetch of the first suggestion has run */
    {
        volatile unsigned char *x = (volatile unsigned char *)acxb;
        if (x[0] && x[25] > 0u && x[25] <= 8u && b[4] == 0u) b[4] = x[25];     /* has a suggestion, none committed: commit it */
        ad_cdone = 1;
    }
#endif
}
static int gns_ad_prep(void)
{
    if (!gns_E || !gns_E->ad_buf) return -1;
    if (!ad_scanned) { ad_text_scan(); return 0; }
    if (!ad_patched) ad_patch();
    ad_commit();
    return 1;
}
static void gns_ad_off(int reset)
{
    unsigned int i, k;
    if (ad_patched) {
        for (i = 0; i < ad_nt; i++) {
            volatile unsigned char *p = (volatile unsigned char *)ad_hit_t[i];
            for (k = 0; k < GNS_AD_TXT_OLD; k++) p[k] = AD_BK_T(i)[k];
            ad_flush(p, GNS_AD_TXT_OLD);
        }
        for (i = 0; i < ad_nv; i++) {
            volatile unsigned char *p = (volatile unsigned char *)ad_hit_v[i];
            for (k = 0; k < FRD_TAB_BYTES; k++) p[k] = AD_BK_V(i)[k];
            ad_flush(p, FRD_TAB_BYTES);
        }
        ad_patched = 0;
    }
    ad_cdone = 0;
    if (reset) { ad_scan = 0; ad_nt = 0; ad_nv = 0; ad_scanned = 0; }
}

const struct gns_api gns_api = { GNS_ABI, gns_enter, gns_leave, gns_arm, gns_step, gns_rx, gns_kill, &P, gns_close, w_begin, w_tick, w_css, w_b0, w_b3, w_sels, w_names, &WP, w_called, w_decline, w_leave, w_zstep, w_begin_live, w_toast, w_begin_friend, gns_ad_prep, gns_ad_off
#ifdef MS_MATCH_MSG
    , w_msg, &MP
#endif
};

#ifndef GNS_HOST                                       /* the host test links the C library's */
void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memset(void *dst, int c, __SIZE_TYPE__ n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
#endif
