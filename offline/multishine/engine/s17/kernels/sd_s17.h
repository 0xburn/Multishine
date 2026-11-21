/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* offline/s17: the console-proven kbench4 kernels in the real engine (included by sd_core.c, inside its translation unit:
 * it uses the engine's tables and counters).  Original work.  Design, budgets and proof structure: offline/s17/.
 *
 *   G (sd_s17 & SD_S17_G)   capture compare = kbench4 G64: the
 *                           live and the mirror lines of batch n + 1 are DMA'd into the locked cache while the CPU compares
 *                           batch n; whole-line runs go through one compare loop, heads and tails through word masks; only
 *                           changed lines touch normal memory.  Exact by construction (no fingerprints).
 *   H (SD_S17_H)            restore step 1 (live := M) = kbench4 H0 (rst_stream): the mirror arena is streamed in by DMA in
 *                           128-line chunks, the CPU writes every range from the locked-cache copy, whole lines with dcbz.
 *   U (SD_S17_U)            undo = kbench4 U1: whole-line entries are stored with dcbz (live and mirror), the mirror line is
 *                           written back with dcbst, the range lookup keeps a hint (consecutive entries lie in one range).
 *   LOGZ (SD_S17_LOGZ)      new log entries zero the lines that start inside them with dcbz when the ring slots behind are free.
 * Coherency: the DMA reads memory, never the normal cache.
 *  (1) live lines are dcbst'd + sync before a G DMA reads them; (2) the MIRROR memory is kept current by every writer (base
 *  copy, new cells, undo, take, CPU compare): s17_mflush after each write, a sync at the start of every pass; (3) no DMA ever
 *  writes memory, so no dcbi is needed (kbench4's X0 control showed 0 wrong words and the invalidates stay out of S17).
 * Locked cache: a pass owns it from lc_begin (LCEnable) to lc_end (LCDisable); when HID2[LCE] is already set (the THP movie
 * decoder is the only other user in the game) the pass is skipped and the CPU path of S16 runs.  Any DMA timeout or error
 * makes a G pass fail (the capture becomes a base capture) and an H pass fall back to the full CPU copy of step 1.
 * Run-time clock: the DMA timeout is 4 x sd_plat_ms_ticks (OS bus clock word read at run time), or a poll count when the
 * platform does not know the rate: no time-base immediate in this file. */
#ifndef SD_S17_H_
#define SD_S17_H_
#include "sd_lc.h"

struct sd_x17 sd_x17;
u32 sd_s17;
__attribute__((weak)) u32 sd_plat_ms_ticks(void) { return 0; }

#define S17_BATCH 64u                        /* lines per batch (kbench4: G64 = G128 within 1 %) */
#define S17_PMAX 66u                         /* pieces per batch (>= S17_BATCH: every piece has a line) */
#define S17_QMAX 12u                         /* DMA commands in flight (the queue holds 15) */
#define S17_SLOT 4096u                       /* locked-cache bytes of one batch slot: 64 live + 64 mirror lines */
#ifdef SD_HOST
#define S17_POLL_CAP 200000u
#else
#define S17_POLL_CAP 0x400000u
#endif

/* B2 C2: the H plan's two radix histograms, one per 9-bit digit, counted together in the key pass (u16: a count or a position is
   at most nst + nce < 16,384, the 14-bit index of a key); the same 2 KB as the former u32[512] counted twice */
static union { u32 w[512]; u16 h[1024]; } s17_cnt;
#define S17_HPF_AHEAD 4u                     /* B2 C2b: range records touched ahead (SD_S17_HPF) */
static u32 s17_plan_bytes;                   /* B2 C2: rsize summed over the ranges of the last plan (step 1's copy_bytes, counted in the key pass) */

static inline u32 s17_mc(void) { return SD_S17F & (SD_S17_G | SD_S17_H); }   /* the mirror memory must be current */

/* the mirror memory of [p, p + bytes) is written: write the lines back (rule 2) */
static void s17_mflush(const void *p, u32 bytes)
{
    if (s17_mc() && bytes) {
        lc_dcbst_range(p, bytes);
        sd_x17.mflush_lines += (bytes + 31u) / 32u;
    }
}

/* ---------------------------------------------------------------- locked-cache session and DMA queue */
static u32 lc_owned, lc_err, lc_ees, lc_issued, lc_to;

static int lc_wait(u32 n)                    /* wait until at most n commands are queued */
{
    u32 t0 = sd_plat_time(), polls = 0;
    while (lc_ql() > n) {
        if (lc_to ? sd_plat_time() - t0 > lc_to : ++polls > S17_POLL_CAP) {
            if (lc_ql() <= n)                /* an interrupt delayed this loop: the queue has drained meanwhile */
                return 1;
            lc_dma(0, 1u);                   /* DMA_L flush bit: drop what is still queued */
            lc_err = 1;
            return 0;
        }
    }
    return 1;
}

static void lc_cmd(u32 mem, u32 lc, u32 nl)  /* one load: memory -> locked cache, nl = 1..128 lines (128 encoded as 0) */
{
    lc_dma((mem & 0x0FFFFFE0u) | ((nl >> 2) & 0x1Fu), (lc & ~31u) | ((nl & 3u) << 2) | 0x12u);
    lc_issued++;
}

static int lc_issue(u32 mem, u32 lc, u32 nl)  /* blocking issue: waits for a free queue position */
{
    if (lc_err || !lc_wait(S17_QMAX - 1u))
        return 0;
    lc_cmd(mem, lc, nl);
    return 1;
}

static int lc_begin(void)                    /* 1 = the pass owns the locked cache, 0 = somebody else has it (skip the pass) */
{
    u32 h = lc_hid2(), ms = sd_plat_ms_ticks();
    lc_err = 0;
    lc_issued = 0;
    lc_to = ms * 4u;
    if ((h & SD_HID2_LCE) || (SD_S17F & SD_S17_FORCE_BUSY)) {
        lc_owned = 0;
        return 0;
    }
    lc_sync();                               /* every dcbst of the mirror lines has reached memory before the first DMA */
    lc_ees = h & SD_HID2_EES;
    lc_enable();
    h = lc_hid2();
    lc_hid2_set(h & ~(SD_HID2_EES | SD_HID2_ERRS));   /* DMA errors only set a flag, never a machine check */
    lc_owned = 1;
    return 1;
}

static void lc_end(void)
{
    u32 e;
    if (!lc_owned)
        return;
    lc_wait(0);
    e = lc_hid2() & SD_HID2_ERRS;
    if (e) {
        sd_x17.dma_err_flags |= e;
        lc_err = 1;
    }
    lc_disable();
    lc_hid2_set((lc_hid2() & ~(SD_HID2_EES | SD_HID2_ERRS)) | lc_ees);
    lc_owned = 0;
}

/* ---------------------------------------------------------------- G: the double-buffered compare */
struct s17_seg { struct sd_range *r; u32 vm, lb, nl, fw, lw; };      /* live [a, b): first live line lb, nl lines, mirror address of line 0 */
struct s17_piece { struct sd_range *r; u32 la, vm, c, off, fw, lw; }; /* c lines of one segment: live line address, mirror address of the first line */
struct s17_cq { u32 mem, lc, nl; };
struct s17_batch { u32 np, nl, ncmd, nis, end; struct s17_piece p[S17_PMAX]; struct s17_cq c[2 * S17_PMAX]; };
static struct s17_batch s17_bt[2];
static struct { u32 phase, i, half, have, j, dskip, neg, bytes; struct s17_seg s; } s17_it;   /* bytes: manifest bytes of every range the walk has passed (statics whole, cells without their hole): sd_st.bytes of this capture when the walk reaches the end (stats_size) */
static struct sd_range *s17_last_r;

/* next segment of the compare worklist: the statics (barrier ranges that are not dirty are skipped, S16 rule), then the cells
   (cells born in this step got a fresh copy: skipped); a cell with a hole has two segments.  0 = end of the list. */
static int s17_seg_next(void)
{
    for (;;) {
        struct sd_range *r;
        u32 a, b;
        if (s17_it.phase == 0) {
            if (s17_it.i >= nst) {
                s17_it.phase = 1;
                s17_it.i = 0;
                s17_it.half = 0;
                continue;
            }
            r = &ST[s17_it.i];
            s17_it.bytes += r->hi - r->lo;
            if ((ST_FL[s17_it.i] & 3) == 1) {
                s17_it.dskip += r->hi - r->lo;
                s17_it.i++;
                continue;
            }
            ST_FL[s17_it.i] &= (u8)~2u;
            s17_it.i++;
            a = r->lo;
            b = r->hi;
        } else {
            if (s17_it.i >= nce)
                return 0;
            r = &CE[s17_it.i];
            if (!s17_it.half)                     /* once per cell: a cell with a hole comes here twice */
                s17_it.bytes += r->hi - r->lo - (r->hhi - r->hlo);
            if (!s17_it.half && r->born == seq) {
                r->site &= ~SD_SITE_SCAN;
                s17_it.i++;
                continue;
            }
            if (r->hhi > r->hlo) {
                if (!s17_it.half) {
                    a = r->lo;
                    b = r->hlo;
                    s17_it.half = 1;
                } else {
                    a = r->hhi;
                    b = r->hi;
                    s17_it.half = 0;
                    s17_it.i++;
                }
            } else {
                a = r->lo;
                b = r->hi;
                s17_it.i++;
            }
        }
        if (b <= a)
            continue;
        s17_it.s.r = r;
        s17_it.s.lb = a & ~31u;
        s17_it.s.nl = (((b + 31u) & ~31u) - s17_it.s.lb) / 32u;
        s17_it.s.fw = (a & 31u) >> 2;
        s17_it.s.lw = ((b - 4u) & 31u) >> 2;
        s17_it.s.vm = SDG(MA) + r->moff + (s17_it.s.lb - r->lo);
        return 1;
    }
}

/* next batch (up to S17_BATCH lines) into x for slot b: its live lines are written back (rule 1) and DMA commands listed */
static u32 s17_build(struct s17_batch *x, u32 b)
{
    u32 lcl = SD_LC_BASE + b * S17_SLOT, lcm = lcl + S17_BATCH * 32u;
    x->np = x->ncmd = x->nis = x->nl = 0;
    while (x->nl < S17_BATCH && x->np < S17_PMAX) {
        struct s17_seg *s = &s17_it.s;
        struct s17_piece *p;
        u32 c, k;
        if (!s17_it.have) {
            if (!s17_seg_next())
                break;
            s17_it.have = 1;
            s17_it.j = 0;
        }
        c = s->nl - s17_it.j;
        if (c > S17_BATCH - x->nl)
            c = S17_BATCH - x->nl;
        p = &x->p[x->np++];
        p->r = s->r;
        p->la = s->lb + 32u * s17_it.j;
        p->vm = s->vm + 32u * s17_it.j;
        p->c = c;
        p->off = x->nl;
        p->fw = s17_it.j == 0 ? s->fw : 0u;
        p->lw = s17_it.j + c == s->nl ? s->lw : 7u;
        if (!(SD_S17F & SD_S17_NODCBST))
            for (k = 0; k < c; k++)
                lc_dcbst(SDP(p->la + 32u * k));       /* (1): dirty live lines to memory first */
        if ((SD_S17F & SD_S17_NEG_G) && !s17_it.neg && c >= 4u)
            s17_it.neg = 1;                           /* TEST ONLY: one live piece never DMA'd (the locked cache keeps stale data) */
        else {
            x->c[x->ncmd].mem = p->la;
            x->c[x->ncmd].lc = lcl + 32u * p->off;
            x->c[x->ncmd++].nl = c;
        }
        if (!(p->vm & 31u)) {                         /* mirror congruent: its lines are whole aligned lines too */
            x->c[x->ncmd].mem = p->vm;
            x->c[x->ncmd].lc = lcm + 32u * p->off;
            x->c[x->ncmd++].nl = c;
        }
        s17_it.j += c;
        x->nl += c;
        if (s17_it.j == s->nl)
            s17_it.have = 0;
    }
    sd_x17.g_cmds_sum += x->ncmd;
    sd_x17.g_lines_sum += x->nl;
    sd_x17.g_batches_sum += x->np != 0;
    lc_sync();
    return x->np;
}

static void s17_pump(struct s17_batch *x)
{
    while (x->nis < x->ncmd && lc_ql() < S17_QMAX) {
        lc_cmd(x->c[x->nis].mem, x->c[x->nis].lc, x->c[x->nis].nl);
        x->nis++;
    }
}

static int s17_pump_all(struct s17_batch *x)
{
    while (x->nis < x->ncmd) {
        if (lc_err || !lc_wait(S17_QMAX - 1u))
            return 0;
        lc_cmd(x->c[x->nis].mem, x->c[x->nis].lc, x->c[x->nis].nl);
        x->nis++;
    }
    x->end = lc_issued;
    return 1;
}

/* first line i < n where the 8-word lines l and m differ (n if none): the hot loop of G (kbench4 scan_full) */
static u32 __attribute__((noinline)) s17_scan(const u32 *__restrict l, const u32 *__restrict m, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++, l += 8, m += 8) {
        u32 d = (l[0] ^ m[0]) | (l[1] ^ m[1]) | (l[2] ^ m[2]) | (l[3] ^ m[3]) |
                (l[4] ^ m[4]) | (l[5] ^ m[5]) | (l[6] ^ m[6]) | (l[7] ^ m[7]);
        if (d)
            break;
    }
    return i;
}

