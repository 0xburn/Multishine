/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* (copy of; S16: changes are marked): strict snapshots and one arena for mirror and
 * fingerprint table, see the header comment of sd_core.c and the S16 block at the end of this file.
 * copy of for stage S11 (experiment E3, fingerprint capture); S11: changes are marked.
 * S11: with sd_fp = 1 (set before sd_init) the delta capture keeps a 64-bit fingerprint per 32-byte mirror line
 * (sd_fp.h) and reads only the LIVE line: it recomputes the line's fingerprint and reads the mirror line (for the undo
 * log) only when the fingerprint differs.  Invariant: FP[line] == fp(M[line]) for every line of every range without a
 * hole (ranges with a hole keep the full compare).  Every undo entry of a mirror line carries the line's previous
 * fingerprint (LFP ring parallel to the log), so the KEEP restore puts the fingerprint table back exactly; base captures
 * and new cells recompute theirs.  A changed line whose fingerprint collides is MISSED (the mirror keeps the old
 * contents): probability 2^-64 per changed line (model, README).  sd_fpv = N verifies every N-th delta capture with the
 * full compare (both streams), checks the table (FP == fp(M)) and counts misses; sd_fpv_fix repairs them.
 * sd_fp_bits < 64 (TEST ONLY) weakens the fingerprint to b bits for the empirical validation of the model.
 * copy of for stage S10 (rollback depth); S10: changes are marked.
 * delta snapshot engine for rollback (stage S6b, copied for stage S6c; S6C:
 * changes are marked).  Original work.
 *
 * S10: restore modes (sd_rmode, default 0 = the S6c restore unchanged):
 *   0 FULL+RELEASE  copy M over the manifest, undo down to G, release every capture (next capture is a base copy).
 *   1 KEEP          copy M over the manifest, then undo newest -> G applied to live AND to M (the list journal is reverted
 *                   step by step, so every undo entry finds its mirror bytes in the list of its own step); captures <= G
 *                   stay valid, M == live == state(G) afterwards, so the next capture is a delta (no base copy).
 *   2 RSET          KEEP, but step 1 compares live vs M only inside the "render set" R (statics marked with
 *                   sd_rs_static, cells for which sd_plat_cell_rs says so) and copies only the differing lines.  Valid
 *                   only if nothing outside R was written since the newest capture (the caller guarantees: post-frame
 *                   capture, no logic since).  sd_rs_verify = 1 (TEST) also compares every other range and counts and
 *                   repairs differing lines (sd_x.rs_miss_*), which measures whether the premise held.
 *
 * S6C: sparse captures need nothing new in the delta scheme itself: when the caller skips frames, the allocator events
 * and pre-images of the skipped frames stay pending and the next capture's compare logs the union of the changed lines,
 * so the undo step between two captures covers 2 or 3 frames.  Added: sd_newest_le (newest capture <= a frame, for
 * loads of a frame that was not captured) and counters for the pending work that accumulates across skipped frames.
 *
 * One mirror M of the manifest (the state at the newest capture) plus, per capture, an UNDO log: the 32-byte lines
 * that changed since the previous capture, with their previous contents.  Restoring frame G = copy M back over the
 * manifest, then apply the undo logs from the newest capture down to G's successor (each in reverse order), then
 * G's "extras" (small dynamic regions copied whole at every capture: OS free-cell headers, uncovered pool links).
 *
 * The manifest is a cached list: fixed "static" ranges (DOL data/BSS lines, bone-dynamics pool, animation buffers,
 * patched-code data) built once per match, and the HSD-heap cells of at most 0x2400 bytes, kept up to date
 * incrementally from OSAllocFromHeap / OSFreeToHeap hooks (no per-capture heap walk or sort).  Each cell has its own
 * mirror block from a size-class allocator, so adding/removing a cell never relayouts M.  The allocator hooks also log
 * the pre-images of the OS heap cell headers they are about to modify (allocated-list links), which makes the OS heap
 * metadata exact without covering every cell header.
 *
 * Semantics follow Slippi: a restore releases every capture; the next capture
 * is a base capture (plain copy of the manifest into M).  The engine itself is game independent (sd_core.c part,
 * host-tested with tests/sd_test.c); the Melee manifest builders live in sd_melee.c.
 */
