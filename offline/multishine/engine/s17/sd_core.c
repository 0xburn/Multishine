/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* offline/s17 (copy of, S17: changes marked): the console-proven kbench4 kernels as opt-in code paths
 * (sd_s17, sd_s17.h, sd_lc.h; design): G = DMA double-buffered capture compare, H = streamed restore, U = whole-line
 * undo, LOGZ = log lines zeroed ahead.  sd_s17 = 0 is the S16 engine unchanged (default and negative control); every S17 pass
 * has a CPU fallback (locked cache in use, DMA failure).  Mirror memory is kept current (dcbst) whenever G or H is on.
 * (copy of, S16: changes marked): (1) strict capture/restore: every range or cell the
 * engine could not hold (mirror arena full, statics / cell list full) is counted and marks the capture being built (and
 * every later one until a rebuild or a restore to an older capture) as INCOMPLETE; incomplete captures are never
 * restored (sd_restore refuses, sd_newest_ok_le picks the newest complete one); an incomplete engine is rebuilt only by an explicit sd_rebuild (a rebuild drops every older capture, so the
 * caller must know that no rollback can be needed; the glue does it in lockstep); sd_strict = 0 is the S12 behaviour (negative control).  (2) one arena for the mirror (grows up
 * from MA) and the fingerprint table (grows down from MA + cap_m, entry of mirror line i at top - 8 * (i + 1)): the
 * mirror has priority, the table covers the prefix fp_lim of the mirror (fp_lim = min(m_top, 4 * (cap_m - m_top))) and
 * ranges beyond it use the exact full compare (fingerprints are an optimisation only).  (3) log share 32 % (was 40 %).
 * (copy of, S11: changes marked): fingerprint capture (sd_fp, see sd_snap.h and
 * sd_fp.h), verify mode (sd_fpv), fingerprint undo (LFP ring), weakened fingerprints for the model validation (TEST).
 * sd_fp 0 = the S10 engine unchanged.
 * (copy of, S10: changes marked): KEEP / RSET restore modes, sorted statics,
 * address -> range lookup, sd_diff diagnostics (see sd_snap.h).  sd_rmode 0 = the S6c engine unchanged.
 * (copy of, S6C: changes marked): delta snapshot engine, game-independent part
 * (original work).  See sd_snap.h for the design.
 * No libc, no heap: everything lives in the store handed to sd_init. */
#include "sd_snap.h"
#include "sd_fp.h"                           /* S11 */
#include "sd_lc.h"                           /* S17 */

struct sd_stats sd_st;
u32 sd_active;
#ifdef SD_LAB_HG
u32 sd_hg_w[8];                              /* LAB ONLY ( B): engine writes after the match scene ended (sd_melee.c scene_gone): [0] armed,
                                                [1] log entries, [2] events, [3] lowest address written, [4] end of the highest, [5] scene word at the first
                                                detection, [6] detections; the glue reports and clears it at the first pass of the next scene */
static void hg_note(const void *p, u32 n)
{
    u32 a = (u32)(unsigned long)p;
    if (!sd_hg_w[3] || a < sd_hg_w[3]) sd_hg_w[3] = a;
    if (a + n > sd_hg_w[4]) sd_hg_w[4] = a + n;
}
#endif
u32 sd_rmode, sd_rs_verify;                  /* S10 */
struct sd_x10 sd_x;                          /* S10 */
u32 sd_verify, sd_barrier_miss;
u32 sd_chain_only;                           /* S17 R1: the glue proves live == M (pass-end capture, nothing ran since): restore step 1 (live := M) is skipped */
/* S11: fingerprint capture */
u32 sd_fp, sd_fp_bits = 64, sd_fpv, sd_fpv_fix;
u32 sd_fp_key[2] = { 0x9E3779B9u, 0x7F4A7C15u };
struct sd_x11 sd_x11;
/* S16 */
u32 sd_strict = 1, sd_nofp_force, sd_arena_limit, sd_log_pct;
u32 sd_build_bulk = 1;                       /* : 1 (default) = build appends the cells of the heap walk and sorts once; 0 = the old per-cell sorted insert (A/B, negative control) */
struct sd_x16 sd_x16;
u32 sd_lost_first[4];                        /* stage smoke the FIRST loss since init: {site, lo, bytes, capture seq}; sites 1 statics list full, 2 static arena, 3 sd_add_cell, 4 capture cell insert, 5 journal revert */
static u32 inc_on, inc_seq;                  /* S16: a drop happened; captures with seq >= inc_seq are incomplete */
static u32 fp_lim, m_free_bytes, step_new;   /* S16 */
static u8 *FPT;                              /* S16: top of the arena = end of the fingerprint table */
u32 sd_cold, sd_cold_n = 8;                  /* S11 part 2 */
u32 sd_cold_mark;                            /* S11: keep SD_SITE_SCAN marks and unexplained-change counts (cold or site profile) */
struct sd_x11c sd_x11c;
static u32 cold_ctr, cold_rescan;              /* S11: cold_rescan = scan every cold cell at the next capture */
__attribute__((weak)) u32 sd_plat_cell_site(u32 lo) { (void)lo; return 0; }
__attribute__((weak)) int sd_plat_site_cold(u32 site) { (void)site; return 0; }
__attribute__((weak)) void sd_plat_cell_gone(const struct sd_range *r) { (void)r; }
static u32 *FP;                              /* S11: non-zero when the table exists (S16: entries at FPT - 8 * (line + 1), see fp_of) */
static u32 *LFP;                             /* S11: 2 words per log entry: previous fingerprint of a mirror line */
static u32 fpm_a = ~0u, fpm_b = ~0u;         /* S11: masks of the (weakened) fingerprint */
static u32 fpv_now, fpv_ctr, fpv_cap_miss;   /* S11: this capture verifies; capture counter; miss in this capture */

/* ---------------------------------------------------------------- storage */
static struct sd_range *ST;                  /* statics */
static u8 *ST_FL;                            /* per static: bit0 barrier, bit1 dirty */
static u32 nst;
static struct sd_range *CE;                  /* cells, sorted by lo */
static u32 nce, cap_ce;
static struct sd_ent *LOG;                   /* undo log ring */
static u32 cap_log, log_head;
static struct sd_ent *XR;                    /* extras ring */
static u32 cap_x, x_head;
static struct sd_jent *JR;                   /* list journal ring */
static u32 cap_j, j_head;
static struct sd_evt *EV;                    /* allocator events since the last capture */
static u32 nev;
static u8 *MA;                               /* mirror arena */
static u32 cap_m, m_top;
#define NCLS (SD_CELL_MAX / SD_LINE + 2)
static u32 mfree[NCLS];                      /* free lists of mirror blocks by size class (offsets, ~0 = none) */
static u16 mfree_n[NCLS];                    /* blocks on each free list (restore_room, review fix 3) */
#ifdef SD_HOST
u32 sd_test_top_steal, sd_test_ce_steal, sd_test_free_steal, sd_test_noroom, sd_test_room_trace;   /* TEST ONLY (s16_room_test): arena top bytes / list slots / the free lists taken away from the engine; skip the restore_room dry pass */
#else
#define sd_test_top_steal 0u
#define sd_test_ce_steal 0u
#define sd_test_free_steal 0u
#define sd_test_noroom 0u
#endif
static u32 m_big_free;                       /* statics are never freed (bump only) */
static struct sd_slot SL[SD_SLOTS];
static u32 nsl;
static u32 seq;
static u32 built;
#define DD_SLOTS 16384u
#define DD_MAXLINES 12288u
static u32 *dd_ht;                           /* S17 DD: 2 words per slot: (gen << 20 | line), live mask | mirror mask << 8 (per word of the 8 of a line) */
static u16 *dd_mk;                           /* S17 DD: per log entry: live mask | mirror mask << 8 */
static u32 dd_gen, dd_lines;
struct sd_dd sd_dd;                          /* S17 DD telemetry */
static u32 *s17_ord, *s17_tmp;               /* S17: H plan arrays (carved by sd_init when H is on) */
static u32 pend_lost;                        /* a pre-image could not be logged: captures are invalid */
static u32 in_build;

#define NONE 0xFFFFFFFFu
#define MP(off) ((u32 *)(MA + (off)))
static void s17_mflush(const void *p, u32 bytes);   /* S17: dcbst of mirror lines just written (only when G or H is on) */
static struct sd_ent *s17_log_put(u32 addr, u32 len);   /* S17: log_put + (LOGZ) the lines of the new entry zeroed ahead */

static void zero_words(u32 *p, u32 n) { while (n--) *p++ = 0; }

int sd_init(u32 store, u32 size)
{
    u32 p = (store + 31) & ~31u, end = store + size, i;
    sd_active = 0;
    zero_words((u32 *)&sd_st, sizeof sd_st / 4);
    zero_words((u32 *)&sd_x16, sizeof sd_x16 / 4);   /* S16 */
    zero_words((u32 *)&sd_x17, sizeof sd_x17 / 4);   /* S17 */
    s17_ord = s17_tmp = 0;
    dd_ht = 0; dd_mk = 0; dd_gen = 0; zero_words((u32 *)&sd_dd, sizeof sd_dd / 4);
    zero_words((u32 *)&sd_uf, sizeof sd_uf / 4);   /* S17 undo-step fix */
    zero_words((u32 *)&sd_u2, sizeof sd_u2 / 4);   /* S17 undo step v2 */
    if (SD_S17F & SD_S17_G)                   /* S17: the exact DMA compare replaces the fingerprint (it is slower on the console, kbench4 F64 vs G64) */
        sd_fp = 0;
#define CARVE(ptr, type, n) do { ptr = (type *)(void *)SDP(p); p = (p + (u32)(n) * sizeof(type) + 31) & ~31u; } while (0)
    CARVE(ST, struct sd_range, SD_STATICS);
    CARVE(ST_FL, u8, SD_STATICS);
    /* S19: compact fixed tables: cells 6144, extras 1024 entries (a capture puts one per free heap cell, Melee in a match: 1; 512 was
       too few for the host model's fragmented heap at stride 1).  Overflow of either is handled as before (cell: incomplete -> gate, extras: oldest
       captures dropped).  B1 / M1a: EVERY store has them now (was 8192 / 3072 at SD_COMPACT_BELOW and above).
       Cube peaks, 2P Stadium real-client combat with 748 rollbacks: cells 4,448 (72 %), extras ring 48.  B1 / M1b: the unused
       163,840 B tail M1a kept on a store >= SD_COMPACT_BELOW is gone and the configured store is 163,840 B smaller instead (s3_cfg.store_size
       2,883,584 -> 2,719,744): the log and the mirror of the new full store keep exactly the old capacity.  The store size alone
       now sets cap_log / cap_m, at every size (console peaks M1a: cells 4,403 Stadium / 3,788 Battlefield, extras 30 / 15). */
    /* stage smoke 8192 cells (was 6144).  Mute City and Great Bay fill the 6144-entry list (6,126 cells at the hang; ~4,400 is Stadium's cube peak): the list overflowed, every later capture was
       INCOMPLETE ("lost" 32-byte cells, S6!K LST1 site 4), the gap guard capped the remote frames at the newest complete capture + 6, and both consoles stalled for ever (S6!H, 7 s, disconnect).
       Costs 64 KB of cells + 16 KB of radix plan out of the mirror arena.  8192 is SD_CE_MAX (the restore_room dry pass is sized for it). */
    cap_ce = 8192u;
    cap_x = 1024u;
    CARVE(CE, struct sd_range, cap_ce);
    cap_j = 2048;
    CARVE(JR, struct sd_jent, cap_j);
    CARVE(EV, struct sd_evt, SD_EVTS);
    CARVE(XR, struct sd_ent, cap_x);
    if (SD_S17F & SD_S17_H) {                 /* S17: mirror-order plan (keys of every range, two radix passes) */
        CARVE(s17_ord, u32, SD_STATICS + cap_ce);
        CARVE(s17_tmp, u32, SD_STATICS + cap_ce);
    }
    if (p >= end)
        return -1;
    if (SD_FP_ON) {                             /* S16: log 32 % (40 B entry + 8 B previous fingerprint); the rest is ONE arena:
                                                mirror up from MA, fingerprint table (8 B per 32 B line) down from MA + cap_m */
        u32 pct = sd_log_pct && sd_log_pct < 90 ? sd_log_pct : 32;
        cap_log = ((end - p) * pct / 100) / (sizeof(struct sd_ent) + 8);
        CARVE(LOG, struct sd_ent, cap_log);
        CARVE(LFP, u32, 2 * cap_log);
        if (p >= end)
            return -1;
        MA = (u8 *)SDP(p);
        cap_m = (end - p) & ~31u;
        if (sd_arena_limit && (sd_arena_limit & ~31u) < cap_m)
            cap_m = sd_arena_limit & ~31u;   /* S16 TEST: artificial capacity limit */
        FP = (u32 *)(void *)MA;              /* non-zero marker; entries are addressed from FPT */
        FPT = MA + cap_m;
        sd_x11.fp_table_bytes = cap_m / 5;   /* nominal; the live size is fp_lim / 4 (sd_x16.fp_lim) */
        sd_x11.lfp_bytes = cap_log * 8;
        sd_x11.mirror_bytes = cap_m;
        sd_x11.log_ents = cap_log;
        goto carved;
    }
    /* remaining space: log ring (45 %) and mirror arena (55 %).  2P FD: mirror ~1.0 MB; a step logs up to ~3500
       lines (140 KB), so a 2.75 MB store keeps >= 7 worst-case steps (~1.07 MB of log) */
    {   /* S17: with any S17 flag the log takes the S16 share (32 %, no fingerprint ring) and the mirror the rest */
        u32 pct = sd_log_pct && sd_log_pct < 90 ? sd_log_pct : (SD_S17F ? 32u : 45u);   /* an explicit sd_log_pct also applies without S17 flags (rbtest V5 K0 uses 32 like K1-K3) */
        cap_log = ((end - p) * pct / 100) / sizeof(struct sd_ent);
    }
    CARVE(LOG, struct sd_ent, cap_log);
    if ((SD_S17F & SD_S17_DD) && !SD_FP_ON) {    /* S17 DD: first-cover table (128 KB) and per-entry masks (2 B per log entry), taken from the MIRROR share: the log keeps
                                                its capacity, so the same captures are dropped as without DD (a smaller log changes which capture a rollback finds) */
        CARVE(dd_ht, u32, 2 * DD_SLOTS);
        CARVE(dd_mk, u16, cap_log);
    }
    if (p >= end)
        return -1;
    MA = (u8 *)SDP(p);
    cap_m = (end - p) & ~31u;
    if (sd_arena_limit && (sd_arena_limit & ~31u) < cap_m)
        cap_m = sd_arena_limit & ~31u;       /* S16 TEST */
    FP = LFP = 0;                            /* S11 */
    FPT = 0;
    sd_x11.mirror_bytes = cap_m;
    sd_x11.log_ents = cap_log;
carved:
#undef CARVE
    if (dd_ht && !dd_mk) dd_ht = 0;          /* S17 DD: not enough store: the old chain runs (flags_eff shows it) */
    if (dd_ht) zero_words(dd_ht, 2 * DD_SLOTS);
    if ((SD_S17F & SD_S17_H) && cap_m > (8u << 20))   /* S17: the plan keys hold moff / 32 in 18 bits */
        s17_ord = s17_tmp = 0;
    sd_x17.flags_eff = (SD_S17F & ~(SD_S17_H | SD_S17_DD)) | (s17_ord ? SD_S17_H : 0u) | (dd_ht ? SD_S17_DD : 0u);
    for (i = 0; i < NCLS; i++) { mfree[i] = NONE; mfree_n[i] = 0; }
    (void)m_big_free;
    sd_x16.arena_bytes = cap_m;
    sd_reset();
    sd_active = 1;
    return 0;
}

