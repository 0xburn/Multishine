/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Online delay "Auto" from the boot connection test.
 * THE ONE TABLE: change the levels here and nowhere else (host test).
 * Header only, C89, integers only, no libc: the engine (s12_gc.c, the measurement and the per-match value) and the menu overlays (the Settings row and the
 * CSS chip stepping) include it; a unit only carries the functions it calls.
 *
 * Input: the INTERNET leg of Test connection (a public resolver, 1.1.1.1 / 8.8.8.8), never the router leg.  It is a loose proxy for the opponent's round
 * trip (the resolver is usually closer than the opponent), so the levels are deliberately not tight.
 *
 * Columns: Wii family = the mass market, the reading most players meet (Wii and Wii U vWii: the study keeps the Wii U "as Wii", so it has
 * no column of its own); GameCube = the halo platform, its lowest-delay column.  Both are first-class; they are never averaged.
 * LEVELS:: the rule D = ceil(RTT / 33.37 ms) + 1 (the RTT
 * boundaries 33.4 / 66.7 / 100.1 / 133.5 / 166.8 ms), floor 2, then the platform cap.  CAPS: the existing cap 5 on both (the study's
 * Wii cap 6 adopted our; AD_CAP_WII is the one constant to change).
 * The rule's "+1 on a jittery link" is ad_rtt_ms's median + half the spread. The first placeholder table of the brief is.
 */
#ifndef MS_AUTO_DELAY_TAB_H
#define MS_AUTO_DELAY_TAB_H

#define AD_AUTO 0u                   /* the stored / engine value meaning Auto (options byte 7 = 0: an older build reads 0 as out of range -> its default 2) */
#define AD_SW 0x100u                 /* bit 8 of the engine delay word s3_cfg.delay: the Settings "Auto delay" switch is On (settings_apply); bits 0-7 = the frames, 0 = Auto */
#define AD_MIN 1u                    /* explicit values 1..AD_MAX */
#define AD_MAX 15u                   /* the engine's allowed maximum (s7_core clamps 1..15) */
#define AD_NOMEAS_GC 2u              /* no measurement yet (offline, the probe failed): GameCube */
#define AD_NOMEAS_WII 3u             /* ... Wii family */
#define AD_WII_FLOOR 2u              /* Auto never goes below this on a Wii family console */
#define AD_CAP_GC 5u                 /* Auto never goes above this on a GameCube (the existing cap,  DA_CAP_DEFAULT) */
#define AD_CAP_WII 6u                /* ... on a Wii family console: 6 (the Wii performance study: above about 133 ms a cap of 5 means constant 2-3 deep rollbacks; costs one frame of lag only on those links) */

/* frames for a round trip of rtt_ms (0 = no measurement) on a Wii family console (wii != 0) or a GameCube.
   THE TABLE: {RTT upper bound in ms (exclusive), Wii family frames, GameCube frames} BEFORE the caps AD_CAP_WII / AD_CAP_GC; the last row catches everything
   above.  (A local of the one function that reads it, so a unit that includes this header without calling it carries nothing.) */
#define AD_TAB_N 5u
static __inline__ unsigned ad_frames(unsigned rtt_ms, int wii)
{
    static const unsigned char tab[AD_TAB_N][3] = {
        /* RTT <  ms   Wii  GC */
        {   51u,        3u,  2u },   /* up to 50 ms: GameCube 2, Wii family 3 (a 22-31 ms LAN / Wi-Fi link must read 2 / 3) */
        {  101u,        4u,  3u },   /* 51 - 100: one more */
        {  134u,        5u,  4u },   /* 101 - 133: two more than the base (the stall raise and the link ceiling take it on from there) */
        {  167u,        6u,  5u },   /* 134 - 166 */
        {  255u,        7u,  6u },   /* 167 and over (the bound of the last row is never compared); the caps below still apply */
    };
    unsigned i, d;
    if (rtt_ms == 0u) return wii ? AD_NOMEAS_WII : AD_NOMEAS_GC;
    for (i = 0; i + 1u < AD_TAB_N && rtt_ms >= tab[i][0]; i++) {}
    d = tab[i][wii ? 1 : 2];
    if (wii && d < AD_WII_FLOOR) d = AD_WII_FLOOR;
    if (d > (wii ? AD_CAP_WII : AD_CAP_GC)) d = wii ? AD_CAP_WII : AD_CAP_GC;
    return d < AD_MIN ? AD_MIN : d > AD_MAX ? AD_MAX : d;
}
/* the RTT the table is read with, ms (0 = unusable), from the internet leg's numbers in tenths of a ms (nettest nt_leg_result): median plus half the spread
   (spread = p95 - median: a conservative nudge for a jittery line).  coarse != 0: the run was stamped once per frame (the background run never busy-polls),
   so every sample is rounded UP to whole frames: half a frame (83 tenths) comes off.  got < AD_MIN_GOT answers: not a measurement. */
