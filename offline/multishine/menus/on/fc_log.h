/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* FC: FRAME COST of a netplay match (test builds only: -DMM_FC=1, RELEASE_TEST=fc).
   Question: the Wii's PowerPC runs 1.5 x the GameCube's under Nintendont, yet the Wii is the slow side (38-45 % of its frames over 17 ms in the game records, the cube waits
   for it at match start, ARM main-loop passes of 20 ms to 3 s in the kernel log).  Where does a Wii frame's time go?
   The engine (s12_gc.c, MM_FC) splits every scene-loop pass (one per game frame) into exclusive time categories with a few time-base reads at hooks it already owns:
   a checkpoint charges the ticks since the previous checkpoint to the category that was running and switches to the next one.  A nested region (the B0 pad hook
   from the VI interrupt, an EXI transaction inside it) charges what ran before it and restores the outer category afterwards, so the categories never overlap.
   This file is the pure part (no game memory, host-tested: menus/tests/fc_log_test.c with ): the per-match accumulators, the conversion into
   FC_W words and the hex lines that the LAB_DIAG upload sends (7 datagrams of 3 lines at most). */
#ifndef FC_LOG_H
#define FC_LOG_H

/* time categories (exclusive; their sum is the pass period) */
#define FC_LOGIC  0u     /* game logic of a NEW frame: the engine iteration body without our hooks (s6n_iter exit .. s6n_end entry) */
#define FC_REPLAY 1u     /* the same for a re-simulated (rollback) frame */
#define FC_CAP    2u     /* B1 capture (snapshot store) */
#define FC_REST   3u     /* B2 restore (base copy + undo steps) */
#define FC_B0     4u     /* our B0 pad hook (be_inputs: network poll, core, queue), EXI time not included */
#define FC_HOOK   5u     /* our other hooks: loop-top tick body (match store, network slice logic), iteration hash, end hook, B3 / B5, sdshim; EXI time not included */
#define FC_NTX    6u     /* inside EXI transactions of the network, send context (sdnet_sendto); on a Wii this is the wait for the Nintendont ARM loop, on a GameCube the BBA */
#define FC_NRX    7u     /* inside EXI transactions of the network, receive / poll context */
#define FC_IDLE   8u     /* loop top .. first engine work (the VI / pad wait) in a pass that ran a new frame without a stalled B0 call */
#define FC_STALL  9u     /* the same wait in a pass whose B0 stalled (the peer's pads were missing) or that ran no new frame */
#define FC_ENG    10u    /* inside the engine loop between iterations (Slippi's StartEngineLoop ASM, camera tasks) */
#define FC_REND   11u    /* after the last iteration to the next loop top: render, audio, the rest of the scene loop */
#define FC_NIDLE  12u    /* version 2: inside EXI transactions of s12_idle's network poll in the VI wait that delivered nothing (a poll that delivered a frame is FC_NRX work) */
#define FC_NC     13u
#define FC_NCW    12u    /* categories in the version-1 word slots 13..84; FC_NIDLE has its own words 115..117 */
#define FC_NP     8u     /* period buckets: < 16, 16-17, 17-18, 18-20, 20-25, 25-33, 33-50, >= 50 ms */
#define FC_NH     6u     /* per-category histogram of the per-frame time: < 0.5, 0.5-2, 2-5, 5-10, 10-16.7, >= 16.7 ms */
#define FC_NB     6u     /* main-thread block (>= 50 ms between two game-thread checkpoints) buckets */
#define FC_B_NET   0u    /*   at least half of it inside network EXI transactions */
#define FC_B_CARD  1u    /*   the memory card was busy (the SDK's EXI lock held by a card, or the Wii kernel's card command counter moved) */
#define FC_B_DISC  2u    /*   a disc read was running (the DVD library's executing block, or our Stadium file wait) */
#define FC_B_STALL 3u    /*   a B0 call stalled (waiting for the peer's pads) */
#define FC_B_RB    4u    /*   at least half of it restore / capture, or a replayed frame */
#define FC_B_OTHER 5u
#define FC_PH0    300u   /* the first 300 match frames are phase 0, the rest phase 1 */
#define FC_MAGIC  0x46430300u   /* 'F' 'C' version 3 (117 = the PV buffer address, 123 / 124 the bad answer; version 2 = 124 words; version 1 = 116), platform in the low byte */
#define FC_W      126u
#define FC_LINES  ((FC_W + 5u) / 6u)    /* 21 lines = 7 datagrams of three lines (the Wii's upload stops after 8) */

