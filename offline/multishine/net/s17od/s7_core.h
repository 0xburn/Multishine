/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* S12: copy of at HEAD for; S12 changes marked S12: (canonical sdenet
   header, the early-rollback helpers). */
/*
 * S7: copy of for stage S7, changes marked S7:.
 *
 * S7 replaces the project's own S3 wire (HELLO handshake with a cfg hash, 'SDS3' PAD packets with a frame-advantage
 * byte and an optional debug trailer) by the wire a Slippi Dolphin netplay peer speaks, taken from the S4 stack
 * (our own ENet implementation sdenet + Slippi's PAD / PAD_ACK / MATCH_SELECTIONS messages), under the
 * unchanged S6b/S6c/S6d rollback engine (prediction rule, gap-aware depth cap, adaptive cap v2 in the glue):
 *   ENet host, 3 channels, both sides connect at once, the lower player index closes the superfluous connection;
 *   0x80 PAD      unsequenced, channel 1: [0x80][frame s32][playerIdx u8][checksumFrame s32][checksum u32] + 8-byte pads
 *                 newest first, every pad from the newest down to the last acked frame (at most 129, Slippi's rule);
 *   0x81 PAD_ACK  unsequenced, channel 2: [0x81][frame s32][playerIdx u8], sent when a PAD added pads;
 *   0x82 MATCH_SELECTIONS reliable, channel 0: char, color, isCharSel, playerIdx, stage u16, isStageSel, rngOffset u32,
 *                 teamId, altStageMode.  The B3 match block is built from the two selections the way Slippi builds it
 *                 (char/color per player index, first stage-selected player's stage, rngOffset of the decider = index 0).
 * Time sync: the S6b frame-advantage byte is gone (no field for it exists in Slippi's packets).  Each side estimates its
 * time offset from packet arrival times exactly like SlippiNetplay.cpp OnData / CalcTimeOffsetUs (30-sample ring,
 * ping from exact-frame acks, middle-third trimmed mean) and acts with whole-frame stalls.
 * Behaviour reference only ( SlippiNetplay.cpp, EXI_DeviceSlippi.cpp, read, nothing copied; GPL).
 *
 * --- S6D / S6C / S6b / S3 history (kept from the copied header) ---
 * S6D: six cfg words at +0xA8 (cost emulation, planned overrun, cap hysteresis, S6d flags, SFX-log dump frame), a
 * stall-run-length histogram.  S6C: sparse snapshots, gap-aware depth cap (load_frame hook).  S6b: Slippi-style
 * prediction (B0 CONTINUE while the newest remote input is at most rb_depth frames behind).  S3: the B0/B3 backend.
 * Project-owned C (gnu99 subset, no libc, no heap).  Not reentrant: the GC glue calls it with interrupts disabled.
 */
#ifndef S3_CORE_H
#define S3_CORE_H

#include "../../common/sdenet/sdenet.h"   /* S12: the canonical ENet library (offline/common/sdenet, built with
                                              -DSDENET_NO_FRAGMENTS as S7/S10); S7: the S4 ENet implementation */

typedef unsigned int s3_u32;
typedef int s3_s32;
typedef unsigned short s3_u16;
typedef unsigned char s3_u8;

/* ---- S7: Slippi wire (all integers big-endian, SFML sf::Packet) ---------------------------------- */
#define S7_MSG_PAD 0x80
#define S7_MSG_PAD_ACK 0x81
#define S7_MSG_SELECTIONS 0x82
#define S7_PAD_HDR 14
#define S7_SEL_LEN 14
#define S3_PAD_BYTES 8              /* buttons(2) LX LY CX CY L R: the 12-byte pad report minus 4 (as Slippi) */
#define S7_WINDOW_MAX 129           /* Slippi: pads below newest - 128 are dropped from the send queue */
#define S7_RX_MAX 128               /* Slippi receiver: inputsToCopy > 128 rejected */
#define S7_PAD_WIRE 9               /* SLIPPI 3.7.0: each pad on the wire = the 8 input bytes + 1 byte of RNG bits (3.6.4: 8) */
#define S7_PKT_MAX (S7_PAD_HDR + S7_WINDOW_MAX * S7_PAD_WIRE)     /* 1175 B */
/* SLIPPI 3.7.0 RXB tail (Online.s d0ce23a): after the inputs of the three remote slots */
#define S7_RXB_DESPAWN 294          /* RXB_DESPAWN_FRAMES s32[3]: 0 = never (a 1v1 ends on a disconnect) */
#define S7_RXB_PRED 306             /* RXB_OPNT_PREDICTED_INPUTS: 12 x 7 x 3, the input to predict each of the 7 frames after a player's latest input */
#define S7_RXB_RNG 558              /* RXB_NEXT_PAD_RNG_BITS u8: random bits the ASM sends with its next pad */
#define S7_RXB_SIZE 559
#define S7_TXB_LEAVING 27           /* TXB_IS_LEAVING: the local player chose to leave (party mode): Slippi force-disconnects */
#define S7_ACK_TIMERS 64            /* Slippi ackTimers ring */
#define S7_TS_RING 30               /* SLIPPI_ONLINE_LOCKSTEP_INTERVAL: offset samples per remote player */
#define S7_FRAME_US 16683           /* Slippi's frame time constant */
#define S7_FREEZE_TAG 78            /* START_SYNC_FRAME (Online.s:63): pads of tags below it are neutral (ASM freeze clear) */