#define AD_MIN_GOT 5u
static __inline__ unsigned ad_rtt_ms(unsigned median_t, unsigned spread_t, unsigned got, int coarse)
{
    unsigned e;
    if (got < AD_MIN_GOT || median_t == 0u) return 0u;
    e = median_t + spread_t / 2u;
    if (coarse) e = e > 93u ? e - 83u : 10u;
    e = (e + 5u) / 10u;
    return e ? e : 1u;
}

/* the player's stepping: Auto sits below 1f.  Up from Auto = 1f, down from 1f = Auto.  wrap != 0 (the Settings row): up from AD_MAX = Auto and down from
   Auto = AD_MAX; wrap == 0 (the CSS chip): it stops at both ends.  dir 0 or an out-of-range value: unchanged (an out-of-range value reads as Auto). */
static __inline__ unsigned ad_step(unsigned cur, int dir, int wrap)
{
    if (cur > AD_MAX) cur = AD_AUTO;
    if (dir > 0) return cur < AD_MAX ? cur + 1u : wrap ? AD_AUTO : cur;
    if (dir < 0) return cur > AD_AUTO ? cur - 1u : wrap ? AD_MAX : cur;
    return cur;
}

/* the engine's Auto word (s12_gc.c, read by the menus through ms_net_cost 115 and by the CSS chip through the hub env): 0 = an engine without it (show plain "Auto") */
#define AD_W_VALID 0x80000000u
#define AD_W_MEAS 0x01000000u        /* the value comes from a measurement (else the no-measurement fallback) */
#define AD_W_FINE 0x02000000u        /* ... from the player's Test connection (fine timing), else the background run */
#define AD_W_DELAY(w) ((w) & 0xFFu)  /* the frames Auto plays now */
#define AD_W_RTT(w) (((w) >> 8) & 0xFFFFu)   /* the RTT the table was read with, ms (0 = none) */
static __inline__ unsigned ad_word(unsigned rtt_ms, int wii, int fine)
{
    return AD_W_VALID | (rtt_ms ? AD_W_MEAS : 0u) | (rtt_ms && fine ? AD_W_FINE : 0u) | ((rtt_ms > 0xFFFFu ? 0xFFFFu : rtt_ms) << 8) | ad_frames(rtt_ms, wii);
}
/* the delay a match plays for the setting word cur (0 = Auto, 1..15 explicit) and the engine's Auto word w */
static __inline__ unsigned ad_resolve(unsigned cur, unsigned w, int wii)
{
    if (cur >= AD_MIN && cur <= AD_MAX) return cur;
    return (w & AD_W_VALID) ? ad_frames(AD_W_RTT(w), wii) : ad_frames(0u, wii);   /* the word's RTT read with the caller's column: a Wii on a positively wired link reads the GameCube column */
}

/* ---- PEER RTT: the round trips to the ACTUAL opponent, before the match's delay is fixed ----
   Samples: one raw ENet round trip per matched acknowledgement of a command WE sent (s7_core.c s3_core_rx_from + s7_rtt_derive, the rtt_raw_hook), i.e. the
   connect handshake, our MATCH_SELECTIONS / setup digest and ENet's own pings (every 500 ms after the last acknowledgement while nothing else is in flight).
   Nothing new on the wire: the same datagrams flow with or without this (a Slippi peer sees a stock ENet peer).  Only collected OUTSIDE the match scene.
   The estimate: the sample one eighth below the top of the last 16 (about p85; before: median + half of its distance to the median; ENet errs upward: a retransmitted
   command, the peer's poll grid); nothing is taken off (the poll grids and the Wii's IOS send latency are in the game's own round trip too).
   A good measurement needs AD_PEER_MIN samples and low loss (retransmissions <= a quarter of the samples). */
#define AD_PEER_N 16u                /* the last 16 samples are kept (about 8 s of idle pings) */
#define AD_PEER_SKIP 3u              /* the first samples of a connection that are thrown away */
#define AD_PEER_MIN 5u               /* fewer: no peer measurement (the DNS estimate, else the default) */
#define AD_PEER_JUNK_MS 2000u        /* a sample over 2 s is a lost acknowledgement, not a round trip: counted as a loss */
struct ad_peer { unsigned short ms[AD_PEER_N]; unsigned short n, junk; unsigned char i, pad[3]; unsigned retx0; };
static __inline__ void ad_peer_reset(struct ad_peer *p)
{
    unsigned k;
    for (k = 0; k < AD_PEER_N; k++) p->ms[k] = 0;
    p->n = 0; p->junk = 0; p->i = 0; p->retx0 = 0; p->pad[0] = 0;
}
/* one raw round trip (ms); retx_now = the ENet host's retransmission counter now: the first sample of a peer takes it as the baseline, so the retransmitted
   CONNECTs of the hole punch (before the peer answered) never count as loss */