#ifndef SD_SNAP_H
#define SD_SNAP_H

#ifdef SD_HOST
#include <stdint.h>
typedef uint32_t u32;
typedef int32_t s32;
typedef uint16_t u16;
typedef uint8_t u8;
extern u8 *sd_host_mem;                       /* guest address a (0x80000000..) lives at sd_host_mem + (a - 0x80000000) */
#define SDP(a) ((u32 *)(sd_host_mem + ((u32)(a) - 0x80000000u)))
#else
typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef unsigned char u8;
#define SDP(a) ((u32 *)(a))
#endif

#define SD_LINE 32u
#define SD_CELL_MAX 0x2400u                 /* largest cell payload in the manifest (Fighter = 0x2400) */

struct sd_range {                            /* static range or heap cell (payload) */
    u32 lo, hi;                              /* [lo, hi), 4-aligned (cells: 32-aligned) */
    u32 hlo, hhi;                            /* hole [hlo, hhi) skipped (exclusion inside a cell), 0/0 = none */
    u32 moff;                                /* mirror offset (byte offset into the mirror arena) */
    u32 born;                                /* capture sequence number at which the mirror block got its content */
    u32 chg;                                 /* diagnostics: captures in which this range had changed lines;
                                                S10: bit 31 = render set (RSET restore compares it) */
    u32 site;                                /* S11: cells: allocation-site index (sd_plat_cell_site, 0 unknown);
                                                bit 31 SD_SITE_SCAN = compare at the next capture (cold mode),
                                                bit 30 SD_SITE_COLD = cold site */
};
#define SD_SITE_SCAN 0x80000000u
#define SD_SITE_COLD 0x40000000u
#define SD_SITE_IDX 0x0000FFFFu
#define SD_SITE_UCHG 0x3FFF0000u                 /* S11 profile: changes no invalidation explains (bits 16-29, saturating) */
#define SD_SITE_UCHG1 0x00010000u

#define SD_ENT_LFP 2u                        /* S16: bit 1 of an undo entry's len: its LFP words hold the line's previous fingerprint */
struct sd_ent {                              /* undo / extras entry: 40 bytes */
    u32 addr, len;                           /* len <= 32, multiple of 4 (| SD_ENT_LFP in the log ring; always use len / 4) */
    u32 d[8];
};

struct sd_slot {
    s32 frame;
    u32 seq;
    u32 lb, lm, le;                          /* undo log [lb, le) restores the previous capture (absolute counters):
                                                [lb, lm) allocator pre-images in time order, [lm, le) previous-capture
                                                contents (removed cells, changed lines) */
    u32 xb, xe;                              /* extras [xb, xe) */
    u32 jb, je;                              /* list journal [jb, je): cell adds/removes applied at this capture */
};

struct sd_jent { u32 kind, lo, hi, hlo, hhi; };   /* kind 1 = added, 2 = removed */
struct sd_evt { u32 kind, addr, size; };          /* kind 1 = alloc (addr = payload), 2 = free (addr = payload) */

#define SD_SLOTS 16
#define SD_EVTS 2048                         /* 3 frames of events pend at stride 3 */
#define SD_NT 512                            /* S6C: distinct addresses per step in apply_events (was 256) */
#define SD_STATICS 512
#define SD_COMPACT_BELOW 0x280000u            /* S19: a store smaller than this got compact fixed tables; since B1 M1a every store has them and since M1b (no reserve) sd_init no longer reads it (kept for references) */