/* ---- states -------------------------------------------------------------------------------------- */
enum {
    S3_NET_WAIT = 0, S3_NET_INIT = 1, S3_NET_LINK = 2, S3_NET_DHCP = 3,
    S7_SES_CONNECTING = 4,           /* S7: ENet CONNECT sent / waiting for the peer */
    S3_SES_RUN = 6,                  /* S7: at least one ENet connection up */
    S3_NOBBA = 0x80, S3_FAIL_INIT = 0x81, S3_FAIL_LINK = 0x82, S3_NO_SIDE = 0x83,
    S7_SES_LOST = 0x91,              /* S7: the ENet connection dropped after the match started */
    S7_LOOPBACK = 0xA0               /* S7: no network, remote pads from the in-image script (reference runs) */
};

#define S3_RING 256                 /* S7: pad rings (power of two, > S7_RX_MAX + delay; S6d: 128) */
#define S6N_RB_MAX 7               /* ROLLBACK_MAX_FRAME_COUNT of the pinned ASM (Online.s:43) */
#define S3_CKS_HIST 64              /* local checksum history for desync comparison */

struct s3_peer {
    s3_u32 mac_lo;                   /* last three MAC bytes, 0x00XXYYZZ, selects this entry */
    s3_u32 side;                     /* 0 = A (player index 0), 1 = B (player index 1) */
    s3_u32 dst_ip;                   /* where this side sends (the relay), host order */
    s3_u32 dst_port;
};

/* Config block (patched into the image by ). Layout unchanged from S6d (fields that only served the
   S3 wire are kept as unused words so every offset, tool and the glue stay the same). */
struct s3_cfg {
    char magic[8];                   /* +0x00 "S3CFG001" */
    s3_u32 session;                  /* +0x08 S7: unused (no S3 handshake) */
    s3_u32 delay;                    /* +0x0C input delay frames 1..15 (local; sent to the ASM through B3) */
    s3_u32 rng_offset;               /* +0x10 this side's MATCH_SELECTIONS rngOffset (the decider's one is used) */
    s3_u32 start_frame;              /* +0x14 hook calls before the BBA probe */
    s3_u32 local_port;               /* +0x18 */
    s3_u32 flags;                    /* +0x1C bit0 disable all, bit1 negative control (side B corrupts the remote
                                                 pad it hands to the game from frame neg_frame), bit2 trace off, bit3 divergence locator,
                                                 bit4 card stress, bit5 allow-any, bit6 no time sync */
    s3_u32 neg_frame;                /* +0x20 */
    s3_u32 spin_us;                  /* +0x24 B0 bounded spin-poll for a missing remote input (0 = none) */
    s3_u32 stall_disc;               /* +0x28 consecutive stalled B0 calls before DISCONNECTED (frame > 1) */
    s3_u32 first_wait;               /* +0x2C same, while waiting at frame 1 (peer still loading) */
    s3_u32 stocks;                   /* +0x30 stock count written into the match struct (0 = template; lab knob, not on the wire) */
    s3_u32 window;                   /* +0x34 S7: max pads per PAD packet (0 = Slippi's 129) */
    s3_u32 hello_ms;                 /* +0x38 S7: unused */
    s3_u32 time_limit;               /* +0x3C match timer in seconds (0 = template 8:00; lab knob, not on the wire) */
    struct s3_peer peer[2];          /* +0x40 */
    s3_u32 allow_ip;                 /* +0x60 S7: only address an incoming ENet CONNECT is accepted from (0 = peer[].dst_ip) */
    s3_u32 allow_port;               /* +0x64 and its source port (0 = any port) */
    s3_u32 rb_depth;                 /* +0x68 S6b: max prediction / rollback depth in frames (0 = lockstep, <= 7) */
    s3_u32 store_size;               /* +0x6C S6b: snapshot store bytes */
    s3_u32 sd_flags;                 /* +0x70 S6b: snapshot manifest flags */
    s3_u32 deny;                     /* +0x74 S6b: static deny mask */
    s3_u32 stage;                    /* +0x78 S6b/S7: template stage when no selection names one */
    s3_u32 dbg_lo, dbg_n;            /* +0x7C S6b: debug dump window (the S6b wire trailer is gone in S7) */
    s3_u32 stride;                   /* +0x84 S6C */
    s3_u32 s6c_flags;                /* +0x88 */
    s3_u32 cap_min;                  /* +0x8C */
    s3_u32 hw_pct;                   /* +0x90 */
    s3_u32 busy_hi;                  /* +0x94 */
    s3_u32 busy_lo;                  /* +0x98 */
    s3_u32 hyst;                     /* +0x9C */
    s3_u32 budget_pct;               /* +0xA0 */
    s3_u32 stride_max;               /* +0xA4 */
    s3_u32 cost_pct;                 /* +0xA8 S6D TEST ONLY cost emulation */
    s3_u32 ovr_pct;                  /* +0xAC */
    s3_u32 ovr_every;                /* +0xB0 */
    s3_u32 cap_hyst;                 /* +0xB4 */
    s3_u32 s6d_flags;                /* +0xB8 */
    s3_u32 sfx_dump;                 /* +0xBC */
};                                   /* 0xC0 */
#define S6DF_CAP_V1 1u
#define S6DF_SFX_LOG 2u
#define S6CF_ADAPT_STRIDE 1u
#define S6CF_ADAPT_CAP 2u
#define S6CF_NO_ODB_FIX 4u
#define S6CF_NO_GAPCAP 8u
#define S3F_ALLOW_ANY 0x20u          /* cfg flags bit5: accept an ENet CONNECT from any address */
#define S6F_NO_SYNC 0x40u            /* cfg flags bit6: no time-sync stalls */