/* word layout:
   0 magic | platform (1 GameCube / Dolphin, 2 Wii, 3 Wii U)     1 match number << 16 | stage id     2 passes of phase 0 | phase 1 << 16
   3 last match frame (16) | ODB delay frames << 16 | 1 << 24 game-over flag seen | 1 << 25 Wii card counter present | 1 << 26 Wii kernel status block present
   4 time-base ticks per ms
   5..12   period histogram, bucket k: phase 0 | phase 1 << 16
   13..36  per category c: microseconds of phase 0 (13 + 2c), phase 1 (14 + 2c)
   37..48  per category c: the longest per-frame value, 0.1 ms units, phase 0 | phase 1 << 16
   49..84  per category c: the per-frame histogram (both phases), buckets 2j | 2j + 1 << 16 in word 49 + 3c + j
   85 / 86 network EXI transactions of phase 0 / phase 1        87 most transactions in one pass, phase 0 | phase 1 << 16
   88 longest single transaction us, phase 0 | phase 1 << 16     89 transactions in send context (whole match)
   90 slices skipped for a card transfer (Wii) | EXI lock refusals << 16     91 B0 calls that saw the card busy | the disc busy << 16
   92 card-side EXI commands served by the Wii kernel in the match (kernel word 0xD3026698; 0 on a GameCube)
   93 restores (B2) phase 0 | phase 1 << 16    94 replayed iterations    95 captures (B1 calls)    96 stalled B0 calls
   97 blocks >= 50 ms | of them in phase 0 << 16     98 their total ms     99 per-bucket counts 0..3 (bytes)     100 counts 4..5 (bytes) | the longest block ms << 16
   101..103 per-bucket ms, bucket 2j | 2j + 1 << 16
   104..109 the three longest blocks: match frame, then ms | bucket << 16 | category that was running << 20
   110 Wii sendto: average us | completions >= 50 ms << 16      111 ARM passes >= 5 ms | >= 20 ms << 16      112 longest ARM pass ms since boot | the IO flags << 16
   113 checkpoints (time-base reads of the measurement) in the match     114 microseconds spent in the per-pass bookkeeping
   version 2: 115 / 116 FC_NIDLE microseconds phase 0 / 1     117 FC_NIDLE longest per-frame value, 0.1 ms, phase 0 | phase 1 << 16 (version 3: the engine's PV buffer address)
   118 / 119 network EXI transactions of the idle polls (part of 85 / 86), phase 0 / 1     120 idle polls run in the VI wait (whole match)
   121 network path at the match end: bits 0-3 0 register / 1 wii_fast / 2 PV, bits 4-7 PV device probe 0 not tried / 1 refused / 2 answered, bits 8-11 why PV was left
       (1 send refused, 2 receive refused, 3 answer without the 'PV' header, 4 long frame), bits 16-23 / 24-31 the first two bytes of that answer
   122 PV datagrams sent | received << 16 since the bring-up (saturating)
   version 3: 120 with bit 31 set (1260+) = the LAB_DIAG upload state at the match end: datagrams sent (12 bits) | refusals << 12 (8) | readouts finished << 20 (4) | pass << 24 | line << 25
   version 3: 123 / 124 the first 8 bytes of the first PV answer without the 'PV' header (byte 0 in bits 31..24)     125 checksum: sum of 0..124 xor 0x5A5A5A5A */

struct fc_live {
    unsigned long long sum[2][FC_NC];          /* ticks per phase and category */
    unsigned mx[2][FC_NC];                     /* longest per-frame value, ticks */
    unsigned short hist[FC_NC][FC_NH];
    unsigned short per[2][FC_NP];
    unsigned passes[2], ops[2], opmax[2], oplong[2], txops, iops[2], ipolls;
    unsigned path, pvc, pvbuf, pvd[2], ldst;         /* set by the engine before fc_words: words 121 / 122, 117 (version 3), 123 / 124 */
    unsigned rb[2], rep[2], cap[2], stall[2];
    unsigned skip_card, lockfail, card_s, dvd_s, card_cmd;
    unsigned bn, b0n, btot, blong, bcnt[FC_NB], bms[FC_NB];
    unsigned top[3][2];                        /* frame, ms | bucket << 16 | category << 20 */
    unsigned cps, self;
    unsigned th_p[FC_NP - 1u], th_h[FC_NH - 1u], th_b, mt;
};