/* a new log entry, optionally with the lines that start inside it zeroed ahead (SD_S17_LOGZ).  A line [s, s + 32) with
   p <= s < p + 40 holds only bytes of this entry and of the next ring slot; it is zeroed when it lies inside the entry, or
   when the next slot is free (the ring is not full and this is not the last slot, whose successor is padding and the LFP
   array).  addr and len are written after the zeroing. */
static struct sd_ent *s17_log_put(u32 addr, u32 len)
{
    struct sd_ent *e = log_put(addr, len);
    if (e && (SD_S17F & SD_S17_LOGZ)) {
        u8 *pe = (u8 *)e, *s = (u8 *)(((unsigned long)pe + 31ul) & ~31ul);
        u32 used = log_head - log_tail(), idx = (u32)(e - LOG), nextfree = used < cap_log && idx != cap_log - 1u;
        for (; s < pe + 40; s += 32)
            if (s + 32 <= pe + 40 || nextfree) {
                lc_dcbz(s);
                sd_x17.logz_lines++;
            }
        e->addr = addr;
        e->len = len;
    }
    return e;
}

/* line j of piece p changed: log the mirror's old words (ow = the mirror line in the locked cache, or 0 = read the mirror memory),
   mirror := the new words (nw = the live line in the locked cache), every written mirror line dcbst'd (rule 2) */
