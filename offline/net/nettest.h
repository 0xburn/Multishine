/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NETTEST_H
#define NETTEST_H

/*
 * nettest: the probe engine of Settings > Network status > Test connection.
 * Strict C89, no libc, no heap, no float, fixed buffers, never blocks: nt_start once, then nt_poll from the same step that runs
 * sdnet_poll until it returns NT_DONE or NT_STOPPED.  Two legs run side by side, ROUTER (the gateway) and INTERNET (a public
 * resolver); every probe is one UDP DNS query (the packet is built with sddns_encode_name / sddns_build_query: nothing duplicated), every
 * reply is checked with sddns_parse_reply (id, question, source address; any RCODE counts as an answer, only a malformed one does not).
 * The network is the sddns_io pair of callbacks, so the same code runs on a host socket (the tests) and on the sdnet UDP socket
 * (sddns_sdnet_bind).  Addresses are host-order u32 (192.168.1.1 == 0xC0A80101), time is a MICROSECOND clock that may wrap (u32, 71 minutes).
 * Fine timing: the caller calls nt_frame once per frame and, while nt_spin_us says so, polls every few hundred microseconds
 * (a bounded busy loop in the menu); a reply read within NT_FINE_US of the previous poll is stamped "fine" (resolution = the poll
 * step), any other is "coarse" (rounded up to the frame).  The statistics use the fine samples when there are enough.
 * nt_spin_us also lets the engine delay a probe inside the frame so that its reply lands in a polled window.
 * The verdict and the texts live in nettext.[ch] (the menu overlay carries only those).
 */

#include "sddns.h"

typedef sddns_u8 nt_u8;
typedef sddns_u16 nt_u16;
typedef sddns_u32 nt_u32;

/* ---- schedule  ---- */
#define NT_PROBES 20u               /* measured probes per leg */
#define NT_SLOTS (NT_PROBES + 1u)   /* slot 0 is the warm-up: sent, answer discarded */
#define NT_SLOT_US 100000u          /* one slot every 100 ms; each slot sends one probe per active leg */
#define NT_TIMEOUT_US 1000000u      /* a probe not answered within this is lost (a later reply is ignored) */
#define NT_HARD_US 20000000u        /* the whole run, fallback included, never lasts longer */
#define NT_FINE_US 1500u            /* a reply read at most this long after the previous poll is stamped fine */
#define NT_FINE_MIN 6u              /* fewer fine samples than this: the statistics use all samples */
#define NT_SPIN_MAX_US 11000u       /* a busy loop never lasts longer than this per frame (from nt_frame) */
#define NT_FRAME_US 16667u          /* the frame period until nt_frame has measured it */
#define NT_RX_BUDGET 4              /* datagrams read per poll */
#define NT_PORT 53u
#define NT_QW_MAX 32                /* the encoded query name ("cloudflare.com" is 16 bytes) */

#define NT_IP_CF1 0x01010101u       /* 1.1.1.1 */
#define NT_IP_G8 0x08080808u        /* 8.8.8.8, tried when 1.1.1.1 gave no answer at all */

enum nt_state { NT_IDLE = 0, NT_RUN = 1, NT_DONE = 2, NT_STOPPED = 3 };   /* STOPPED: aborted (B) or the hard limit; the data so far stays readable */
enum nt_leg_id { NT_ROUTER = 0, NT_INTERNET = 1 };

typedef struct nt_leg {
    nt_u32 target, alt;             /* the address probed now; the fallback (0 = none) */
    nt_u8 enabled;                  /* 0: not measured on this console (Wii) or no address: no probe goes out */
    nt_u8 active;                   /* sending in the current run */
    nt_u8 on_alt;                   /* the fallback target is in use */
    nt_u8 sent, got;                /* measured probes (slots 1..) sent / answered */
    nt_u8 st[NT_SLOTS];             /* 0 unsent, 1 waiting, 2 answered, 3 lost */
    nt_u8 pend, pslot;              /* a probe of slot pslot is due at pdue (delayed inside the frame) */
    nt_u8 nfine;                    /* fine samples among rtt[] */
    nt_u32 pdue;
    nt_u32 est;                     /* smallest round trip seen so far, microseconds (0 = none yet): where the next reply is expected */
    nt_u32 sent_at[NT_SLOTS];
    nt_u32 rtt[NT_PROBES];          /* microseconds of the answered measured probes, in arrival order */
    nt_u8 fine[NT_PROBES];          /* 1: stamped by a dense poll; 2: coarse although a window was expected (a late reply); 0: coarse before any estimate */
    nt_u8 kn[NT_SLOTS];             /* the probe of this slot was sent with an estimate in hand */
} nt_leg;

