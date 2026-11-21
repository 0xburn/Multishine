/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * livediag.h: the PURE part of the live-stall diagnostic.  Opt-in build option `livediag=on`
 * (-DMS_LIVE_DIAG=1); without it nothing here is compiled into an image.
 *
 * Why it exists.  On the card-route hand-off engine played the first seconds of two real matches against strangers and then the
 * console went back to the 'Now loading' scene and stayed there; the console has no log and nothing in RAM can be read after the power-off.
 * The only record of such a run is the screen.  This unit makes the screen carry the facts that separate the candidate causes:
 *   - WHY the match ended (Y): an ENet timeout, a peer-sent ENet disconnect, our 7 s stall rule, the fixed-rules refusal, a refused
 *     restore, the glue's silence rule, a session end by the glue, or none of the counters moved;
 *   - WHEN (G: the match frame of the first B0 answer 3, Z: video retraces since the match started);
 *   - what the ENet layer and the pad stream looked like (stall runs, ENet timeouts / disconnects / retransmissions, RTT, packets in / out);
 *   - what the bridge says (state, error, peer step) and which scene the game is in;
 *   - WHERE the game thread is stuck while a scene loads for more than 10 s (srr0, lr and three return addresses of the interrupted
 *     context, as the S6!H dump prints them on a host).
 * It samples the engine's existing status counters once per video retrace (HSD's user pre-retrace callback, which keeps running when the
 * game thread hangs with interrupts on) and paints six text rows into the frame buffers while no match is running (never during a match:
 * the engine's timing is not touched).  Nothing here sends, receives, allocates, or writes anywhere but the frame buffers.
 *
 * The includer provides u32 and (for the painter) LDG_R32 / LDG_W32 (default: volatile memory access).  Pure functions are host-tested
 *.
 */
#ifndef LIVEDIAG_H
#define LIVEDIAG_H

#ifndef LDG_R32
#define LDG_R32(a) (*(volatile u32 *)(a))
#endif
#ifndef LDG_W32
#define LDG_W32(a, v) (*(volatile u32 *)(a) = (v))
#endif

/* indices into struct s3_status and struct s7_status */
enum {
    LDG_S3_STATE = 1, LDG_S3_B0_FRAME = 6, LDG_S3_CONT = 7, LDG_S3_STALL_CALLS = 8, LDG_S3_STALL_MAX = 10, LDG_S3_STALL_RUN = 11, LDG_S3_DISC = 12,
    LDG_S3_RX_PKTS = 13, LDG_S3_TX_PKTS = 14, LDG_S3_RX_BAD = 15, LDG_S3_RX_DUP = 17, LDG_S3_RLATEST = 18, LDG_S3_LACKED = 20, LDG_S3_CKS_CMP = 21, LDG_S3_CKS_BAD = 22,
    LDG_S3_CKS_FIRST = 23, LDG_S3_LOCK_FAIL = 26,
    LDG_S3_TX_FAIL = 27, LDG_S3_TICK_MAX = 29, LDG_S3_FIRST_STALLS = 32, LDG_S3_MATCH_NO = 34, LDG_S3_SOCK_DROP = 38, LDG_S3_B0_LOCKFAIL = 50,
    LDG_S3_SYNC_STALLS = 57, LDG_S3_RESV0 = 60, LDG_S3_RESV1 = 61, LDG_S3_RESV3 = 63
};
enum { LDG_S7_PING_US = 3, LDG_S7_ENET_CONNECTS = 6, LDG_S7_ENET_DISCS = 7, LDG_S7_ENET_RTT = 9, LDG_S7_ENET_RETX = 10, LDG_S7_ENET_TIMEOUTS = 14 };
#define LDG_RB_B2_MISS 10u                      /* s6_rb.w[RB_B2_MISS] */
#define LDG_ST_LOST 0x91u                       /* S7_SES_LOST */
#define LDG_ST_RUN 6u

/* why a match (or a session) ended; 0 = nothing ended yet */
enum {
    LDG_W_NONE = 0, LDG_W_ENET_TIMEOUT = 1, LDG_W_ENET_PEER = 2, LDG_W_STALL = 3, LDG_W_RULES = 4, LDG_W_B2 = 5, LDG_W_GONE = 6, LDG_W_ENDED = 7, LDG_W_OTHER = 8
};

/* one sample of the engine's counters.  Counters are cumulative since boot; `base` (taken when a match starts) turns them into per-match numbers. */
struct ldg_s {
    u32 st_state, b0_frame, cont, stall_calls, stall_max, stall_run, disc, rx_pkts, tx_pkts, rx_bad, rx_dup, rlatest, lacked, match_no, resv0, resv1, resv3;
    u32 e_discs, e_timeouts, e_retx, e_rtt, e_connects, ping_us, b2miss;
    u32 lock_fail, b0_lockfail, tx_fail, sock_drop, sync_stalls, first_stalls, tick_max;     /* the EXI / BBA side: was the network serviced? */
    u32 scene, loading, sbr_state, sbr_err, sbr_peer;
    u32 cks_cmp, cks_bad, cks_first;                              /* the core's own compare of the peer's Slippi checksums with ours */
    u32 su_dg, su_peer, su_bits, su_ref;                          /* MATCH SETUP: s7_setup_info[0..3] (our canonical setup digest, the peer's, S7SU_* bits, refusals) */
};

static void ldg_fill(struct ldg_s *o, const volatile u32 *s3, const volatile u32 *s7, const volatile u32 *rb)
{
    o->st_state = s3[LDG_S3_STATE]; o->b0_frame = s3[LDG_S3_B0_FRAME]; o->cont = s3[LDG_S3_CONT]; o->stall_calls = s3[LDG_S3_STALL_CALLS];
    o->stall_max = s3[LDG_S3_STALL_MAX]; o->stall_run = s3[LDG_S3_STALL_RUN]; o->disc = s3[LDG_S3_DISC]; o->rx_pkts = s3[LDG_S3_RX_PKTS];
    o->tx_pkts = s3[LDG_S3_TX_PKTS]; o->rx_bad = s3[LDG_S3_RX_BAD]; o->rx_dup = s3[LDG_S3_RX_DUP]; o->rlatest = s3[LDG_S3_RLATEST];
    o->lacked = s3[LDG_S3_LACKED]; o->match_no = s3[LDG_S3_MATCH_NO]; o->resv0 = s3[LDG_S3_RESV0]; o->resv1 = s3[LDG_S3_RESV1]; o->resv3 = s3[LDG_S3_RESV3];
    o->e_discs = s7[LDG_S7_ENET_DISCS]; o->e_timeouts = s7[LDG_S7_ENET_TIMEOUTS]; o->e_retx = s7[LDG_S7_ENET_RETX]; o->e_rtt = s7[LDG_S7_ENET_RTT];
    o->e_connects = s7[LDG_S7_ENET_CONNECTS]; o->ping_us = s7[LDG_S7_PING_US]; o->b2miss = rb[LDG_RB_B2_MISS];
    o->lock_fail = s3[LDG_S3_LOCK_FAIL]; o->b0_lockfail = s3[LDG_S3_B0_LOCKFAIL]; o->tx_fail = s3[LDG_S3_TX_FAIL]; o->sock_drop = s3[LDG_S3_SOCK_DROP];
    o->sync_stalls = s3[LDG_S3_SYNC_STALLS]; o->first_stalls = s3[LDG_S3_FIRST_STALLS]; o->tick_max = s3[LDG_S3_TICK_MAX];
    o->cks_cmp = s3[LDG_S3_CKS_CMP]; o->cks_bad = s3[LDG_S3_CKS_BAD]; o->cks_first = s3[LDG_S3_CKS_FIRST];
}

/* did anything happen since `prev` that ends a match or a session?  (the trigger for the snapshot).  A raw ENet disconnect or timeout is NOT a trigger: the real
   client closes the superfluous one of the two simultaneous connections (a normal event at the start of every match) and a half-open connection times out after
   5 s; the loss of the ACTIVE peer shows as the state LOST, a stall disconnect as the first B0 answer 3. */
static int ldg_event(const struct ldg_s *n, const struct ldg_s *prev)
{
    return n->disc != prev->disc || (n->st_state == LDG_ST_LOST && prev->st_state != LDG_ST_LOST) || n->resv0 != prev->resv0 || n->resv1 != prev->resv1 ||
           n->resv3 != prev->resv3 || n->b2miss != prev->b2miss;
}

/* classification at the trigger.  n = the sample that saw the event, prev = the sample one retrace earlier, base = the sample when the session / match started.
   Order = most specific first. */
static u32 ldg_why(const struct ldg_s *n, const struct ldg_s *base, const struct ldg_s *prev)
{
    if (n->b2miss != base->b2miss) return LDG_W_B2;
    if (n->resv0 != base->resv0) return LDG_W_RULES;
    if (n->resv1 != prev->resv1) return LDG_W_GONE;
    if (n->st_state == LDG_ST_LOST) {
        if (n->e_timeouts != prev->e_timeouts) return LDG_W_ENET_TIMEOUT;
        if (n->e_discs != prev->e_discs) return LDG_W_ENET_PEER;
    }
    if (n->stall_run >= 420u || (n->stall_max >= 420u && n->disc != base->disc && n->st_state != LDG_ST_LOST)) return LDG_W_STALL;
    if (n->resv3 != prev->resv3) return LDG_W_ENDED;
    if (n->disc != base->disc || n->st_state == LDG_ST_LOST) return LDG_W_OTHER;
    return LDG_W_NONE;
}

/* ---- text rows -------------------------------------------------------------------------------------------------------------------------- */
/* MATCH SETUP CONFORMANCE: the engine's s7_setup_info words */
static void ldg_fill_setup(struct ldg_s *o, const volatile u32 *su) { o->su_dg = su[0]; o->su_peer = su[1]; o->su_bits = su[2]; o->su_ref = su[3]; }

#define LDG_ROWS 8
#define LDG_COLS 48
static char *ldg_put(char *p, char tag, u32 v, int nd)           /* "<tag><nd hex digits> " */
{
    int i;
    *p++ = tag;
    for (i = (nd - 1) * 4; i >= 0; i -= 4) {
        u32 d = (v >> i) & 15u;
        *p++ = (char)(d < 10u ? '0' + d : 'A' + d - 10u);
    }
    *p++ = ' ';
    return p;
}
static u32 ldg_d(u32 a, u32 b) { return a - b; }

struct ldg_th { u32 srr0, lr, bc[3]; };                          /* the interrupted game thread */

/* rows[0..6], each NUL-terminated within LDG_COLS.  s = the sample to show (the snapshot, or the live one), base = match-start sample,
   why/frame/retr = verdict and the match frame / retraces since match start at the trigger, th = thread context (all 0 = not shown). */
static void ldg_rows(char rows[LDG_ROWS][LDG_COLS], const struct ldg_s *s, const struct ldg_s *base, u32 why, u32 frame, u32 retr, const struct ldg_th *th)
{
    char *p;
    u32 i, res = s->disc != base->disc ? 3u : s->stall_run ? 2u : 1u;
    for (i = 0; i < LDG_ROWS; i++) rows[i][0] = 0;
    p = rows[0]; p = ldg_put(p, 'Y', why, 1); p = ldg_put(p, 'G', frame, 4); p = ldg_put(p, 'H', s->match_no, 2); p = ldg_put(p, 'I', s->st_state, 2);
    p = ldg_put(p, 'J', res, 1); p = ldg_put(p, 'K', s->sbr_state, 1); p = ldg_put(p, 'L', s->sbr_err, 2); p = ldg_put(p, 'M', s->sbr_peer, 1); p = ldg_put(p, 'Z', retr, 5); *p = 0;
    p = rows[1]; p = ldg_put(p, 'N', s->stall_run, 4); p = ldg_put(p, 'P', s->stall_max, 4); p = ldg_put(p, 'Q', ldg_d(s->stall_calls, base->stall_calls), 5);
    p = ldg_put(p, 'R', ldg_d(s->disc, base->disc), 3); p = ldg_put(p, 'S', ldg_d(s->cont, base->cont), 5); *p = 0;
    p = rows[2]; p = ldg_put(p, 'T', ldg_d(s->e_timeouts, base->e_timeouts), 2); p = ldg_put(p, 'U', ldg_d(s->e_discs, base->e_discs), 2);
    p = ldg_put(p, 'V', ldg_d(s->e_retx, base->e_retx), 4); p = ldg_put(p, 'W', s->e_rtt, 3); p = ldg_put(p, 'X', s->ping_us / 1000u, 3); *p = 0;
    p = rows[3]; p = ldg_put(p, 'G', ldg_d(s->rx_pkts, base->rx_pkts), 5); p = ldg_put(p, 'H', ldg_d(s->tx_pkts, base->tx_pkts), 5);
    p = ldg_put(p, 'I', s->rx_dup, 3); p = ldg_put(p, 'J', s->rx_bad, 3); p = ldg_put(p, 'K', s->rlatest, 4); p = ldg_put(p, 'L', s->lacked, 4); *p = 0;
    p = rows[4]; p = ldg_put(p, 'T', s->scene, 8); p = ldg_put(p, 'U', s->loading, 2); p = ldg_put(p, 'V', s->b2miss, 1); p = ldg_put(p, 'W', s->e_connects, 2); *p = 0;
    p = rows[5]; p = ldg_put(p, 'K', ldg_d(s->lock_fail, base->lock_fail), 4); p = ldg_put(p, 'L', ldg_d(s->b0_lockfail, base->b0_lockfail), 4);
    p = ldg_put(p, 'M', ldg_d(s->tx_fail, base->tx_fail), 4); p = ldg_put(p, 'N', ldg_d(s->sock_drop, base->sock_drop), 4);
    p = ldg_put(p, 'P', ldg_d(s->sync_stalls, base->sync_stalls), 4); p = ldg_put(p, 'Q', ldg_d(s->first_stalls, base->first_stalls), 4); p = ldg_put(p, 'R', s->tick_max, 6); *p = 0;
    if (th && (th->srr0 || th->lr)) {
        p = rows[6]; p = ldg_put(p, 'P', th->srr0, 8); p = ldg_put(p, 'Q', th->lr, 8); *p = 0;
        p = rows[7]; p = ldg_put(p, 'R', th->bc[0], 8); p = ldg_put(p, 'S', th->bc[1], 8); p = ldg_put(p, 'U', th->bc[2], 8); *p = 0;
    } else {   /* checksums: compared and mismatching peer checksums of this match, the FIRST mismatching frame since boot */
        p = rows[6]; p = ldg_put(p, 'G', ldg_d(s->cks_cmp, base->cks_cmp), 4); p = ldg_put(p, 'H', ldg_d(s->cks_bad, base->cks_bad), 4); p = ldg_put(p, 'I', s->cks_first, 5); *p = 0;
        /* match setup: our canonical setup digest of the last match block, the peer's (our route only; 0 = none), S7SU_* bits, refusals since boot */
        p = rows[7]; p = ldg_put(p, 'G', s->su_dg, 8); p = ldg_put(p, 'H', s->su_peer, 8); p = ldg_put(p, 'I', s->su_bits, 2); p = ldg_put(p, 'J', s->su_ref, 2); *p = 0;
    }
}

/* ---- 3x5 font, 5 rows of 3 bits (top first, bit 2 = left), octal; tags avoid A-F so a value never reads as a tag ------------------------ */
static u32 ldg_glyph(char c)
{
    static const u16 dig[16] = { 075557, 026227, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075717, 025755, 065656, 074447, 065556, 074747, 074744 };
    if (c >= '0' && c <= '9') return dig[c - '0'];
    if (c >= 'A' && c <= 'F') return dig[10 + c - 'A'];
    switch (c) {
    case 'G': return 074557; case 'H': return 055755; case 'I': return 072227; case 'J': return 011157; case 'K': return 055655; case 'L': return 044447;
    case 'M': return 057755; case 'N': return 065555; case 'P': return 075744; case 'Q': return 075571; case 'R': return 065655; case 'S': return 074717;
    case 'T': return 072222; case 'U': return 055557; case 'V': return 055552; case 'W': return 055775; case 'X': return 055255; case 'Y': return 055222;
    case 'Z': return 071247; case '-': return 000700;
    default: return 0;
    }
}

/* HSD_VIData: fbWidth u16 at +4, xfb[i].buffer at +0x58 + 0x60 i, nb_xfb at +0x1D4 */
#define LDG_VIDATA 0x804C1D80u
#define LDG_YC_FG 0xEB80EB80u                    /* white */
#define LDG_YC_BG 0x10801080u                    /* black */
#define LDG_X0 8u                                /* left margin, in words (2 pixels each) */
#define LDG_Y0 300u                              /* below the rollback readout lines (rbread draws at the top of the CSS; at 40 the rows hid its RB / PK / HSD lines on the live photo) */
static int ldg_in_mem1(u32 a) { return a >= 0x80000000u && a < 0x81800000u; }

/* one row of text into every frame buffer HSD knows, through the uncached alias so that the VI sees it at once.  Characters are 4 words (8 px) wide
   and 12 lines high (each glyph pixel is 2 x 2).  Returns 0 when the geometry looks wrong (nothing is written then). */
static int ldg_paint_row(u32 row, const char *str)
{
    u32 nb = LDG_R32(LDG_VIDATA + 0x1D4u), fbw = LDG_R32(LDG_VIDATA + 4u) >> 16, i;
    if (nb < 1u || nb > 3u || fbw < 320u || fbw > 720u) return 0;
    for (i = 0; i < nb; i++) {
        u32 buf = LDG_R32(LDG_VIDATA + 0x58u + 0x60u * i), y, j;
        const char *c;
        if (!ldg_in_mem1(buf)) continue;
        buf = (buf & 0x01FFFFFFu) | 0xC0000000u;
        for (y = 0; y < 12u; y++) {
            u32 line = buf + (LDG_Y0 + row * 13u + y) * fbw * 2u;
            for (c = str, j = 0; j < LDG_COLS; j++) {
                u32 g = *c ? ldg_glyph(*c) : 0u, fy = y / 2u, fx;
                for (fx = 0; fx < 4u; fx++) {
                    u32 on = fy < 5u && fx < 3u && ((g >> (3u * (4u - fy) + (2u - fx))) & 1u);
                    LDG_W32(line + 4u * (LDG_X0 + 4u * j + fx), on ? LDG_YC_FG : LDG_YC_BG);
                }
                if (*c) c++;
            }
        }
    }
    return 1;
}

/* A scene that loads for more than LDG_LOAD_RETRACES video retraces (10 s) is "stuck": the pre-match VS splash loads for about 4 s in Dolphin and a few seconds longer from
   the console's SD card, the hang seen on lasted 85 s and more. */
#define LDG_LOAD_RETRACES 600u
/* the paint rule: never inside a running match; otherwise when a snapshot exists, or a scene has been loading for more than LDG_LOAD_RETRACES retraces */
static int ldg_want_paint(int in_match, int have_snap, u32 loading_retraces)
{
    if (in_match) return 0;
    return have_snap || loading_retraces > LDG_LOAD_RETRACES;
}

#endif