static int s17_take(struct s17_piece *p, u32 j, u32 w0, u32 w1, const u32 *nw, const u32 *ow)
{
    u32 n = w1 - w0 + 1u, la = p->la + 32u * j + 4u * w0, ma = p->vm + 32u * j + 4u * w0, w;
    u32 *m = SDP(ma);
    struct sd_ent *e = s17_log_put(la, 4u * n);
    if (!e) {
        cmp_fail = 1;
        return 0;
    }
    for (w = 0; w < n; w++)
        e->d[w] = ow ? ow[w0 + w] : m[w];
    if (n == 8u && !(ma & 31u))
        lc_dcbz(m);                               /* whole aligned mirror line: no read for ownership */
    for (w = 0; w < n; w++)
        m[w] = nw[w0 + w];
    SD_NOTE_W(m, 4u * n);
    lc_dcbst(m);
    if (((unsigned long)m ^ (unsigned long)(m + n - 1u)) & ~31ul)   /* the last word lies on another line */
        lc_dcbst(m + n - 1u);
    sd_x17.mflush_lines++;
    lines++;
    if (p->r != s17_last_r) {                     /* sd_range.chg counts the captures in which the range had changed lines */
        p->r->chg++;
        s17_last_r = p->r;
    }
    return 1;
}

/* compare the c lines of piece p; L = the live lines in the locked cache, M = the mirror lines in the locked cache or 0 */
static void s17_cmp_piece(struct s17_piece *p, const u32 *L, const u32 *M)
{
    u32 j = 0;
    while (j < p->c) {
        u32 w0 = j == 0 ? p->fw : 0u, w1 = j + 1u == p->c ? p->lw : 7u, w, d = 0;
        const u32 *l = L + 8u * j;
        if (M && w0 == 0 && w1 == 7u) {           /* a run of whole lines (the last one may be partial: handled below) */
            u32 r = p->c, i;
            if (p->lw != 7u)
                r--;
            i = s17_scan(l, M + 8u * j, r - j);
            j += i;
            if (j < r) {
                if (!s17_take(p, j, 0, 7u, L + 8u * j, M + 8u * j))
                    return;
                j++;
            }
            continue;
        }
        if (M) {
            const u32 *m = M + 8u * j;
            for (w = w0; w <= w1; w++)
                d |= l[w] ^ m[w];
            if (d && !s17_take(p, j, w0, w1, l, m))
                return;
        } else {                                  /* the mirror is not congruent: read its words from memory */
            const u32 *m = SDP(p->vm + 32u * j + 4u * w0);
            for (w = w0; w <= w1; w++)
                d |= l[w] ^ m[w - w0];
            if (d && !s17_take(p, j, w0, w1, l, 0))
                return;
        }
        j++;
    }
}