void sd_reset(void)
{
    u32 i;
    nst = nce = nev = nsl = 0;
    m_top = 0;
    for (i = 0; i < NCLS; i++) { mfree[i] = NONE; mfree_n[i] = 0; }
    built = 0;
    pend_lost = 0;
    log_head = x_head = j_head = 0;
    inc_on = 0;                              /* S16 */
    inc_seq = 0;
    sd_lost_first[0] = sd_lost_first[1] = sd_lost_first[2] = sd_lost_first[3] = 0;
    m_free_bytes = 0;
    fp_lim = 0;
    step_new = 0;
}

/* S16: the arena could not hold something: it is missing from the manifest.  The capture being built has seq `seq`. */
static void lost_at(u32 site, u32 lo, u32 bytes);
static void lost_at(u32 site, u32 lo, u32 bytes)
{
    if (!sd_lost_first[0]) { sd_lost_first[0] = site ? site : 0xFFu; sd_lost_first[1] = lo; sd_lost_first[2] = bytes; sd_lost_first[3] = seq; }
    sd_x16.lost_ranges++;
    sd_x16.lost_bytes += bytes;
    if (!inc_on) {
        inc_on = 1;
        inc_seq = seq;
        sd_x16.inc_onsets++;
    }
}

/* S16: fp_lim after the mirror top moved: the table (fp_lim / 4 bytes from the arena top) must not overlap the mirror */
static void fp_update(void)
{
    u32 room;
    if (!SD_FP_ON || !FP || sd_nofp_force) {
        fp_lim = 0;
        return;
    }
    room = ((cap_m - m_top) * 4u) & ~31u;
    fp_lim = m_top < room ? m_top : room;
}

/* ---------------------------------------------------------------- mirror arena */
static u32 m_alloc(u32 bytes, int cell)
{
    u32 sz = (bytes + SD_LINE - 1) & ~(SD_LINE - 1), o;
    if (cell && !sd_test_free_steal && sz / SD_LINE < NCLS && mfree[sz / SD_LINE] != NONE) {
        o = mfree[sz / SD_LINE];
        mfree[sz / SD_LINE] = *MP(o);
        mfree_n[sz / SD_LINE]--;
        m_free_bytes -= sz;
        return o;
    }
    if (m_top + sz + sd_test_top_steal > cap_m) {
        sd_st.arena_full++;
        return NONE;
    }
    o = m_top;
    m_top += sz;
    if (m_top > sd_x16.arena_hi)             /* S16 */
        sd_x16.arena_hi = m_top;
    fp_update();                             /* S16 */
    return o;
}

static void m_free(u32 o, u32 bytes)
{
    u32 sz = (bytes + SD_LINE - 1) & ~(SD_LINE - 1);
    if (o == NONE || sz / SD_LINE >= NCLS)
        return;
    *MP(o) = mfree[sz / SD_LINE];
    mfree[sz / SD_LINE] = o;
    mfree_n[sz / SD_LINE]++;
    m_free_bytes += sz;                      /* S16 */
}

/* ---------------------------------------------------------------- copies */
#ifndef SD_HOST
void sd_fcopy(u32 *d, const u32 *s, u32 nlines);      /* sd_fcopy.S: lfd/stfd, 32 bytes per iteration */
#endif

static void cpy(u32 *d, const u32 *s, u32 n)
{
    SD_NOTE_W(d, n * 4u);                    /* S17: host write-back cache model */
#ifndef SD_HOST
    if (n >= 8 && !(((u32)d | (u32)s) & 7)) {
        sd_fcopy(d, s, n / 8);
        d += n & ~7u;
        s += n & ~7u;
        n &= 7;
    }
#endif
    while (n >= 8) {
        u32 a = s[0], b = s[1], c = s[2], e = s[3], f = s[4], g = s[5], h = s[6], i = s[7];
        d[0] = a; d[1] = b; d[2] = c; d[3] = e; d[4] = f; d[5] = g; d[6] = h; d[7] = i;
        d += 8;
        s += 8;
        n -= 8;
    }
    while (n--)
        *d++ = *s++;
}

/* range with hole: apply fn on [lo,hlo) and [hhi,hi) */
static void copy_live_to_m(const struct sd_range *r)
{
    if (r->hhi > r->hlo) {
        cpy(MP(r->moff), SDP(r->lo), (r->hlo - r->lo) / 4);
        cpy(MP(r->moff + (r->hhi - r->lo)), SDP(r->hhi), (r->hi - r->hhi) / 4);
        s17_mflush(MP(r->moff), r->hlo - r->lo);                          /* S17: mirror memory current */
        s17_mflush(MP(r->moff + (r->hhi - r->lo)), r->hi - r->hhi);
    } else {
        cpy(MP(r->moff), SDP(r->lo), (r->hi - r->lo) / 4);
        s17_mflush(MP(r->moff), r->hi - r->lo);
    }
}

static void copy_m_to_live(const struct sd_range *r)
{
    if (r->hhi > r->hlo) {
        cpy(SDP(r->lo), MP(r->moff), (r->hlo - r->lo) / 4);
        cpy(SDP(r->hhi), MP(r->moff + (r->hhi - r->lo)), (r->hi - r->hhi) / 4);
    } else {
        cpy(SDP(r->lo), MP(r->moff), (r->hi - r->lo) / 4);
    }
}

/* ---------------------------------------------------------------- S11: fingerprints */
static inline __attribute__((always_inline)) void fpl(const u32 *p, u32 k, u32 *h)
{
    if (k == 8)
        sd_fp8(p, sd_fp_key[0], sd_fp_key[1], h);
    else
        sd_fpn(p, k, sd_fp_key[0], sd_fp_key[1], h);
    if (sd_fp_bits < 64) {                   /* TEST: weakened fingerprint = b bits of the finalised state */
        sd_fp_fin(h, h);
        h[0] &= fpm_a;
        h[1] &= fpm_b;
    }
}

static void fp_masks(void)
{
    u32 b = sd_fp_bits;
    if (b >= 64 || b == 0) { fpm_a = fpm_b = ~0u; }
    else if (b >= 32) { fpm_a = ~0u; fpm_b = b == 32 ? 0u : (1u << (b - 32)) - 1u; }
    else { fpm_a = (1u << b) - 1u; fpm_b = 0u; }
}

/* S16: the table covers the mirror prefix [0, fp_lim); a range is fingerprinted only if it lies inside it and has no hole.
   Entry of mirror line i = (FPT - 8 * (i + 1)); consecutive lines of a range therefore have DECREASING addresses. */
static int fp_on(const struct sd_range *r)
{
    return SD_FP_ON && FP && !(r->hhi > r->hlo) && r->moff + ((r->hi - r->lo + SD_LINE - 1) & ~(SD_LINE - 1)) <= fp_lim;
}
static u32 *fp_of(const struct sd_range *r, u32 a) { return (u32 *)(void *)(FPT - 8u * (((r->moff + (a - r->lo)) / SD_LINE) + 1u)); }

/* table entries of a range from the LIVE contents (the caller has just copied live -> M) */
static void fp_range(const struct sd_range *r)
{
    u32 *p, *f, n;
    if (!fp_on(r))
        return;
    p = SDP(r->lo); f = fp_of(r, r->lo); n = (r->hi - r->lo) / 4;
    while (n) {
        u32 k = n < 8 ? n : 8;
        fpl(p, k, f);
        sd_x11.fp_base_lines++;
        p += k; f -= 2; n -= k;
    }
}

/* S11 TEST: entries != fp(M) over the whole manifest */
u32 sd_fp_check(void)
{
    u32 i, bad = 0;
    if (!SD_FP_ON || !FP || !nsl)               /* no capture: the table is not in use (next capture is a base) */
        return 0;
    for (i = 0; i < nst + nce; i++) {
        const struct sd_range *r = i < nst ? &ST[i] : &CE[i - nst];
        const u32 *m, *f;
        u32 n, h[2];
        if (!fp_on(r))
            continue;
        m = MP(r->moff); f = fp_of(r, r->lo); n = (r->hi - r->lo) / 4;
        while (n) {
            u32 k = n < 8 ? n : 8;
            fpl(m, k, h);
            if (h[0] != f[0] || h[1] != f[1])
                bad++;
            m += k; f -= 2; n -= k;
        }
    }
    return bad;
}

/* ---------------------------------------------------------------- rings */
static u32 log_tail(void)
{
    if (nsl >= 2)
        return SL[1].lb;
    if (nsl == 1)
        return SL[0].le;
    return log_head;
}

static void drop_oldest(void)
{
    u32 i;
    if (!nsl)
        return;
    for (i = 1; i < nsl; i++)
        SL[i - 1] = SL[i];
    nsl--;
}

/* room for one more log entry; drops the oldest captures if needed (never the newest) */
static int log_room(void)
{
    while (log_head - log_tail() >= cap_log) {
        if (nsl <= 1)
            return 0;
        drop_oldest();
        sd_st.log_full_drops++;
    }
    return 1;
}

static struct sd_ent *log_put(u32 addr, u32 len)
{
    struct sd_ent *e;
    if (!log_room())
        return 0;
    e = &LOG[log_head % cap_log];
    log_head++;
    e->addr = addr;
    e->len = len;
    if (log_head - log_tail() > sd_st.log_used_max)
        sd_st.log_used_max = log_head - log_tail();
    return e;
}

void sd_log_pre(u32 addr, u32 len)
{
    struct sd_ent *e;
    if (!sd_active || !nsl || in_build)
        return;
    addr &= ~3u;
    if (len > 32)
        len = 32;
    e = log_put(addr, (len + 3) & ~3u);
    if (!e) {
        pend_lost = 1;
        return;
    }
    cpy(e->d, SDP(addr), e->len / 4);
    sd_st.hdr_pre++;
#ifdef SD_LAB_HG
    if (sd_hg_w[0]) { sd_hg_w[1]++; hg_note(e, sizeof *e); }
#endif
}

void sd_put_extra(u32 addr, u32 len)
{
    struct sd_ent *e;
    u32 xt = nsl ? SL[0].xb : x_head;
    while (x_head - xt >= cap_x) {          /* extras ring full: drop the oldest captures */
        if (!nsl) {
            sd_st.errors++;
            return;
        }
        drop_oldest();
        sd_st.log_full_drops++;
        xt = nsl ? SL[0].xb : x_head;
    }
    e = &XR[x_head % cap_x];
    x_head++;
    addr &= ~3u;
    if (len > 32)
        len = 32;
    e->addr = addr;
    e->len = (len + 3) & ~3u;
    cpy(e->d, SDP(addr), e->len / 4);
}

static u32 j_tail(void)
{
    if (nsl >= 2)
        return SL[1].jb;
    if (nsl == 1)
        return SL[0].je;
    return j_head;
}

static int journal(u32 kind, const struct sd_range *r)
{
    struct sd_jent *j;
    while (j_head - j_tail() >= cap_j) {     /* journal full: drop the oldest captures */
        if (nsl <= 1)
            return -1;
        drop_oldest();
        sd_st.log_full_drops++;
    }
    j = &JR[j_head % cap_j];
    j_head++;
    j->kind = kind;
    j->lo = r->lo;
    j->hi = r->hi;
    j->hlo = r->hlo;
    j->hhi = r->hhi;
    return 0;
}

/* ---------------------------------------------------------------- manifest list */
void sd_add_static(u32 lo, u32 len)
{
    struct sd_range *r;
    u32 hi = (lo + len + 3) & ~3u;
    lo &= ~3u;
    if (hi <= lo || nst >= SD_STATICS) {
        if (nst >= SD_STATICS) {
            sd_st.errors++;
            lost_at(1, lo, hi > lo ? hi - lo : 0);     /* S16 */
        }
        return;
    }
    r = &ST[nst];
    r->lo = lo;
    r->hi = hi;
    r->hlo = r->hhi = 0;
    r->moff = m_alloc(hi - lo, 0);
    r->born = seq;
    r->chg = 0;
    r->site = 0;                             /* S11 */
    if (r->moff == NONE) {
        lost_at(2, lo, hi - lo);                       /* S16 */
        return;
    }
    ST_FL[nst] = 0;
    nst++;
}

/* S10: mark statics overlapping [lo, hi) as render set */
void sd_rs_static(u32 lo, u32 hi)
{
    u32 i;
    for (i = 0; i < nst; i++)
        if (lo < ST[i].hi && hi > ST[i].lo)
            ST[i].chg |= SD_RS_BIT;
}

void sd_barrier_range(u32 lo, u32 hi)
{
    u32 i;
    for (i = 0; i < nst; i++)
        if (ST[i].lo == lo && ST[i].hi == hi)
            ST_FL[i] |= 1;
}

void sd_mark_dirty(u32 lo, u32 len)
{
    u32 i, hi = lo + len;
    for (i = 0; i < nst; i++)
        if ((ST_FL[i] & 1) && lo < ST[i].hi && hi > ST[i].lo)
            ST_FL[i] |= 2;
}

static s32 cell_find(u32 lo)            /* exact match on lo, or -(insert position) - 1 */
{
    s32 a = 0, b = (s32)nce - 1;
    while (a <= b) {
        s32 m = (a + b) >> 1;
        if (CE[m].lo == lo)
            return m;
        if (CE[m].lo < lo)
            a = m + 1;
        else
            b = m - 1;
    }
    return -a - 1;
}

static u32 bulk;                                /* inside build: cells are appended in walk order (see cell_sort) */
static int cell_insert(u32 lo, u32 hi, u32 hlo, u32 hhi, u32 born, int copy)
{
    s32 k, i;
    struct sd_range *r;
    if (bulk)
        k = (s32)nce;                   /* append: the list is sorted once at the end of the walk, duplicates are dropped there */
    else {
        k = cell_find(lo);
        if (k >= 0)
            return 0;                   /* already present */
        k = -k - 1;
    }
    if (nce + sd_test_ce_steal >= cap_ce) {
#ifdef SD_HOST
        if (sd_test_room_trace) { extern int printf(const char *, ...); printf("  REAL cell_insert list full lo %08x nce %u cap_ce %u\n", lo, nce, cap_ce); }
#endif
        sd_st.cell_overflow++;
        return -1;
    }
    for (i = (s32)nce; i > k; i--)
        CE[i] = CE[i - 1];
    r = &CE[k];
    r->lo = lo;
    r->hi = hi;
    r->hlo = hlo;
    r->hhi = hhi;
    r->born = born;
    r->chg = 0;
    r->site = sd_plat_cell_site(lo) & SD_SITE_IDX;           /* S11 */
    if (r->site && sd_plat_site_cold(r->site))
        r->site |= SD_SITE_COLD;
    r->moff = m_alloc(hi - lo, 1);
    nce++;
#ifdef SD_HOST
    if (sd_test_room_trace && r->moff == NONE) { extern int printf(const char *, ...); printf("  REAL cell_insert fail lo %08x hi %08x nce %u cap_ce %u m_top %u cap_m %u free_bytes %u\n", lo, hi, nce, cap_ce, m_top, cap_m, m_free_bytes); }
#endif
    if (r->moff == NONE) {               /* arena full: drop the cell again (counted) */
        for (i = k; i + 1 < (s32)nce; i++)
            CE[i] = CE[i + 1];
        nce--;
        return -1;
    }
    if (copy) {
        copy_live_to_m(r);
        fp_range(r);                         /* S11 */
    }
    return 1;
}