struct sd_stats {
    u32 captures, base_captures, restores, restore_miss, errors;
    u32 last_lines, max_lines;               /* changed lines in the last / largest step */
    u32 log_full_drops;                      /* oldest captures dropped because the undo log was full */
    u32 evt_overflow, cell_overflow, arena_full;
    u32 cells, statics, bytes;               /* manifest size */
    u32 cell_adds, cell_removes;
    u32 t_cap_last, t_cap_max, t_base_last, t_res_last, t_res_max;
    u32 extras_last, extras_max;
    u32 log_used_max;                        /* entries */
    u32 hdr_pre;                             /* header pre-images logged by the allocator hooks */
    u32 dirty_skip_bytes;                    /* bytes not compared thanks to write barriers (last capture) */
    /* S6C: pending work between two captures (grows with the stride) */
    u32 evt_max;                             /* largest number of allocator events pending at a capture (cap SD_EVTS) */
    u32 net_max;                             /* largest number of distinct addresses in apply_events (cap SD_NT) */
    u32 pre_max;                             /* largest number of pending pre-images at a capture */
    u32 evt_rebuilds;                        /* captures that had to rebuild the manifest (event table overflow) */
    u32 cells_max, xring_max, jring_max;     /* B0: largest cells list (cap_ce), extras ring fill (cap_x), journal fill (cap_j) seen at a capture */
};

/* memory provided by the caller (32-aligned) */
int sd_init(u32 store, u32 size);
/* Platform hooks the engine calls (sd_melee.c on the console, the test on the host). */
void sd_plat_build_statics(void);            /* call sd_add_static for every fixed range */
void sd_plat_build_cells(void);              /* call sd_add_cell for every allocated cell in the manifest */
void sd_plat_extras_put(void);               /* call sd_put_extra for every extras region of the current state */
void sd_put_extra(u32 addr, u32 len);        /* only inside sd_plat_extras_put (len <= 32) */
int sd_plat_cell_ok(u32 lo, u32 hi, u32 *hlo, u32 *hhi);   /* manifest policy for a new cell (size, exclusions) */
u32 sd_plat_time(void);
void sd_plat_pre_capture(void);              /* start of every capture: mark barrier ranges dirty */

void sd_add_static(u32 lo, u32 len);         /* only inside sd_plat_build_statics */
void sd_add_cell(u32 lo, u32 hi);            /* only inside sd_plat_build_cells */

void sd_reset(void);                         /* forget everything (new match / scene) */
int sd_capture(s32 frame);                   /* 0 ok, <0 error */
int sd_restore(s32 frame);                   /* 0 ok, -1 frame not captured, -4 incomplete capture (strict), -5 the journal revert would not fit (nothing written), -3 arena/list full inside the revert (after writes; unreachable since -5 exists, counted in sd_x16.room_half_done), -2 inactive */
int sd_have(s32 frame);
/* ---- S10 ---- */
#define SD_RS_BIT 0x80000000u
extern u32 sd_rmode;                         /* S10: 0 FULL+RELEASE (S6c), 1 KEEP, 2 RSET (see the header comment) */
extern u32 sd_rs_verify;                     /* S10 TEST: RSET also compares (and repairs) every non-R range */
struct sd_x10 {                              /* S10 restore statistics (last / sums) */
    u32 res_n, res_keep, res_rset;           /* restores, of them KEEP-full, RSET */
    u32 cmp_bytes_last, copy_bytes_last;     /* step 1: bytes compared (RSET) / copied (M -> live) in the last restore */
    u32 cmp_bytes_sum, copy_bytes_sum;       /* sums over all restores */
    u32 undo_ents_last, undo_bytes_sum;      /* undo entries applied to live in the last restore; bytes over all */
    u32 m_ents_last, m_bytes_sum;            /* undo entries also applied to M (KEEP/RSET) */
    u32 m_nomap;                             /* undo entries without a range in their step's list (must stay 0) */
    u32 rs_lines_last, rs_lines_sum;         /* RSET: differing lines inside R repaired */
    u32 rs_miss_lines, rs_miss_ranges;       /* RSET verify: differing lines / ranges OUTSIDE R (premise violations) */
    u32 rs_miss_restores;                    /* restores with at least one miss */
    u32 rs_miss_a0;                          /* first missed address (diagnostics) */
    u32 kept_slots_sum;                      /* captures kept by KEEP restores (older targets that stay valid) */
    u32 unsorted;                            /* statics were not sorted at build (sorted by the engine) */
};
extern struct sd_x10 sd_x;
void sd_rs_static(u32 lo, u32 hi);           /* S10: mark statics overlapping [lo, hi) as render set (builders only) */
int sd_plat_cell_rs(const struct sd_range *r, const u32 *mirror);   /* S10: platform: is this cell in the render set? */
/* S10 diagnostics: compare live vs M over the whole manifest without logging; fn is called for every range with
   differing lines (kind 0 static, 1 cell; nl = differing 32-byte lines, a0 = first differing word).  Returns the total. */