static void s17_proc(u32 b, struct s17_batch *nxt)
{
    struct s17_batch *x = &s17_bt[b];
    u32 lcl = SD_LC_BASE + b * S17_SLOT, lcm = lcl + S17_BATCH * 32u, i;
    for (i = 0; i < x->np && !cmp_fail; i++) {
        struct s17_piece *p = &x->p[i];
        const u32 *L = LCP(lcl + 32u * p->off), *M = (p->vm & 31u) ? 0 : LCP(lcm + 32u * p->off);
        if (nxt)
            s17_pump(nxt);
        s17_cmp_piece(p, L, M);
    }
}

/* the double-buffered pipeline over the whole worklist; 1 = done, 0 = failure (DMA or log full) */
static int s17_pipeline(void)
{
    u32 b = 0;
    if (!s17_build(&s17_bt[0], 0))
        return 1;
    if (!s17_pump_all(&s17_bt[0]))
        return 0;
    for (;;) {
        u32 more = s17_build(&s17_bt[b ^ 1u], b ^ 1u);
        if (more)
            s17_pump(&s17_bt[b ^ 1u]);
        if (!lc_wait(lc_issued - s17_bt[b].end))   /* batch b complete: only commands issued after it may still be queued */
            return 0;
        s17_proc(b, more ? &s17_bt[b ^ 1u] : 0);
        if (cmp_fail)
            return 0;
        if (!more)
            break;
        if (!s17_pump_all(&s17_bt[b ^ 1u]))
            return 0;
        b ^= 1u;
    }
    return !lc_err;
}

