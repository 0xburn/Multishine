/* SPDX-License-Identifier: GPL-3.0-or-later */
/* LOCAL FORK: rec_gc, the game recorder's console half.  #included at the end of s12_gc.c (needs its accessors).
 *
 * What it does in a Slippi match (Unranked / Direct through the bridge) on a console with a Lucky Stats key on the card:
 *   - arms Slippi's recording code (ShouldRecord's flag, codeset flag ms-record) while the online scene is loading, so the stream starts with the game info (0x35 / 0x36);
 *   - ms_rec_feed (sdshim, one call per EXI write, interrupts off) runs rec_core: the finalized frames come out as the MSR1 stream, gathered in a 4 KB stage buffer;
 *   - a full stage buffer goes to ARAM with one low-priority ARQ request (no wait: two buffers alternate), into ONE block of the preload cache's ARAM heap (lbHeap 5 'AllA',
 *     lbMemory allocator) taken after the match has loaded: the heap holds four players' animation files, a two-player match leaves megabytes free, and nothing allocates from it in a match.
 * Nothing here is inside the rollback snapshot: struct rec is an HSD cell above the 0x2400-byte snapshot limit, the rest is image data, the stream lives in ARAM.  Nothing is read by the game.
 * Every failure (no room, an unexpected event, a frame that never finalizes, a DMA not done in time, no game end) drops this game's recording and nothing else.
 *
 * After the match the recording stays in ARAM.  Each 4 KB piece is checked against a CRC-32 taken in the match when it is read back, so a block that the preload cache moved (compaction,
 * the address is looked up every time), or that was overwritten after the online scene ended, is noticed: never uploaded damaged. */
#include "rec_core.h"
#include "rec_gc.h"
#include "rec_core.c"

#define RC_POOL_WANT 0x280000u           /* 2.5 MB: five to six three-minute games, or one eight-minute game with items */
#define RC_POOL_MIN 0x40000u
#define RC_POOL_KEEP 0x180000u           /* free bytes left in the heap after the pool */
#define RC_NCHUNK (RC_POOL_WANT / REC_CHUNK)
#define RC_N 6u                          /* finished recordings waiting */
#define RC_HSD_KEEP 0x160000u            /* never taken from a heap that would have less than this left */
#define RC_HEAP5 LB_HEAP(5)
#define RC_PRELOAD_BUSY (W(0x80432078u + 0x96Cu) == 5u)   /* preloadCache.persistent_heap: lbDvd is compacting heap 5 */
#define RC_ARQ ((void (*)(void *, u32, u32, u32, u32, u32, u32, void (*)(u32)))0x80352114u)   /* ARQPostRequest(req, owner, type, prio, src, dst, len, cb) */
#define RC_DCFLUSH ((void (*)(void *, u32))0x8034480Cu)
#define RC_DCINVAL ((void (*)(void *, u32))0x803447DCu)

/* RAM proof words: [0] 'RECS' [1] state [2] why dropped [3] games begun [4] done [5] dropped [6] MSR bytes (last / running) [7] frames [8] re-simulated frames [9] deepest unfinalized span
   [10] finalized frames that ran again [11] .. and differed [12] first differing byte + 1 [13] its frame [14] pool ARAM address [15] pool bytes [16] heap 5 free before the pool [17] largest gap
   [18] HSD free before struct rec [19] feed ticks sum [20] feed ticks max [21] feeds [22] chunks written [23] DMA busy drops [24] pending recordings [25] reads [26] read CRC failures
   [27] uploaded [28] forgotten [29] arm flag [30] ticks sum of feeds that only buffered (re-simulated) [31] their count */
volatile u32 rec_st[40] __attribute__((section(".data"))) = { 0x52454353u };
#define RS(i) (rec_st[i])
extern int rep_rec_wanted(void) __attribute__((weak));            /* rep_res.c: a Lucky Stats key is on the card */

static u8 rc_stage[2][REC_CHUNK] __attribute__((aligned(32)));
static u32 rc_req[2][8] __attribute__((aligned(32)));
static volatile u32 rc_busy[2];
static u32 rc_crc[RC_NCHUNK];
static struct rec_pub rc_q[RC_N];
static struct rec *rc_r;                                          /* in the HSD heap of the running match, 0 outside */
static u32 rc_blk_h, rc_lo, rc_size;                             /* pool: its heap handle, last known ARAM address, size */
static u32 rc_wr, rc_start, rc_fill, rc_si, rc_t0, rc_seq, rc_cur_chunks;
static u32 rc_rd_seq, rc_rd_off, rc_rd_state, rc_rd_bad;          /* reader: 0 idle, 1 in flight */
static u32 rc_tab[256];