u32 sd_diff(void (*fn)(u32 kind, const struct sd_range *r, u32 nl, u32 a0));
s32 sd_newest_frame(void);                   /* S10: frame of the newest capture, or 0x80000000 when none */
int sd_newest_le(s32 frame, s32 *out);      /* S6C: newest capture with frame <= `frame`: 1 found (*out set), 0 none */
u32 sd_nslots(void);                         /* S6C: live captures */
int sd_covered(u32 a);
/* diagnostics: visit every manifest range (kind 0 static, 1 cell) */
void sd_each(void (*fn)(u32 kind, const struct sd_range *r));                        /* inside the current manifest (builders may use it) */
void sd_drop_all(void);
const u32 *sd_mirror_of(const struct sd_range *r);   /* diagnostics: mirror of a range (state at the newest capture) */

/* allocator hooks (HSD heap only; the caller filters the heap) */
void sd_evt_alloc_pre(u32 head_cell);        /* allocated-list head before OSAllocFromHeap (DLAddFront writes it) */
void sd_evt_alloc(u32 payload, u32 size);    /* after OSAllocFromHeap: payload pointer, cell size (header incl.) */
void sd_evt_free(u32 payload);               /* before OSFreeToHeap */
void sd_log_pre(u32 addr, u32 len);          /* pre-image of [addr, addr+len) (<= 32 B) into the pending undo log */

/* write barrier: ranges registered as "barrier" ranges are compared only when marked dirty */
void sd_barrier_range(u32 lo, u32 hi);       /* only inside sd_plat_build_statics, after sd_add_static of it */
void sd_mark_dirty(u32 lo, u32 len);
extern u32 sd_verify;                        /* 1 = also compare barrier ranges that are not dirty, count misses */
extern u32 sd_barrier_miss;

extern struct sd_stats sd_st;