static int s17_g_ok(void)
{
    return (SD_S17F & SD_S17_G) && !sd_cold && !sd_cold_mark && !sd_verify && !(SD_FP_ON && FP);
}

/* the compare pass of a delta capture: 1 = done (the caller skips its CPU loops), 0 = not run (locked cache in use: the CPU
   compare runs), -1 = failed (the caller makes a base capture; cmp_fail may be set by a full log) */
static int s17_cap_g(u32 *dskip)
{
    u32 t0 = sd_plat_time(), l0 = lines, ok, dt;
    if (!lc_begin()) {
        sd_x17.g_busy++;
        return 0;
    }
    s17_it.phase = s17_it.i = s17_it.half = s17_it.have = s17_it.j = s17_it.dskip = s17_it.neg = s17_it.bytes = 0;
    s17_last_r = 0;
    ok = (u32)s17_pipeline();
    lc_end();
    if (lc_err)
        ok = 0;
    *dskip += s17_it.dskip;
    dt = sd_plat_time() - t0;
    sd_x17.g_ticks_last = dt;
    sd_x17.g_ticks_sum += dt;
    if (dt > sd_x17.g_ticks_max)
        sd_x17.g_ticks_max = dt;
    sd_x17.g_chg_sum += lines - l0;
    if (!ok) {
        sd_x17.g_fail++;
        return -1;
    }
    sd_x17.g_passes++;
    return 1;
}