static __inline__ void fc_init(struct fc_live *L, unsigned mt)
{
    static const unsigned char pms[FC_NP - 1u] = { 16, 17, 18, 20, 25, 33, 50 };
    unsigned char *p = (unsigned char *)L;
    unsigned i;
    for (i = 0; i < sizeof *L; i++) p[i] = 0;
    if (!mt) mt = 40500u;
    L->mt = mt;
    for (i = 0; i < FC_NP - 1u; i++) L->th_p[i] = pms[i] * mt;
    L->th_h[0] = mt / 2u; L->th_h[1] = 2u * mt; L->th_h[2] = 5u * mt; L->th_h[3] = 10u * mt; L->th_h[4] = mt * 50u / 3u;
    L->th_b = 50u * mt;
}
static __inline__ unsigned fc_bucket(const unsigned *th, unsigned n, unsigned v) { unsigned k = 0; while (k < n && v >= th[k]) k++; return k; }
static __inline__ void fc_inc16(unsigned short *c) { if (*c != 0xFFFFu) (*c)++; }
/* one scene-loop pass: acc = ticks per category, period = ticks since the previous pass start (0 = the first pass), ops / oplong = network EXI transactions and the longest,
   iops = the transactions of them that were idle polls (FC_NIDLE) */
static __inline__ void fc_pass(struct fc_live *L, unsigned ph, const unsigned *acc, unsigned period, unsigned ops, unsigned oplong, unsigned iops)
{
    unsigned c, v;
    ph = ph ? 1u : 0u;
    L->passes[ph]++;
    if (period) fc_inc16(&L->per[ph][fc_bucket(L->th_p, FC_NP - 1u, period)]);
    for (c = 0; c < FC_NC; c++) {
        v = acc[c];
        L->sum[ph][c] += v;
        if (v > L->mx[ph][c]) L->mx[ph][c] = v;
        fc_inc16(&L->hist[c][fc_bucket(L->th_h, FC_NH - 1u, v)]);
    }
    L->ops[ph] += ops;
    L->iops[ph] += iops;
    if (ops > L->opmax[ph]) L->opmax[ph] = ops;
    if (oplong > L->oplong[ph]) L->oplong[ph] = oplong;
}
/* one main-thread block (ticks >= th_b) at match frame f */
static __inline__ void fc_block(struct fc_live *L, unsigned ph, unsigned f, unsigned ticks, unsigned bucket, unsigned cat)
{
    unsigned ms = ticks / L->mt, k, j, pk;
    if (ms > 0xFFFFu) ms = 0xFFFFu;
    if (bucket >= FC_NB) bucket = FC_B_OTHER;
    L->bn++; if (!ph) L->b0n++;
    L->btot += ms; if (ms > L->blong) L->blong = ms;
    if (L->bcnt[bucket] < 255u) L->bcnt[bucket]++;
    L->bms[bucket] += ms;
    pk = ms | (bucket << 16) | ((cat & 15u) << 20);
    for (k = 0; k < 3u; k++) if (!L->top[k][1] || ms > (L->top[k][1] & 0xFFFFu)) break;
    if (k < 3u) {
        for (j = 2u; j > k; j--) { L->top[j][0] = L->top[j - 1u][0]; L->top[j][1] = L->top[j - 1u][1]; }
        L->top[k][0] = f; L->top[k][1] = pk;
    }
}
static __inline__ unsigned fc_s16(unsigned v) { return v > 0xFFFFu ? 0xFFFFu : v; }
static __inline__ unsigned fc_pair(unsigned a, unsigned b) { return fc_s16(a) | (fc_s16(b) << 16); }
/* the readout words; hdr = { platform, match number, stage, last frame, delay, flags (bits 24..26 of word 3), io avg sendto us, io >= 50 ms, ARM >= 5 ms, ARM >= 20 ms, ARM longest ms, io flags } */
static __inline__ void fc_words(const struct fc_live *L, unsigned *w, const unsigned *hdr)
{
    unsigned c, k, mt = L->mt, t = mt / 10u, s = 0;
    for (k = 0; k < FC_W; k++) w[k] = 0;
    w[0] = FC_MAGIC | (hdr[0] & 0xFFu);
    w[1] = (hdr[1] << 16) | (hdr[2] & 0xFFFFu);
    w[2] = fc_pair(L->passes[0], L->passes[1]);
    w[3] = fc_s16(hdr[3]) | ((hdr[4] & 0xFFu) << 16) | (hdr[5] & 0x07000000u);
    w[4] = mt;
    for (k = 0; k < FC_NP; k++) w[5 + k] = L->per[0][k] | ((unsigned)L->per[1][k] << 16);
    for (c = 0; c < FC_NCW; c++) {
        w[13 + 2 * c] = (unsigned)(L->sum[0][c] * 1000u / mt);
        w[14 + 2 * c] = (unsigned)(L->sum[1][c] * 1000u / mt);
        w[37 + c] = fc_pair(L->mx[0][c] / t, L->mx[1][c] / t);
        for (k = 0; k < 3u; k++) w[49 + 3 * c + k] = L->hist[c][2 * k] | ((unsigned)L->hist[c][2 * k + 1] << 16);
    }
    w[85] = L->ops[0]; w[86] = L->ops[1];
    w[87] = fc_pair(L->opmax[0], L->opmax[1]);
    w[88] = fc_pair((unsigned)((unsigned long long)L->oplong[0] * 1000u / mt), (unsigned)((unsigned long long)L->oplong[1] * 1000u / mt));
    w[89] = L->txops;
    w[90] = fc_pair(L->skip_card, L->lockfail);
    w[91] = fc_pair(L->card_s, L->dvd_s);
    w[92] = L->card_cmd;
    w[93] = fc_pair(L->rb[0], L->rb[1]); w[94] = fc_pair(L->rep[0], L->rep[1]); w[95] = fc_pair(L->cap[0], L->cap[1]); w[96] = fc_pair(L->stall[0], L->stall[1]);
    w[97] = fc_pair(L->bn, L->b0n); w[98] = L->btot;
    w[99] = L->bcnt[0] | (L->bcnt[1] << 8) | (L->bcnt[2] << 16) | (L->bcnt[3] << 24);
    w[100] = L->bcnt[4] | (L->bcnt[5] << 8) | (fc_s16(L->blong) << 16);
    for (k = 0; k < 3u; k++) w[101 + k] = fc_pair(L->bms[2 * k], L->bms[2 * k + 1]);
    for (k = 0; k < 3u; k++) { w[104 + 2 * k] = L->top[k][0]; w[105 + 2 * k] = L->top[k][1]; }
    w[110] = fc_pair(hdr[6], hdr[7]); w[111] = fc_pair(hdr[8], hdr[9]); w[112] = fc_s16(hdr[10]) | ((hdr[11] & 0xFFu) << 16);
    w[113] = L->cps; w[114] = (unsigned)((unsigned long long)L->self * 1000u / mt);
    w[115] = (unsigned)(L->sum[0][FC_NIDLE] * 1000u / mt); w[116] = (unsigned)(L->sum[1][FC_NIDLE] * 1000u / mt);
    w[117] = L->pvbuf;
    w[118] = L->iops[0]; w[119] = L->iops[1]; w[120] = L->ldst ? L->ldst | 0x80000000u : L->ipolls;
    w[121] = L->path; w[122] = L->pvc; w[123] = L->pvd[0]; w[124] = L->pvd[1];
    for (k = 0; k < FC_W - 1u; k++) s += w[k];
    w[FC_W - 1u] = s ^ 0x5A5A5A5Au;
}
/* the words as hex text, six per line ("%08X %08X ..." = 53 characters), FC_LINES lines; words past FC_W are 0 */
static __inline__ void fc_lines(char (*l)[56], const unsigned *w)
{
    unsigned i, j, k, d, v;
    for (i = 0; i < FC_LINES; i++) {
        char *p = l[i];
        for (j = 0; j < 6u; j++) {
            k = i * 6u + j;
            v = k < FC_W ? w[k] : 0u;
            if (j) *p++ = ' ';
            for (d = 8u; d > 0; d--) { unsigned x = (v >> (4u * (d - 1u))) & 15u; *p++ = (char)(x < 10u ? '0' + x : 'A' + x - 10u); }
        }
        *p = 0;
    }
}

#endif