/* ---- S11: fingerprint capture ---- */
extern u32 sd_fp;                            /* 1 = fingerprint capture; read by sd_init (carves the table) */
#ifdef SD_NO_FP                              /* card release (option fp=off): the fingerprint capture is dead there (s17_flags G forces sd_fp 0 in sd_init), so the engine reads it as the constant 0 and the compiler drops the code */
#define SD_FP_ON 0u
#else
#define SD_FP_ON sd_fp
#endif
extern u32 sd_fp_bits;                       /* 64 (default); TEST ONLY 1..63: b bits of sd_fp_fin(fp) */
extern u32 sd_fp_key[2];                     /* lane keys (kA, kB); changing them needs a base capture (sd_drop_all) */
extern u32 sd_fpv;                           /* verify every N-th delta capture (0 off, 1 every capture) */
extern u32 sd_fpv_fix;                       /* verify repairs a missed line (logs it as changed) */
#define SD_FP_SHADOW 5                       /* shadow widths counted by the verify of a 64-bit run: 8 12 16 20 24 */
struct sd_x11 {
    u32 fp_lines;                            /* lines fingerprinted in delta captures (sum) */
    u32 fp_hit;                              /* of them: fingerprint differed (mirror line read, logged) */
    u32 fp_holed;                            /* lines compared fully because their range has a hole */
    u32 fpv_caps;                            /* verified captures */
    u32 fpv_lines;                           /* lines checked by verify */
    u32 fpv_chg;                             /* of them changed (live != M) */
    u32 fpv_miss;                            /* changed but the fingerprint matched: missed by the fingerprint capture */
    u32 fpv_miss_caps;                       /* verified captures with at least one miss */
    u32 fpv_miss_a0;                         /* first missed line (diagnostics) */
    u32 fpv_tab_bad;                         /* table entries != fp(M) (must stay 0) */
    u32 fpv_false;                           /* fingerprint differed but live == M (only possible with a bad table) */
    u32 shadow[SD_FP_SHADOW];                /* 64-bit runs: changed lines whose 8/12/16/20/24-bit weakened fingerprints collide */
    u32 fp_base_lines;                       /* lines fingerprinted by base captures / new cells */
    u32 fp_table_bytes, lfp_bytes;           /* sizes carved by sd_init */
    u32 mirror_bytes, log_ents;              /* arena and log capacity (for the README) */
    u32 t_cap_sum, t_cap_n, t_cap_vsum, t_cap_vn;   /* delta-capture time (sd_plat_time units): plain / verified */
    u32 lines_sum;                           /* changed lines logged by delta captures */
};
extern struct sd_x11 sd_x11;
u32 sd_fp_check(void);
/* S11 part 2: cold-site mode.  Cells of cold allocation sites (sd_plat_site_cold) are compared only at every
   sd_cold_n-th delta capture, at a base capture, and at the next capture after an allocator event re-used their address
   (freed and re-allocated with the same size), and at the next capture after any restore (sd_cold bit 1 = TEST ONLY:
   no rescan after a restore).  Not exact by construction: a write into a cold cell between two scans
   is logged late (restores of the captures in between are wrong); verify (sd_fpv) compares skipped cold cells too and
   counts such lines (sd_x11.cold_miss).  Profile: sd_plat_cell_gone sees every cell leaving the list. */
extern u32 sd_cold, sd_cold_n, sd_cold_mark;
void sd_touch(u32 a);                        /* S11: hooked mutator write into the cell containing a */
u32 sd_plat_cell_site(u32 lo);               /* platform: site index of the cell with payload lo (weak default 0) */
int sd_plat_site_cold(u32 site);             /* platform: is the site cold (weak default 0) */
void sd_plat_cell_gone(const struct sd_range *r);   /* platform: a cell leaves the list (profile; weak default) */
u32 sd_seq(void);                            /* captures since the reset (cell age = sd_seq - born) */
struct sd_x11c {
    u32 skip_lines, skip_caps, scans, inval, cells_cold, miss_lines, miss_caps, vlines, bytes_cold_last, rescans, touch;
};
extern struct sd_x11c sd_x11c;                       /* S11 TEST: table entries != fp(M) over the whole manifest (no repair) */
extern u32 sd_active;

