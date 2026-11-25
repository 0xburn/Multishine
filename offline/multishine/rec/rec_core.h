/* SPDX-License-Identifier: GPL-3.0-or-later */
/* LOCAL FORK: rec_core, the game recorder's pure half (no console, no libc, no FPU; host-tested by tests/run_tests.sh).
 *
 * Input: the byte stream Slippi's own recording code (Recording/ *.asm, codeset flag ms-record) hands to the EXI device: 0x35 + 0x36 once, one buffer per simulated
 * frame (0x3A .. 0x3C), 0x39 at the end.  Frames are re-simulated by the rollback engine, so a frame arrives once per simulation of it; its bookend carries the latest
 * frame that can no longer change (clamped by Slippi's code to the frame just simulated).  A frame is encoded once, when a bookend finalizes it, from its LAST simulation.
 *
 * Output: the MSR1 stream through `sink`, lossless for those events: the decoder (tests/msr_dec.c, and the website) rebuilds the .slp `raw` element byte for byte.
 *   "MSR1" ver(1) flags(0)  u16 n, the 0x35 event (n bytes)  u16 n, the 0x36 event (n bytes)        all integers big endian
 *   frames from -123 up, one per record:
 *     tag 1  same event list as the previous frame      tag 2  u8 count, then per event: code, for 0x37 / 0x38 the port and follower bytes, for 0x3F the side byte
 *     then each event's body (after the frame number): 0x3B first a byte = index of the previous frame's item with the same spawn id (255 none);
 *     XOR against a prediction, as: bitmap of 8-byte groups that are not zero, one bitmap byte per such group, the non-zero bytes.
 *   tag 3  the 0x39 event (7 bytes), u32 frames, u32 raw bytes, u32 CRC-32 (zlib) of the raw bytes 0x35 .. 0x39
 * Predictions (P = the previous record of the same key, PP the one before; u32 arithmetic on the bit patterns):
 *   0x3A  seed 2P-PP, scene frame P+1     0x37  seed = this frame's 0x3A, state / x / y / facing / percent = the key's last 0x38, rest P
 *   0x38  P; state frame, x, y, self x / y and ground speed 2P-PP      0x3B  P; x speed, x, y, timer 2P-PP      0x3C  finalized P+1 (first: the frame)
 * Wind / transformation events (0x40, 0x41) are produced every frame by our variants (Slippi's code keeps their last value outside the rollback) and kept here only when
 * the finalized value changes: Slippi's own rule on the finalized frames. */
#ifndef REC_CORE_H
#define REC_CORE_H

typedef unsigned int rec_u32;
typedef int rec_s32;
typedef unsigned short rec_u16;
typedef unsigned char rec_u8;

#define REC_RING 16u                     /* simulated frames kept: must exceed the deepest unfinalized span (rollback 7 + lag) */
#define REC_FB 1360u                     /* bytes of one frame: two Ice Climbers pairs (268 + 372) + 15 items (675) + platforms (20) + start and bookend (22) = 1357 */
#define REC_FIRST (-123)
#define REC_KEYS 8u                      /* port * 2 + follower */
#define REC_ITEMS 15u
#define REC_EV 93u                       /* largest event */
#define REC_MAXEV 48u

enum { REC_OFF = 0, REC_WAIT, REC_RUN, REC_DONE, REC_DROPPED };
/* why a recording was dropped */
enum { REC_D_NONE = 0, REC_D_HEADER, REC_D_EVENT, REC_D_SIZE, REC_D_ORDER, REC_D_DEPTH, REC_D_SINK, REC_D_ABORT, REC_D_NOEND, REC_D_TIME };

struct rec_slot { rec_s32 frame; rec_u32 len; rec_u8 d[REC_FB]; };
struct rec_hist { rec_u8 p[REC_EV], pp[REC_EV]; rec_u8 n; };          /* n = how many are valid (0..2) */
struct rec {
    int (*sink)(void *ctx, const rec_u8 *b, rec_u32 n);               /* 0 ok; nonzero = no room: the recording is dropped */
    void *ctx;
    int state, why;
    rec_s32 newest, final;                                            /* newest simulated, last encoded frame */
    rec_u32 raw, crc, out;                                            /* raw bytes / their CRC state / MSR bytes so far */
    rec_u32 frames, resim, refinal_n, refinal_diff, diff_at, maxdepth;   /* statistics */
    rec_s32 diff_f;
    rec_u8 h35[64], h36[832];
    rec_u32 n35, n36;
    rec_u32 wind, trans;
    rec_u32 nshape;
    rec_u8 shape[REC_MAXEV * 3];
    struct rec_hist start, book, pre[REC_KEYS], post[REC_KEYS];
    struct rec_hist wl[2], ds, ps;                                    /* platform heights (by side), wind, transformation */
    struct rec_hist it[2][REC_ITEMS];
    rec_u32 itn[2], iti;
    rec_u8 seed[4];
    rec_u32 crct[256];
    struct rec_slot ring[REC_RING];
};

void rec_init(struct rec *r, int (*sink)(void *, const rec_u8 *, rec_u32), void *ctx);
/* one EXI write of the recording stream */
void rec_feed(struct rec *r, const rec_u8 *p, rec_u32 n);
void rec_drop(struct rec *r, int why);
rec_u32 rec_crc(const rec_u32 *t, rec_u32 c, const rec_u8 *p, rec_u32 n);
void rec_crc_table(rec_u32 *t);
#endif