/* ---------------------------------------------------------------- H: the streamed restore */
/* A contiguous memory region streamed through the 4 slots of 128 lines, up to 4 chunks in flight; the CPU consumes it in
   address order, a slot is refilled as soon as the CPU has moved past its chunk (kbench4 s_begin / s_ptr). */
static u32 sb_base, sb_nl, sb_nch, sb_cur, sb_next, sb_wait, sb_seq[4];

static int s17_s_issue(u32 c)
{
    u32 nl = sb_nl - 128u * c;
    if (nl > 128u)
        nl = 128u;
    if (!lc_issue(sb_base + 4096u * c, SD_LC_BASE + (c & 3u) * 4096u, nl))
        return 0;
    sb_seq[c & 3u] = lc_issued;
    sd_x17.h_cmds_sum++;
    return 1;
}

static int s17_s_begin(u32 base, u32 nl)
{
    sb_base = base;
    sb_nl = nl;
    sb_nch = (nl + 127u) / 128u;
    sb_cur = 0;
    sb_wait = 0xFFFFFFFFu;
    for (sb_next = 0; sb_next < sb_nch && sb_next < 4u; sb_next++)
        if (!s17_s_issue(sb_next))
            return 0;
    return 1;
}

/* locked-cache address of memory address a (the chunks are consumed in order); 0 on a DMA failure */
static u32 s17_s_ptr(u32 a)
{
    u32 c = (a - sb_base) >> 12;
    while (sb_cur < c) {                          /* chunk sb_cur is finished: its slot takes chunk sb_cur + 4 */
        if (sb_next < sb_nch && !s17_s_issue(sb_next++))
            return 0;
        sb_cur++;
    }
    if (sb_wait != c) {
        if (!lc_wait(lc_issued - sb_seq[c & 3u]))
            return 0;
        sb_wait = c;
    }
    return SD_LC_BASE + (c & 3u) * 4096u + ((a - sb_base) & 4095u);
}

static inline void s17_word(u32 d, u32 s)
{
    u32 *q = SDP(d);
    *q = *LCP(s);
    SD_NOTE_W(q, 4);
}

/* copy bytes from the locked-cache copy s to live memory d: whole aligned lines with dcbz (when d and s agree mod 32), words else */
static void s17_copy_out(u32 d, u32 s, u32 bytes)
{
    if (!((d ^ s) & 31u) && bytes >= 32u) {
        while (d & 31u) {
            s17_word(d, s);
            d += 4;
            s += 4;
            bytes -= 4;
        }
        if (bytes >= 32u) {
            lc_lc2live(d, s, bytes / 32u);
            d += bytes & ~31u;
            s += bytes & ~31u;
            bytes &= 31u;
        }
    }
    while (bytes) {
        s17_word(d, s);
        d += 4;
        s += 4;
        bytes -= 4;
    }
}

/* the ranges (statics, then cells born at or before born_max) in mirror order: a stable LSD radix sort on moff / 32 (two
   9-bit digits, 18 bits = 8 MB of arena) of keys (moff / 32) << 14 | index (statics 0.., cells nst..); returns the count.
   B2 C2: both digit histograms are counted in the key pass, which also sums the ranges' bytes (s17_plan_bytes: the
   range record is already in the cache there, so step 1 no longer walks the whole list a second time for its byte count); a
   histogram is a function of the key multiset only, so the two scatter passes and the sorted keys are those of the counted-per-pass sort. */