static __inline__ void ad_peer_add(struct ad_peer *p, unsigned ms, unsigned retx_now)
{
    if (!p->n && !p->junk) p->retx0 = retx_now;
    if (ms > AD_PEER_JUNK_MS) { if (p->junk < 0xFFFFu) p->junk++; return; }
    if (p->pad[0] < AD_PEER_SKIP) { p->pad[0]++; return; }          /* the first samples of a connection are the hole punch and the sdenet DEFAULT_RTT (500 ms) decay: never used */
    p->ms[p->i] = (unsigned short)(ms ? ms : 1u);
    p->i = (unsigned char)((p->i + 1u) % AD_PEER_N);
    if (p->n < 0xFFFFu) p->n++;
}
/* the RTT (ms) the table is read with, 0 = no good measurement.  retx_now: the ENet host's retransmission counter now (its value at the first sample is retx0). */
static __inline__ unsigned ad_peer_rtt(const struct ad_peer *p, unsigned retx_now)
{
    unsigned short s[AD_PEER_N];
    unsigned k, j, m, v, lost = (retx_now - p->retx0) + p->junk;
    m = p->n < AD_PEER_N ? p->n : AD_PEER_N;
    if (m < AD_PEER_MIN || lost * 4u > p->n) return 0u;
    for (k = 0; k < m; k++) {                                    /* insertion sort of at most 16 values */
        v = p->ms[k];
        for (j = k; j > 0u && s[j - 1u] > v; j--) s[j] = s[j - 1u];
        s[j] = (unsigned short)v;
    }
    return s[(m - 1u) / 2u];                                      /* the (lower) MEDIAN of the last 16 clean samples: 1269 read the top-eighth sample and a 31 ms link read 5 frames; jitter is the stall raise's and the link ceiling's job */
}
/* where the delay of a match came from (logged, never shown:, no on-screen readouts) */
#define AD_SRC_SET 0u                /* an explicit setting 1..15 */
#define AD_SRC_PEER 1u               /* Auto from the opponent's round trips */
#define AD_SRC_DNS 2u                /* Auto from the boot / background / Test connection internet leg (the Auto word) */
#define AD_SRC_DEFAULT 3u            /* Auto without any measurement: GameCube 2, Wii family 3 */
/* ad_resolve with the peer estimate (peer_ms, 0 = none) in front: explicit setting > peer > the Auto word's measurement > the default.  With peer_ms 0 the
   value is exactly ad_resolve's. */
static __inline__ unsigned ad_resolve_src(unsigned cur, unsigned w, unsigned peer_ms, int wii, unsigned *src)
{
    if (cur >= AD_MIN && cur <= AD_MAX) { *src = AD_SRC_SET; return cur; }
    if (peer_ms) { *src = AD_SRC_PEER; return ad_frames(peer_ms, wii); }
    *src = ((w & AD_W_VALID) && (w & AD_W_MEAS)) ? AD_SRC_DNS : AD_SRC_DEFAULT;
    return ad_resolve(cur, w, wii);
}

/* STALL-AWARE AUTO DELAY: while the delay is Auto, a match raises its own input delay by one frame when the last window
   of frames shows stalls.  (Lives in this header because it is staged into every build already; host-tested in tests/stall_delay_test.c, used by glue/s17od/s12_gc.c.)

   Input: once per ADVANCED frame (a B0 that returned "continue"), the frame number and the running count of stalled B0 calls (s3_status stall_calls, the number the game record's go_stall counts).
   Output: 1 = raise the delay by one frame now.  The caller applies it at that same safe point (the frame boundary inside the B0 exchange).

   Rule (constants below, all in frames of the game, 60 per second):
     * nothing is judged before SDL_WARM (the start freeze, the first sync, the connection's own settling),
     * the stalled frames of each window of SDL_WIN frames (about 5 s) are counted; SDL_THRESH or more (3.3 %) raise the delay by one, never above the cap,
     * after a raise the window right after it is thrown away (SDL_SETTLE): the peer's time sync and the pad queues re-converge first.  Together with the
       window that is a hysteresis of 2 windows (about 10 s) between two raises, and the delay is NEVER lowered inside a match (a lower delay would need
       the same ring surgery the other way and a calm stretch proves nothing about the next minute; the next match starts again from the Auto value). */
#define SDL_WARM 240u            /* frames before the first window starts */
#define SDL_WIN 300u             /* window length, frames (5 s) */
#define SDL_THRESH 8u            /* stalled B0 CALLS in one window that raise the delay (2.7 %).  CALLS, not distinct frames (console result 13 calls per 300 frames = a handful of distinct frames, each stall lasts several calls, so 10 distinct frames never came) */
#define SDL_CAP 6u               /* the delay never goes above this by stalls (the Wii family's Auto cap) */