/* S7: second config block (own magic, image +0x800) */
struct s7_cfg {
    char magic[8];                   /* +0x00 "S7CFG001" */
    s3_u32 sel[2];                   /* +0x08 MATCH_SELECTIONS per side: char << 24 | color << 16 | stage (u16) */
    s3_u32 ts_mode;                  /* +0x10 time sync: 0 off, 1 Slippi start rule only (frame <= 120), 2 console pair
                                                (start rule + run rule), 3 passive (against a PC: start rule + slow run rule) */
    s3_u32 ts_thr_us;                /* +0x14 run rule: act when the trimmed-mean offset >= this (0 = mode default) */
    s3_u32 ts_gap;                   /* +0x18 run rule: frames between two corrections (0 = mode default) */
    s3_u32 s7_flags;                 /* +0x1C S7F_* */
    s3_u32 script_seed;              /* +0x20 loopback script seed */
    s3_u32 connect_data;             /* +0x24 ENet CONNECT data (Slippi: 0) */
    s3_u32 resv[6];                  /* +0x28 */
};                                   /* 0x40 */
#define S7F_LOOPBACK 1u              /* reference: no network; remote pad of frame F = s4_script_pad(seed, F) (F > delay) */
#define S7_PUNCH_START_MS 300u        /* PUNCH: after this long without a connection (a LAN or open-router pair is connected in about 0.1 s) ... */
#define S7_PUNCH_WIDE_MS 6000u        /* ... a second, wider ring after this long (+1 .. +64 and -1 .. -16: 80 ports, each about every 1.3 s) ... */
#define S7_PUNCH_WIDE_N 80u
#define S7_PUNCH_END_MS 20000u        /* ... until the engine's own connect window ends ... */
#define S7_PUNCH_GAP_MS 16u           /* ... one datagram this often (the ring below: each port about every 0.45 s) */
#define S7_PUNCH_RING 28
#define S7F_PASSIVE 2u               /* do not initiate the ENet connect (accept only) */
#define S7F_NO_RSEL 4u               /* NEGATIVE TEST: side B builds B3 from the template, ignoring the received selections */
#define S7F_ACK_LATE 8u              /* TEST: send PAD_ACKs at the next B0 instead of at receipt (TIME_SYNC T6) */
#define S7F_RXB_S6B 16u              /* COMPARISON: S6b RXB/limit rule on the uncapped remote head (no finalized + 6 cap) */
/* SLIPPI INTEROP (the Slippi interop rules section 4): peer kind = a REAL Slippi client (set in s7_flags for the rig,
   or at run time by s7_core_slippi_match from the bridge).  Gated behaviour, everything else is unchanged for our own peers:
     D3  the pad window is Slippi's 129 (the cfg lab window of 32 is ignored) and there is no stall at 248 unacked pads (Slippi has none);
     D9  the stall-disconnect counter counts only rollback-limit stalls, never time-sync stalls (Slippi's stallFrameCounts);
     D10 stage as Slippi does it (random draw from the allowed list, first selected player index wins, Battlefield when none);
     D11 the relaxed stick compare is OFF (glue: s7_core_is_slippi).
   D5 (selection merge) is applied to every peer: our own peers always send char and stage selected, so the merge is the identity there. */
#define S7F_SLIPPI_PEER 0x200u
/* AUTO INPUT DELAY (-10-10).  S7F_DELAY_AUTO: the glue decides the input delay ONCE per match, at B3 (delay_hook), from the
   raw ENet RTT samples (rtt_raw_hook) through offline/delay_auto; the cfg delay stays the fallback (too few samples).  Off by default in every image
   (the delay is the cfg word, Slippi's 2).  The delay is local and never on the wire; two sides may choose different values.
   S7F_DA_SKEW (TEST): side B plays the chosen delay + 3 (asymmetric delays on purpose, the exactness check).
   S7F_DA_NEG (TEST negative control): side B TAGS its pads one frame later than the delay it plays (a tag skew the peer cannot know): must DIVERGE. */
#define S7F_DELAY_AUTO 0x400u
#define S7F_DA_SKEW 0x800u
#define S7F_DA_NEG 0x1000u
/* AUTO DELAY FROM THE OPPONENT: with the Online delay setting on Auto, the glue's delay hook reads
   the frames from the raw ENet round trips to the session's peer (collected outside the match scene only) when the measurement is good, else from the Auto word.
   Off by default in every image; S7F_DELAY_AUTO (the older engine Auto) wins when both are set. */
#define S7F_AD_PEER 0x2000u

/* Status block (RAM, watched by the harness through MemoryWatcher).  Word index = offset / 4.  S7: words that were
   S3-wire specific are renamed (same index): 13 rx_pad, 14 tx_pad, 17 rx_gap, 24 sel_tx, 25 sel_rx, 36 peer_tag,
   57 ts_stalls, 58 ts_offset_us. */