/* ---- S16: strict snapshots ---- */
extern u32 sd_strict;                        /* 1 (default): incomplete captures are never restored; 0 = S12 behaviour (negative control) */
extern u32 sd_build_bulk;                    /* : 1 (default) = the manifest build appends cells and sorts once (same list, no O(n^2) shifts); 0 = old sorted inserts */
extern u32 sd_nofp_force;                    /* TEST: fp table off (fp_lim 0): every range is compared in full */
extern u32 sd_arena_limit;                   /* TEST: cap the mirror+table arena to this many bytes (0 = no limit) */
extern u32 sd_log_pct;                       /* log share of the store after the fixed tables, percent (0 = 32); read by sd_init */
struct sd_x16 {
    u32 lost_ranges, lost_bytes;             /* ranges / bytes the engine could not hold (statics, cells) */
    u32 inc_onsets;                          /* times the engine became incomplete */
    u32 inc_caps;                            /* captures taken while incomplete (unusable when strict) */
    u32 refused;                             /* restores refused because the capture is incomplete */
    u32 rebuilds_ok, rebuilds_fail;          /* forced rebuilds that cleared / did not clear the incomplete state */
    u32 arena_bytes, arena_hi, m_top;        /* arena size, high water of the mirror top, mirror top now */
    u32 fp_lim, fp_cov_bytes;                /* fingerprint table prefix (mirror bytes), manifest bytes with a table entry */
    u32 fp_off_lines;                        /* lines compared in full because the range is outside the table prefix (sum) */
    u32 fp_recomp;                           /* undo entries whose fingerprint was recomputed (no valid LFP) */
    u32 step_new_max;                        /* largest bytes of new cells in one capture step */
    u32 free_bytes;                          /* arena bytes free at the newest capture (top gap + free lists) */
    u32 room_checks, room_refused, room_refused_ce, room_refused_top, room_ins_max, room_add_ovf, room_half_done;   /* restore_room dry pass (review fix 3): checks, refusals (-5, nothing written; by list / by arena), largest insert count, re-insert list overflows (pessimistic), restores that failed AFTER writing (-3; must stay 0) */
};
extern struct sd_x16 sd_x16;
#ifdef SD_HOST
extern u32 sd_test_top_steal, sd_test_ce_steal, sd_test_free_steal, sd_test_noroom, sd_test_room_trace;   /* TEST ONLY: see sd_core.c */
#endif
int sd_newest_ok_le(s32 frame, s32 *out);    /* newest complete capture with frame <= `frame` (1 found, 0 none) */
#define SD_H_INC 1u
#define SD_H_LOST 2u
#define SD_H_LOW 4u
u32 sd_health(void);                         /* nonzero: prediction must stop (see sd_core.c) */
u32 sd_free_bytes(void);
u32 sd_reserve(void);
void sd_forget(void);                        /* drop every capture and the manifest list (next capture rebuilds); keeps the incomplete flag */
int sd_rebuild(s32 frame);                   /* forced rebuild when no rollback can be needed: 0 complete again, 1 still incomplete */

/* ---- S17: the console-proven kbench4 kernels in the engine (opt-in, sd_s17 = 0 is the S16 engine, see sd_s17.h) ---- */
#define SD_S17_G 0x01u                       /* capture compare: DMA double-buffered full compare through the locked cache (kbench4 G64) */
#define SD_S17_H 0x02u                       /* restore step 1 (live := M): mirror streamed in by DMA, CPU dcbz stores (kbench4 H0) */
#define SD_S17_U 0x04u                       /* undo: whole-line entries stored with dcbz, range lookup with a hint (kbench4 U1) */
#define SD_S17_LOGZ 0x08u                    /* log ring: lines that start inside a new entry are zeroed ahead (dcbz, no read for ownership) */
#define SD_S17_DD 0x10u                      /* restore_keep step 2: deduplicated undo chain (each word written once from the oldest pre-image; exact by construction) */
#define SD_S17_HPF 0x20u                     /* OPT-IN: step 1 with H touches (dcbt) the range record 4 keys ahead in the plan's key pass and in the
                                                stream's copy-out walk (a cache hint: no architectural effect, same writes; console timing A/B only) */