static void cell_remove_at(s32 k)
{
    s32 i;
    sd_plat_cell_gone(&CE[k]);               /* S11: profile */
    m_free(CE[k].moff, CE[k].hi - CE[k].lo);
    for (i = k; i + 1 < (s32)nce; i++)
        CE[i] = CE[i + 1];
    nce--;
}

void sd_add_cell(u32 lo, u32 hi)
{
    u32 hlo = 0, hhi = 0;
    if (!sd_plat_cell_ok(lo, hi, &hlo, &hhi))
        return;
    if (cell_insert(lo, hi, hlo, hhi, seq, 0) < 0)
        lost_at(3, lo, hi - lo);                       /* S16 */
}

/* ---------------------------------------------------------------- allocator events */
void sd_evt_alloc_pre(u32 head_cell)
{
    if (head_cell)
        sd_log_pre(head_cell, 12);
}

static void evt_push(u32 kind, u32 addr, u32 size)
{
    if (!sd_active || in_build)
        return;
    if (nev >= SD_EVTS) {
        sd_st.evt_overflow++;
        return;
    }
#ifdef SD_LAB_HG
    if (sd_hg_w[0]) { sd_hg_w[2]++; hg_note(&EV[nev], sizeof EV[0]); }
#endif
    EV[nev].kind = kind;
    EV[nev].addr = addr;
    EV[nev].size = size;
    nev++;
}

void sd_evt_alloc(u32 payload, u32 size)
{
    if (payload)
        evt_push(1, payload, size);
}

void sd_evt_free(u32 payload)
{
    if (!payload)
        return;
    /* the OS allocator unlinks the cell from the allocated list: its own header and its neighbours' links change */
    if (nsl && sd_active) {
        u32 c = payload - 0x20, pv = *SDP(c), nx = *SDP(c + 4);
        sd_log_pre(c, 12);
        if (pv >= 0x80000000u && pv < 0x81800000u)
            sd_log_pre(pv, 12);
        if (nx >= 0x80000000u && nx < 0x81800000u)
            sd_log_pre(nx, 12);
    }
    evt_push(2, payload, 0);
}

/* Net effect of the pending events on the cell list.  state per address: 1 added, 2 removed (was listed),
   3 unchanged (removed then re-added with the same size). */
#define NT SD_NT                              /* S6C: was 256 */
/* (memory, card hole): apply_events' address table and restore_room's scratch (review fix 3, below) share one static block.
   Both are per-call scratch, rebuilt at the start of every call (nt / nadd / room_rm / room_n are set before any read); restore_room runs
   at the top of sd_restore and apply_events only inside base_capture / sd_capture, neither calls the other, and no state lives in them
   between calls.  Same values, same order: only the addresses of the arrays changed (-4,676 B of bss). */
#define SD_CE_MAX 8192u
#define SD_ROOM_ADD 512u
static union {
    struct { u32 addr[NT], size[NT], orig[NT], state[NT]; } t;
    struct { u32 rm[SD_CE_MAX / 32u]; u32 add_lo[SD_ROOM_ADD]; u16 add_cls[SD_ROOM_ADD], n[NCLS]; } room;
} sd_scr;
typedef char sd_scr_room_fits[sizeof(sd_scr.room) <= sizeof(sd_scr.t) ? 1 : -1];   /* the union never grows past the S6C table */
#define t_addr sd_scr.t.addr
#define t_size sd_scr.t.size
#define t_orig sd_scr.t.orig
#define t_state sd_scr.t.state