struct s3_status {
    volatile s3_u32 magic;           /*  0 'S3ST' */
    volatile s3_u32 state;           /*  1 */
    volatile s3_u32 side;            /*  2 */
    volatile s3_u32 calls;           /*  3 */
    volatile s3_u32 ip;              /*  4 */
    volatile s3_u32 b0_calls;        /*  5 */
    volatile s3_u32 b0_frame;        /*  6 */
    volatile s3_u32 cont;            /*  7 */
    volatile s3_u32 stall_calls;     /*  8 */
    volatile s3_u32 stall_frames;    /*  9 */
    volatile s3_u32 stall_max_run;   /* 10 */
    volatile s3_u32 stall_run;       /* 11 */
    volatile s3_u32 disc;            /* 12 */
    volatile s3_u32 rx_pkts;         /* 13 S7: PAD messages with a valid header */
    volatile s3_u32 tx_pkts;         /* 14 S7: PAD messages sent */
    volatile s3_u32 rx_bad;          /* 15 malformed Slippi messages / wrong player index */
    volatile s3_u32 rx_new;          /* 16 remote pads stored */
    volatile s3_u32 rx_dup;          /* 17 S7: PAD rejected (fewer pads than frame - head, or > 128) */
    volatile s3_u32 rlatest;         /* 18 newest contiguous remote frame */
    volatile s3_u32 lnewest;         /* 19 newest local tagged frame */
    volatile s3_u32 lacked;          /* 20 newest local frame acked by the peer */
    volatile s3_u32 cks_cmp;         /* 21 */
    volatile s3_u32 cks_bad;         /* 22 */
    volatile s3_u32 cks_first_bad;   /* 23 */
    volatile s3_u32 hello_tx;        /* 24 S7: MATCH_SELECTIONS sent */
    volatile s3_u32 hello_rx;        /* 25 S7: MATCH_SELECTIONS received */
    volatile s3_u32 lock_fail;       /* 26 */
    volatile s3_u32 tx_fail;         /* 27 */
    volatile s3_u32 b3_calls;        /* 28 */
    volatile s3_u32 tick_max;        /* 29 */
    volatile s3_u32 b0_tick_max;     /* 30 */
    volatile s3_u32 trace_frames;    /* 31 */
    volatile s3_u32 first_stalls;    /* 32 */
    volatile s3_u32 neg_applied;     /* 33 */
    volatile s3_u32 match_no;        /* 34 */
    volatile s3_u32 win_max;         /* 35 */
    volatile s3_u32 peer_frame;      /* 36 S7: frame field (tag) of the newest PAD header received */
    volatile s3_u32 cks_last_frame;  /* 37 */
    volatile s3_u32 sock_drop;       /* 38 */
    volatile s3_u32 spin_polls;      /* 39 */
    volatile s3_u32 spin_hits;       /* 40 */
    volatile s3_u32 lfull;           /* 41 */
    volatile s3_u32 tick_run_sum;    /* 42 */
    volatile s3_u32 run_ticks;       /* 43 */
    volatile s3_u32 b0_tick_sum;     /* 44 */
    volatile s3_u32 rx_old;          /* 45 PAD with nothing new (no ack, as Slippi) */
    volatile s3_u32 sync_frame;      /* 46 hook calls when first connected */
    volatile s3_u32 slice_skip;      /* 47 */
    volatile s3_u32 dz_first;         /* 48 DESYNC LOCATOR (this game): the first frame whose checksum the peer sent and ours disagreed on; 0 = none.  Cleared at every game start (was stress_ops, card stress lab only) */
    volatile s3_u32 dz_lcks;          /* 49 our checksum of dz_first's frame (was stress_id) */
    volatile s3_u32 b0_lockfail;     /* 50 */
    volatile s3_u32 dz_pcks;          /* 51 the peer's checksum of dz_first's frame (was stress_hold) */
    volatile s3_u32 dz_shift;         /* 52 DESYNC LOCATOR: at dz_first, the k in -3..+3 whose stored local checksum of frame dz_first + k equals the peer's (low byte, s8); 0x7F none; 0 = no mismatch (was stress_busy) */
    volatile s3_u32 rx_allow_drop;   /* 53 S7: ENet CONNECTs dropped by the allow-list */
    volatile s3_u32 cont_pred;       /* 54 */
    volatile s3_u32 stall_cap;       /* 55 */
    volatile s3_u32 pred_max;        /* 56 */
    volatile s3_u32 sync_stalls;     /* 57 S7: time-sync stalls (start rule + run rule) */
    volatile s3_u32 sync_adv;        /* 58 S7: last trimmed-mean time offset, us (signed; > 0 = this side is ahead) */
    volatile s3_u32 stall_gap;       /* 59 */
    volatile s3_u32 resv[4];         /* 60..63 */
};                                   /* 0x100 */