static void rc_cb0(u32 r) { (void)r; rc_busy[0] = 0; }
static void rc_cb1(u32 r) { (void)r; rc_busy[1] = 0; }
static void rc_post(u32 i, u32 to_aram, u32 aram, u32 n)
{
    u32 a = (u32)rc_stage[i];
    n = (n + 31u) & ~31u;
    if (to_aram) RC_DCFLUSH(rc_stage[i], n); else RC_DCINVAL(rc_stage[i], n);
    rc_busy[i] = 1;
    RC_ARQ(rc_req[i], 0x52454321u, to_aram ? 0u : 1u, 0u, to_aram ? a : aram, to_aram ? aram : a, n, i ? rc_cb1 : rc_cb0);
}
static int rc_room(u32 *total, u32 *big)                          /* free gaps of heap 5 (as allm_room) */
{
    u32 d = RC_HEAP5, h, cur, it, n = 0, t = 0, b = 0;
    *total = *big = 0;
    if (W(d + 0x18u) != 0u || W(d + 0x10u) != 4u) return 0;
    h = W(d + 4u);
    if (!ptr_like(h) || !LBMEM_FREE_HEAD) return 0;
    cur = W(h + 4u); it = W(h + 0xCu);
    for (;;) {
        u32 end = ptr_like(it) ? W(it + 4u) : W(h + 8u), gap = end > cur ? end - cur : 0u;
        t += gap; if (gap > b) b = gap;
        if (!ptr_like(it)) break;
        cur = W(it + 4u) + W(it + 8u);
        it = W(it);
        if (++n > 4096u) return 0;
    }
    *total = t; *big = b;
    return 1;
}
static u32 rc_blk;                                                /* the lbMemory block (Handle): {next, lo, size} */
static int rc_pool_live(void)                                     /* the heap still lists our block; refreshes the address (compaction moves it) */
{
    u32 d = RC_HEAP5, it, n = 0;
    if (!rc_blk) return 0;
    if (W(d + 0x18u) == 0u && W(d + 4u) == rc_blk_h && ptr_like(rc_blk_h))
        for (it = W(rc_blk_h + 0xCu); ptr_like(it) && n++ < 4096u; it = W(it))
            if (it == rc_blk) { if (W(it + 8u) != rc_size) break; rc_lo = W(it + 4u); RS(14) = rc_lo; return 1; }
    rc_blk = 0;                                                   /* the heap was rebuilt: the data may still be there (the reader checks), the block is not ours any more */
    return 0;
}
static u32 rc_pending(void) { u32 i, n = 0; for (i = 0; i < RC_N; i++) n += rc_q[i].used; return n; }
static void rc_clear_q(void) { u32 i; for (i = 0; i < RC_N; i++) { if (rc_q[i].used) RS(28)++; rc_q[i].used = 0; } rc_wr = rc_start = rc_cur_chunks = 0; rc_rd_state = 0; RS(24) = 0; }   /* only before anything of the running game is in the block */
static int rc_chunk_used(u32 pos)                                 /* circular pool: 1 if chunk at `pos` belongs to a waiting recording or the current one wrapped full */
{
    u32 i;
    if (rc_cur_chunks * REC_CHUNK >= rc_size) return 1;
    for (i = 0; i < RC_N; i++) {
        u32 alen, d;
        if (!rc_q[i].used) continue;
        alen = (rc_q[i].len + REC_CHUNK - 1u) & ~(REC_CHUNK - 1u);
        d = pos >= rc_q[i].off ? pos - rc_q[i].off : rc_size - rc_q[i].off + pos;
        if (d < alen) return 1;
    }
    return 0;
}
static int rc_pool_get(void)
{
    u32 t, b, n, blk;
    if (rc_pool_live()) return 1;
    if (LOADING != 0 || RC_PRELOAD_BUSY || !rc_room(&t, &b)) return 0;
    RS(16) = t; RS(17) = b;
    n = t > RC_POOL_KEEP ? t - RC_POOL_KEEP : 0u;
    if (n > b) n = b;
    if (n > RC_POOL_WANT) n = RC_POOL_WANT;
    n &= ~(REC_CHUNK - 1u);
    if (n < RC_POOL_MIN) return 0;
    rc_clear_q();                                                 /* recordings of an older block: its data is not protected any more and the piece checksums are reused */
    blk = LBMEM_ALLOC(W(RC_HEAP5 + 4u), n);
    if (!ptr_like(blk)) return 0;
    rc_blk = blk; rc_blk_h = W(RC_HEAP5 + 4u); rc_lo = W(blk + 4u); rc_size = n;
    RS(14) = rc_lo; RS(15) = n;
    return 1;
}
static int rc_flush(u32 n)                                        /* stage buffer rc_si, n bytes, to the pool */
{
    u32 c;
    if (!rc_pool_get()) return -1;                                /* may start a new block (rc_wr 0): the index comes after it */
    if (rc_wr >= rc_size) rc_wr = 0;
    c = rc_wr / REC_CHUNK;
    if (c >= RC_NCHUNK || rc_chunk_used(rc_wr)) return -1;
    rc_crc[c] = rec_crc(rc_tab, 0xFFFFFFFFu, rc_stage[rc_si], n);
    rc_post(rc_si, 1u, rc_lo + rc_wr, n);
    rc_wr += REC_CHUNK;
    if (rc_wr >= rc_size) rc_wr = 0;
    rc_cur_chunks++; RS(22)++;
    rc_si ^= 1u; rc_fill = 0;
    return 0;
}
static int rc_sink(void *ctx, const u8 *b, u32 n)
{
    (void)ctx;
    while (n) {
        u32 k = REC_CHUNK - rc_fill;
        if (k > n) k = n;
        if (!rc_fill && rc_busy[rc_si]) { RS(23)++; return -1; }  /* the buffer written ~100 frames ago is still in the queue: the disc or the audio holds ARAM; never wait */
        cpy(rc_stage[rc_si] + rc_fill, b, k);
        rc_fill += k; b += k; n -= k;
        if (rc_fill == REC_CHUNK && rc_flush(REC_CHUNK)) return -1;
    }
    return 0;
}
static void rc_end(int ok)
{
    u32 i;
    struct rec *r = rc_r;
    if (!r) return;
    RS(1) = (u32)r->state; RS(2) = (u32)r->why; RS(6) = r->out; RS(7) = r->frames; RS(8) = r->resim; RS(9) = r->maxdepth;
    RS(10) = r->refinal_n; RS(11) = r->refinal_diff; RS(12) = r->diff_at; RS(13) = (u32)r->diff_f;
    if (ok && r->state == REC_DONE && !(rc_fill && rc_flush(rc_fill))) {
        for (i = 0; i < RC_N && rc_q[i].used; i++) {}
        if (i < RC_N) {
            rc_q[i].used = 1; rc_q[i].seq = ++rc_seq; rc_q[i].off = rc_start; rc_q[i].len = r->out; rc_q[i].t_ms = rc_t0; rc_q[i].frames = r->frames; rc_q[i].raw = r->raw; rc_q[i].sent = rc_q[i].sha_ok = rc_q[i].fails = 0;
            RS(4)++; RS(24) = rc_pending();
            rc_cur_chunks = 0;
            rc_r = 0;
            return;
        }
    }
    RS(5)++;
    rc_wr = rc_start;                                             /* this game's pieces are free again */
    rc_cur_chunks = 0;
    rc_r = 0;
}
static u32 rc_flag_at(void)                                       /* ShouldRecord's flag word (0 = the module is not in this image) */
{
    u32 w = W(0x80005604u), a;
    s32 d;
    if ((w >> 26) != 18u || (w & 3u)) return 0;
    d = (s32)(w << 6) >> 6;
    a = 0x80005604u + (u32)(d & ~3);
    if (!ptr_like(a) || W(a + 12u) != 0x4D535243u) return 0;
    return a + 8u;
}
void rec_tick(void)                                               /* every scene-loop pass, interrupts on */
{
    u32 a = rc_flag_at(), sc = SCENE_WORD, want, m;
    if (!a) return;
    m = irq_off();
    want = (sc >> 24) == 8u && ms_rules.route == MR_ROUTE_SLIPPI_BRIDGE && rep_rec_wanted && rep_rec_wanted();
    if ((sc >> 24) == 8u && (sc & 0xFFu) == 2u) { if (!W(a)) want = 0; }        /* never switched on inside a match (the buffers are made at its start) */
    else if (rc_r) { rc_r = 0; rc_wr = rc_start; rc_cur_chunks = 0; RS(1) = REC_DROPPED; RS(2) = REC_D_NOEND; RS(5)++; }   /* the scene and its heap (struct rec) are gone: never touched again; a finished recording was queued at its end */
    W(a) = want ? 1u : 0u;
    RS(29) = W(a);
    if ((sc >> 24) != 8u && rc_blk && rc_pool_live()) { LBMEM_FREE(rc_blk_h, rc_lo); rc_blk = 0; }   /* another game mode may need the whole heap: the block goes back, what is in it is read while it lasts */
    irq_restore(m);
}
void sdshim_record_hook(const u8 *p, u32 n)                       /* sdshim.c, interrupts off */
{
    u32 t0 = tbl(), dt, rs;
    if (!n) return;
    if (p[0] == 0x35) {
        struct rec *r;
        u32 t, b;
        if (rc_r) { rc_r = 0; rc_wr = rc_start; rc_cur_chunks = 0; RS(5)++; RS(2) = REC_D_NOEND; }
        hsd_room(&t, &b);
        RS(18) = t;
        RS(3)++;
        r = t > RC_HSD_KEEP + (u32)sizeof(struct rec) ? (struct rec *)safe_alloc((u32)sizeof(struct rec), RC_HSD_KEEP) : 0;
        if (!r) { RS(5)++; RS(2) = 100u; return; }
        if (!rc_tab[1]) rec_crc_table(rc_tab);
        rec_init(r, rc_sink, 0);
        rc_r = r;
        if (!rc_pending()) rc_wr = 0;                             /* everything before is uploaded: the pool is empty */
        else if (rc_size && rc_wr >= rc_size) rc_wr = 0;
        rc_start = rc_wr; rc_fill = 0; rc_cur_chunks = 0; rc_t0 = clock_ms(0);
        rc_rd_state = 0;
        RS(19) = RS(20) = RS(21) = RS(30) = RS(31) = 0;
    }
    if (!rc_r) return;
    rs = rc_r->resim;
    rec_feed(rc_r, p, n);
    dt = tbl() - t0;
    if (rc_r->resim != rs) { RS(30) += dt; RS(31)++; }
    else { RS(19) += dt; RS(21)++; if (dt > RS(20) && p[0] != 0x35) RS(20) = dt; }
    RS(1) = (u32)rc_r->state; RS(6) = rc_r->out; RS(7) = rc_r->frames;
    if (rc_r->state == REC_DONE) rc_end(1);
    else if (rc_r->state == REC_DROPPED) rc_end(0);
}