#define SD_S17_ALL (SD_S17_G | SD_S17_H | SD_S17_U)
#define SD_S17_NEG_G 0x100u                  /* TEST negative control: one live DMA piece of a G pass is never issued */
#define SD_S17_NEG_H 0x200u                  /* TEST negative control: one stream piece of an H pass is never copied out */
#define SD_S17_NEG_U 0x400u                  /* TEST negative control: one undo entry is not applied to the mirror */
#define SD_S17_NEG_DD 0x8000u                /* TEST negative control: DD gives every word to the NEWEST entry (wrong whenever two steps log the same line) */
#define SD_S17_FORCE_BUSY 0x1000u            /* TEST: every pass behaves as if the locked cache were in use by somebody else (the CPU fallback runs) */
#define SD_S17_NODCBST 0x800u                /* TEST control: no dcbst of the live lines before the G DMA (console: missed lines expected) */
#define SD_S17_U_OLD 0x10000u                /* TEST / A-B control: with U, the per-entry undo step as it was up to rbtest V5 (undo_ent / dd_apply with a
                                                rfind per entry, s17_mflush per entry); without it U runs the undo-step fix (see sd_core.c "undo-step fix").
                                                Above the 16 bits the netplay glue passes (s17_flags & 0xFFFF): images and the host tests only */
#define SD_S17_U2 0x10000000u                /* OPT-IN: with U (and no U_OLD), the undo step of a KEEP restore is one lean loop (uf2_run):
                                                the mirror range cached in locals, the log read ahead with dcbt, one dcbst per mirror line at once, no per-entry counters or calls.
                                                Same writes in the same order (host digest test), default OFF; above the 16 bits of the glue: images and the host tests only */
#define SD_S17_U2_NOPF 0x20000000u            /* A/B control for U2 (images, host tests): no dcbt read-ahead of the log (everything else as U2) */
#define SD_S17_SKIPCAP 0x40000000u           /* OPT-IN: capture policy "skip own capture in a
                                                rollback frame": the first capture decision after a load (the frame's own capture, g_loadcap in the glue) is NOT taken while
                                                the newest capture is fewer than sd_cp.limit (3) frames behind; the next normal frame captures (the load still owes one), the capture-gap guard
                                                inside replays and stride 2 are unchanged.  Changes WHEN states are saved, never the state reached.  Policy bit only: the engine kernels ignore it;
                                                the glue (s12_gc.c want_capture) and the rbtest harness read it.  Above the 16 bits the glue passes to sd_s17: it is read from the glue's own s17_flags word */
#define SD_S17_CHAIN2 0x80000000u            /* OPT-IN: chain-only restore with a proven predicate and a hole-free capture
                                                policy.  A KEEP restore skips restore step 1 (live := M, the base copy, 3.5-5.2 ms) and runs the undo chain only, when ALL of the predicate holds: (1) the load
                                                is FRESH (the newest capture was taken after the last frame ran, at the end of the pass, or by the previous restore), (2) the learned set V of
                                                residual lines (live != newest mirror, at most 16 lines, learned from verified censuses) has been repaired from the mirror, (3) the engine is healthy,
                                                (4) the set was verified (full census live vs mirror) at this load: every load while learning, then every 16th, and any line outside V latches the flag OFF.
                                                Every other load does the normal full restore (counted: holes).  Policy bit only: the engine kernels and sd_core.c ignore it; the glue (s12_gc.c) and
                                                the rbtest harness read it from their own flags word (the bit is above the 16 bits the glue passes to sd_s17).  Capture policy with the bit: the pass-end capture
                                                (S17F_PE) is taken opportunistically (only when it fits into the idle time before the next frame, never forced) so that the next load is fresh. */