static u32 s17_plan(u32 born_max)
{
    u32 n = 0, i, pass, k, bytes = 0;
    u32 *src, *dst;
    for (i = 0; i < 512u; i++)
        s17_cnt.w[i] = 0;
    for (i = 0; i < nst; i++) {
        const struct sd_range *r = &ST[i];
        k = ((r->moff >> 5) << 14) | i;
        s17_ord[n++] = k;
        s17_cnt.h[(k >> 14) & 511u]++;
        s17_cnt.h[512u + ((k >> 23) & 511u)]++;
        bytes += r->hi - r->lo - (r->hhi - r->hlo);
    }
    for (i = 0; i < nce; i++) {
        const struct sd_range *r = &CE[i];
        if ((SD_S17F & SD_S17_HPF) && i + S17_HPF_AHEAD < nce)   /* C2b (opt-in): the record 4 ahead is on its way while this one is keyed */
            lc_dcbt(r + S17_HPF_AHEAD);
        if (r->born <= born_max) {
            k = ((r->moff >> 5) << 14) | (nst + i);
            s17_ord[n++] = k;
            s17_cnt.h[(k >> 14) & 511u]++;
            s17_cnt.h[512u + ((k >> 23) & 511u)]++;
            bytes += r->hi - r->lo - (r->hhi - r->hlo);
        }
    }
    s17_plan_bytes = bytes;
    src = s17_ord;
    dst = s17_tmp;
    for (pass = 0; pass < 2; pass++) {
        u32 sh = 14u + 9u * pass, d, sum = 0;
        u16 *cnt = s17_cnt.h + 512u * pass;
        for (d = 0; d < 512u; d++) {
            u32 t = cnt[d];
            cnt[d] = (u16)sum;
            sum += t;
        }
        for (i = 0; i < n; i++)
            dst[cnt[(src[i] >> sh) & 511u]++] = src[i];
        {
            u32 *t = src;
            src = dst;
            dst = t;
        }
    }
    return n;                                     /* after two passes the sorted keys are back in s17_ord */
}

static inline struct sd_range *s17_range(u32 key) { u32 ix = key & 0x3FFFu; return ix < nst ? &ST[ix] : &CE[ix - nst]; }

/* one segment (live address d, mirror guest address m, bytes) out of the stream; 0 on a DMA failure */
static int s17_copy_seg(u32 d, u32 m, u32 n)
{
    while (n) {
        u32 lim = sb_base + (((m - sb_base) >> 12) + 1u) * 4096u, take = lim - m < n ? lim - m : n, s = s17_s_ptr(m);
        if (!s)
            return 0;
        if ((SD_S17F & SD_S17_NEG_H) && take >= 64u && (s17_it.neg++ % 3u) == 0u)
            ;                                     /* TEST ONLY: every third stream piece is never copied out (the restore must be wrong; one piece per pass was invisible in the pair rig) */
        else
            s17_copy_out(d, s, take);
        d += take;
        m += take;
        n -= take;
        sd_x17.h_bytes_last += take;
    }
    return 1;
}

/* live := M for every range of the list (cells born at or before born_max) by the H kernel.
   1 = done, 0 = not run (locked cache in use, no plan arrays, arena too large: the caller runs the CPU copy),
   -1 = failed (the caller runs the CPU copy, which rewrites everything) */
/* B2 C2 small pieces.  The walk below used to call s17_copy_seg for every segment: a prologue of 15 saved registers, the chunk
   bookkeeping of s17_s_ptr and the telemetry in memory, about 130 PowerPC instructions around the copy of a piece that is 240 bytes on
   average (4,400 ranges per restore: more instructions than the 31,000 line copies themselves).  A segment that lies inside the chunk the
   CPU already waited for (sb_wait: s17_s_ptr would issue nothing, wait for nothing and return the same locked-cache address) is now
   copied out right here; every other segment (first use of a chunk, a segment across a chunk border, the NEG_H control) takes s17_copy_seg
   as before.  Same DMA commands, same waits, same stores in the same order; the byte and segment counts are kept in registers and written once. */