typedef struct nt_leg_result {
    nt_u16 median, spread;          /* tenths of a millisecond; spread = p95 - median */
    nt_u8 got, sent;                /* sent == 0: the leg was not measured */
    nt_u8 coarse;                   /* 1: too few fine samples, the numbers are rounded up to the frame */
    nt_u8 na;                       /* 1: the leg is categorically unavailable on this console (the router on a Wii family console), not merely unanswered */
} nt_leg_result;

typedef struct nettest {
    sddns_io io;
    nt_u8 router_na;                /* nt_start was told the router leg cannot be measured on this console */
    int state;
    int run;                        /* 0 first run, 1 fallback run */
    nt_u32 t0, slot_due;
    nt_u32 frame_t0, frame_us, last_poll;   /* start of this frame's slice, measured frame period, time of the previous poll */
    nt_u16 nonce;
    nt_u8 slot;                     /* next slot to send in this run (NT_SLOTS = all sent) */
    nt_u8 qwire[NT_QW_MAX];
    nt_u32 qlen;
    nt_leg leg[2];
    nt_u32 st_sent, st_rx, st_ignored;
} nettest;

/* ---- statistics (public for the tests) ---- */
/* median (even count: mean of the middle two, rounded down) and spread (value at rank ceil(0.95 n) minus median) of n <= NT_PROBES samples, microseconds; n == 0 gives 0, 0 */
void nt_stats(const nt_u32 *s, nt_u32 n, nt_u32 *median, nt_u32 *spread);

/* ---- run ---- */
/* router / router_alt: the gateway and the fallback (the DHCP resolver when it differs; 0 = none); router_ok 0 = the router leg is not measured on this console.
   inet / inet_alt: NT_IP_CF1 / NT_IP_G8.  0 started, -1 bad arguments (no internet target).  seed makes the transaction ids. */
int nt_start(nettest *t, const sddns_io *io, int router_ok, nt_u32 router, nt_u32 router_alt, nt_u32 inet, nt_u32 inet_alt, nt_u32 now, nt_u32 seed);
int nt_poll(nettest *t, nt_u32 now);            /* returns t->state (now in microseconds) */
void nt_frame(nettest *t, nt_u32 now);          /* once per frame, before the polls of that frame: measures the frame period */
nt_u32 nt_spin_us(const nettest *t, nt_u32 now);/* microseconds the caller should keep polling (every ~300 us) in this frame; 0 = stop */
void nt_abort(nettest *t);                      /* RUN -> STOPPED; harmless in any other state */
void nt_leg_get(const nettest *t, int leg, nt_leg_result *r);
nt_u32 nt_progress(const nettest *t);           /* slots sent so far in the current run, 0..NT_SLOTS (what "Testing n of 21" shows) */

/* ---- the words that cross the engine/overlay door (ms_net_cost 112..114) ---- */
nt_u32 nt_pack(const nt_leg_result *r);         /* median bits 0..9, spread 10..19 (a 10-bit code: 0.1 ms steps below 60 ms, then whole ms), got 20..25, sent 26..31 */
nt_u32 nt_state_word(const nettest *t);         /* bits 0..3 state, 8..15 progress, 16 router not measured, 17 internet used the alternate target, 18 internet coarse, 19 router coarse */
nt_u32 nt_code(nt_u32 tenths);                  /* tenths of a ms -> 10-bit code */

#endif