/* S7: status of the Slippi wire layer (image +0x840, 48 words) */
struct s7_status {
    volatile s3_u32 magic;           /*  0 'S7ST' */
    volatile s3_u32 rx_ack;          /*  1 PAD_ACK received */
    volatile s3_u32 tx_ack;          /*  2 PAD_ACK sent */
    volatile s3_u32 ping_us;         /*  3 last Slippi-style ping (exact-frame ack) */
    volatile s3_u32 ping_max_us;     /*  4 */
    volatile s3_u32 ping_n;          /*  5 */
    volatile s3_u32 enet_connects;   /*  6 */
    volatile s3_u32 enet_discs;      /*  7 */
    volatile s3_u32 dup_disc;        /*  8 superfluous connections we closed (lower index) */
    volatile s3_u32 enet_rtt;        /*  9 ms, ENet estimate of the active peer */
    volatile s3_u32 enet_retx;       /* 10 */
    volatile s3_u32 enet_rx;         /* 11 datagrams */
    volatile s3_u32 enet_tx;         /* 12 */
    volatile s3_u32 enet_drop;       /* 13 */
    volatile s3_u32 enet_timeouts;   /* 14 */
    volatile s3_u32 active;          /* 15 active peer | state << 8 (0xFF none) */
    volatile s3_u32 rsel;            /* 16 remote char << 24 | color << 16 | stage */
    volatile s3_u32 rsel2;           /* 17 isCharSel << 24 | isStageSel << 16 | playerIdx << 8 | teamId */
    volatile s3_u32 rsel_rng;        /* 18 */
    volatile s3_u32 b3_nosel;        /* 19 B3 answers built without the remote selections (template fallback) */
    volatile s3_u32 b3_last;         /* 20 last B3 block: P1 char << 24 | P2 char << 16 | stage */
    volatile s3_u32 b3_rng;          /* 21 rng offset in the last B3 block */
    volatile s3_u32 ts_samples;      /* 22 offset samples taken */
    volatile s3_u32 ts_syncs;        /* 23 sync points evaluated (frame % 30 == 0) */
    volatile s3_u32 ts_start_skips;  /* 24 frames skipped by the start rule (frame <= 120) */
    volatile s3_u32 ts_run_skips;    /* 25 frames skipped by the run rule */
    volatile s3_u32 ts_corr;         /* 26 corrections (start + run) */
    volatile s3_u32 ts_off_min;      /* 27 smallest offset at a sync point after frame 120 (signed) */
    volatile s3_u32 ts_off_max;      /* 28 largest */
    volatile s3_u32 ts_abs_sum_ms;   /* 29 sum of |offset| / 1000 over sync points after frame 120 */
    volatile s3_u32 ts_n_run;        /* 30 sync points after frame 120 */
    volatile s3_u32 ts_hist[8];      /* 31..38 |offset| at sync points after 120: <2, <4, <8, <12.5, <16.7, <25, <50, >= 50 ms */
    volatile s3_u32 rx_msgs;         /* 39 Slippi messages received (any id) */
    volatile s3_u32 rx_other;        /* 40 messages with other ids (0x83..0x86, ignored) */
    volatile s3_u32 lpad_hash;       /* 41 FNV-1a over (tag, pad) of local pads queued, tags 1..3600 */
    volatile s3_u32 rpad_hash;       /* 42 same over remote pads stored */
    volatile s3_u32 rx_reset;        /* 43 remote queue resets by a received MATCH_SELECTIONS */
    volatile s3_u32 ack_late_q;      /* 44 PAD_ACKs deferred to the next B0 (S7F_ACK_LATE) */
    volatile s3_u32 resv[2];         /* 45 sel_late: MATCH_SELECTIONS after frame 1 (recorded, no queue reset),
                                        46 stall_fin: rollback-limit stalls caused only by the finalized + 6 cap */
    volatile s3_u32 canary;          /* 47 0x5A5AC0DE */
};                                   /* 0xC0 */

struct s3_cks_ring { volatile s3_u32 e[8][2]; };

typedef int (*s7_send_fn)(void *ctx, s3_u32 ip, s3_u16 port, const s3_u8 *data, s3_u32 len);   /* S7: with address */
typedef void (*s3_poll_fn)(void *ctx);
typedef s3_u32 (*s3_clock_fn)(void *ctx);                                /* microseconds / milliseconds, may wrap */

/* MATCH SETUP CONFORMANCE: the negotiated settings of OUR modes (the server's assignment, hub route).  The match block is
   built ONLY from these, the two MATCH_SELECTIONS and constants; nothing comes from one player's local preference.  0 in a field = the canonical default. */