static int s17_restore_h(u32 born_max)
{
    u32 t0, n, idx, ee, ok = 1, lo_m, end_m, mg = SDG(MA), wc, base, segs = 0, fbytes = 0;
    const u32 negh = SD_S17F & SD_S17_NEG_H;
    const u32 *ord;
    const struct sd_range *a, *z;
    if (!(SD_S17F & SD_S17_H) || !s17_ord)
        return 0;
    t0 = sd_plat_time();
    n = s17_plan(born_max);
    sd_x17.h_plan_last = sd_plat_time() - t0;
    sd_x17.h_plan_sum += sd_x17.h_plan_last;
    sd_x17.h_bytes_last = sd_x17.h_segs_last = 0;
    if (!n) {
        sd_x17.h_passes++;
        return 1;
    }
    ee = lc_irq_off();                            /* nothing may see a half-restored state, as kbench4 */
    if (!lc_begin()) {
        sd_x17.h_busy++;
        lc_irq_restore(ee);
        return 0;
    }
    t0 = sd_plat_time();
    a = s17_range(s17_ord[0]);
    z = s17_range(s17_ord[n - 1]);
    lo_m = a->moff;
    end_m = z->moff + ((z->hi - z->lo + 31u) & ~31u);
    s17_it.neg = 0;
    if (!s17_s_begin(mg + lo_m, (end_m - lo_m) / 32u))
        ok = 0;
    ord = s17_ord;
    base = sb_base;
    wc = sb_wait;                                 /* the chunk the CPU has waited for (none yet) */
    for (idx = 0; ok && idx < n; idx++) {
        const struct sd_range *r = s17_range(ord[idx]);
        u32 d = r->lo, m = mg + r->moff, nb, d2 = 0, nb2 = 0;
        if ((SD_S17F & SD_S17_HPF) && idx + S17_HPF_AHEAD < n)   /* C2b (opt-in): mirror order is random in the list: touch the record 4 keys ahead */
            lc_dcbt(s17_range(ord[idx + S17_HPF_AHEAD]));
        if (r->hhi > r->hlo) {                    /* a cell with a hole: the part below it, then the part above it */
            segs += 2;
            nb = r->hlo > d ? r->hlo - d : 0u;
            if (r->hi > r->hhi) { d2 = r->hhi; nb2 = r->hi - r->hhi; }
        } else {
            segs++;
            nb = r->hi - d;
        }
        for (;;) {
            if (nb) {
                u32 off = m - base;
                if ((off >> 12) == wc && ((off + nb - 1u) >> 12) == wc) {
                    s17_copy_out(d, SD_LC_BASE + (wc & 3u) * 4096u + (off & 4095u), nb);
                    fbytes += nb;
                } else {
                    if (!s17_copy_seg(d, m, nb)) {
                        ok = 0;
                        break;
                    }
                    wc = negh ? 0xFFFFFFFFu : sb_wait;   /* the TEST control counts its pieces in s17_copy_seg: every segment goes there */
                }
            }
            if (!nb2)
                break;
            m += d2 - d;                          /* the mirror block keeps the cell's layout: the upper part sits at the same offset */
            d = d2;
            nb = nb2;
            nb2 = 0;
        }
    }
    sd_x17.h_segs_last += segs;
    sd_x17.h_bytes_last += fbytes;
    if (ok && !lc_wait(0))
        ok = 0;
    lc_end();
    if (lc_err)
        ok = 0;
    lc_irq_restore(ee);
    t0 = sd_plat_time() - t0;
    sd_x17.h_ticks_last = t0;
    sd_x17.h_ticks_sum += t0;
    if (t0 > sd_x17.h_ticks_max)
        sd_x17.h_ticks_max = t0;
    if (!ok) {
        sd_x17.h_fail++;
        return -1;
    }
    sd_x17.h_passes++;
    return 1;
}
#endif