/* ---- the upload side (menus) ---- */
static int rc_oldest(void)
{
    int i, b = -1;
    for (i = 0; i < (int)RC_N; i++) if (rc_q[i].used && (b < 0 || rc_q[i].seq < rc_q[b].seq)) b = i;
    return b;
}
struct rec_pub *ms_rec_pending(void)
{
    int i = rc_oldest();
    return i < 0 || rc_r ? 0 : &rc_q[i];
}
int ms_rec_read(u32 seq, u32 off, u8 *buf)
{
    int i = rc_oldest();
    u32 n, m, c, aoff;
    if (i < 0 || rc_q[i].seq != seq || rc_r || off >= rc_q[i].len || (off & (REC_CHUNK - 1u))) return -1;
    n = rc_q[i].len - off; if (n > REC_CHUNK) n = REC_CHUNK;
    aoff = rc_q[i].off + off;
    if (rc_size && aoff >= rc_size) aoff -= rc_size;
    if (RC_PRELOAD_BUSY) { rc_rd_state = 0; return 0; }           /* the block may be moving */
    if (rc_rd_state && (rc_rd_seq != seq || rc_rd_off != off)) { if (rc_busy[0]) return 0; rc_rd_state = 0; }
    if (!rc_rd_state) {
        if (rc_busy[0]) return 0;
        m = irq_off();
        (void)rc_pool_live();
        rc_rd_seq = seq; rc_rd_off = off; rc_rd_state = 1; RS(25)++;
        rc_post(0u, 0u, rc_lo + aoff, n);
        irq_restore(m);
        return 0;
    }
    if (rc_busy[0]) return 0;
    rc_rd_state = 0;
    c = aoff / REC_CHUNK;
    RC_DCINVAL(rc_stage[0], (n + 31u) & ~31u);
    if (rec_crc(rc_tab, 0xFFFFFFFFu, rc_stage[0], n) != rc_crc[c]) {
        RS(26)++;
        if (++rc_rd_bad < 30u) return 0;                          /* half a second: a move may have been under way */
        rc_rd_bad = 0;
        return -1;
    }
    rc_rd_bad = 0;
    cpy(buf, rc_stage[0], n);
    return (int)n;
}
void ms_rec_forget(u32 seq, int why)
{
    u32 i;
    for (i = 0; i < RC_N; i++) if (rc_q[i].used && rc_q[i].seq == seq) { rc_q[i].used = 0; if (why) RS(28)++; else RS(27)++; }
    RS(24) = rc_pending();
    if (!RS(24) && !rc_r && rc_blk) { u32 m = irq_off(); if (rc_pool_live()) LBMEM_FREE(rc_blk_h, rc_lo); rc_blk = 0; rc_wr = 0; irq_restore(m); }   /* nothing waits: the block goes back */
}