#define S7R_PAUSE_OFF 1u             /* flags: Start does not pause (Slippi: pause only in Direct) */
#define S7R_ITEMS_ALL 2u             /* flags: (Wild And Crazy): every item switch bit on and item frequency 4 (Very High); the Slippi route never sets it */
#define S7R_ALT_CHOOSER 0u           /* alt_mode: the stage chooser's altStageMode from its selections (Slippi Direct / Teams) */
#define S7R_ALT_OFF 1u               /* alt_mode: Pokemon Stadium transforms (Slippi Unranked / Ranked) */
#define S7R_ALT_ON 2u                /* alt_mode: frozen Pokemon Stadium for everybody */
struct s7_rules {
    s3_u32 flags, alt_mode;
    s3_u32 stocks;                   /* 0 = 4 */
    s3_u32 time_s;                   /* 0 = 480 (8:00) */
    s3_u32 items;                    /* Slippi's items bitfield, 0 = items off */
    s3_u32 n_stages;                 /* allowed stages (random pick and check); 0 = the six-stage pool, no check */
    s3_u16 stages[16];
};
struct s3_core {
    const struct s3_cfg *cfg;
    const struct s7_cfg *xcfg;       /* S7 */
    struct s3_status *st;
    struct s7_status *xs;            /* S7 */
    struct s3_cks_ring *cks;
    s7_send_fn send;
    s3_poll_fn poll;
    s3_clock_fn clock_us, clock_ms;
    void *ctx;
    struct sdenet_host enet;         /* S7 */
    int side;                        /* -1 unknown */
    int state;
    int active;                      /* S7: ENet peer used for sending (-1 none) */
    s3_u32 dst_ip, dst_port;         /* S7 */
    s3_u32 next_connect_ms;          /* S7 */
    int disconnected;
    s3_u8 lsel[S7_SEL_LEN];          /* S7: local MATCH_SELECTIONS message */
    s3_u8 rsel[S7_SEL_LEN];          /* S7: last remote MATCH_SELECTIONS (rsel_have) */
    int rsel_have;
    /* match */
    s3_s32 last_frame;
    s3_s32 lnewest, lacked;
    s3_s32 rlatest;
    s3_s32 stall_frame;
    s3_s32 peer_cks_frame;
    s3_u32 peer_cks;
    s3_u32 lhash, rhash;             /* S7 */
    s3_s32 ltag[S3_RING];
    s3_u8 lpad[S3_RING][S3_PAD_BYTES];
    s3_s32 lckf[S3_RING];            /* S7: checksumFrame / checksum stored with each queued local pad (Slippi SlippiPad) */
    s3_u32 lck[S3_RING];
    s3_s32 rtag[S3_RING];
    s3_u8 rpad[S3_RING][S3_PAD_BYTES];
    s3_s32 hist_frame[S3_CKS_HIST];
    s3_u32 hist_cks[S3_CKS_HIST];
    s3_u8 pkt[S7_PKT_MAX + 2];
    /* S7: Slippi ackTimers (send time of every PAD by its frame field) and time-offset estimator */
    s3_s32 at_frame[S7_ACK_TIMERS];
    s3_u32 at_us[S7_ACK_TIMERS];
    s3_u32 at_head, at_n;
    s3_u32 ping_us;
    int ever_up;                     /* OD: the session reached RUN once (a later loss of the connection is S7_SES_LOST, not a reconnect: the opponent left) */
    int game_started;                /* Slippi hasGameStarted: a PAD was sent since the last match start / selections */
    int sel_reset;                   /* the remote pad queue was reset by this session's MATCH_SELECTIONS for the coming match: new_match must not wipe the pads that arrived since */
    s3_s32 lft_frame;                /* Slippi lastFrameTiming: tag and send time of the last PAD sent */
    s3_u32 lft_us;
    s3_s32 ts_buf[S7_TS_RING];
    s3_u32 ts_n, ts_idx;
    s3_s32 ts_skip;                  /* framesToSkip */
    int ts_skipping;                 /* isCurrentlySkipping */
    s3_s32 ts_last_corr;             /* frame of the last run-rule correction */
    int ts_over;                     /* consecutive sync points over the threshold (passive mode) */
    s3_s32 ack_q_frame;              /* S7F_ACK_LATE: pending ack (0 none) */
    int ack_q_peer;
    s3_s32 cur_fin;                  /* S7: finalized frame of the current B0 (TXB) */
    /* S6C/S6D hooks used by the glue (unchanged) */
    s3_s32 (*load_frame)(void *ctx, s3_s32 x, s3_s32 frame);
    s3_u32 cap;
    void (*rtt_hook)(void *ctx, s3_u32 rtt_ms);               /* S18 pacing: ENet RTT of the active peer, from the status refresh */
    s3_u32 (*delay_hook)(void *ctx, s3_u32 cur);              /* S18 pacing: delay for the match being set up (B3, the safe moment) */
    s3_u32 (*raise_hook)(void *ctx, s3_u32 frame, s3_u32 delay);   /* STALL AUTO DELAY: at each advanced frame of a 2-seat match; 1 = the glue raised the ASM's delay by one right now (the core then fills the one tag hole) */
    s3_s32 (*latest_cap)(void *ctx, s3_s32 fin);   /* S18 gap guard: newest remote frame the RXB may show (0 = no limit) */
    s3_u32 stall_hist[8];
    /* SLIPPI INTEROP */
    int slippi;                      /* run-time peer kind (s7_core_slippi_match); S7F_SLIPPI_PEER in s7_flags is the static form */
    s3_u32 disc_run;                 /* D9: consecutive rollback-limit stalls (Slippi stallFrameCounts), time-sync stalls excluded */
    int ts_stalled;                  /* D9: the current stalled call is a time-sync stall */
    int fixed_rules;                 /* D10: Unranked rules: character < 26 and the stage must be in the allowed list */
    s3_u32 n_stages;                 /* D10: allowed stage ids (the ticket's `stages`, 0 = Slippi's built-in six) */
    s3_u16 stages[16];
    s3_u32 gen;                      /* D10: xorshift state of the stage / rngOffset draws (Slippi: mt19937 `generator()`) */
    int seats;                       /* DELAY-ONLY LOBBIES: human seats of the match named by the ticket (1..4); 0 = not named (the cfg word or 2).  See s7_core_seats */
    void (*rtt_raw_hook)(void *ctx, s3_u32 rtt_us);   /* DELAY_AUTO: one RAW ENet round trip per matched acknowledgement (ms resolution, 0 ms reported as 1 ms); 0 = off (no cost) */
    /* MATCH SETUP CONFORMANCE */
    s3_u32 items;                    /* Slippi route: the ticket's `items` (s7_slippi_match.items) */
    int rules_have;                  /* our route: the glue gave the negotiated settings (s7_core_set_rules); also our "setup check" capability */
    struct s7_rules rules;
    s3_u32 dg, dg_sent, dg_peer;     /* canonical setup digest of the last B3 block, the one last sent, the peer's */
    int dg_peer_have, dg_want, peer_cap, setup_bad;
    s3_u32 dg_t, dg_key;             /* when the current (ours, peer's) disagreement began (0 = none) and which pair it was */
#ifdef MS_QCHAT
    s3_u8 chat_tx[8];                /* QUICK CHAT (menus/qc/qc_gc.h): the next 0x84 message (6 bytes), sent by the next s3_core_tick; chat_want = 1 while it waits */
    int chat_want;
#endif
    s3_u32 punch_t0, punch_last, punch_n;   /* PUNCH: session start, last punch datagram, how many were sent; LAST in the struct on purpose (tests and the images index the fields above) */
    s3_u32 up_ms[SDENET_MAX_PEERS];         /* REJOIN: clock_ms | 1 when each ENet slot came up (e_connect); 0 = never.  Appended after the punch fields for the same reason */
    /* PQ GUARD: the glue sets pq_hold before s3_core_b0 when the game's raw pad queue
       already holds 4 entries; the call then stalls like a time-sync stall instead of handing the game an entry it would drop.  0 = off (no effect). */
    int pq_hold;
    s3_u32 pq_holds;                 /* calls stalled by the guard (lifetime; the glue takes per-match differences) */
    /* SLIPPI 3.7.0: the RNG bits byte that travels with every pad (pad report byte 8, PAD_RNG_BITS), by frame tag like lpad / rpad; rng_s = our own source for RXB_NEXT_PAD_RNG_BITS */
    s3_u8 lrng[S3_RING], rrng[S3_RING];
    s3_u32 rng_s;
    /* WHY the session ended (first cause only; shown as a code in the opponent-left box, since a console has no log): [0] cause S7W_*, [1] B0 frame, [2] ENet disconnect data,
       [3] pad packets received, [4] pad packets refused (size / gap rule), [5] stalled calls in a row, [6] messages of other ids, [7] malformed */
    s3_u32 why[8];
};
#define S7W_ENET_DISC 1u             /* the peer sent an ENet DISCONNECT */
#define S7W_ENET_TIMEOUT 2u          /* ENet gave the peer up (no acknowledgements) */
#define S7W_STALL 3u                 /* the rollback-limit stall lasted past the limit (Slippi: 7 s) */
#define S7W_FIRST 4u                 /* the first frame never came */
#define S7W_PADLESS 5u               /* our own peers: link alive, no pads (it left the match) / 30 s */
#define S7W_LEAVING 6u               /* the local player left (party mode) */
#define S7W_SILENT 7u                /* the lobby silence backstop */
void s7_core_why(const struct s3_core *c, s3_u32 *o8);