struct sdl { unsigned w0, s0, settle; };

static __inline__ void sdl_reset(struct sdl *s)
{
    s->w0 = 0u; s->s0 = 0u; s->settle = 0u;
}
static __inline__ int sdl_tick(struct sdl *s, unsigned frame, unsigned stalled_total, unsigned delay)
{
    unsigned n;
    if (frame < s->w0 || frame < SDL_WARM) { s->w0 = frame < SDL_WARM ? SDL_WARM : frame; s->s0 = stalled_total; return 0; }   /* warm-up, or the frame counter went back (a new match) */
    if (frame - s->w0 < SDL_WIN) return 0;
    n = stalled_total - s->s0;
    s->w0 = frame; s->s0 = stalled_total;
    if (s->settle) { s->settle = 0u; return 0; }
    if (n < SDL_THRESH || delay >= SDL_CAP) return 0;
    s->settle = 1u;
    return 1;
}


/* the ASM's delay ring (Online.s ODB_DELAY_FRAMES 0x21 u8, ODB_DELAY_BUFFER_INDEX 0x22 u8, ODB_DELAY_BUFFER 0x23: `delay` slots of 12 bytes; TriggerSendInput section 6 plays slot idx,
   section 8 overwrites it with the new pad and steps idx, wrapping at delay).  Raise the delay by one: insert one slot at the WRITE end (the slots from idx up move up by one; the new slot
   repeats the newest pad = the slot before idx, circular), the index follows its slot.  The pads already in the ring play unchanged, so every tag already sent stays true.
   b = the ODB address; delay = the value the caller read from it; 1 = done, 0 = refused (the ring does not look like `delay`, or the cap). */
#define SDL_ODB_DELAY 0x21u
#define SDL_ODB_IDX 0x22u
#define SDL_ODB_BUF 0x23u
static __inline__ int sdl_ring_raise(volatile unsigned char *b, unsigned delay)
{
    volatile unsigned char *r = b + SDL_ODB_BUF;
    unsigned d = b[SDL_ODB_DELAY], ix = b[SDL_ODB_IDX], k;
    if (d != delay || d < 1u || d >= SDL_CAP || ix >= d) return 0;
    for (k = d * 12u; k-- > ix * 12u;) r[k + 12u] = r[k];
    for (k = 0; k < 12u; k++) r[ix * 12u + k] = r[(ix ? ix - 1u : d) * 12u + k];
    b[SDL_ODB_IDX] = (unsigned char)(ix + 1u);
    b[SDL_ODB_DELAY] = (unsigned char)(d + 1u);
    return 1;
}

/* CARRY-OVER BETWEEN THE GAMES OF A SET: no lowering inside a game (a lower delay would play a pad already sent under another tag);
   at the start of the next game against the SAME opponent the delay is chosen again: start = max(auto start, carried - 1 if the last game was calm else carried), at most SDL_CAP.
   calm = the last game stalled under half the raise trigger on average (calls * 300 / frames < SDL_THRESH / 2, i.e. calls * 75 < frames for 8) AND did not raise in its last SDL_QUIET frames (30 s). */
#define SDL_QUIET 1800u
static __inline__ unsigned sdc_calm(unsigned calls, unsigned frames, unsigned nraise, unsigned last_raise)
{
    if (!frames) return 0u;
    if (calls * 300u >= (SDL_THRESH / 2u) * frames) return 0u;
    if (nraise && frames - last_raise < SDL_QUIET) return 0u;
    return 1u;
}
static __inline__ unsigned sdc_start(unsigned auto_d, unsigned carried, unsigned calm)
{
    unsigned c = carried > calm ? carried - calm : 0u, d = auto_d > c ? auto_d : c;
    return d > SDL_CAP && auto_d <= SDL_CAP ? SDL_CAP : d;
}

/* SANITY CEILING of the in-match raise ("5 frames at 22 ms ping"): a raise helps only when the link really delivers pads late, so the delay may not go above what the measured
   link needs.  start = the delay the game began with; p95_ms = the 95th percentile of the match's RTT samples (0 = not known).  ceiling = max(start + 1, table frames of p95 + 1), at most SDL_CAP:
   a LAN (p95 32 ms) may rise one frame above its start (GameCube 2 -> 3, Wii 3 -> 4); Wi-Fi / 5G (p95 160 ms) up to the cap. */
static __inline__ unsigned sdl_ceiling(unsigned start, unsigned p95_ms, int wii)
{
    unsigned c = start + 1u, t = p95_ms ? ad_frames(p95_ms, wii) + 1u : 0u;
    if (t > c) c = t;
    return c > SDL_CAP ? SDL_CAP : c;
}

#endif