static int apply_events(int logging)
{
    u32 i, nt = 0, k;
    if (nev > sd_st.evt_max)                 /* S6C */
        sd_st.evt_max = nev;
    if (sd_st.evt_overflow) {
        sd_st.evt_rebuilds++;                /* S6C */
        nev = 0;
        built = 0;
        return -1;
    }
    for (i = 0; i < nev; i++) {
        u32 a = EV[i].addr, j;
        for (j = 0; j < nt && t_addr[j] != a; j++)
            ;
        if (EV[i].kind == 1) {
            if (j < nt) {
                t_size[j] = EV[i].size;
                if (t_state[j] == 2)          /* listed cell freed earlier in this step: same size = unchanged */
                    t_state[j] = t_orig[j] == EV[i].size ? 3 : 4;
                else
                    t_state[j] = 1;
            } else {
                if (nt >= NT) {
                    nev = 0;
                    built = 0;
                    sd_st.evt_rebuilds++;    /* S6C */
                    return -1;
                }
                t_addr[nt] = a;
                t_size[nt] = EV[i].size;
                t_orig[nt] = 0;
                t_state[nt++] = 1;
            }
        } else {
            if (j < nt) {
                if (t_state[j] == 1)
                    t_state[j] = 0;           /* added and freed in the same step: nothing */
                else if (t_state[j] == 3 || t_state[j] == 4)
                    t_state[j] = 2;
            } else {
                s32 c = cell_find(a);
                if (c >= 0) {
                    if (nt >= NT) {
                        nev = 0;
                        built = 0;
                        sd_st.evt_rebuilds++;    /* S6C */
                        return -1;
                    }
                    t_addr[nt] = a;
                    t_size[nt] = t_orig[nt] = CE[c].hi - CE[c].lo + 0x20;
                    t_state[nt++] = 2;
                }
            }
        }
    }
    nev = 0;
    if (nt > sd_st.net_max)                  /* S6C */
        sd_st.net_max = nt;
    for (k = 0; k < nt; k++)                 /* S11: freed and re-allocated with the same size: the new owner wrote it */
        if (t_state[k] == 3) {
            s32 c = cell_find(t_addr[k]);
            if (c >= 0 && sd_cold_mark) {    /* any cell (the site profile counts unexplained changes) */
                CE[c].site |= SD_SITE_SCAN;
                sd_x11c.inval += (CE[c].site & SD_SITE_COLD) != 0;
            }
        }
    /* removals first (their mirror contents are the previous capture's state) */
    for (k = 0; k < nt; k++) {
        if (t_state[k] == 2 || t_state[k] == 4) {
            s32 c = cell_find(t_addr[k]);
            if (c < 0)
                continue;
            if (logging) {
                const struct sd_range *r = &CE[c];
                u32 a, sp;
                /* S17 FIX (S16 and older: lines were taken from lo in 32-byte steps and every line that STARTS inside the hole was
                   skipped, so a hole whose end is not 32-aligned lost the words after it, and a line that starts before the hole
                   logged the hole's words as well): the two segments [lo, hlo) and [hhi, hi) are logged separately.  Without a
                   hole, or with a 32-aligned hole, the entries are the same as before. */
                for (sp = 0; sp < 2; sp++) {
                    u32 s0 = r->lo, s1 = r->hi;
                    if (r->hhi > r->hlo) {
                        if (!sp)
                            s1 = r->hlo;
                        else
                            s0 = r->hhi;
                    } else if (sp)
                        break;
                    for (a = s0; a < s1; a += SD_LINE) {
                        struct sd_ent *e;
                        u32 l = s1 - a < SD_LINE ? s1 - a : SD_LINE;
                        e = log_put(a, l);
                        if (!e) {
                            built = 0;
                            return -1;
                        }
                        cpy(e->d, MP(r->moff + (a - r->lo)), l / 4);
                        if (fp_on(r)) {          /* S11: the line's fingerprint goes with it */
                            const u32 *f = fp_of(r, a);
                            LFP[2 * (u32)(e - LOG)] = f[0];
                            LFP[2 * (u32)(e - LOG) + 1] = f[1];
                            e->len |= SD_ENT_LFP;    /* S16: the previous fingerprint is valid */
                        }
                    }
                }
                if (journal(2, r) < 0) {
                    built = 0;
                    return -1;
                }
            }
            cell_remove_at(c);
            sd_st.cell_removes++;
        }
    }
    for (k = 0; k < nt; k++) {
        if (t_state[k] == 1 || t_state[k] == 4) {
            u32 lo = t_addr[k], hi = t_addr[k] - 0x20 + t_size[k], hlo = 0, hhi = 0;
            int ins;
            if (!sd_plat_cell_ok(lo, hi, &hlo, &hhi))
                continue;
            ins = cell_insert(lo, hi, hlo, hhi, seq, 1);
            if (ins < 0) {
                lost_at(4, lo, hi - lo);               /* S16: the cell is not in the manifest (this capture is incomplete) */
            } else if (ins > 0) {
                step_new += hi - lo;         /* S16 */
                sd_st.cell_adds++;
                if (logging) {
                    struct sd_range r;
                    r.lo = lo; r.hi = hi; r.hlo = hlo; r.hhi = hhi;
                    if (journal(1, &r) < 0) {
                        built = 0;
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- compare pass */
static u32 lines;                            /* changed lines in this step */
static int cmp_fail;

static inline __attribute__((always_inline)) void cmp_span(u32 lo, u32 hi, u32 *m)
{
    u32 *p = SDP(lo), n = (hi - lo) / 4, a = lo;
    while (n >= 8) {
        u32 d = (p[0] ^ m[0]) | (p[1] ^ m[1]) | (p[2] ^ m[2]) | (p[3] ^ m[3]) |
                (p[4] ^ m[4]) | (p[5] ^ m[5]) | (p[6] ^ m[6]) | (p[7] ^ m[7]);
        if (d) {
            struct sd_ent *e = s17_log_put(a, 32);
            if (!e) {
                cmp_fail = 1;
                return;
            }
            cpy(e->d, m, 8);
            cpy(m, p, 8);
            s17_mflush(m, 32);               /* S17 */
            lines++;
        }
        p += 8;
        m += 8;
        a += 32;
        n -= 8;
    }
    if (n) {
        u32 d = 0, i;
        for (i = 0; i < n; i++)
            d |= p[i] ^ m[i];
        if (d) {
            struct sd_ent *e = s17_log_put(a, n * 4);
            if (!e) {
                cmp_fail = 1;
                return;
            }
            cpy(e->d, m, n);
            cpy(m, p, n);
            s17_mflush(m, n * 4);            /* S17 */
            lines++;
        }
    }
}

/* S11: verify one line of the fingerprint capture (full compare + table check); returns 1 if the line must be logged */
static u32 fpv_line(const u32 *p, u32 *m, u32 *f, u32 k, const u32 *h, u32 a)
{
    u32 d = 0, i, g[2];
    sd_x11.fpv_lines++;
    for (i = 0; i < k; i++)
        d |= p[i] ^ m[i];
    fpl(m, k, g);
    if (g[0] != f[0] || g[1] != f[1]) {      /* the table must equal fp(M): repair and count */
        sd_x11.fpv_tab_bad++;
        f[0] = g[0];
        f[1] = g[1];
    }
    if (!d) {
        if (h[0] != f[0] || h[1] != f[1])
            sd_x11.fpv_false++;
        return 0;
    }
    sd_x11.fpv_chg++;
    if (sd_fp_bits >= 64) {                  /* shadow widths: would a b-bit (weakened) fingerprint have missed it? */
        static const u32 wm[SD_FP_SHADOW] = { 0xFFu, 0xFFFu, 0xFFFFu, 0xFFFFFu, 0xFFFFFFu };
        u32 x, hf[2], ff[2];
        sd_fp_fin(h, hf);
        sd_fp_fin(f, ff);
        x = hf[0] ^ ff[0];
        for (i = 0; i < SD_FP_SHADOW; i++)
            if (!(x & wm[i]))
                sd_x11.shadow[i]++;
    }
    if (h[0] == f[0] && h[1] == f[1]) {      /* changed, fingerprint equal: the fingerprint capture misses it */
        if (!sd_x11.fpv_miss)
            sd_x11.fpv_miss_a0 = a;
        sd_x11.fpv_miss++;
        fpv_cap_miss = 1;
        return sd_fpv_fix;
    }
    return 1;
}

/* S11: fingerprint compare of a range without a hole: reads the live line, reads the mirror line only if its
   fingerprint differs from the table (undo entry: previous contents + previous fingerprint) */
static void cmp_range_fp(const struct sd_range *r)
{
    u32 *p = SDP(r->lo), *m = MP(r->moff), *f = fp_of(r, r->lo), n = (r->hi - r->lo) / 4, a = r->lo, nl = 0;
    while (n) {
        u32 k = n < 8 ? n : 8, h[2];
        fpl(p, k, h);
        nl++;
        if (fpv_now) {
            if (!fpv_line(p, m, f, k, h, a))
                goto next;
        } else if (h[0] == f[0] && h[1] == f[1])
            goto next;
        {
            struct sd_ent *e = s17_log_put(a, k * 4);
            if (!e) {
                cmp_fail = 1;
                break;
            }
            LFP[2 * (u32)(e - LOG)] = f[0];
            LFP[2 * (u32)(e - LOG) + 1] = f[1];
            e->len |= SD_ENT_LFP;            /* S16 */
            cpy(e->d, m, k);
            cpy(m, p, k);
            s17_mflush(m, k * 4);            /* S17 */
            f[0] = h[0];
            f[1] = h[1];
            lines++;
            sd_x11.fp_hit++;
        }
    next:
        p += k; m += k; f -= 2; a += 4 * k; n -= k;
    }
    sd_x11.fp_lines += nl;
}

static inline __attribute__((always_inline)) void cmp_range(struct sd_range *r)
{
    u32 l0 = lines;
    if (fp_on(r)) {                          /* S11 */
        cmp_range_fp(r);
    } else if (r->hhi > r->hlo) {
        if (SD_FP_ON)
            sd_x11.fp_holed += (r->hi - r->lo - (r->hhi - r->hlo) + SD_LINE - 1) / SD_LINE;
        cmp_span(r->lo, r->hlo, MP(r->moff));
        cmp_span(r->hhi, r->hi, MP(r->moff + (r->hhi - r->lo)));
    } else {
        if (SD_FP_ON && FP)                     /* S16: no hole, but outside the table prefix */
            sd_x16.fp_off_lines += (r->hi - r->lo + SD_LINE - 1) / SD_LINE;
        cmp_span(r->lo, r->hi, MP(r->moff));
    }
    if (lines != l0)
        r->chg++;
}

/* verify mode: compare without logging, count differing lines */
void sd_each(void (*fn)(u32 kind, const struct sd_range *r))
{
    u32 i;
    for (i = 0; i < nst; i++)
        fn(0, &ST[i]);
    for (i = 0; i < nce; i++)
        fn(1, &CE[i]);
}

/* S10 diagnostics: live vs M over the whole manifest, no logging */
static u32 diff_span(const struct sd_range *r, u32 lo, u32 hi, u32 *a0)
{
    u32 *p = SDP(lo), *m = MP(r->moff + (lo - r->lo)), n = (hi - lo) / 4, nl = 0, a = lo;
    while (n) {
        u32 k = n < 8 ? n : 8, d = 0, i;
        for (i = 0; i < k; i++)
            d |= p[i] ^ m[i];
        if (d) {
            if (!nl && !*a0) {
                for (i = 0; i < k && p[i] == m[i]; i++)
                    ;
                *a0 = a + 4 * i;
            }
            nl++;
        }
        p += k; m += k; a += 4 * k; n -= k;
    }
    return nl;
}

u32 sd_diff(void (*fn)(u32 kind, const struct sd_range *r, u32 nl, u32 a0))
{
    u32 i, tot = 0;
    if (!sd_active || !nsl)
        return 0;
    for (i = 0; i < nst + nce; i++) {
        const struct sd_range *r = i < nst ? &ST[i] : &CE[i - nst];
        u32 a0 = 0, nl;
        if (r->hhi > r->hlo)
            nl = diff_span(r, r->lo, r->hlo, &a0) + diff_span(r, r->hhi, r->hi, &a0);
        else
            nl = diff_span(r, r->lo, r->hi, &a0);
        if (nl) {
            tot += nl;
            if (fn)
                fn(i < nst ? 0u : 1u, r, nl, a0);
        }
    }
    return tot;
}

/* S17 R1 (AVENUE 1): every 32-byte live line (aligned address) of the manifest whose words differ from the mirror of the newest capture;
   fn(line, kind) per differing line (kind 0 static, 1 cell), returns the count.  Hole words are not in the manifest.  Read-only. */
u32 sd_census(void (*fn)(u32 line, u32 kind))
{
    u32 i, tot = 0;
    if (!sd_active || !nsl)
        return 0;
    for (i = 0; i < nst + nce; i++) {
        const struct sd_range *r = i < nst ? &ST[i] : &CE[i - nst];
        u32 *p = SDP(r->lo), *m = MP(r->moff), n = (r->hi - r->lo) / 4, w, last = 0xFFFFFFFFu;
        for (w = 0; w < n; w++) {
            u32 a = r->lo + 4 * w;
            if (r->hhi > r->hlo && a >= r->hlo && a < r->hhi)
                continue;
            if (p[w] != m[w] && (a & ~31u) != last) {
                last = a & ~31u;
                tot++;
                if (fn)
                    fn(last, i < nst ? 0u : 1u);
            }
        }
    }
    return tot;
}

static u32 diff_count(const struct sd_range *r)
{
    u32 *p = SDP(r->lo), *m = MP(r->moff), n = (r->hi - r->lo) / 4, c = 0, i;
    for (i = 0; i < n; i++)
        if (p[i] != m[i])
            c++;
    return c;
}

#include "sd_s17.h"                          /* S17: G / H / U kernels and the locked-cache layer (use the tables above) */

/* ---------------------------------------------------------------- capture */
/* : the heap walk of the first build visits about 3,600 cells in ALLOCATION-list order, not address order.  Inserting each one into the
   address-sorted list shifted on average n/2 entries of 32 bytes (about 210 MB of moves): 235 ms in Dolphin, 330 ms on the console, inside the first base
   capture, a visible freeze at the first predicted frame.  With `bulk` the cells are appended (their mirror blocks are still handed out in walk order, so
   the offsets are the same) and sorted ONCE by (lo, moff).  The list is entry for entry the one the sorted inserts built; a payload address met twice
   keeps its first (lowest moff) entry as cell_find did (the later copy only wastes its mirror block; a consistent heap list has none). */
static int ce_lt(const struct sd_range *a, const struct sd_range *b)
{
    return a->lo < b->lo || (a->lo == b->lo && a->moff < b->moff);
}

static void ce_sift(u32 root, u32 n)
{
    struct sd_range t = CE[root];
    for (;;) {
        u32 c = 2 * root + 1;
        if (c >= n)
            break;
        if (c + 1 < n && ce_lt(&CE[c], &CE[c + 1]))
            c++;
        if (!ce_lt(&t, &CE[c]))
            break;
        CE[root] = CE[c];
        root = c;
    }
    CE[root] = t;
}

static void cell_sort(void)
{
    u32 n = nce, i, w;
    for (i = 1; i < n && !ce_lt(&CE[i], &CE[i - 1]); i++)
        ;
    if (i < n) {                             /* not in order already: heap sort in place (no extra memory, n log n) */
        for (i = n / 2; i-- > 0;)
            ce_sift(i, n);
        for (i = n - 1; i > 0; i--) {
            struct sd_range t = CE[0];
            CE[0] = CE[i];
            CE[i] = t;
            ce_sift(0, i);
        }
    }
    for (i = w = 1; i < n; i++)              /* a repeated payload address: the first entry stays */
        if (CE[i].lo != CE[w - 1].lo)
            CE[w++] = CE[i];
    if (n)
        nce = w;
}

#ifdef SD_HOST
/* TEST ONLY (tests/sd_test.c SDT_BUILD_HASH): FNV-1a of the cell list, the mirror top and the static list, to compare build with sd_build_bulk 0 and 1 */
unsigned long long sd_test_list_hash(void)
{
    unsigned long long h = 0xcbf29ce484222325ull;
    u32 i, w[7];
    for (i = 0; i < nce + nst + 2; i++) {
        const struct sd_range *r = i < nce ? &CE[i] : i < nce + nst ? &ST[i - nce] : 0;
        u32 k;
        if (i >= nce + nst) { w[0] = i == nce + nst ? m_top : nce; w[1] = w[2] = w[3] = w[4] = w[5] = w[6] = 0; }
        else { w[0] = r->lo; w[1] = r->hi; w[2] = r->hlo; w[3] = r->hhi; w[4] = r->moff; w[5] = r->born; w[6] = r->site; }
        for (k = 0; k < 7; k++) { h ^= w[k]; h *= 0x100000001b3ull; }
    }
    return h;
}
#endif

static void build(void)
{
    u32 i;
    in_build = 1;
    nst = nce = 0;
    m_top = 0;
    m_free_bytes = 0;                        /* S16 */
    fp_lim = 0;
    inc_on = 0;                              /* S16: a rebuild starts from nothing; drops below mark it again */
    for (i = 0; i < NCLS; i++)
        mfree[i] = NONE;
    bulk = sd_build_bulk;
    sd_plat_build_cells();                   /* cells first: the static builder skips ranges inside cells */
    if (bulk) {
        bulk = 0;
        cell_sort();
    }
    sd_plat_build_statics();
    {   /* S10: statics sorted by lo (the KEEP/RSET restore looks addresses up by binary search); the Melee builder
           already emits them in order, so this normally only checks */
        u32 a, b;
        for (a = 1; a < nst; a++)
            for (b = a; b > 0 && ST[b - 1].lo > ST[b].lo; b--) {
                struct sd_range t = ST[b];
                u8 f = ST_FL[b];
                ST[b] = ST[b - 1]; ST_FL[b] = ST_FL[b - 1];
                ST[b - 1] = t; ST_FL[b - 1] = f;
                sd_x.unsorted++;
            }
    }
    in_build = 0;
    nev = 0;
    sd_st.evt_overflow = 0;
    built = 1;
}

/* B2 D: `have` = the G pass of this capture walked the whole list to its end and summed the same bytes on
   its way (s17_it.bytes; the list does not change between that walk and this call), so the two walks over all range records are not
   repeated after every captured frame.  The host build recomputes the sum and stops on any difference. */
static void stats_size(u32 have, u32 gb)
{
    u32 i, b = gb;
    if (!have) {
        b = 0;
        for (i = 0; i < nst; i++)
            b += ST[i].hi - ST[i].lo;
        for (i = 0; i < nce; i++)
            b += CE[i].hi - CE[i].lo - (CE[i].hhi - CE[i].hlo);
    }
#ifdef SD_HOST
    else {
        u32 b2 = 0;
        for (i = 0; i < nst; i++)
            b2 += ST[i].hi - ST[i].lo;
        for (i = 0; i < nce; i++)
            b2 += CE[i].hi - CE[i].lo - (CE[i].hhi - CE[i].hlo);
        if (b2 != b) { extern int printf(const char *, ...); printf("stats_size: the G walk summed %u bytes, the list holds %u\n", b, b2); __builtin_trap(); }
    }
#endif
    {   /* S16: bytes with a fingerprint-table entry (the rest is compared in full) */
        u32 c = 0;
        for (i = 0; SD_FP_ON && FP && i < nst + nce; i++) {   /* without a table fp_on is false for every range */
            const struct sd_range *r = i < nst ? &ST[i] : &CE[i - nst];
            if (fp_on(r))
                c += r->hi - r->lo;
        }
        sd_x16.fp_cov_bytes = c;
        sd_x16.fp_lim = fp_lim;
        sd_x16.m_top = m_top;
        sd_x16.free_bytes = cap_m - m_top + m_free_bytes;
    }
    sd_st.cells = nce;
    sd_st.statics = nst;
    sd_st.bytes = b;
    {   /* B0: capacity peaks at a capture (cells list vs cap_ce, extras ring vs cap_x, journal vs cap_j) */
        u32 xf = x_head - (nsl ? SL[0].xb : x_head), jf = j_head - j_tail();
        if (nce > sd_st.cells_max) sd_st.cells_max = nce;
        if (xf > sd_st.xring_max) sd_st.xring_max = xf;
        if (jf > sd_st.jring_max) sd_st.jring_max = jf;
    }
}

static void push_slot(s32 frame, u32 lb, u32 lm, u32 xb, u32 jb)
{
    struct sd_slot *s;
    if (nsl >= SD_SLOTS) {
        drop_oldest();
        sd_st.log_full_drops++;
    }
    s = &SL[nsl++];
    s->frame = frame;
    s->seq = seq;
    s->lb = lb;
    s->lm = lm;
    s->le = log_head;
    s->xb = xb;
    s->xe = x_head;
    s->jb = jb;
    s->je = j_head;
}

static u32 forced_reb;                       /* S16: this base capture is a forced rebuild (counted ok / failed) */
static int base_capture(s32 frame)
{
    u32 i, t0 = sd_plat_time(), xb;
    nsl = 0;
    step_new = 0;
    if (built)
        sd_plat_pre_capture();
    pend_lost = 0;
    if (!built || sd_st.evt_overflow || apply_events(0) < 0) {
        build();
        sd_plat_pre_capture();               /* barrier keys of the new list */
    }
    fp_masks();                              /* S11 */
    for (i = 0; i < nst; i++) {
        copy_live_to_m(&ST[i]);
        fp_range(&ST[i]);                    /* S11 */
        ST_FL[i] &= (u8)~2u;
    }
    for (i = 0; i < nce; i++) {
        copy_live_to_m(&CE[i]);
        fp_range(&CE[i]);                    /* S11 */
    }
    log_head = 0;
    x_head = 0;
    j_head = 0;
    xb = x_head;
    sd_plat_extras_put();
    push_slot(frame, log_head, log_head, xb, j_head);
    if (inc_on && seq >= inc_seq)
        sd_x16.inc_caps++;                   /* S16 */
    seq++;
    stats_size(0, 0);
    if (forced_reb) {                        /* S16 */
        if (inc_on) sd_x16.rebuilds_fail++; else sd_x16.rebuilds_ok++;
        forced_reb = 0;
    }
    sd_st.base_captures++;
    sd_st.captures++;
    sd_st.t_base_last = sd_plat_time() - t0;
    sd_st.extras_last = x_head - xb;
    return 0;
}

int sd_capture(s32 frame)
{
    u32 i, t0, lb, lm, xb, jb, dskip = 0, g_done = 0;
    if (!sd_active)
        return -1;
    if (!nsl || pend_lost || sd_st.evt_overflow)
        return base_capture(frame);
    t0 = sd_plat_time();
    sd_plat_pre_capture();                   /* barrier keys -> sd_mark_dirty */
    lb = SL[nsl - 1].le;                     /* pending pre-images since the previous capture belong to this step */
    if (log_head - lb > sd_st.pre_max)       /* S6C: pre-images of all frames since the previous capture */
        sd_st.pre_max = log_head - lb;
    jb = j_head;
    lm = log_head;                           /* the entries below are previous-capture contents */
    lines = 0;
    cmp_fail = 0;
    fp_masks();                              /* S11 */
    fpv_now = sd_fpv && ((SD_FP_ON && FP) || sd_cold) && ++fpv_ctr % sd_fpv == 0;   /* S11: also verifies cold mode */
    fpv_cap_miss = 0;
    step_new = 0;                            /* S16: bytes of new cells in this step */
    if (apply_events(1) < 0)
        return base_capture(frame);
    if (step_new > sd_x16.step_new_max)
        sd_x16.step_new_max = step_new;
    if (s17_g_ok()) {                        /* S17: the compare pass by DMA through the locked cache (CPU loops below when not run) */
        int gr = s17_cap_g(&dskip);
        if (gr > 0)
            g_done = 1;
        else if (gr < 0)
            cmp_fail = 1;                    /* DMA failure: the capture below becomes a base capture */
    }
    for (i = 0; !g_done && i < nst && !cmp_fail; i++) {
        if ((ST_FL[i] & 3) == 1) {           /* barrier range, not dirty */
            dskip += ST[i].hi - ST[i].lo;
            if (sd_verify) {
                u32 c = diff_count(&ST[i]);
                if (c) {
                    sd_barrier_miss += c;
                    cmp_range(&ST[i]);
                }
            }
            continue;
        }
        cmp_range(&ST[i]);
        ST_FL[i] &= (u8)~2u;
    }
    {   /* S11 part 2: cold-site mode */
        u32 scan = !sd_cold || !sd_cold_n || ++cold_ctr % sd_cold_n == 0, skipped = 0, ncold = 0, bcold = 0;
        u32 rescan_now = cold_rescan;
        if (sd_cold && !scan && cold_rescan && !(sd_cold & 2u)) {   /* after any restore (bit 1: TEST ONLY, off) */
            scan = 1;
            sd_x11c.rescans++;
        }
        cold_rescan = 0;
        if (sd_cold && scan)
            sd_x11c.scans++;
        for (i = 0; !g_done && i < nce && !cmp_fail; i++) {
            struct sd_range *r = &CE[i];
            u32 c0, marked;
            if (r->born == seq) {            /* cells added in this step got a fresh copy */
                r->site &= ~SD_SITE_SCAN;
                continue;
            }
            if (sd_cold && (r->site & SD_SITE_COLD)) {
                ncold++;
                bcold += r->hi - r->lo;
                if (!scan && !(r->site & SD_SITE_SCAN)) {
                    u32 nl = (r->hi - r->lo + SD_LINE - 1) / SD_LINE;
                    skipped += nl;
                    if (fpv_now) {           /* verify: a skipped cold cell must not differ from M */
                        u32 a0 = 0, d = (r->hhi > r->hlo) ? diff_span(r, r->lo, r->hlo, &a0) + diff_span(r, r->hhi, r->hi, &a0)
                                                          : diff_span(r, r->lo, r->hi, &a0);
                        sd_x11c.vlines += nl;
                        if (d) {
                            sd_x11c.miss_lines += d;
                            fpv_cap_miss |= 2;
                            if (sd_fpv_fix)
                                cmp_range(r);
                        }
                    }
                    continue;
                }
            }
            c0 = r->chg;
            marked = (r->site & SD_SITE_SCAN) || rescan_now;
            r->site &= ~SD_SITE_SCAN;
            cmp_range(r);
            if (sd_cold_mark && r->chg != c0 && !marked && (r->site & SD_SITE_UCHG) != SD_SITE_UCHG)
                r->site += SD_SITE_UCHG1;    /* S11 profile: a change no invalidation explains (saturating) */
        }
        sd_x11c.skip_lines += skipped;
        sd_x11c.skip_caps += skipped != 0;
        sd_x11c.cells_cold = ncold;
        sd_x11c.bytes_cold_last = bcold;
        if (fpv_cap_miss & 2)
            sd_x11c.miss_caps++;
    }
    if (fpv_now) {                           /* S11 */
        sd_x11.fpv_caps++;
        sd_x11.fpv_miss_caps += fpv_cap_miss & 1;
    }
    if (cmp_fail || pend_lost) {
        sd_st.errors++;
        return base_capture(frame);
    }
    xb = x_head;
    sd_plat_extras_put();
    if (!nsl)                                /* the extras ring dropped every capture: start over */
        return base_capture(frame);
    push_slot(frame, lb, lm, xb, jb);
    if (inc_on && seq >= inc_seq)
        sd_x16.inc_caps++;                   /* S16 */
    seq++;
    stats_size(g_done, s17_it.bytes);
    sd_st.captures++;
    sd_st.last_lines = lines;
    if (lines > sd_st.max_lines)
        sd_st.max_lines = lines;
    sd_st.extras_last = x_head - xb;
    if (sd_st.extras_last > sd_st.extras_max)
        sd_st.extras_max = sd_st.extras_last;
    sd_st.dirty_skip_bytes = dskip;
    sd_st.t_cap_last = sd_plat_time() - t0;
    if (fpv_now) { sd_x11.t_cap_vsum += sd_st.t_cap_last; sd_x11.t_cap_vn++; }   /* S11 */
    else { sd_x11.t_cap_sum += sd_st.t_cap_last; sd_x11.t_cap_n++; }
    sd_x11.lines_sum += lines;
    if (sd_st.t_cap_last > sd_st.t_cap_max)
        sd_st.t_cap_max = sd_st.t_cap_last;
    return 0;
}

int sd_have(s32 frame)
{
    u32 i;
    for (i = 0; i < nsl; i++)
        if (SL[i].frame == frame)
            return 1;
    return 0;
}

/* S6C: newest capture whose frame is <= `frame` (captures are pushed in frame order between two restores) */
int sd_newest_le(s32 frame, s32 *out)
{
    s32 j, best = 0;
    int found = 0;
    for (j = (s32)nsl - 1; j >= 0; j--)
        if (SL[j].frame <= frame && (!found || SL[j].frame > best)) {
            best = SL[j].frame;
            found = 1;
        }
    if (found && out)
        *out = best;
    return found;
}

/* S16: newest COMPLETE capture with frame <= `frame` (same rule as sd_newest_le, incomplete captures skipped) */
static int slot_ok(u32 j) { return !(sd_strict && inc_on && SL[j].seq >= inc_seq); }
int sd_newest_ok_le(s32 frame, s32 *out)
{
    s32 j, best = 0;
    int found = 0;
    for (j = (s32)nsl - 1; j >= 0; j--)
        if (SL[j].frame <= frame && slot_ok((u32)j) && (!found || SL[j].frame > best)) {
            best = SL[j].frame;
            found = 1;
        }
    if (found && out)
        *out = best;
    return found;
}

u32 sd_free_bytes(void) { return cap_m - m_top + m_free_bytes; }
u32 sd_reserve(void) { return 2u * sd_x16.step_new_max + 16384u; }
/* S16: bit0 the newest capture is incomplete (a drop since the last rebuild / older restore), bit1 pending pre-images
   were lost (restores refused until the next capture), bit2 free arena below the reserve (a new cell could be lost) */
u32 sd_health(void)
{
    u32 h = 0;
    if (!sd_active)
        return 0;
    if (inc_on)
        h |= SD_H_INC;
    if (pend_lost)
        h |= SD_H_LOST;
    if (sd_free_bytes() < sd_reserve())
        h |= SD_H_LOW;
    return h;
}

/* S16: forced rebuild (base capture of a fresh manifest) at the caller's request; the caller guarantees that no rollback
   can be needed (every simulated frame confirmed).  Returns 0 and the state is complete again, or 1 (still incomplete). */
int sd_rebuild(s32 frame)
{
    if (!sd_active)
        return -1;
    built = 0;
    forced_reb = 1;
    base_capture(frame);
    return inc_on ? 1 : 0;
}

u32 sd_nslots(void) { return nsl; }

u32 sd_seq(void) { return seq; }             /* S11 */

/* S11: a hooked mutator (HSD_ObjAlloc / HSD_ObjFree, sd_melee.c) writes into the cell that contains a: compare that cell at
   the next capture even if it is cold (cells only; binary search over the sorted list) */
void sd_touch(u32 a)
{
    s32 lo = 0, hi;
    if (!sd_cold_mark || !sd_active)
        return;
    hi = (s32)nce - 1;
    while (lo <= hi) {
        s32 m = (lo + hi) >> 1;
        if (a < CE[m].lo) hi = m - 1;
        else if (a >= CE[m].hi) lo = m + 1;
        else {
            CE[m].site |= SD_SITE_SCAN;
            sd_x11c.touch++;
            return;
        }
    }
}

s32 sd_newest_frame(void) { return nsl ? SL[nsl - 1].frame : (s32)0x80000000; }   /* S10 */

void sd_drop_all(void)
{
    nsl = 0;
    nev = 0;
}

/* S16: forget every capture AND the manifest list (the next capture builds it afresh, as at the start of a match); the
   incomplete flag is kept (a probe capture that found the engine too small keeps the glue gate closed) */
void sd_forget(void)
{
    nsl = 0;
    nev = 0;
    built = 0;
    pend_lost = 0;
    sd_st.evt_overflow = 0;
    m_free_bytes = 0;
}

/* ---------------------------------------------------------------- S10: address -> range, KEEP / RSET restore */
/* the range of the current list that contains [a, a+4): cells (sorted, binary search), then statics (sorted) */
static struct sd_range *rf_hint;             /* S17 (SD_S17_U): the range of the previous hit (cell or static): consecutive undo entries lie in one range or the next one */
static struct sd_range *rfind(u32 a)
{
    s32 lo = 0, hi = (s32)nce - 1;
    if (SD_S17F & SD_S17_U) {
        struct sd_range *h = rf_hint;
        if (h) {
            struct sd_range *end = (h >= CE && h < CE + nce) ? CE + nce : ((h >= ST && h < ST + nst) ? ST + nst : 0);
            if (end) {                       /* ranges are disjoint, so a hit in the hint or in its successor is what the search would find */
                if (a >= h->lo && a < h->hi) { sd_x17.rf_hit++; return h; }
                if (h + 1 < end && a >= h[1].lo && a < h[1].hi) { rf_hint = h + 1; sd_x17.rf_hit++; return h + 1; }
            }
        }
    }
    while (lo <= hi) {
        s32 m = (lo + hi) >> 1;
        if (a < CE[m].lo) hi = m - 1;
        else if (a >= CE[m].hi) lo = m + 1;
        else { rf_hint = &CE[m]; sd_x17.rf_miss++; return &CE[m]; }
    }
    lo = 0; hi = (s32)nst - 1;
    while (lo <= hi) {
        s32 m = (lo + hi) >> 1;
        if (a < ST[m].lo) hi = m - 1;
        else if (a >= ST[m].hi) lo = m + 1;
        else { rf_hint = &ST[m]; sd_x17.rf_miss++; return &ST[m]; }
    }
    return 0;
}

/* compare live vs M on [lo, hi) of r; copy M -> live where a 32-byte line differs.  Returns the differing lines. */
static u32 fix_span(const struct sd_range *r, u32 lo, u32 hi, u32 *a0)
{
    u32 *p = SDP(lo), *m = MP(r->moff + (lo - r->lo)), n = (hi - lo) / 4, nl = 0, a = lo;
    while (n) {
        u32 k = n < 8 ? n : 8, d = 0, i;
        for (i = 0; i < k; i++)
            d |= p[i] ^ m[i];
        if (d) {
            if (!nl && a0) {
                for (i = 0; i < k && p[i] == m[i]; i++)
                    ;
                *a0 = a + 4 * i;
            }
            cpy(p, m, k);
            nl++;
        }
        p += k; m += k; a += 4 * k; n -= k;
    }
    return nl;
}

static u32 fix_range(const struct sd_range *r, u32 *a0)
{
    if (r->hhi > r->hlo)
        return fix_span(r, r->lo, r->hlo, a0) + fix_span(r, r->hhi, r->hi, a0);
    return fix_span(r, r->lo, r->hi, a0);
}

static u32 rsize(const struct sd_range *r) { return r->hi - r->lo - (r->hhi - r->hlo); }

static int in_rs(u32 kind, const struct sd_range *r)
{
    return kind ? sd_plat_cell_rs(r, MP(r->moff)) : (r->chg & SD_RS_BIT) != 0;
}

/* apply one undo entry to live, and to M when the address lies in a range of the current list */
static u32 s17_neg_u;                        /* S17 TEST: the negative control skips one mirror write */
static void undo_ent(const struct sd_ent *e, int to_m)
{
    u32 ix = (u32)(e - LOG);                 /* S11: entries come from the log ring */
    u32 whole = (SD_S17F & SD_S17_U) && (e->len & ~3u) == 32u && !(e->addr & 31u);   /* S17: a whole aligned line: dcbz, no read for ownership */
    if (whole) {
        lc_dcbz(SDP(e->addr));
        sd_x17.u_whole++;
    } else if (SD_S17F & SD_S17_U)
        sd_x17.u_part++;
    cpy(SDP(e->addr), e->d, e->len / 4);
    sd_x.undo_ents_last++;
    sd_x.undo_bytes_sum += e->len & ~3u;
    if (to_m) {
        struct sd_range *r = rfind(e->addr);
        if (r && !(r->hhi > r->hlo && e->addr >= r->hlo && e->addr < r->hhi)) {
            u32 *mm = MP(r->moff + (e->addr - r->lo));
            if (whole && !((unsigned long)mm & 31ul))
                lc_dcbz(mm);                 /* S17: the whole mirror line is overwritten */
            if ((SD_S17F & SD_S17_NEG_U) && whole && !s17_neg_u)
                s17_neg_u = 1;               /* TEST ONLY: one mirror write left out (the mirror must then differ from live) */
            else
                cpy(mm, e->d, e->len / 4);
            s17_mflush(mm, e->len & ~3u);    /* S17: mirror memory current */
            if (fp_on(r)) {                  /* S11: the line's previous fingerprint */
                u32 *f = fp_of(r, e->addr);
                if (e->len & SD_ENT_LFP) {
                    f[0] = LFP[2 * ix];
                    f[1] = LFP[2 * ix + 1];
                } else {                     /* S16: logged while this range was not fingerprinted (or is a new block): recompute */
                    fpl(e->d, e->len / 4, f);
                    sd_x16.fp_recomp++;
                }
            }
            sd_x.m_ents_last++;
            sd_x.m_bytes_sum += e->len & ~3u;
        } else
            sd_x.m_nomap++;
    }
}

/* ---------------------------------------------------------------- S17 undo-step fix
   The per-entry undo of rbtest V5 (undo_ent with to_m, dd_apply) cost 1.2 ms per step on the console against 0.30-0.33 in kbench U1 and the same
   in Dolphin instruction ticks: per entry a function call, two copy calls, an rfind whose hint is re-validated against both tables on every call
   (and a binary search over cells, then statics, on a miss), a dcbst-range call, a fingerprint check and memory counters, plus `c % cap_log`.
   The fix keeps every write and its order (live then mirror, entry by entry, the same words) and changes only how the work is done:
   - one run per step over [lm, le) with a ring cursor (no modulo), counters kept in registers and added once per run;
   - the mirror line is found by a CURSOR into the current range table, valid for the whole run (the list does not change inside a step's
     entry run: its journal was reverted before): the cursor range, its successor, a forward gallop (entries come in capture order, i.e.
     ascending inside a table), and the full rfind otherwise.  Disjoint sorted ranges: the range found is the one rfind returns
     (checked on every lookup in the host build, SD_HOST);
   - a whole aligned line is read once into registers and stored to live and to the mirror (dcbz first, as U1);
   - mirror dcbsts are batched per line: a written mirror line is flushed when the run moves to another line and at the end of the run
     (no DMA reads the mirror inside the undo; rule 2 of sd_s17.h holds when restore_keep returns);
   - the DD chain (0x10) applies its entries through the same cursor and a whole-line fast path (both masks full, the line inside the range
     and outside its hole); other DD entries keep the per-word path.
   The mirror address is NOT stored in the log entry (the other option of rank 1): a journal revert or an earlier restore re-allocates the
   mirror block of a re-inserted cell, so a stored offset would need a validity stamp per cell; the cursor gives the same lookup cost without it.
   Off: SD_S17_U_OLD (TEST / A-B), no U, or a fingerprint table (FP: undo_ent keeps the LFP handling). */
#ifdef SD_HOST
#include <stdio.h>
#include <stdlib.h>
#endif
struct sd_uf sd_uf;
static int uf_on(void) { return (SD_S17F & (SD_S17_U | SD_S17_U_OLD)) == SD_S17_U && !FP; }

static u32 uf_kind, uf_i;                    /* cursor: table (0 none, 1 cells, 2 statics) and index; valid for one run */
static unsigned long uf_pend;                /* mirror line written and not yet flushed (pointer value, 0 = none) */
static u32 uf_mc;                            /* the mirror memory must be kept current (G or H on) */

static void uf_begin(void)
{
    struct sd_range *h = rf_hint;
    uf_kind = 0;
    if (h && h >= CE && h < CE + nce) { uf_kind = 1; uf_i = (u32)(h - CE); }
    else if (h && h >= ST && h < ST + nst) { uf_kind = 2; uf_i = (u32)(h - ST); }
    uf_pend = 0;
    uf_mc = s17_mc() != 0;
    sd_uf.runs++;
}

static inline __attribute__((always_inline)) void uf_flush_line(unsigned long line)
{
    if (line != uf_pend) {
        if (uf_pend) { lc_dcbst((const void *)(unsigned long)uf_pend); sd_uf.dcbst++; sd_x17.mflush_lines++; }
        uf_pend = line;
    } else
        sd_uf.dcbst_saved++;
}

static void uf_end(void)
{
    if (uf_pend) { lc_dcbst((const void *)(unsigned long)uf_pend); sd_uf.dcbst++; sd_x17.mflush_lines++; }
    uf_pend = 0;
    if (uf_kind == 1 && uf_i < nce) rf_hint = &CE[uf_i];
    else if (uf_kind == 2 && uf_i < nst) rf_hint = &ST[uf_i];
}

/* note the mirror bytes [p, p + bytes) as written: their lines are flushed later (pending-line batch); only when the mirror must be current */
static inline __attribute__((always_inline)) void uf_mnote(const u32 *p, u32 bytes)
{
    unsigned long l, e;
    if (!uf_mc || !bytes) return;
    l = (unsigned long)p & ~31ul;
    e = ((unsigned long)p + bytes - 1ul) & ~31ul;
    for (;;) {
        uf_flush_line(l);
        if (l == e) break;
        l += 32u;
    }
}

#ifdef SD_HOST
static struct sd_range *rfind_plain(u32 a)   /* the lookup without any hint (host check of the cursor) */
{
    s32 lo = 0, hi = (s32)nce - 1;
    while (lo <= hi) { s32 m = (lo + hi) >> 1; if (a < CE[m].lo) hi = m - 1; else if (a >= CE[m].hi) lo = m + 1; else return &CE[m]; }
    lo = 0; hi = (s32)nst - 1;
    while (lo <= hi) { s32 m = (lo + hi) >> 1; if (a < ST[m].lo) hi = m - 1; else if (a >= ST[m].hi) lo = m + 1; else return &ST[m]; }
    return 0;
}
#endif

static struct sd_range *uf_find2(u32 a)
{
    struct sd_range *r;
    if (uf_kind) {
        struct sd_range *t = uf_kind == 1 ? CE : ST;
        u32 n = uf_kind == 1 ? nce : nst, i = uf_i;
        if (a >= t[i].hi) {                  /* forward gallop: first index whose hi > a (hi ascends: ranges are sorted and disjoint) */
            u32 lo = i + 1, hi = i + 1, st = 1;
            while (hi < n && t[hi].hi <= a) { lo = hi + 1; hi += st; st <<= 1; }
            hi = hi < n ? hi + 1 : n;
            while (lo < hi) { u32 m = lo + ((hi - lo) >> 1); if (t[m].hi <= a) lo = m + 1; else hi = m; }
            if (lo < n && a >= t[lo].lo) { uf_i = lo; sd_uf.gallop++; return &t[lo]; }
        }
    }
    r = rfind(a);                            /* the full lookup (cells, then statics) */
    sd_uf.full++;
    if (r && r >= CE && r < CE + nce) { uf_kind = 1; uf_i = (u32)(r - CE); }
    else if (r) { uf_kind = 2; uf_i = (u32)(r - ST); }
    return r;
}

static inline __attribute__((always_inline)) struct sd_range *uf_find(u32 a)
{
    struct sd_range *r = 0;
    if (uf_kind) {
        struct sd_range *t = uf_kind == 1 ? CE : ST;
        u32 n = uf_kind == 1 ? nce : nst;
        if (a >= t[uf_i].lo && a < t[uf_i].hi) { r = &t[uf_i]; sd_uf.hint++; }
        else if (uf_i + 1 < n && a >= t[uf_i + 1].lo && a < t[uf_i + 1].hi) { uf_i++; r = &t[uf_i]; sd_uf.next++; }
    }
    if (!r)
        r = uf_find2(a);
#ifdef SD_HOST
    if (r != rfind_plain(a)) { fprintf(stderr, "undo-step fix: cursor lookup of %08x differs from rfind\n", a); abort(); }
#endif
    return r;
}

#ifndef SD_U2_DEFAULT                        /* with undo step v2 as the build's default the fix's own entry run is never called (uf2_run replaces it; cursor, lookup and flush helpers stay) */
/* [c, ce) of the log: apply the previous-capture contents to live and to the mirror (undo_ent(e, 1) for each, in order) */
static void uf_run(u32 c, u32 ce)
{
    u32 pos = c % cap_log, ne = 0, nb = 0, me = 0, mb = 0, nomap = 0, whole = 0, part = 0;
    uf_begin();
    for (; c != ce; c++) {
        const struct sd_ent *e = &LOG[pos];
        u32 a = e->addr, bytes = e->len & ~3u, *lv = SDP(a);
        struct sd_range *r;
        if (++pos == cap_log) pos = 0;
        ne++; nb += bytes;
        r = uf_find(a);
        if (bytes == 32u && !(a & 31u)) {    /* a whole aligned line: one read, two stores (dcbz first, as U1) */
            const u32 *s = e->d;
            u32 w0 = s[0], w1 = s[1], w2 = s[2], w3 = s[3], w4 = s[4], w5 = s[5], w6 = s[6], w7 = s[7];
            whole++;
            lc_dcbz(lv);
            lv[0] = w0; lv[1] = w1; lv[2] = w2; lv[3] = w3; lv[4] = w4; lv[5] = w5; lv[6] = w6; lv[7] = w7;
            SD_NOTE_W(lv, 32u);
            if (r && !(r->hhi > r->hlo && a >= r->hlo && a < r->hhi)) {
                u32 *mm = MP(r->moff + (a - r->lo));
                if (!((unsigned long)mm & 31ul))
                    lc_dcbz(mm);
                if ((SD_S17F & SD_S17_NEG_U) && !s17_neg_u)
                    s17_neg_u = 1;           /* TEST ONLY: one mirror write left out (the mirror must then differ from live) */
                else {
                    mm[0] = w0; mm[1] = w1; mm[2] = w2; mm[3] = w3; mm[4] = w4; mm[5] = w5; mm[6] = w6; mm[7] = w7;
                    SD_NOTE_W(mm, 32u);
                }
                uf_mnote(mm, 32u);
                me++; mb += 32u;
            } else
                nomap++;
        } else {
            u32 n = bytes >> 2, i;
            part++;
            for (i = 0; i < n; i++) lv[i] = e->d[i];
            SD_NOTE_W(lv, bytes);
            if (r && !(r->hhi > r->hlo && a >= r->hlo && a < r->hhi)) {
                u32 *mm = MP(r->moff + (a - r->lo));
                for (i = 0; i < n; i++) mm[i] = e->d[i];
                SD_NOTE_W(mm, bytes);
                uf_mnote(mm, bytes);
                me++; mb += bytes;
            } else
                nomap++;
        }
    }
    uf_end();
    sd_x.undo_ents_last += ne; sd_x.undo_bytes_sum += nb;
    sd_x.m_ents_last += me; sd_x.m_bytes_sum += mb; sd_x.m_nomap += nomap;
    sd_x17.u_whole += whole; sd_x17.u_part += part;
    sd_uf.ents += me;
}
#endif


/* ---------------------------------------------------------------- S17 undo step v2 (SD_S17_U2; ROLLBACK_OPTIMIZATION section 8)
   The undo-step fix above still ran about 110 PowerPC instructions per whole-line entry (the compiler reloads every file-scope variable after each
   `memory` asm, the cursor and the pending-line batch live in memory, three counters per entry, the NEG_U test, the lookup and the dcbst loop are
   out of line) and read the log cold from main memory (the entries of a step are 7 frames old: not in the 256 KB L2; the engine reads them one
   miss at a time, kbench U1 read a log the capture had just written).  uf2_run keeps the same writes in the same order (per entry: live line,
   then the mirror line; entries of a step in log order) and changes only how they are issued:
   - the mirror range of the previous entry is cached in registers (lo, span, hole, mirror base): an entry inside it needs two compares, no
     lookup; any other entry goes through uf_find (cursor, successor, gallop, rfind: the same range, checked against rfind_plain in the host build);
   - the run is cut at the ring end into at most two straight loops (no wrap test per entry);
   - the log is read ahead with dcbt, U2_AHEAD entries (two touches per entry cover every line of the 40-byte entries);
   - a whole mirror line is flushed with dcbst right after its stores (a line is never flushed late), partial entries flush their lines;
   - no counter is touched per entry: the totals are derived at the end of the run from the entry count and the partial/unmapped tallies.
   Final memory contents of live and of the mirror are those of uf_run (host digest test, run_tests.sh section 11f). */
#define U2_AHEAD 5u                          /* entries read ahead: about 5 x 50 cycles of work against a 48-cycle miss, 3 misses in flight at most */
struct sd_u2 sd_u2;
#ifndef SD_NO_U2
#ifdef SD_U2_DEFAULT                         /* B2 C1: the lean loop is the undo step whenever the undo-step fix would run (U, no U_OLD, no
                                                fingerprint table); the SD_S17_U2 bit is not needed (the release reads sd_s17 masked to G|H|U, SD_REL_S17) and the read-ahead is on */
static int u2_on(void) { return uf_on(); }
#else
static int u2_on(void) { return uf_on() && (SD_S17F & SD_S17_U2); }
#endif

static __attribute__((noinline)) struct sd_range *u2_seek(u32 a) { return uf_find(a); }

static void uf2_run(u32 c, u32 ce)
{
    u32 total = ce - c, n = total, pos = c % cap_log, part = 0, partb = 0, partm = 0, nomap_w = 0, nomap_p = 0, seeks = 0, mfl = 0, i;
    const u32 mc = s17_mc() != 0, negu = (SD_S17F & SD_S17_NEG_U) != 0, pf = (SD_S17F & SD_S17_U2_NOPF) == 0;
    u32 lo = 0, span = 0;                          /* the cached range: [lo, lo + span); span 0 = none */
    const struct sd_range *rp = 0;                 /* ... and its record (hole, mirror offset are read from it only when the entry is mirrored) */
    struct sd_range *t = 0;                        /* the cursor's table, index and size: the successor is tried inline */
    u32 ti = 0, tn = 0;
    u32 hlo = 0, hspan = 0;                        /* B2 C1b: the hole of the cached range, [hlo, hlo + hspan) (0 = none), and the mirror address of its first byte: */
    u8 *mb = 0;                                    /* read from the record when the range changes, not per entry (MA and the list are fixed for the run) */
    uf_begin();
    while (n) {
        u32 k = cap_log - pos;
        const struct sd_ent *e = &LOG[pos], *ee;
        if (k > n) k = n;
        n -= k;
        pos = 0;
        ee = e + k;
        if (pf)
            for (i = 0; i < U2_AHEAD; i++) { lc_dcbt(e + i); lc_dcbt((const char *)(e + i) + 32); }
        for (; e != ee; e++) {
            u32 a = e->addr, len = e->len, *lv = SDP(a), ok;
            if (pf) {
                lc_dcbt(e + U2_AHEAD);
                lc_dcbt((const char *)(e + U2_AHEAD) + 32);
            }
            if (a - lo >= span) {                  /* not in the cached range */
                const struct sd_range *q = ti + 1 < tn ? &t[ti + 1] : 0;
                if (q && a - q->lo < q->hi - q->lo) {   /* the successor in the cursor's table (73 % of the lookups in the FD run) */
                    ti++;
                    rp = q; lo = q->lo; span = q->hi - q->lo;
                } else {                           /* gallop / full lookup (uf_find: the same range as rfind), then re-seat the cursor */
                    struct sd_range *r;
                    if (t && ti < tn)              /* B2 C1b: the global cursor follows the inline successor steps, so the gallop starts at the range before this entry */
                        uf_i = ti;
                    r = u2_seek(a);
                    seeks++;
                    rp = r; lo = r ? r->lo : 0; span = r ? r->hi - r->lo : 0;
                    if (uf_kind == 1) { t = CE; tn = nce; ti = uf_i; }
                    else if (uf_kind == 2) { t = ST; tn = nst; ti = uf_i; }
                    else { t = 0; tn = 0; ti = 0; }
                    if (!r) { ti = tn; }           /* no range: the next lookup takes the full path */
                }
                if (rp) {
                    hlo = rp->hlo;
                    hspan = rp->hhi > hlo ? rp->hhi - hlo : 0u;
                    mb = MA + rp->moff;
                } else
                    hspan = 0;
            }
            ok = a - lo < span;                    /* a mirror range holds the entry */
#ifdef SD_HOST
            {   /* host check of the cache: an entry inside the cached range is in the range a hint-free rfind finds, and one outside it is in no range */
                struct sd_range *q = rfind_plain(a);
                if (ok != (q != 0) || (q && q != rp)) { fprintf(stderr, "undo step v2: cached range of %08x differs from rfind\n", a); abort(); }
            }
#endif
            if (ok && a - hlo < hspan)
                ok = 0;                            /* the entry starts inside the hole of the cell: no mirror */
            if ((len & ~3u) == 32u && !(a & 31u)) {   /* a whole aligned line: one read, two stores (dcbz first, as U1) */
                const u32 *s = e->d;
                u32 w0 = s[0], w1 = s[1], w2 = s[2], w3 = s[3], w4 = s[4], w5 = s[5], w6 = s[6], w7 = s[7];
                lc_dcbz(lv);
                lv[0] = w0; lv[1] = w1; lv[2] = w2; lv[3] = w3; lv[4] = w4; lv[5] = w5; lv[6] = w6; lv[7] = w7;
                SD_NOTE_W(lv, 32u);
                if (ok) {
                    u32 *mm = (u32 *)(void *)(mb + (a - lo));
                    if (!((unsigned long)mm & 31ul))
                        lc_dcbz(mm);
                    if (negu && !s17_neg_u)
                        s17_neg_u = 1;            /* TEST ONLY: one mirror write left out (the mirror must then differ from live) */
                    else {
                        mm[0] = w0; mm[1] = w1; mm[2] = w2; mm[3] = w3; mm[4] = w4; mm[5] = w5; mm[6] = w6; mm[7] = w7;
                        SD_NOTE_W(mm, 32u);
                    }
                    if (mc) {
                        lc_dcbst(mm);
                        if ((unsigned long)mm & 31ul) {   /* the mirror line is not congruent to the live line: the entry spans two mirror lines */
                            lc_dcbst((const char *)mm + 28);
                            mfl++;
                        }
                    }
                } else
                    nomap_w++;
            } else {                               /* a partial entry (statics cut at odd words, a line that is not whole): word stores */
                u32 bytes = len & ~3u, nw = bytes >> 2, j;
                part++;
                partb += bytes;
                for (j = 0; j < nw; j++) lv[j] = e->d[j];
                SD_NOTE_W(lv, bytes);
                if (ok) {
                    u32 *mm = (u32 *)(void *)(mb + (a - lo));
                    for (j = 0; j < nw; j++) mm[j] = e->d[j];
                    SD_NOTE_W(mm, bytes);
                    partm += bytes;
                    if (mc && bytes) {
                        lc_dcbst_range(mm, bytes);
                        mfl += (u32)(((unsigned long)mm + bytes + 31ul) / 32ul - (unsigned long)mm / 32ul);
                    }
                } else
                    nomap_p++;
            }
        }
    }
    if (t && ti < tn) uf_i = ti;                   /* the cursor for the next run (rf_hint) */
    uf_end();
    {
        u32 whole = total - part, mw = whole - nomap_w, me = mw + part - nomap_p;
        sd_x.undo_ents_last += total; sd_x.undo_bytes_sum += 32u * whole + partb;
        sd_x.m_ents_last += me; sd_x.m_bytes_sum += 32u * mw + partm; sd_x.m_nomap += nomap_w + nomap_p;
        sd_x17.u_whole += whole; sd_x17.u_part += part;
        if (mc) sd_x17.mflush_lines += mw + mfl;
        sd_uf.ents += me;
        sd_u2.runs++; sd_u2.ents += total; sd_u2.seek += seeks; sd_u2.part += part; sd_u2.nomap += nomap_w + nomap_p;
    }
}
#else                                        /* SD_NO_U2 (product profiles): U2 is opt-in above the 16 flag bits the product glue passes, so it is unreachable there */
static int u2_on(void) { return 0; }
static void uf2_run(u32 c, u32 ce) { (void)c; (void)ce; }
#endif

/* S17 R1 lab: copy the live line `line` (32-aligned address) from the mirror of the newest capture (words inside a range and outside its hole); 1 = copied.
   Used by the glue after a verified census to repair the few residual lines before a chain-only restore. */
int sd_census_fix(u32 line)
{
    struct sd_range *r = rfind(line), *r2 = rfind(line + 28u);
    u32 w, n = 0;
    if (!sd_active || !nsl)
        return 0;
    for (w = 0; w < 8; w++) {
        u32 a = line + 4u * w;
        struct sd_range *q = (r && a >= r->lo && a < r->hi) ? r : ((r2 && a >= r2->lo && a < r2->hi) ? r2 : 0);
        if (!q || (q->hhi > q->hlo && a >= q->hlo && a < q->hhi))
            continue;
        *SDP(a) = *MP(q->moff + (a - q->lo));
        SD_NOTE_W(SDP(a), 4u);
        n++;
    }
    return (int)n;
}

/* ---------------------------------------------------------------- S17 DD (AVENUE 2): deduplicated undo chain
   Old order (newest step first; within a step the allocator pre-images [lb, lm) in reverse, live only, then the previous-capture
   contents [lm, le) ascending, live and M): the LAST write of a word wins.  DD scans the entries in the REVERSE of that order (oldest step
   first; within a step [le, lm) descending, then [lb, lm) ascending) and gives every word to the first entry that covers it: that is the entry
   the old order would have applied last, so live gets exactly the same bytes.  The mirror gets the same words from the [lm, le) entries only
   (its own first-cover mask: an allocator pre-image never writes M), mapped through the FINAL cell list (all journals reverted first).
   A pre-pass computes the masks without writing anything; when it cannot (hash table full, address outside MEM1, a fingerprint table) the
   old loop runs (counted, never silent).  Per entry: dd_mk[c - dd_base] = live mask | (mirror mask << 8), bit i = word i of the entry. */
static int dd_on(void) { return (SD_S17F & SD_S17_DD) && dd_ht && dd_mk && !FP && nsl >= 2; }

static u32 *dd_slot(u32 line)
{
    u32 h = (line * 0x9E3779B1u) >> 18, n;
    for (n = 0; n < DD_SLOTS; n++, h = (h + 1) & (DD_SLOTS - 1u)) {
        u32 *s = &dd_ht[2 * h];
        if ((s[0] >> 20) != dd_gen) {        /* empty: claim */
            if (dd_lines >= DD_MAXLINES) return 0;
            s[0] = (dd_gen << 20) | line;
            s[1] = 0;
            dd_lines++;
            return s;
        }
        if ((s[0] & 0xFFFFFu) == line)
            return s;
    }
    return 0;
}

/* scan one entry; isb = the entry is a previous-capture content entry (writes M); ix = position in the walk (c - dd_base).  0 = ok, -1 = give up.
   Fast path: the entry lies in one line (a whole aligned line or a partial one): word masks by shifts, no per-word loop. */
static int dd_scan(const struct sd_ent *e, u32 ix, int isb)
{
    u32 n = e->len >> 2, a = e->addr, w0 = (a >> 2) & 7u, lm = 0, mm = 0;
    if ((a & 3u) || (a & 0xFE000000u) != 0x80000000u || n > 8u || ix >= cap_log)
        return -1;
    if (w0 + n <= 8u) {                      /* one line */
        u32 *s = dd_slot((a >> 5) & 0xFFFFFu), want, got;
        if (!s) return -1;
        want = ((1u << n) - 1u) << w0;
        got = want & ~s[1];
        s[1] |= got;
        lm = got >> w0;
        if (isb) {
            got = want & ~(s[1] >> 8);
            s[1] |= got << 8;
            mm = got >> w0;
        }
    } else {                                 /* an unaligned entry that crosses a line */
        u32 i = 0;
        while (i < n) {
            u32 *s = dd_slot((a >> 5) & 0xFFFFFu), w = (a >> 2) & 7u;
            if (!s) return -1;
            for (; i < n && w < 8u; i++, w++, a += 4u) {
                u32 bit = 1u << w;
                if (!(s[1] & bit)) { s[1] |= bit; lm |= 1u << i; }
                if (isb && !(s[1] & (bit << 8))) { s[1] |= bit << 8; mm |= 1u << i; }
            }
        }
    }
    dd_mk[ix] = (u16)(lm | (mm << 8));
    return 0;
}

static u32 dd_base;                          /* first log counter of the walk: dd_mk is indexed by c - dd_base (no modulo) */

static int dd_prepass(s32 j)
{
    s32 k;
    u32 c;
    if (++dd_gen >= 0xFFFu) {                /* generation wrap: clear (once per 4,000 restores) */
        zero_words(dd_ht, 2 * DD_SLOTS);
        dd_gen = 1;
    }
    dd_lines = 0;
    dd_base = SL[j + 1].lb;
    if (SD_S17F & SD_S17_NEG_DD) {            /* TEST ONLY: the old order scanned as if it were the priority order: the newest pre-image wins */
        for (k = (s32)nsl - 1; k > j; k--) {
            for (c = SL[k].le; c != SL[k].lm;) { c--; if (dd_scan(&LOG[c % cap_log], c - dd_base, 1)) return -1; }
            for (c = SL[k].lb; c != SL[k].lm; c++) if (dd_scan(&LOG[c % cap_log], c - dd_base, 0)) return -1;
        }
        return 0;
    }
    for (k = j + 1; k < (s32)nsl; k++) {
        u32 pos = SL[k].le % cap_log;
        for (c = SL[k].le; c != SL[k].lm; c--) {                       /* [lm, le) newest entry first */
            pos = pos ? pos - 1u : cap_log - 1u;
            if (dd_scan(&LOG[pos], c - 1u - dd_base, 1)) return -1;
        }
        pos = SL[k].lb % cap_log;
        for (c = SL[k].lb; c != SL[k].lm; c++) {                       /* [lb, lm) oldest first */
            if (dd_scan(&LOG[pos], c - dd_base, 0)) return -1;
            if (++pos == cap_log) pos = 0;
        }
    }
    return 0;
}

/* write the words of `e` selected by mask to d (live or mirror); a full whole aligned line is stored with dcbz when U is on */
static void dd_put(u32 *d, const struct sd_ent *e, u32 mask, int line_store)
{
    u32 n = e->len >> 2, i = 0;
    if (mask == (1u << n) - 1u) {
        if (line_store && n == 8u && !((unsigned long)d & 31ul)) { lc_dcbz(d); sd_x17.u_whole++; }
        cpy(d, e->d, n);
        return;
    }
    while (i < n) {
        u32 r;
        if (!(mask & (1u << i))) { i++; continue; }
        for (r = 0; i + r < n && (mask & (1u << (i + r))); r++)
            ;
        cpy(d + i, e->d + i, r);
        i += r;
    }
}

static void dd_apply(const struct sd_ent *e, u32 c)
{
    u32 mk = dd_mk[c - dd_base], lm = mk & 0xFFu, mm = mk >> 8, n = e->len >> 2;
    int ustore = (SD_S17F & SD_S17_U) != 0, uf = uf_kind != 3u;   /* uf: the undo-step fix cursor is active (uf_begin in dd_chain) */
    sd_dd.ents_all++;
    if (!lm) { sd_dd.ents_skipped++; return; }
    sd_dd.ents_applied++;
    if (uf && mk == 0xFFFFu && n == 8u && !(e->addr & 31u)) {   /* S17 undo-step fix: a whole aligned line, every word to live and to M */
        u32 a = e->addr;
        struct sd_range *r = uf_find(a);
        if (r && a + 32u <= r->hi && !(r->hhi > r->hlo && a < r->hhi && a + 32u > r->hlo)) {
            const u32 *s = e->d;
            u32 w0 = s[0], w1 = s[1], w2 = s[2], w3 = s[3], w4 = s[4], w5 = s[5], w6 = s[6], w7 = s[7];
            u32 *lv = SDP(a), *mp = MP(r->moff + (a - r->lo));
            lc_dcbz(lv); sd_x17.u_whole++;
            lv[0] = w0; lv[1] = w1; lv[2] = w2; lv[3] = w3; lv[4] = w4; lv[5] = w5; lv[6] = w6; lv[7] = w7;
            SD_NOTE_W(lv, 32u);
            if (!((unsigned long)mp & 31ul)) { lc_dcbz(mp); sd_x17.u_whole++; }
            mp[0] = w0; mp[1] = w1; mp[2] = w2; mp[3] = w3; mp[4] = w4; mp[5] = w5; mp[6] = w6; mp[7] = w7;
            SD_NOTE_W(mp, 32u);
            uf_mnote(mp, 32u);
            sd_x.undo_ents_last++; sd_x.undo_bytes_sum += 32u;
            sd_x.m_ents_last++; sd_x.m_bytes_sum += 32u;
            sd_uf.fast_dd++; sd_uf.ents++;
            return;
        }
    }
    dd_put(SDP(e->addr), e, lm, ustore);
    sd_x.undo_ents_last++;
    sd_x.undo_bytes_sum += n * 4u;
    if (mm) {
        struct sd_range *r = uf ? uf_find(e->addr) : rfind(e->addr);
        if (r && !(r->hhi > r->hlo && e->addr >= r->hlo && e->addr < r->hhi)) {
            u32 *mp = MP(r->moff + (e->addr - r->lo)), i;
            for (i = 0; i < n; i++) {        /* only the words inside the range and outside its hole: an entry cut in 32-byte steps may run past the range end
                                                (the old chain writes those words into the neighbouring mirror block, which can be a free block by now) */
                u32 wa = e->addr + 4u * i;
                if (wa >= r->hi || (r->hhi > r->hlo && wa >= r->hlo && wa < r->hhi))
                    mm &= ~(1u << i);
            }
            if (mm) {
                dd_put(mp, e, mm, ustore);
                if (uf) uf_mnote(mp, n * 4u);   /* S17 undo-step fix: batched per line */
                else s17_mflush(mp, n * 4u);
            }
            sd_x.m_ents_last++;
            sd_x.m_bytes_sum += n * 4u;
        } else
            sd_x.m_nomap++;
    }
}

/* step 2 of restore_keep with DD; 0 = done, 1 = the pre-pass gave up (nothing was written: the caller runs the old loop), -3 = list error */
static int dd_chain(s32 j)
{
    s32 k;
    u32 c, d0 = sd_plat_time(), d1, d2;      /* S17 undo step v2: telemetry of the three parts of a DD chain */
    if (dd_prepass(j)) { sd_dd.fb_cap++; return 1; }
    d1 = sd_plat_time();
    sd_u2.t_ddp += d1 - d0;
    sd_dd.used++;
    sd_dd.lines += dd_lines;
    for (k = (s32)nsl - 1; k > j; k--) {     /* journals first (newest to oldest): the list becomes the list of capture j */
        u32 born = SL[k - 1].seq;
        for (c = SL[k].je; c != SL[k].jb;) {
            const struct sd_jent *q;
            c--;
            q = &JR[c % cap_j];
            sd_u2.jr_n++;
            if (q->kind == 1) {
                s32 x = cell_find(q->lo);
                if (x >= 0)
                    cell_remove_at(x);
            } else if (cell_insert(q->lo, q->hi, q->hlo, q->hhi, born, 0) < 0)
                return -3;
        }
    }
    d2 = sd_plat_time();
    sd_u2.t_ddj += d2 - d1;
    if (uf_on()) uf_begin(); else uf_kind = 3u;   /* S17 undo-step fix: one cursor run over the whole chain (the list is final here) */
    for (k = j + 1; k < (s32)nsl; k++) {
        u32 pos = SL[k].lm % cap_log;
        for (c = SL[k].lm; c != SL[k].le; c++) { dd_apply(&LOG[pos], c); if (++pos == cap_log) pos = 0; }
        pos = SL[k].lb % cap_log;
        for (c = SL[k].lb; c != SL[k].lm; c++) { dd_apply(&LOG[pos], c); if (++pos == cap_log) pos = 0; }
    }
    if (uf_kind != 3u) uf_end(); else uf_kind = 0;
    sd_u2.t_dda += sd_plat_time() - d2;
    return 0;
}

static u32 chain_now;                        /* S17 R1: this restore skips step 1 (taken from sd_chain_only at the start of sd_restore) */

/* Review fix 3: restore_keep / dd_chain can fail only inside the journal revert (cell_insert: list full or
   no mirror block, -3), and by then step 0 and step 1 have already written live memory: a half-done restore the glue cannot undo.  This dry pass
   runs the same journal revert (same entries, same order, same membership and allocation rules) on counters only, BEFORE any write: a bitmap of
   cells removed so far over the real list, a small list of cells re-inserted so far, the free-list counts per class, the list count and the top.
   It refuses (nonzero, nothing written) exactly when the real pass would fail; if it passes, the real pass cannot fail (every decision it makes
   is reproduced here on the same state).  The one approximation is the re-insert list bound (SD_ROOM_ADD): past it a re-inserted cell is not
   remembered, so a later remove of it frees nothing and a later re-insert of it is counted again: pessimistic, never optimistic. */
#define room_rm sd_scr.room.rm                /* SD_CE_MAX / SD_ROOM_ADD and the arrays: see sd_scr above apply_events */
#define room_add_lo sd_scr.room.add_lo
#define room_add_cls sd_scr.room.add_cls
#define room_n sd_scr.room.n
static int restore_room(s32 j)
{
    s32 k;
    u32 c, i, nadd = 0, snce = nce, stop = m_top, words = (nce + 31u) / 32u, ins = 0;   /* B2 C2: only the words of the real list (cell_find returns x < nce; the list does not change here) */
    sd_x16.room_checks++;
    if (cap_ce > SD_CE_MAX) return 0;        /* never (cap_ce is 6144 or 8192); a larger list would skip the pass, not refuse */
    for (i = 0; i < words; i++) room_rm[i] = 0;
    for (i = 0; i < NCLS; i++) room_n[i] = sd_test_free_steal ? 0u : mfree_n[i];
    for (k = (s32)nsl - 1; k > j; k--)
        for (c = SL[k].je; c != SL[k].jb;) {
            const struct sd_jent *q;
            s32 x;
            u32 a, sz, cls, present = 0, found_add = SD_ROOM_ADD;
            c--;
            q = &JR[c % cap_j];
            x = cell_find(q->lo);
            if (x >= 0 && !(room_rm[(u32)x >> 5] & (1u << ((u32)x & 31u)))) present = 1;
            for (a = 0; a < nadd; a++) if (room_add_lo[a] == q->lo) { found_add = a; break; }
            if (found_add != SD_ROOM_ADD) present = 1;
#ifdef SD_HOST
            if (sd_test_room_trace) { extern int printf(const char *, ...); printf("  room k %d c %u kind %u lo %08x hi %08x x %d bit %u add %u present %u snce %u stop %u\n", k, c, q->kind, q->lo, q->hi, x, x >= 0 ? (room_rm[(u32)x >> 5] >> ((u32)x & 31u)) & 1u : 0u, found_add != SD_ROOM_ADD, present, snce, stop); }
#endif
            if (q->kind == 1) {              /* the real pass removes the cell when it is present */
                if (!present) continue;
                if (found_add != SD_ROOM_ADD) { cls = room_add_cls[found_add]; nadd--; room_add_lo[found_add] = room_add_lo[nadd]; room_add_cls[found_add] = room_add_cls[nadd]; }
                else { room_rm[(u32)x >> 5] |= 1u << ((u32)x & 31u); cls = ((CE[x].hi - CE[x].lo + SD_LINE - 1u) & ~(SD_LINE - 1u)) / SD_LINE; }
                if (cls < NCLS && !sd_test_free_steal) room_n[cls]++;   /* the real pass reuses a block freed earlier in the same pass (m_free then m_alloc); the TEST knob makes it ignore the lists altogether */
                snce--;
                continue;
            }
            if (present) continue;           /* cell_insert: already present, nothing allocated */
            ins++;
            if (snce + sd_test_ce_steal >= cap_ce) { sd_x16.room_refused_ce++; return -1; }
            sz = (q->hi - q->lo + SD_LINE - 1u) & ~(SD_LINE - 1u);
            cls = sz / SD_LINE;
#ifdef SD_HOST
            if (sd_test_room_trace) { extern int printf(const char *, ...); printf("  room insert sz %u cls %u room_n %u stop %u top_steal %u free_steal %u cap_m %u\n", sz, cls, cls < NCLS ? room_n[cls] : 0u, stop, sd_test_top_steal, sd_test_free_steal, cap_m); }
#endif
            if (cls < NCLS && room_n[cls]) room_n[cls]--;
            else if (stop + sz + sd_test_top_steal > cap_m) { sd_x16.room_refused_top++; return -1; }
            else stop += sz;
            snce++;
            if (nadd < SD_ROOM_ADD) { room_add_lo[nadd] = q->lo; room_add_cls[nadd] = (u16)(cls < NCLS ? cls : NCLS); nadd++; }
            else sd_x16.room_add_ovf++;
        }
    if (ins > sd_x16.room_ins_max) sd_x16.room_ins_max = ins;
    return 0;
}

static int restore_keep(s32 j, int rset)
{
    s32 k;
    u32 i, c, nl, miss = 0, depth = (u32)((s32)nsl - 1 - j), t_a, t_b, t_c;   /* S17: depth = steps undone */
    /* 0. pending allocator pre-images (states after the newest capture), newest first (live only: OS heap headers) */
    for (c = log_head; c != SL[nsl - 1].le;) {
        const struct sd_ent *e;
        c--;
        e = &LOG[c % cap_log];
        cpy(SDP(e->addr), e->d, e->len / 4);
    }
    /* 1. live := M (the state at the newest capture) on the newest list */
    t_a = sd_plat_time();                    /* S17 */
    sd_x.cmp_bytes_last = sd_x.copy_bytes_last = sd_x.rs_lines_last = 0;
    if (!rset && chain_now) {            /* S17 R1: live == M by the glue's proof: the undo chain alone restores (counted, see sd_dd.chain) */
        sd_dd.chain++;
        sd_x.res_keep++;
    } else if (!rset) {
        int hr = s17_restore_h(~0u);         /* S17 H: streamed restore; 1 = done, else the CPU copy below runs (and completes a failed H pass) */
        if (hr > 0)                          /* B2 C2: the plan summed the same ranges' bytes in its key pass (no second walk over the cold list) */
            sd_x.copy_bytes_last = s17_plan_bytes;
        else {
            for (i = 0; i < nst; i++) { copy_m_to_live(&ST[i]); sd_x.copy_bytes_last += rsize(&ST[i]); }
            for (i = 0; i < nce; i++) { copy_m_to_live(&CE[i]); sd_x.copy_bytes_last += rsize(&CE[i]); }
        }
        sd_x.res_keep++;
    } else {
        for (i = 0; i < nst + nce; i++) {
            const struct sd_range *r = i < nst ? &ST[i] : &CE[i - nst];
            u32 kind = i < nst ? 0 : 1, a0 = 0;
            if (in_rs(kind, r)) {
                nl = fix_range(r, 0);
                sd_x.cmp_bytes_last += rsize(r);
                sd_x.copy_bytes_last += nl * SD_LINE;
                sd_x.rs_lines_last += nl;
            } else if (sd_rs_verify && (nl = fix_range(r, &a0)) != 0) {
                if (!sd_x.rs_miss_lines) sd_x.rs_miss_a0 = a0;
                sd_x.rs_miss_lines += nl;
                sd_x.rs_miss_ranges++;
                miss = 1;
            }
        }
        sd_x.rs_lines_sum += sd_x.rs_lines_last;
        sd_x.rs_miss_restores += miss;
        sd_x.res_rset++;
    }
    sd_x.cmp_bytes_sum += sd_x.cmp_bytes_last;
    sd_x.copy_bytes_sum += sd_x.copy_bytes_last;
    t_b = sd_plat_time();                    /* S17 */
    /* 2. newest step first: revert the step's list journal (back to the list of the previous capture), then apply its
          undo entries to live and M.  Within a step the allocator pre-images are applied in reverse first (live only),
          then the previous-capture contents (removed cells, changed lines). */
    if (dd_on() && (s32)nsl - 1 - j >= 2) {  /* S17 DD (two or more steps: one step has nothing to share): deduplicated chain (exact by construction); 1 = it gave up before writing: the loop below runs */
        int dr = dd_chain(j);
        if (dr < 0)
            return dr;
        if (dr == 0)
            goto chain_done;
    }
    for (k = (s32)nsl - 1; k > j; k--) {
        u32 born = SL[k - 1].seq, u2a, u2b, u2c;
        u2a = sd_plat_time();                /* S17 undo step v2: telemetry of the three parts of a step (journal revert, pre-images, entry run) */
        for (c = SL[k].je; c != SL[k].jb;) {
            const struct sd_jent *q;
            c--;
            q = &JR[c % cap_j];
            sd_u2.jr_n++;
            if (q->kind == 1) {
                s32 x = cell_find(q->lo);
                if (x >= 0)
                    cell_remove_at(x);
            } else if (cell_insert(q->lo, q->hi, q->hlo, q->hhi, born, 0) < 0)
                return -3;                       /* arena full / list full: the mirror cannot follow */
        }
        u2b = sd_plat_time();
        for (c = SL[k].lm; c != SL[k].lb;) {
            c--;
            undo_ent(&LOG[c % cap_log], 0);
            sd_u2.pre_n++;
        }
        u2c = sd_plat_time();
        if (u2_on())                         /* S17 undo step v2 (SD_S17_U2): the lean loop, same writes in the same order */
            uf2_run(SL[k].lm, SL[k].le);
#ifndef SD_U2_DEFAULT
        else if (uf_on())                    /* S17 undo-step fix (same writes, same order) */
            uf_run(SL[k].lm, SL[k].le);
#endif
        else
            for (c = SL[k].lm; c != SL[k].le; c++)
                undo_ent(&LOG[c % cap_log], 1);
        sd_u2.t_jr += u2b - u2a; sd_u2.t_pre += u2c - u2b; sd_u2.t_run += sd_plat_time() - u2c;
    }
chain_done:
    t_c = sd_plat_time();                    /* S17 */
    sd_x17.step1_t[depth < 7u ? depth : 7u] += t_b - t_a;
    sd_x17.undo_t[depth < 7u ? depth : 7u] += t_c - t_b;
    /* 3. G's extras (outside the manifest: OS free-cell headers, uncovered pool links) */
    for (c = SL[j].xb; c != SL[j].xe; c++) {
        const struct sd_ent *e = &XR[c % cap_x];
        cpy(SDP(e->addr), e->d, e->len / 4);
    }
    /* S10: barrier ranges (Melee: fighter animation buffers, compared only when their key changed since the last
       capture) are compared at the next capture: their keys were taken at the newest capture, but live is now G's
       state, so a key that returns to the newest value would hide a real change (Dolphin run p3b diverged this way) */
    for (i = 0; i < nst; i++)
        if (ST_FL[i] & 1)
            ST_FL[i] |= 2;
    /* 4. keep G and every older capture: the rings end where G's step ended */
    sd_x.kept_slots_sum += (u32)j + 1;
    log_head = SL[j].le;
    x_head = SL[j].xe;
    j_head = SL[j].je;
    nsl = (u32)j + 1;
    nev = 0;
    sd_st.evt_overflow = 0;
    pend_lost = 0;
    return 0;
}

/* ---------------------------------------------------------------- restore */
int sd_restore(s32 frame)
{
    s32 j, k;
    u32 i, t0 = sd_plat_time(), seqg, c, dep;
    chain_now = sd_chain_only && (sd_rmode == 1);   /* S17 R1: only KEEP; always consumed (a stale request never leaks into the next restore) */
    sd_chain_only = 0;
    if (!sd_active)
        return -2;
    for (j = (s32)nsl - 1; j >= 0 && SL[j].frame != frame; j--)
        ;
    if (j < 0 || pend_lost) {
        sd_st.restore_miss++;
        return -1;
    }
    if (!slot_ok((u32)j)) {                  /* S16: strict: an incomplete capture is never restored */
        sd_x16.refused++;
        sd_st.restore_miss++;
        return -4;
    }
    if ((sd_rmode == 1 || sd_rmode == 2) && !sd_test_noroom && restore_room(j)) {   /* review fix 3: the journal revert would not fit: refuse BEFORE any write */
        sd_x16.room_refused++;
        sd_st.restore_miss++;
        return -5;
    }
    seqg = SL[j].seq;
    dep = (u32)((s32)nsl - 1 - j);           /* S17: steps undone */
    if (dep > 7u)
        dep = 7u;
    if (inc_on && seqg < inc_seq)            /* S16: G is older than the first drop: the list reverts to a complete one */
        inc_on = 0;
    sd_x.res_n++;                             /* S10 */
    sd_x.undo_ents_last = sd_x.m_ents_last = 0;
    if (sd_rmode == 1 || sd_rmode == 2) {    /* S10: KEEP / RSET */
        int rc = restore_keep(j, sd_rmode == 2);
        if (rc) {                             /* the engine cannot continue: force a manifest rebuild (unreachable since restore_room: counted as proof) */
            sd_st.errors++;
            sd_x16.room_half_done++;
            nsl = 0; nev = 0; built = 0;
            return rc;
        }
        sd_st.restores++;
        cold_rescan = 1;                      /* S11: cold cells are compared at the next capture */
        sd_st.t_res_last = sd_plat_time() - t0;
        if (sd_st.t_res_last > sd_st.t_res_max)
            sd_st.t_res_max = sd_st.t_res_last;
        sd_x17.rest_n[dep]++;                 /* S17 */
        sd_x17.rest_t[dep] += sd_st.t_res_last;
        return 0;
    }
    /* 0. pending allocator pre-images (states after the newest capture), newest first */
    for (c = log_head; c != SL[nsl - 1].le;) {          /* pending: pre-images only */
        const struct sd_ent *e;
        c--;
        e = &LOG[c % cap_log];
        cpy(SDP(e->addr), e->d, e->len / 4);
    }
    /* 1. mirror (state at the newest capture) back over the manifest; cells added after G are skipped */
    {
        int hr = s17_restore_h(seqg);        /* S17 H (cells born after G are not in the plan); else the CPU copy */
        if (hr <= 0) {
            for (i = 0; i < nst; i++)
                copy_m_to_live(&ST[i]);
            for (i = 0; i < nce; i++)
                if (CE[i].born <= seqg)
                    copy_m_to_live(&CE[i]);
        }
    }
    /* 2. undo, newest step first.  Within a step the allocator pre-images (intermediate states, time order) are applied
          in reverse first, then the previous-capture contents (removed cells, changed lines) last. */
    for (k = (s32)nsl - 1; k > j; k--) {
        for (c = SL[k].lm; c != SL[k].lb;) {
            const struct sd_ent *e;
            c--;
            e = &LOG[c % cap_log];
            cpy(SDP(e->addr), e->d, e->len / 4);
        }
        for (c = SL[k].lm; c != SL[k].le; c++) {
            const struct sd_ent *e = &LOG[c % cap_log];
            cpy(SDP(e->addr), e->d, e->len / 4);
        }
    }
    /* 3. G's extras */
    for (c = SL[j].xb; c != SL[j].xe; c++) {
        const struct sd_ent *e = &XR[c % cap_x];
        cpy(SDP(e->addr), e->d, e->len / 4);
    }
    /* 4. cell list back to G: revert the journal of the later captures; pending events are void */
    for (c = j_head; c != SL[j].je;) {
        const struct sd_jent *q;
        c--;
        q = &JR[c % cap_j];
        if (q->kind == 1) {
            s32 k = cell_find(q->lo);
            if (k >= 0)
                cell_remove_at(k);
        } else if (cell_insert(q->lo, q->hi, q->hlo, q->hhi, seqg, 0) < 0) {
            lost_at(5, q->lo, q->hi - q->lo);             /* S16: the list lacks a cell (the next capture is incomplete) */
        }
    }
    nev = 0;
    sd_st.evt_overflow = 0;
    /* 5. Slippi semantics: every capture is released; the next capture is a base capture */
    nsl = 0;
    sd_st.restores++;
    cold_rescan = 1;                          /* S11 (rmode 0: the next capture is a base capture anyway) */
    sd_st.t_res_last = sd_plat_time() - t0;
    if (sd_st.t_res_last > sd_st.t_res_max)
        sd_st.t_res_max = sd_st.t_res_last;
    sd_x17.rest_n[dep]++;                     /* S17 */
    sd_x17.rest_t[dep] += sd_st.t_res_last;
    return 0;
}

#ifdef SD_HOST
#include <stdio.h>
void sd_dbg_cell(u32 a)
{
    u32 i;
    for (i = 0; i < nce; i++)
        if (a >= CE[i].lo && a < CE[i].hi)
            printf("  cell [%08x,%08x) born %u moff %x (seq now %u, nsl %u)\n", CE[i].lo, CE[i].hi, CE[i].born, CE[i].moff, seq, nsl);
}
#endif
#ifdef SD_HOST
int sd_dbg_has(u32 lo, u32 hi) { s32 k = cell_find(lo); return k >= 0 && CE[k].hi == hi; }
u32 sd_dbg_ncells(void) { return nce; }
#endif

/* diagnostics: mirror bytes of a range (the state at the newest capture) */
const u32 *sd_mirror_of(const struct sd_range *r) { return MP(r->moff); }

/* is address a inside a manifest range (static or cell, holes excluded)? */
int sd_covered(u32 a)
{
    s32 lo = 0, hi = (s32)nce - 1;
    u32 i;
    while (lo <= hi) {
        s32 m = (lo + hi) >> 1;
        if (a < CE[m].lo)
            hi = m - 1;
        else if (a >= CE[m].hi)
            lo = m + 1;
        else
            return !(CE[m].hhi > CE[m].hlo && a >= CE[m].hlo && a < CE[m].hhi);
    }
    for (i = 0; i < nst; i++)
        if (a >= ST[i].lo && a < ST[i].hi)
            return 1;
    return 0;
}

#ifndef SD_HOST
/* struct assignments compile to memcpy calls; the image has no libc */
void *memcpy(void *d, const void *s, unsigned long n)
{
    u8 *dd = (u8 *)d;
    const u8 *ss = (const u8 *)s;
    if (!(((u32)dd | (u32)ss | n) & 3)) {
        u32 *dw = (u32 *)d;
        const u32 *sw = (const u32 *)s;
        n >>= 2;
        while (n--)
            *dw++ = *sw++;
        return d;
    }
    while (n--)
        *dd++ = *ss++;
    return d;
}
#endif

/* S17 CHAIN2: repair of ONE residual live line from the mirror of the newest capture, word by word through the range that holds each word
   (sd_census_fix above looks up only the ranges of the first and the last word of the line and so skips a middle range when three or more small statics share a line: the host replay of
   the recorded FD frames hit it, the manifest cut there has a 2-word static between two others).  No hint, no counters.  Appended at the end of the file: every function above is unchanged.
   Returns the number of words copied. */
int sd_line_fix(u32 line)
{
    u32 w, n = 0;
    if (!sd_active || !nsl)
        return 0;
    for (w = 0; w < 8; w++) {
        u32 a = (line & ~31u) + 4u * w;
        const struct sd_range *q = 0;
        s32 lo = 0, hi = (s32)nce - 1;
        while (lo <= hi) { s32 m = (lo + hi) >> 1; if (a < CE[m].lo) hi = m - 1; else if (a >= CE[m].hi) lo = m + 1; else { q = &CE[m]; break; } }
        lo = 0; hi = (s32)nst - 1;
        while (!q && lo <= hi) { s32 m = (lo + hi) >> 1; if (a < ST[m].lo) hi = m - 1; else if (a >= ST[m].hi) lo = m + 1; else q = &ST[m]; }
        if (!q || (q->hhi > q->hlo && a >= q->hlo && a < q->hhi))
            continue;
        *SDP(a) = *MP(q->moff + (a - q->lo));
        SD_NOTE_W(SDP(a), 4u);
        n++;
    }
    return (int)n;
}

/* S17 CHAIN2: 1 = the allocator has not touched the heap since the newest capture or the last restore: no pending cell event, no pending allocator pre-image (the OS heap headers / free-cell headers
   logged since), no pending extras, nothing lost.  Chain-only needs it: a cell freed and re-allocated after the capture rewrites payload lines that the mirror still holds for the old owner, and only
   the base copy would bring them back.  Also appended: no function above changed. */
int sd_pend_clean(void)
{
    return sd_active && nsl && !nev && !pend_lost && !sd_st.evt_overflow && log_head == SL[nsl - 1].le && x_head == SL[nsl - 1].xe;
}