/* ---- SLIPPI INTEROP: hand-off from the bridge ---- */
#define S7_MSG_SETUP 0xD5            /* ours only (never sent to a Slippi client): [1] S7_SETUP_VER, [2..5] canonical setup digest (u32 BE) */
#define S7_SETUP_VER 1u
#define S7_SETUP_WAIT_MS 8000u       /* both locked in, digests disagree (or the peer's is missing) this long: refuse the match with a message */
#define S7SU_LOCAL 1u                /* s7_setup_info[2] bits: our digest valid */
#define S7SU_PEER 2u                 /* the peer's digest received */
#define S7SU_MATCH 4u                /* equal */
#define S7SU_BAD 8u                  /* refused (setup differs) */
#define S7SU_CAP 0x10u               /* the peer announced the setup check */
#define S7SU_ON 0x20u                /* we run the setup check (our route with negotiated settings) */
#define S7SU_SLIPPI 0x40u            /* Slippi route (no peer digest exists) */
extern volatile s3_u32 s7_setup_info[4];   /* [0] our digest, [1] the peer's (0 none), [2] S7SU_* bits, [3] refused matches since boot (livediag row 7) */
int s7_core_set_rules(struct s3_core *c, const struct s7_rules *r);   /* our route: before s3_core_start (0 ok); NULL forgets them */
s3_u32 s7_setup_digest(const s3_u8 *msrb);                           /* FNV-1a over the 0x138-byte match block (MSRB 598), rngOffset (MSRB 5) and altStageMode (MSRB 961) */
struct s7_slippi_match {
    s3_u32 is_decider;               /* sbr_match.is_host: the decider is player index 0 (Slippi: localPlayerIndex = isDecider ? 0 : 1) */
    s3_u32 opp_ip;                   /* host order */
    s3_u32 opp_port;
    s3_u32 fixed_rules;              /* 1 for Unranked (Slippi IsFixedRulesMode) */
    s3_u32 n_stages;                 /* sbr_match.nstages / stages (the ticket's allowed stages; 0 = the built-in six) */
    s3_u16 stages[16];
    s3_u32 seed;                     /* entropy for the stage and rngOffset draws (any source; never a secret) */
    s3_u32 char_color;               /* local selection: character << 8 | colour (the CSS lock-in normally comes through B5; used when B5 never ran) */
    s3_u32 items;                    /* the ticket's `items` (Slippi m_mmResult.items; 0 = items off) */
};
int s7_core_slippi_match(struct s3_core *c, const struct s7_slippi_match *m);   /* 0 ok, -1 bad */
int s7_core_is_slippi(const struct s3_core *c);                                  /* peer kind: real Slippi client */
/* DELAY_AUTO: the raw RTT of ONE acknowledgement from the ENet smoothing update (rtt += (r - rtt) / 8; var = var - var / 4 + |r - rtt'| / 4, sdenet.c handle_ack):
   old/new smoothed rtt and variance in ms -> r in ms (error at most 3 ms when the smoothed value moved, at most 7 ms when it did not). */
s3_u32 s7_rtt_derive(s3_u32 rtt0, s3_u32 var0, s3_u32 rtt1, s3_u32 var1);
/* A Slippi message as received from ENet peer p (any channel): also the entry point of the host tests (no ENet framing needed). */
void s7_core_rx_msg(struct s3_core *c, int p, const s3_u8 *d, s3_u32 len);
#ifdef MS_QCHAT
/* QUICK CHAT: queue one Slippi chat message (0x84 [s32 id][u8 sender], 6 bytes) for the connected opponent; it goes out reliably on channel 0 with the next s3_core_tick.
   0 = queued (the session is running, or still connecting: it then waits for the connection), -1 = no session or the slot is still full.  An incoming 0x84 is handed to qc_rx (menus/qc/qc_gc.c). */