extern u32 sd_s17;                           /* S17 flags; read by sd_init (G forces the fingerprint off), every pass re-reads them */
#ifdef SD_REL_S17                            /* card release (option s17x=off): the contract passes G|H|U (s17_flags 0x40000007); the other engine bits (LOGZ, DD, HPF, U_OLD, NEG_*, FORCE_BUSY, NODCBST, U2) are lab / opt-in only, so the engine reads them as 0 and the compiler drops that code */
#define SD_S17F (sd_s17 & (SD_S17_G | SD_S17_H | SD_S17_U))
#else
#define SD_S17F sd_s17
#endif
struct sd_x17 {
    u32 flags_eff;                           /* flags in effect after sd_init */
    u32 g_passes, g_busy, g_fail;            /* G passes done / skipped because the locked cache was in use (CPU compare ran) / failed (base capture) */
    u32 g_ticks_last, g_ticks_sum, g_ticks_max;   /* sd_plat_time ticks of the G pass (all of it, interrupts enabled) */
    u32 g_lines_sum, g_cmds_sum, g_batches_sum;   /* lines compared, DMA commands, batches */
    u32 g_chg_sum;                           /* changed lines logged by G */
    u32 h_passes, h_busy, h_fail;            /* H passes done / skipped (CPU copy ran) / failed (CPU copy completed the restore) */
    u32 h_ticks_last, h_ticks_sum, h_ticks_max;   /* ticks of the stream + copy-out (interrupts off) */
    u32 h_plan_last, h_plan_sum;             /* ticks of the mirror-order radix plan */
    u32 h_bytes_last, h_segs_last, h_cmds_sum;
    u32 u_whole, u_part;                     /* undo entries applied with dcbz (whole lines) / as words */
    u32 rf_hit, rf_miss;                     /* range lookups answered by the hint / by a binary search */
    u32 logz_lines;                          /* log lines zeroed ahead */
    u32 dma_err_flags;                       /* OR of the HID2 error flags seen after a pass */
    u32 mflush_lines;                        /* mirror lines flushed (dcbst) to keep the mirror memory current */
    u32 rest_n[8], rest_t[8], undo_t[8], step1_t[8];   /* restores / ticks in total / ticks of step 2 (undo) / ticks of step 1, by depth = steps undone (7 = 7 or more) */
};
extern struct sd_x17 sd_x17;
struct sd_dd { u32 used, fb_cap, ents_all, ents_applied, ents_skipped, lines, words_dup, chain; };   /* S17 DD: restores deduplicated / fallbacks to the old chain / entries seen, applied, skipped / distinct lines / duplicate words */
extern struct sd_dd sd_dd;
struct sd_uf { u32 runs, ents, hint, next, gallop, full, dcbst, dcbst_saved, fast_dd; };   /* S17 undo-step fix telemetry: entry runs / entries applied
                                                with the mirror; range lookups answered by the cursor, its successor, a forward gallop, the full rfind;
                                                mirror dcbst issued / saved by the pending-line batch; DD entries taken by the whole-line fast path */
extern struct sd_uf sd_uf;
struct sd_u2 { u32 runs, ents, seek, part, nomap, jr_n, pre_n, t_jr, t_pre, t_run, t_ddp, t_ddj, t_dda; };   /* S17 undo step v2 telemetry (all undo steps of a KEEP restore without DD, old or new): entry runs / entries /
                                                range seeks (the cached range did not hold the entry) / partial entries / entries without a mirror range; journal entries reverted /
                                                allocator pre-images applied; ticks (sd_plat_time) of the journal revert, the pre-images and the entry runs, summed over the steps;
                                                DD chains: ticks of the pre-pass (first-cover scan), of the journal revert and of the apply loop */
extern struct sd_u2 sd_u2;
extern u32 sd_chain_only;                    /* S17 R1: set by the glue before sd_restore: live == the mirror of the newest capture (proven by it), skip restore step 1 */
u32 sd_census(void (*fn)(u32 line, u32 kind));
int sd_census_fix(u32 line);                 /* S17 R1 lab: copy one live line from the newest mirror (after a verified census); looks up only the ranges of the first and the last word of the line */
int sd_pend_clean(void);                     /* S17 CHAIN2: no allocator event since the newest capture / restore (appended to sd_core.c) */
int sd_line_fix(u32 line);                   /* S17 CHAIN2: the same for every word of the line (any number of ranges in it); appended to sd_core.c */   /* S17 R1: lines where live differs from the newest mirror (read-only) */
u32 sd_plat_ms_ticks(void);                  /* platform: time-base ticks per millisecond read from the OS at run time, 0 = unknown (weak default) */

#endif