int s7_core_chat_tx(struct s3_core *c, const s3_u8 *d, s3_u32 len);
void s7_core_chat_cancel(struct s3_core *c);                    /* QUICK CHAT: drop a message that still waits for the connection (the character screen was left) */
#define S7_CHAT_NOW(c) ((s3_u16)((c)->clock_ms((c)->ctx) >> 4))  /* 16 ms ticks, 16 bit */
#define S7_CHAT_HOLD_TICKS 375u                                  /* 6 s: a message queued while the peer is still connecting is sent when it connects, but never later than this */
#endif

void s7_core_init(struct s3_core *c, const struct s3_cfg *cfg, const struct s7_cfg *xcfg, struct s3_status *st,
                  struct s7_status *xs, struct s3_cks_ring *cks, s7_send_fn send, s3_poll_fn poll,
                  s3_clock_fn clock_us, s3_clock_fn clock_ms, void *ctx);
int s3_core_set_mac(struct s3_core *c, const s3_u8 *mac);      /* 0 ok, -1 no peer-table entry */
#ifdef MS_HIL_DEV
int s3_core_set_mac_any(struct s3_core *c);   /* HIL dev image: no peer entry for this chip MAC, side 0, any peer */
#endif
void s7_core_loopback(struct s3_core *c);                       /* S7: enter S7_LOOPBACK (side 0) */
void s3_core_start(struct s3_core *c, s3_u32 now_ms);           /* network up: start connecting */
void s3_core_rx_from(struct s3_core *c, s3_u32 ip, s3_u16 port, const s3_u8 *d, s3_u32 len);   /* one datagram */
void s3_core_tick(struct s3_core *c, s3_u32 now_ms);            /* ENet service, reconnects */
int s3_core_b0(struct s3_core *c, const s3_u8 *txb, s3_u8 *rxb);
/* S12: RXB opponent block of s3_core_b0 as a function (early-rollback check) and the capped newest remote frame */
void s12_core_rxb_inputs(struct s3_core *c, s3_u8 *rxb, s3_s32 frame, int count_neg);
s3_s32 s12_core_eff_latest(const struct s3_core *c);
int s3_core_b3(struct s3_core *c, s3_u8 *msrb);
/* POST-GAME PEER GONE / SESSION END.
   s7_core_silence_ms: ms since the last valid datagram (ENet pings and acks count) from the active peer; 0 when no peer is connected or nothing was ever received.
   s7_core_peer_gone: the peer is treated as gone (silence rule of the glue, or the caller's own decision): best-effort DISCONNECT, state S7_SES_LOST, B0 answers 3.
   s7_core_end_session: back to "no session" (state S3_NET_WAIT, side unknown, selections cleared, every ENet peer dropped after a best-effort DISCONNECT) so that s7_core_set_peer +
   s3_core_start can open a session with ANOTHER peer (or the same one again).  Not called while a match is running (B0 would lose its disconnect verdict). */
/* DELAY-ONLY LOBBIES: the player-count guard.  Rollback (prediction) exists for 2 human seats only; with 3 or 4 the
   engine is a lockstep (effective depth 0: a frame runs only when the real pad of its tag is in) and the glue allocates no snapshot store.
   The seat count comes from the server's ticket (s7_core_set_seats, forgotten by s7_core_end_session) or, as a static lab override, from the cfg word
   s7_cfg.resv[1] (S7X_SEATS, 0 = unset).  Runtime wins over the cfg word; neither set = 2 = today's behaviour, byte for byte.
   set_seats: 0 = back to the default, 1..4 accepted (1 behaves as 2), anything else -1 and nothing changes. */
#define S7_SEATS_MAX 4
#define S7X_SEATS resv[1]
int s7_core_set_seats(struct s3_core *c, int seats);
int s7_core_seats(const struct s3_core *c);                     /* effective: runtime, else cfg word, else 2 (never 0) */
int s7_core_rollback_allowed(const struct s3_core *c);          /* 1 for 2 seats or fewer, 0 for 3 or 4 */
int s7_core_set_peer(struct s3_core *c, int side, s3_u32 ip, s3_u32 port);   /* the peer from the server's ticket (OD): side, destination, allow-list, local selections */
s3_u32 s7_core_silence_ms(const struct s3_core *c, s3_u32 now_ms);
void s7_core_peer_gone(struct s3_core *c);
void s7_core_end_session(struct s3_core *c, s3_u32 now_ms);
/* REJOIN (1267,: the second pairing in one cube session failed, a cube restart cured it).  The session ends because a new ticket names (ip, port): peers at THAT
   address get no DISCONNECT (it would reach the opponent's NEW connection: a restarted console reuses the same ENet session ids) and are dropped silently, except a connection
   from that address that came up in the last S7_REJOIN_KEEP_MS (the new opponent's own CONNECT, faster than our ticket): it is kept and taken over by s3_core_start. */
#define S7_REJOIN_KEEP_MS 5000u
void s7_core_end_session_to(struct s3_core *c, s3_u32 now_ms, s3_u32 ip, s3_u32 port);
int s7_core_b5(struct s3_core *c, const s3_u8 *pstb);           /* S7: SetMatchSelections */
/* S7: CalcTimeOffsetUs over the current ring (0 when empty), exposed for the host tests */
s3_s32 s7_time_offset_us(const struct s3_core *c);
/* n / d for 1 <= d < 65536 with 32-bit divisions only (no libgcc __udivdi3 in the engine); s7_core.c */
unsigned long long s7_udiv64s(unsigned long long n, s3_u32 d);

extern const s3_u32 s3_match_struct[78];

#endif
