/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * S17: copy of for stage S17, changes marked S17:
 *   - the engine is  with the console-proven kbench4 kernels as opt-in code paths (G capture compare, H
 *     restore, U undo, LOGZ); cfg word s11_cfg.s17_flags is handed to the engine before sd_init (s11_match_start), 0 = S16;
 *   - telemetry s17_tel (net/s17.h, 128 words, an ordinary global: its address is in the build json "x17_addr"): the engine's
 *     own costs (G pass, H pass and plan, restore / step 1 / undo ticks by rollback depth), copied after every capture / restore.
 *
 * S16: copy of for stage S16 (strict snapshots), changes marked S16:
 *   - the engine is: ranges the engine cannot hold are counted and make captures incomplete,
 *     incomplete captures are never restored (sd_restore returns -4), one arena for mirror and fingerprint table;
 *   - config words s11_cfg.s16_flags / s16_arena_kb / s16_log_pct are handed to the engine before sd_init;
 *   - restores and early-rollback targets use sd_newest_ok_le (the newest COMPLETE capture <= target, deeper than the
 *     target when needed: the S6c gap path handles g < frame);
 *   - GLUE GATE (be_inputs, B0): prediction is stopped (C.cap = 0, lockstep) while sd_health is nonzero (engine
 *     incomplete, pending pre-images lost, or free arena below the reserve); an OPT-IN probe capture (S16F_PROBE, unresolved: diverges Marth/Sheik) at the first in-game
 *     frame end would make an incomplete engine known before any prediction; a gated match stays in lockstep (the forced rebuild
 *     every 30 frames is opt-in, S16F_REBUILD, and UNRESOLVED: it diverges in Dolphin);
 *   - telemetry Y_S16_* (s11_tel words 46..62) and Z_S16_* (s12_tel words 76..79).
 *
 * S12: copy of (the S11 glue, HEAD 1817288) for stage S12, with
 * the S12 work first done on top of S10 (WIP dd4796b) merged in; changes marked S12:
 *   - S12 config block s12_cfg ("S12CFG01", image +0xB80) and telemetry s12_tel (+0xBC0, 128 words), code from +0xDC0
 *     (the S11 blocks keep +0xA40 / +0xA80);
 *   - the S7 core is  (S12: the RXB opponent block factored out); the ENet library is the
 *     canonical offline/common/sdenet (no fragments), the S4 script;
 *   - more re-simulation trim classes (cfg trim_fn[], built-in classes, camera-task skip hooks, DYNB: the dynamic-bone
 *     simulation alone through a hook on lb_8001044C), see s12.h;
 *   - early rollback start in the idle gap of the scene loop (s12_idle, s12_early.c), render skip after it;
 *   - per-frame re-simulation timing and a tick breakdown; TEST ONLY locator lines; gameplay-hash scope fix.
 *   With s12_cfg all zero the image behaves as the S11 image (and with s11_cfg zero as the S10 image).
 *
 * S11: copy of for stage S11 (fingerprint capture), changes marked
 * S11: the S11 config block s11_cfg ("S11CFG01", image +0xA40) and telemetry s11_tel (+0xA80), code from +0xB80; the
 * engine is; at match start the fingerprint options are handed to the engine before sd_init
 * (sd_fp, sd_fp_bits, sd_fpv, sd_fpv_fix, a per-match key from the timebase with fp bit1); the engine counters are copied
 * into s11_tel after every capture.  fp 0 (default) = the S10 image.
 * S10: copy of for stage S10 (rollback depth), changes marked S10:
 *   - S10 config block s10_cfg ("S10CFG01", image +0x900) and telemetry block s10_tel (+0x940), code from +0xA40;
 *   - the snapshot engine is  (restore modes KEEP / RSET, sd_snap.h) with;
 *   - GObj-proc invoke wrapper s10_invoke (04 `bl` at 0x80390DFC inside HSD_GObj_RunProcs): trimmed re-simulation
 *     (cfg trim mask / function list, re-simulated frames only) and the TEST ONLY per-proc time / RNG-step profile;
 *   - post-frame capture (cfg flags S10F_POSTCAP): the capture of frame F+1 is taken at the end of the logic of frame
 *     F (s6n_end) when F was predicted (B1 seen) or re-simulated; B1 then only captures when no such capture exists
 *     (fallback, counted).  RSET restores are allowed only when nothing ran since the newest capture (g_fresh);
 *   - TEST ONLY profiles (W = outer-loop writes into the manifest, NW = never-written ranges, EQ = input equivalence of
 *     rollbacks), printed with OSReport ("S10x" lines) once global frame prof_frame is reached.
 *   With s10_cfg all zero the image behaves as the S7 image (the invoke wrapper then only calls through).
 *
 * S7: copy of for stage S7, changes marked S7:
 *   - the wire core is s7_core.c (Slippi's ENet wire,  unchanged, instead of the S3 wire): the
 *     send callback takes the destination address (ENet replies to the address a datagram came from), the core gets a
 *     millisecond clock, B5 SetMatchSelections is wired to the core;
 *   - second config block s7_cfg ("S7CFG001") and the S7 wire status block at image +0x800, code from +0x900;
 *   - cfg s7_flags bit0 = loopback reference (no BBA; the in-image S4 script plays the remote player);
 *   - the S6b debug-locator exchange (gameplay hash appended to our own PAD packets) is removed: nothing but Slippi's
 *     messages goes on the wire.  The locator dump itself (dbg_lo/dbg_n) is kept for local use.
 *
 * S6D: copy of for stage S6d, changes marked S6D:
 *   - adaptive depth cap v2 (s6d_cap.h): idle budget from the Time Base (frame - base work), measured cost per
 *     re-simulated frame, planned overruns through a token bucket (a rollback may also use the next frame's idle
 *     time, at most one frame's worth per `ovr_every` frames), hysteresis (raise after `up` frames, drop after `dn`);
 *     cfg s6d_flags bit0 selects the S6c rule for comparison runs.  Defaults follow the console numbers
 * delay 2, stride 2, adaptive cap on, cap 2..7 starting at 3.
 *   - TEST ONLY cost emulation (cfg cost_pct): a Time Base busy-wait after every engine logic iteration, B1 capture and
 *     B2 restore makes them take cost_pct % of their Dolphin time (130 ~ the console's slower logic).  It is an
 *     emulation of cost, not of the console.
 *   - TEST ONLY SFX log (cfg s6d_flags bit1): pre-hooks on AXDriver_8038CFF4 (sound start request), its real-start
 *     tail 0x8038D29C (instance id known) and lbAudioAx_800236B8 (SFX_StopSFXInstance, with the caller), plus one record
 *     per engine iteration, into a heap buffer; printed with OSReport (`SFX ` lines) from global frame `sfx_dump` on,
 *     time-budgeted per frame.  Analysed by  (doubled / missing / leftover predicted sounds).
 *   - S6d telemetry block .s6drb (64 words at image +0x700, code from +0x800).
 *
 * S6C: copy of for stage S6c, changes marked S6C:
 *   - sparse captures: B1 captures only when `frame - newest capture >= stride` (or there is no capture since the last
 *     load); stride 1 = S6b.  Optional adaptive stride (cfg s6c_flags bit0) from the Time Base: work of the previous
 *     rendered frames vs busy_hi / busy_lo with hysteresis, applied only at a B0 (never inside a rollback).
 *   - B2 of a frame f that was not captured: restore the newest capture g < f, write the preserve blocks back as before,
 *     then move the ODB savestate frame (ODB_SAVESTATE_FRAME, ODB_STABLE_SAVESTATE_FRAME) back to g.  StartEngineLoop
 *     re-reads both right after the load (ODB_FRAME = ODB_SAVESTATE_FRAME; global frame from the restored RAM), so the
 *     unchanged Slippi ASM re-simulates g..end with its own rollback loop and its own recorded inputs (local ring, RXB
 *     history): the gap frames are re-simulated exactly like rollback frames.  (A hidden re-simulation inside the EXI
 *     call is not possible: B2 runs inside StartEngineLoop with interrupts off, and the engine frame would re-enter the
 *     same hooks.)
 *   - B0 depth cap counts the gap and is optionally adaptive (bit1) from measured costs:
 *     cap = (idle x budget - restore) / (logic + capture / stride), all TB ticks x hw_pct / 100.
 *   - telemetry block .s6crb (64 words) + rehash check (a finalized frame that is re-simulated must hash the same).
 *
 * Stage S6b GameCube glue: plus (marked S6b)
 *   - the sdshim `savestate` hook: Slippi's B1 CAPTURE_SAVESTATE / B2 LOAD_SAVESTATE on the delta snapshot engine
 *     (../sd_core.c, ../sd_melee.c), with Slippi's preserve blocks (ODB, RXB, SSCB) saved and written back around a load;
 *   - the snapshot store allocated from the HSD heap on the first scene-loop pass of every online match;
 *   - a per-engine-iteration trace (04 hook at 0x801A4DE8, right after StartEngineLoop) keyed by Slippi's global frame,
 *     published only once the frame is finalized (ODB stable finalized frame >= F), so the two sides compare final
 *     states, not predicted ones; end-of-logic hook 0x801A4FF0 for CPU accounting of rollbacks.
 *
 * Original S3 header:
 * Stage S3 GameCube glue: BBA + sdnet bring-up (the S1 state machine), the S3 session/wire core
 * and the sdshim backend for B0/B3, plus a per-frame game-state trace for the two-instance comparison.
 *
 * Entry points:
 *   s3_tick        once per rendered frame (hook 0x801A4D98, top of the scene loop gm_801A4D34,
 *                    game thread).  Network slices run with interrupts disabled and a non-blocking EXILock(0, 2).
 *   s3_exi_entry   FN_EXITransferBuffer replacement (a 04 code writes `b s3_exi_entry` at 0x800055F0): runs the
 *                    project sdshim with interrupts disabled (the original also disabled them).  B0 arrives from the
 *                    VI post-retrace callback (HSD_PadRenewRawStatus via fn_800195FC), i.e. interrupt context, B3 from
 *                    the game thread.
 * Trace: when the online in-game scene (major 8 / minor 2) is running and VsSceneState.frame_count (0x8046B6C4)
 * changed since the last scene-loop iteration, hash the four static player blocks (0x80453080 stride 0xE90), both
 * fighter data blocks (gobj->user_data, 0x23EC bytes) and the RNG seed (0x804D5F90), skipping words that look like MEM1
 * pointers, and store {frame, full hash, core hash, rng, %P1, %P2, xP1, xP2} in an 8-entry ring (frame & 7).
 */
#include "../../bba/bba_mx98728.h"
#include "../../common/sdenet/sdenet_glue_clock.h"   /* time-base rate read once from the OS bus clock (40.5 MHz GC, 60.75 MHz Wii), sdenet_clock.c */
#include "../../net/sdnet.h"
#include "../../net/sddns.h"   /* HUB-DNS: the server host name is resolved with the DHCP-provided resolver (od_glue.c od_resolve_step) */
#ifdef SD_CARD_HOLE
#include "../../net/nettest.h" /* Settings > Network status > Test connection */
#endif
#include "../../enet/s4_script.h"   /* S7 */
#include "../../exi_shim/sdshim.h"
#include "../dep/s7_core.h"   /* S12: copy of the S7 core with the early-rollback helpers (S10: the S7 core, unchanged) */
#include "../sd_snap.h"   /* S12: the S11 engine (fingerprint capture) copied to */
#include "../sd_melee.h"
#include "../sd_cappol.h"   /* SKIPCAP capture policy (opt-in, s17_flags 0x40000000), synced */
#if defined(MM_FC) || defined(MM_FTSTATS) || defined(MM_PROBE) || defined(MS_RB_READOUT) || defined(SBR_NETCFG) || defined(MS_ENGINE_REPORT)
#ifndef MS_ENGINE_REPORT
#define MS_ENGINE_REPORT 1            /* the engine's OSReport lines and the Auto-delay source telemetry stay in the frame-cost, readout, probe and lab/dev builds; the normal release compiles them out */
#endif
#endif
#include "../../delay_auto/auto_delay_tab.h"   /* ONLINE DELAY AUTO: the one table, header only */
volatile unsigned int ms_ad_word __attribute__((used));   /* the Auto delay word (auto_delay_tab.h AD_W_*): the no-measurement value from boot (s3_config), then the boot / background / player test result */
static struct ad_peer g_adp;             /* AUTO DELAY FROM THE OPPONENT (s7_flags S7F_AD_PEER, section 8): the round trips of this session's peer */
#ifdef MS_DA_ENGINE   /* the engine Auto (S7F_DELAY_AUTO) is OFF in every image and superseded by the Auto word / peer Auto (auto_delay_tab.h); compiled only for a lab build that asks for it (-DMS_DA_ENGINE=1), engine size */
#include "../../delay_auto/delay_auto.c"   /* AUTO DELAY: rule + estimator, pure integer code, compiled into this unit (no extra object) */
#endif
#include "s10.h"
#ifdef SD_REL_S17   /* card release (manifest option s17x=off): every S17F_* lab / opt-in bit is compiled as 0; the contract word is 0x40000007 (G|H|U, SKIPCAP), payload.py refuses a contract with other bits */
#define S17CFG (s11_cfg.s17_flags & 0x40000007u)
#else
#define S17CFG (s11_cfg.s17_flags)
#endif
#ifdef SD_REL_S17   /* the same for s12_flags: the contract word is 0x104E since (EARLY, EARLY_NORENDER, RSTIME, GPMASK, C_COSM; C_EFLIB and the two particle procs are no longer skipped in re-simulation) and S12F_STORE_ALLM (0x200000, S19d, off in the contract but a product option, kept live) pass; every other S12F_* bit (locator, trims, TEST ONLY, diagnostics) reads 0 */
#define S12CFG (s12_cfg.flags & 0x4020304Eu)   /* + S12F_STOCK_KO 0x40000000 */
#else
#define S12CFG (s12_cfg.flags)
#endif
#define S12F_STOCK_KO 0x40000000u        /* s12_flags: the stock HUD proc runs in a re-simulated frame in which a stock count changed */
static u32 g_stk_ring[64];                                      /* stocks packed at the end of frame f (index f & 63), rewritten by every execution of f: the value the proc must compare against is the end of frame f-1 of the CORRECT timeline, not of the previous iteration (which is the mispredicted head when a re-simulation starts) */
static u32 s12_stocks_now(void)                                 /* StaticPlayer.stocks (+0x8E) of the 6 slots (player_slots 0x80453080, stride 0xE90) */
{
    u32 p, v = 0;
    for (p = 0; p < 6u; p++) v = (v << 5) ^ (u32)*(volatile u8 *)(0x80453080u + p * 0xE90u + 0x8Eu);
    return v;
}
#ifdef SD_REL_S17   /* s10_flags: the contract word is 0x101 (TRIM, RBCAP_LAST); s16_flags: 0 in the contract (every S16F_* bit is a TEST / OPT-IN switch) */
#define S10CFG (s10_cfg.flags & 0x101u)
#define S16CFG 0u
#else
#define S10CFG (s10_cfg.flags)
#define S16CFG (s11_cfg.s16_flags)
#endif
#ifdef SD_REL_S17   /* s3_cfg.flags: 0 in the contract (stress / heap_check / refusal injector / skip bits are TEST ONLY); s6c_flags 2 (ADAPT_CAP); s6d_flags 0 (no SFX log, cap v2) */
#define S3CFG 0u
#define S6CCFG (s3_cfg.s6c_flags & 2u)
#define S6DCFG 0u
#else
#define S3CFG (s3_cfg.flags)
#define S6CCFG (s3_cfg.s6c_flags)
#define S6DCFG (s3_cfg.s6d_flags)
#endif
#include "s12.h"         /* S12 */
#include "s12_gpmask.h" /* S12: render-cache bits left out of the gameplay trace hash (host-tested) */
#include "ms_match_rules.h"   /* the per-match rules record (pure, host-tested: tests/match_rules_test.c) */
#include "pause_policy.h"   /* the pause policy of our modes (a consumer of the record; pure, host-tested: tests/pause_policy_test.c) */
extern struct ms_match_rules ms_rules;                 /* defined at od_rules below */
#include "s12_macpub.h"   /* the active interface MAC the Wii's vBBA publishes (Settings > Network > Adapter; pure part host-tested: tests/macpub_test.c) */
#include "s19_fit.h"     /* S19 store fit (pure part, host-tested: tests/s19_fit_test.c) */
#include "s17.h"
#include "s17_relax.h"
#include "od_glue.h"   /* OD (online-direct): the Direct controller (offline/multishine/menus/od), */
#include "../../pacing/pacing.h"         /* S17: kernel telemetry layout */
#include "s12_early.h"   /* S12 */
#include "../../s6d/net/s6d_cap.h"   /* S7: S6d cap v2, used unchanged */
/* CARD ROUTE: with -DSD_CARD_HOLE the patched code (Gecko bodies + this image) lives in the card hole, always inside the
   loader ABI bounds [0x81700000, 0x817F0000) (offline/cardloader/cl/mscf.h), whatever base the exporter chose.  Without the define: the historic constants. */
#ifdef SD_CARD_HOLE
#define SD_B_LSLO 0x81700000u      /* patch_lsfail scan range */
#define SD_B_LSHI 0x817F0000u
#define SD_B_SFXLO 0x81700000u     /* "called from the patched code area" */
#define SD_B_SFXHI 0x817F0000u
#define SD_B_CAMLO 0x81700000u     /* s12_cam_skip: caller must be patched code */
#define SD_B_CAMHI 0x817F0000u
#else
#define SD_B_LSLO 0x804F0C20u
#define SD_B_LSHI 0x80510000u
#define SD_B_SFXLO 0x804F0000u
#define SD_B_SFXHI 0x81000000u
#define SD_B_CAMLO 0x804F0C20u
#define SD_B_CAMHI 0x80800000u
#endif

#define W(a) (*(volatile u32 *)(a))
static int ptr_like(u32 w) { return w >= 0x80000000u && w < 0x81800000u; }


#define SDK_EXILock ((int (*)(int, u32, void *))0x80346D80)
#define SDK_EXIUnlock ((int (*)(int))0x80346E74)
#define SDK_EXISelect ((int (*)(int, u32, u32))0x80346688)
#define SDK_EXIDeselect ((int (*)(int))0x803467B4)
#define SDK_EXIImm ((int (*)(int, void *, int, u32, void *))0x80345B64)
#define SDK_EXIDma ((int (*)(int, void *, int, u32, void *))0x80345E60)
#define SDK_EXISync ((int (*)(int))0x80345F4C)
#define SDK_DCFlushRange ((void (*)(void *, u32))0x8034480C)
#define SDK_DCInvalidateRange ((void (*)(void *, u32))0x803447DC)
#define EXI_READ 0u
#define EXI_WRITE 1u
#define EXI_FREQ_32MHZ 5u

#define SCENE_WORD (*(volatile u32 *)0x80479D30)        /* [major, pending, prev, minor] */
#define LOADING (*(volatile u32 *)0x80479D64)           /* TriggerSendInput.asm: 0 when the game runs */
#define MATCH_FRAME (*(volatile u32 *)0x8046B6C4)       /* VsSceneState.frame_count */
#define RNG_SEED (*(volatile u32 *)0x804D5F90)
#define STATIC_P0 0x80453080u
#define STATIC_STRIDE 0xE90u
#define FIGHTER_SIZE 0x23ECu

#define TR_N 32
struct s6_trace { volatile u32 e[TR_N][5]; };      /* {frame | match << 24, full, gameplay, core, camera} */

struct s3_status s3_status __attribute__((section(".s3status"), used)) = { .magic = 0x53335354u };
struct s3_cfg s3_cfg __attribute__((section(".s3cfg"), used)) = {
    { 'S', '3', 'C', 'F', 'G', '0', '0', '1' }, 0x53330001u, 2u, 0x00001234u, 120u, 43200u, 0u, 1800u, 0u,
    1800u, 3600u, 0u, 32u, 100u, 0u,      /* stall_disc 420 -> 1800 stalled calls (7 s -> 30 s at 60 a second): EXPERIMENT, a long-distance pair lost the opponent's pads for 7 s at match frame ~90 in every game and disconnected */
    { { 0x000A01u, 0u, 0u, 7002u }, { 0x000B02u, 1u, 0u, 7001u } }, 0u, 0u,
    7u, 0x298000u, 3u, 0x3FFFu, 0x20u, 0u, 0u,               /* B1 M1b: store 2,719,744 B (was 0x2C0000 = 2,883,584; same log + mirror, compact tables) */
    2u, 2u, 2u, 100u, 55u, 40u, 60u, 90u, 3u,                      /* S6D defaults: stride 2, adaptive cap, cap_min 2 */
    100u, 100u, 30u, (4u << 16) | 30u, 0u, 3000u };                /* S6D: cost_pct .. sfx_dump (hysteresis: up 4, dn 30) */
struct s6_trace s6_trace __attribute__((section(".s3trace"), used));
/* S7: Slippi wire config + status (image +0x800 / +0x840).  sel: Fox (0x02) / Falco (0x14), colour 0, stage 0x20 (FD);
   loopback script seed 0x5EED.
   S17 ( fix R1, owner-approved): time sync mode 3 (passive, against a PC peer) is the image
   default (was 2, console pair).  The wire cannot tell a PC from a console, and mode 2 stacks our stalls on top of a real Slippi PC's
   own speed nudge (risk 1); mode 3 is quiet against a PC and still closes a start skew (rule 1, frames <= 120) and a clock difference
   (25 ms at two sync points).  Mode 2 stays selectable for console-against-console runs: patch key `ts_mode=2` (offline/s17/net/build_s17_net.py). */
struct s7_cfg s7_cfg __attribute__((section(".s7cfg"), used)) = {
    { 'S', '7', 'C', 'F', 'G', '0', '0', '1' }, { 0x02000020u, 0x14000020u }, 3u, 0u, 0u, 0u, 0x5EEDu, 0u, { 0 } };
struct s7_status s7_status __attribute__((section(".s7status"), used)) = { .magic = 0x53375354u, .canary = 0x5A5AC0DEu };
struct s3_cks_ring s3_cks __attribute__((section(".s3cks"), used));
/* S10: config (+0x900) and telemetry (+0x940) */
struct s10_cfg s10_cfg __attribute__((section(".s10cfg"), used)) = {
    { 'S', '1', '0', 'C', 'F', 'G', '0', '1' }, 0u, 0u, 0u, 0u, 0u, 0u, 0u, { 0u, 0u, 0u, 0u }, { 0u, 0u, 0u } };
struct s10_tel { volatile u32 w[64]; };
struct s10_tel s10_tel __attribute__((section(".s10tel"), used)) = { { 0x53313054u, [63] = 0x5A5AC0DEu } };
#define XL s10_tel.w
/* S11: config (+0xA40) and telemetry (+0xA80) */
struct s11_cfg s11_cfg __attribute__((section(".s11cfg"), used)) = {
    { 'S', '1', '1', 'C', 'F', 'G', '0', '1' }, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, { 0u, 0u } };   /* S17: + s17_flags */
struct s11_tel { volatile u32 w[64]; };
struct s11_tel s11_tel __attribute__((section(".s11tel"), used)) = { { 0x53313154u, [63] = 0x5A5AC0DEu } };
#define YL s11_tel.w
/* S12: config (+0xB80) and telemetry (+0xBC0) */
struct s12_cfg s12_cfg __attribute__((section(".s12cfg"), used)) = {
    { 'S', '1', '2', 'C', 'F', 'G', '0', '1' }, 0u, { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u }, 0u, 0u, 0u, 0u, 0u };
struct s12_tel { volatile u32 w[128]; };
struct s12_tel s12_tel __attribute__((section(".s12tel"), used)) = { { 0x53313254u, [127] = 0x5A5AC0DEu } };
#define ZL s12_tel.w
/* S17: engine kernel telemetry (an ordinary global, address from the symbol table: "x17_addr") */
struct s17_tel s17_tel __attribute__((used)) = { { 0x53313754u, [127] = 0x5A5AC0DEu } };
#define QL s17_tel.w
#define S17F_PE 0x20u                /* S17 R1: pass-end capture in the idle gap (after the render), labelled like the post-frame capture (frame + 1) */
#define S17F_CENSUS 0x40u            /* S17 R1: at every load count the lines where live differs from the newest mirror (preserve blocks excluded) */
#define S17F_CHAIN 0x80u             /* S17 R1: restore by the undo chain only (no live := M) when the newest capture is fresh */
#define S17F_CHAIN_VERIFY 0x2000u    /* S17 R1 lab: every chain-only restore first verifies live == M and falls back to the full restore (counted) */
#define S17F_CHAIN_FIX 0x100000u     /* S17 R1 lab, with CHAIN_VERIFY: up to 16 residual lines are repaired from the mirror (sd_census_fix) instead of falling back */
#define S17F_CHAIN_NEG 0x200000u     /* TEST ONLY negative control for CHAIN: before the census of every 7th fresh load one live word of a fighter (percent) is flipped, an unlogged write the chain-only restore must NOT keep */
#define S17F_NOGUARD 0x800000u       /* S18: opt-OUT of the gap guard (it is ON by default in every image; 0x400000 is now a no-op bit) */
#define S17F_GAPGUARD 0x400000u      /* S18: the RXB never shows a remote frame more than 6 past the capture a rollback of the oldest unconfirmed frame would restore; rollback captures at most 3 frames apart */
#define S17F_CHAIN_PROD 0x1000000u   /* S18 production chain-only (default OFF): learn the residual line set V from verified censuses, then repair V from the mirror at every fresh load and skip step 1; a sampled census every 16th load latches it OFF (counted, reported) on any line outside V */
#define S17F_PACE_DELAY 0x2000000u   /* S18 pacing, default OFF: input delay from the ENet RTT, committed only at match setup (B3) */
#define S17F_PACE_MODE 0x4000000u    /* S18 pacing: ts_mode from pc_select(peer), peer = S17F_PACE_CONSOLE ? recognised console: Slippi/unknown (mode 3) */
#define S17F_PACE_CONSOLE 0x8000000u /* S18 pacing test input standing in for the recognition result (offline/chat/client_recog.h) */
#define S12F_NOBULK 0x4000000u       /* s12_flags,: opt-OUT of the one-sort manifest build (the first base capture appends the heap cells and sorts once instead of ~3,600 sorted inserts: the same list, about 0.3 s less); ON in every image */
#define S17F_RELAX 0x4000u           /* S17 step 3: relaxed stick compare */
#define S17F_RELAX_A 0x10000u        /* with RELAX: only side A (the other side stays strict: the differential inside one match) */
#define S17F_RELAX_B 0x20000u        /* with RELAX: only side B */
#define S17F_RELAX_NEG1 0x40000u     /* TEST ONLY negative control: dead zone widened to |x| <= 60 */
#define S17F_RELAX_NEG2 0x80000u     /* TEST ONLY negative control: the dead-zone-only rule (no guards, no patches) */

#define ST s3_status
/* S20 breadcrumbs (freeze hunt): the last place the PowerPC passed, in s3_status.spin_polls (printed by the vBBA
   ARM as the second number of "spin hits/polls"; spin_us is 0 in these images, so the word is otherwise unused).  Flushed to memory so the ARM sees it
   while the PPC spins with interrupts off.  0x01..0x06 the hook, 0x1x an EXI operation (0x1x started, 0x2xx issued and waiting in EXISync, 0x1xx done). */
static void bc_wii(u32 x);
#define BC(x) do { bc_wii(x); ST.spin_polls = (x); __asm__ volatile("dcbst 0,%0; sync" : : "r"(&ST.spin_polls) : "memory"); } while (0)

static struct sdnet S;
static struct bba B;
static struct sdnet_nic NIC;
/* HUB-DNS: the resolver the hub uses for a server NAME.  Combined image: the bridge's (already linked and bound, br_glue.c);
   otherwise one local instance on its own UDP socket, bound at link-up.  Used only in the menu (phase WAIT_NET of a search), never in a match. */
#ifdef BR_BUILD
extern struct sddns *br_dns(void);
#define OD_DNS_OBJ() br_dns()
#else
static struct sddns g_od_dns;
static struct sddns_sdnet_ctx g_od_dctx;
static int g_od_dns_ok;
static void od_dns_link_up(void);
#define OD_DNS_OBJ() (g_od_dns_ok ? &g_od_dns : (struct sddns *)0)
#endif
#ifdef BR_BUILD    /* BR (online image + Slippi bridge, offline/bridge/br_glue.c): the bridge shares this network stack */
extern void br_link_up(struct sdnet *s);
extern void br_slice(u32 s3);
extern void br_tick(u32 s3);
#endif
static struct s3_core C;
static int g_sel, g_locked, g_sock = -1, g_net;     /* g_net: 1 once sdnet runs */
static u32 g_link_frames, g_dst_ip, g_dst_port;
static u8 g_rc;                     /* Settings > Network status > Reconnect: 0 none, 1 asked (the tick tears the stack down), 2 adapter / link / address coming back, 3 address is back, waiting for the server's first answer */
static u32 g_rc_t;                  /* the clock when stage 3 began (it gives up after RC_SERVER_MS) */
#define RC_SERVER_MS 15000u
static u32 g_exi_id, g_init_rc = 0xFFu;   /* : the EXI id read when the last bring-up ended (0 = not read yet) and its result byte (0xFF = none yet), for the network report (ms_net_dbg 14 / 15) */
static u32 g_relink;                /* frames since the last cable probe after S3_FAIL_LINK */

/* ---- time (rate from the OS bus clock word 0x800000F8 / 4, see sdenet_glue_clock.h; 40.5 MHz on a GameCube) ---- */
static u32 tbl(void) { return sdg_tbl(); }
#ifdef MM_FTSTATS
/* EXI cost readout (line XE): every EXI transaction of the BBA driver is timed from the call to the end of its SDK_EXISync, counted since match frame 3.  [0] ops, [1] sum of ticks, [2] longest op in ticks, [3] ticks in x_delay, [4] imm ops, [5] dma ops, [6] selects */
u32 rbr_xe[8];
#define XE_B() u32 xe0 = tbl()
#define XE_E(k) do { u32 xd = tbl() - xe0; rbr_xe[0]++; rbr_xe[1] += xd; if (xd > rbr_xe[2]) rbr_xe[2] = xd; rbr_xe[k]++; } while (0)
#else
#define XE_B() do { } while (0)
#define XE_E(k) do { } while (0)
#endif
#ifdef MM_FC
/* FRAME COST (test build RELEASE_TEST=fc, menus/on/fc_log.h): exclusive time categories per scene-loop pass.  fc_go charges the ticks since the last
   checkpoint to the running category and switches; it returns the category it interrupted so a nested region (B0 from the VI interrupt, one EXI transaction) puts it back.  The callers
   of fc_go run with interrupts off (s3_exi_entry, the BBA ops); the game-thread hooks use fc_gt (below), which turns them off around the update.  Read only: nothing here is on the wire,
   in the simulation or in a checksum. */
#include "fc_log.h"
static struct fc_live g_fc;
static u32 fc_on, fc_t, fc_cat, fc_acc[FC_NC], fc_ops, fc_oplong, fc_tx, fc_ip, fc_iops;   /* fc_ip: inside s12_idle's poll (FC_NIDLE); fc_iops: its commands this pass */
static u32 fc_go(u32 cat)
{
    u32 now, p;
    if (!fc_on) return 0;
    now = tbl(); p = fc_cat;
    fc_acc[p] += now - fc_t; fc_t = now; fc_cat = cat; g_fc.cps++;
    return p;
}
static u32 fc_xb(void) { return fc_go(fc_tx ? FC_NTX : fc_ip ? FC_NIDLE : FC_NRX); }
static void fc_xe(u32 p, u32 op)                                 /* the end of one network EXI transaction (op 1) or driver delay (op 0): its length, then back to the category it interrupted */
{
    u32 now, d;
    if (!fc_on) return;
    now = tbl(); d = now - fc_t;
    fc_acc[fc_cat] += d; fc_t = now; fc_cat = p; g_fc.cps++;
    if (!op) return;
    fc_ops++; if (fc_tx) g_fc.txops++;
    if (fc_ip && !fc_tx) fc_iops++;
    if (d > fc_oplong) fc_oplong = d;
}
#define FC_XB() u32 fcx = fc_xb()
#define FC_XE() fc_xe(fcx, 1u)
#define FC_XD() fc_xe(fcx, 0u)
#define FC_TX(v) (fc_tx = (v))
#else
#define FC_XB() do { } while (0)
#define FC_XE() do { } while (0)
#define FC_XD() do { } while (0)
#define FC_TX(v) ((void)0)
#endif
static u32 g_ms;                                            /* last ms value handed out */
static void clock_step(void) { g_ms = sdg_clock_ms(); }
static u32 clock_ms(void *c) { (void)c; clock_step(); return g_ms; }
#ifndef BR_BUILD
static void od_dns_link_up(void)
{
    sddns_io io;
    if (sddns_sdnet_bind(&io, &g_od_dctx, &S, (u16)(49152u + (clock_ms(0) & 0x3FFFu))) >= 0) { sddns_init(&g_od_dns, &io, clock_ms(0) ^ 0x444E53u); g_od_dns_ok = 1; }
}
#endif
static s3_u32 clock_us(void *c) { (void)c; return sdg_clock_us(); }

/* WII freeze hunt (q17/q18: after the freeze the PowerPC takes no interrupt at all, the vBBA ARM cannot find the status block in the quiet build):
   on the Wii kernel (marker 'EXPB' at 0xD3026694) the breadcrumb, the time base, the caller of the last irq_off, a count of irq_off calls and an "inside" flag go to uncached shared
   words 0xD30267D0..E0 that the ARM prints in the freeze dump.  Off (one test of a cached flag) everywhere else. */
static int g_bcw = 2;                                          /* the Wii breadcrumb mirror: 2 = not yet decided, 1 on, 0 off (decided once by bc_wii_on from the first tick; irq_off / irq_restore only READ it: they must stay leaf functions) */
static int bc_wii_on(void)
{
    if (g_bcw == 2) {
        g_bcw = (sd_plat_ms_ticks() >= 55000u && *(volatile u32 *)0xD3026694u == 0x45585042u) ? 1 : 0;
        if (g_bcw == 1) *(volatile u32 *)0xD30267D8u = (u32)&s3_status;   /* DESYNC LOCATOR: the shutdump (FZ11) finds the status block here (word D8 was the never-written 'irq_off lr'); the scan of FZ3 does not find it in current builds */
    }
    return g_bcw;
}
static void bc_wii(u32 x) { if (bc_wii_on()) { *(volatile u32 *)0xD30267D0u = x; *(volatile u32 *)0xD30267D4u = tbl(); } }

static u32 irq_off(void)
{
    u32 m;
    __asm__ volatile("mfmsr %0" : "=r"(m));
    __asm__ volatile("mtmsr %0" : : "r"(m & ~0x8000u) : "memory");
    if (g_bcw == 1) { *(volatile u32 *)0xD30267DCu += 1u; *(volatile u32 *)0xD30267E0u = 1u; }      /* count and "inside" flag (the caller is not recorded: reading LR is not allowed here) */
    return m;
}
static void irq_restore(u32 m)
{
    if ((m & 0x8000u) && g_bcw == 1) *(volatile u32 *)0xD30267E0u = 0u;
    __asm__ volatile("mtmsr %0" : : "r"(m) : "memory");
}

/* ---- bba_exi_ops over the retail SDK (as ); caller holds EXILock(0, 2) ---- */
static u32 g_bba_sel_used;                                    /* a BBA select happened under this EXI lock (Wii: the device routing must be handed back, see unlock) */
static u32 g_bch, g_bdev = 2u;                                /* the EXI channel / device the BBA is on: 0 / 2 as on a GameCube; 2 / 0 on a Wii whose kernel says so (marker 'BCH2'), so the card never shares its state (finding 51) */
static u32 g_pvr;                                             /* PV fast path: 1 = channel 2 is routed to the PV device (device 1) in this lock window */
static u32 g_pvp;                                             /* PV device probe at the last bring-up: 0 not tried, 1 refused, 2 answered (the FC readout's path word) */
static int x_select(void *c) { XE_B(); FC_XB(); (void)c; BC(0x11); g_pvr = 0; g_sel = SDK_EXISelect(g_bch, g_bdev, EXI_FREQ_32MHZ) ? 1 : 0; g_bba_sel_used = 1; BC(0x111); FC_XE(); XE_E(6); return g_sel ? 0 : -1; }
static void x_deselect(void *c) { (void)c; if (g_sel) { FC_XB(); BC(0x12); SDK_EXIDeselect(g_bch); g_sel = 0; BC(0x112); FC_XE(); } }
static void x_imm_write(void *c, u32 d, int len)
{
    u32 tmp __attribute__((aligned(4)));
    u8 *p = (u8 *)&tmp;
    int i;
    (void)c;
    if (!g_sel) return;
    for (i = 0; i < len; i++) p[i] = (u8)(d >> (24 - 8 * i));
    { XE_B(); FC_XB(); BC(0x13); if (SDK_EXIImm(g_bch, &tmp, len, EXI_WRITE, 0)) { BC(0x213); SDK_EXISync(0); } BC(0x113); FC_XE(); XE_E(4); }
}
static u32 x_imm_read(void *c, int len)
{
    u32 tmp __attribute__((aligned(4))) = 0;
    u8 *p = (u8 *)&tmp;
    u32 v = 0;
    int i;
    (void)c;
    if (!g_sel) return 0;
    { XE_B(); FC_XB(); BC(0x14); if (SDK_EXIImm(g_bch, &tmp, len, EXI_READ, 0)) { BC(0x214); SDK_EXISync(0); } BC(0x114); FC_XE(); XE_E(4); }
    for (i = 0; i < len; i++) v |= (u32)p[i] << (24 - 8 * i);
    return v;
}
static void x_dma_write(void *c, const void *b, u32 n) { XE_B(); (void)c; if (g_sel) { FC_XB(); BC(0x15); if (SDK_EXIDma(g_bch, (void *)b, (int)n, EXI_WRITE, 0)) { BC(0x215); SDK_EXISync(0); } BC(0x115); FC_XE(); XE_E(5); } }
static void x_dma_read(void *c, void *b, u32 n) { XE_B(); (void)c; if (g_sel) { FC_XB(); BC(0x16); if (SDK_EXIDma(g_bch, b, (int)n, EXI_READ, 0)) { BC(0x216); SDK_EXISync(0); } BC(0x116); FC_XE(); XE_E(5); } }
static void x_flush(void *c, const void *b, u32 n) { (void)c; SDK_DCFlushRange((void *)b, n); }
static void x_inval(void *c, void *b, u32 n) { (void)c; SDK_DCInvalidateRange(b, n); }
static void x_delay(void *c, u32 us) { u32 t0 = tbl(), t = sdg_us_ticks(us); FC_XB(); (void)c; while (tbl() - t0 < t) { }
    FC_XD();
#ifdef MM_FTSTATS
    rbr_xe[3] += tbl() - t0;
#endif
}
/* PV fast path: one DMA per datagram / per receive poll on the PV device (EXI channel 2 device 1; only a kernel built with NIN_MS_PVBBA answers its select).  The device is
   selected before every transfer; an older kernel answers the select with 0 and is then never sent a PV transfer (an imm or
   DMA to a device it does not know would leave the PowerPC waiting for good). */
static int x_pv_xfer(void *c, void *b, u32 n, int w)
{
    (void)c;
    { FC_XB(); g_pvr = SDK_EXISelect(2, 1, EXI_FREQ_32MHZ) ? 1u : 0u; FC_XE(); if (!g_pvr) return -1; }      /* selected before EVERY PV transfer (a receive can be answered without the header after one send: a routing doubt removed for one command) */
    { XE_B(); FC_XB(); if (SDK_EXIDma(2, b, (int)n, w == 1 ? EXI_WRITE : EXI_READ, 0)) SDK_EXISync(2); FC_XE(); XE_E(5); }   /* w: 1 send, 0 receive, 2 receive retried */
    return 0;
}
static const struct bba_exi_ops OPS = { x_select, x_deselect, x_imm_write, x_imm_read,
                                        x_dma_write, x_dma_read, x_flush, x_inval, x_delay };

/* ---- core callbacks (only valid while g_locked) ---- */
#ifdef BR_BUILD
/* SLIPPI HAND-OFF: in a match against a real Slippi client the engine's datagrams
   use the BRIDGE's socket (the port the matchmaker ticket came from: the opponent connects to that address), g_slp_ip is the opponent */
static int g_slp_sock = -1;
static u32 g_slp_ip;
#endif
static int send_cb(void *ctx, s3_u32 ip, s3_u16 port, const s3_u8 *d, s3_u32 len)   /* S7: address from ENet */
{
    int rc, sk = g_sock;
    (void)ctx;
#ifdef BR_BUILD
    if (g_slp_sock >= 0) sk = g_slp_sock;
#endif
    if (!g_locked || sk < 0) return -1;
    FC_TX(1u);
    rc = sdnet_sendto(&S, sk, ip, port, d, (u16)len);
    FC_TX(0u);
    return rc >= 0 ? rc : -1;
}
/* ---- OD (online-direct): the server step in front of the engine's own session ------------------------------------------------------
   The engine does not start its ENet session at DHCP any more: od_glue asks the server (TICKET_CREATE from this very socket, wire v2),
   the ticket names the peer, then the S7 core is pointed at it and started.  Server datagrams share the game socket and are routed by
   source address.  Everything runs in slice (interrupts off, EXI lock held), like the engine's own network work. */
#define OD_NET_UP 0x30u                                          /* ST.state while the server step runs (no peer yet); the core states are 4..6, 0x91 */
extern struct od od_o;                                           /* od_backend.c (state lives.data) */
extern struct od_status od_st;
int s7_core_set_peer(struct s3_core *c, int side, s3_u32 ip, s3_u32 port);   /* net/s17od/s7_core.c */
#ifdef SD_CARD_HOLE
/* OTA on the card route: the Update row drives the hub client's wire session from the MENU thread, where the BBA must not be touched (no EXI lock).  Once armed
   (od_ob_arm, by the first hello), a datagram that od_send gets outside the slice is queued here and sent by the next slice (od_ob_flush, in front of od_tick).  Unarmed: exactly the
   old behaviour (refused, mnc resends later).  Depth 4 and 128 bytes: a handshake frame is 96 bytes, an UPD_FETCH request about 40. */
#define OB_N 4
#define OB_MAX 128
static u8 ob_buf[OB_N][OB_MAX];
static u8 ob_len[OB_N];
static u8 ob_head, ob_n, ob_on;
void od_ob_arm(void) { ob_on = 1; }
#endif
int od_send(void *ctx, const unsigned char *p, unsigned int len)   /* od_glue send callback, set up by od_backend.c */
{
    int rc;
    (void)ctx;
#ifdef SD_CARD_HOLE
    if (!g_locked && ob_on) {
        unsigned int i;
        u8 slot;
        if (len == 0 || len > OB_MAX || ob_n >= OB_N) return -1;
        slot = (u8)((ob_head + ob_n) % OB_N);
        for (i = 0; i < len; i++) ob_buf[slot][i] = p[i];
        ob_len[slot] = (u8)len;
        ob_n++;
        return (int)len;
    }
#endif
    if (!g_locked || g_sock < 0) return -1;
    FC_TX(1u);
    rc = sdnet_sendto(&S, g_sock, od_server_ip(&od_o), (u16)(od_o.cfg->server_port ? od_o.cfg->server_port : 43150u), p, (u16)len);
    FC_TX(0u);
    return rc >= 0 ? rc : -1;
}
#if defined(SD_CARD_HOLE)
/* (every card-route build since Settings > Network status reads it; was netcfg=on only) the on-screen network diagnostic of the sign overlay and the menu reads the glue state and the sdnet facts here (ml_glue.c hands the function over in
   sgo_env.netdbg).  Plain reads from the menu thread, no lock: a diagnostic number may be one frame old.  w[0] glue state (S3_*), 1 ip, 2 mask, 3 gateway, 4 dns, 5 dhcp state | tries << 8 |
   NWAYS link byte << 16 | stack running << 24, 6 / 7 frames sent / received by sdnet (ARP and DHCP included), 8 clock (ms) of the last DHCP ACK (0 = never), 9 frames not for us | socket drops << 16. */
static int macpub(unsigned char *pm, unsigned int *pk) { return ms_macpub_read((const volatile unsigned int *)MACPUB_ADDR, *(volatile unsigned int *)0x800000F8u, pm, pk); }
/* WII ON WI-FI: 1 = a Wii whose vBBA published a VALID interface kind 1 (wireless).  A GameCube (the bus clock check inside), nothing published or kind
   0 / 2 = 0: a detection miss never locks out a wired player.  Read by on_backend.c ms_slippi_mm_off (the Slippi rows only) and the panel reason 6. */
int ms_wii_wifi(void) { unsigned char pm[6]; unsigned int pk = 0; int ok = macpub(pm, &pk); return ms_macpub_wifi(ok, pk); }
/* TIERED MATCHMAKING: the link class the wired filter reports (on_backend.c gr_link, the same sources): 1 wired, 2 Wi-Fi, 0 unknown.  A GameCube / Dolphin (bus clock word
   below 200 MHz) has the BBA = wired; a Wii is wired / Wi-Fi only when its vBBA published a valid interface kind (2 wired, 1 wireless), else unknown. */
int ms_link_kind(void)
{
    unsigned char pm[6]; unsigned int pk = 0;
    if (*(volatile unsigned int *)0x800000F8u < MACPUB_BUS_WII) return 1;
    return macpub(pm, &pk) ? (pk == 2u ? 1 : pk == 1u ? 2 : 0) : 0;
}
void ms_net_dbg(unsigned *w)
{
    w[0] = ST.state; w[1] = S.ip; w[2] = S.mask; w[3] = S.gw; w[4] = S.dns;
    w[5] = (u32)S.dhcp.state | ((u32)S.dhcp.tries << 8) | ((u32)B.link << 16) | ((u32)g_net << 24);
    w[6] = S.stats.tx_frames; w[7] = S.stats.rx_frames; w[8] = S.dhcp.t_bound;
    w[9] = (S.stats.rx_nomatch & 0xFFFFu) | (S.stats.sock_drop << 16);
    w[10] = ((u32)NIC.mac[0] << 24) | ((u32)NIC.mac[1] << 16) | ((u32)NIC.mac[2] << 8) | NIC.mac[3];      /* 10 / 11: the adapter's MAC (callers pass at least 16 words) */
    w[11] = ((u32)NIC.mac[4] << 8) | NIC.mac[5];
    {   /* on a Wii the vBBA (Nintendont) publishes the MAC of the interface IOS really uses as soon as the address is up: show that one (the user binds it in the router), not the chip MAC the driver read at init */
        unsigned char pm[6]; unsigned int pk;
        if (macpub(pm, &pk)) {
            w[10] = ((u32)pm[0] << 24) | ((u32)pm[1] << 16) | ((u32)pm[2] << 8) | pm[3];
            w[11] = ((u32)pm[4] << 8) | pm[5];
        }
    }
    w[12] = od_o.m.whoami_ip;                                     /* 12: the public address the server last reported (0 = not asked yet) */
    w[13] = g_rc;                                                 /* 13: a full reconnect is under way (1..3, see g_rc) */
    w[14] = g_exi_id;                                             /* 14 / 15: EXI id read when the last bring-up ended, and its result byte (BBA_E_* as unsigned, 0 ok, 0xFF = none yet): the network report */
    w[15] = g_init_rc;
}
extern int br_busy(void) __attribute__((weak));                  /* the Slippi bridge is searching or in a match (bridge/br_glue.c) */
/* Settings > Network status > Reconnect (ms_net_cost 108): 0 started, 1 refused (a search, a match, a Game Night session or a Slippi search is active), 2 already running.  Only asks: the tick below tears the stack down
   and brings it up again from the adapter (bba_init_begin, link, sdnet_init = ARP table and sockets cleared, a new DHCP lease, the resolver and its cache bound again), then the hub client handshakes with the server. */
int ms_net_reconnect(void)
{
    if (g_rc) return 2;
    if (ST.state == S3_NET_INIT || ST.state == S3_NET_LINK || ST.state == S3_NET_DHCP) return 2;     /* the first bring-up still runs */
    if (ST.state != S3_NET_WAIT && ST.state != OD_NET_UP && ST.state < S3_NOBBA) return 1;           /* session states 4..6 */
    if (ST.state == S7_SES_LOST || ST.state == S7_LOOPBACK || C.seats == 3) return 1;
    if (od_o.phase >= OD_WAIT_NET && od_o.phase != OD_ERR) return 1;
    if (br_busy && br_busy()) return 1;
    g_rc = 1;
    return 0;
}
/* Settings > Network status > Test connection: the probe engine (offline/net/nettest.c) on a socket of its own that exists only while the run lasts.  Started by the menu
   through ms_net_cost 110 (ms_net_test_q), stepped from slice right after sdnet_poll, stopped by cancel (111), the end of the run, a session, a reconnect or a lost address.  The menu reads the words 112 to 114. */
static struct nettest g_nt;
static struct sddns_sdnet_ctx g_nt_ctx;
static u8 g_nt_on;                                               /* 1 while the socket is open */
static u8 g_nt_seen;                                             /* a run was started since boot (the state word is 0 before) */
/* ONLINE DELAY AUTO: the same runner measures in the BACKGROUND, internet leg only (no router probe), once the network is up
   after boot and again every AD_BG_EVERY_MS while idle in the menus; a background run never busy-polls (one poll per frame: no hitch, the samples are coarse) and stops when a session,
   a search or a match starts (nt_step).  The player's own Test connection wins (it aborts a background run) and its internet result is used too.  The result is ms_ad_word. */
#define AD_BG_FIRST_MS 2000u                                     /* after the first time the address is up: let the start-up traffic (update check, HELLO) go first */
#define AD_BG_EVERY_MS 600000u                                   /* again every 10 minutes while idle */
#define AD_BG_RETRY_MS 60000u                                    /* a run without an answer: again in a minute */
static u8 g_nt_bg;                                               /* the run in g_nt is a background one (its words are not the menu's) */
static u32 g_nt_fgw[3];                                          /* the player's last Test connection words (112..114), kept while a background run reuses g_nt */
static u32 g_ad_next;                                            /* clock ms of the next background run (0 = not armed yet) */
static void nt_close(void)
{
    if (g_nt_on) { sdnet_close(&S, g_nt_ctx.sock); g_nt_on = 0; }
}
static void nt_stop(void)                                        /* abort a running test and give the socket back */
{
    nt_abort(&g_nt);
    nt_close();
}
static int nt_session_busy(void)                                 /* the refusal of Reconnect: a search, a match, a Game Night session, a Slippi search */
{
    if (ST.state != S3_NET_WAIT && ST.state != OD_NET_UP && ST.state < S3_NOBBA) return 1;
    if (ST.state == S7_SES_LOST || ST.state == S7_LOOPBACK || C.seats == 3) return 1;
    if (od_o.phase >= OD_WAIT_NET && od_o.phase != OD_ERR) return 1;
    if (br_busy && br_busy()) return 1;
    return 0;
}
static void ad_harvest(void)                                     /* a finished run (either kind): its internet leg sets the Auto word; a run without enough answers keeps the last value */
{
    nt_leg_result r;
    u32 ms;
    int wii = sd_plat_ms_ticks() >= 55000u;
    nt_leg_get(&g_nt, NT_INTERNET, &r);
    ms = ad_rtt_ms(r.median, r.spread, r.got, r.coarse);
    if (ms) ms_ad_word = ad_word(ms, wii, !g_nt_bg);
    g_ad_next = clock_ms(0) + (ms ? AD_BG_EVERY_MS : AD_BG_RETRY_MS);
}
static int in_game(void);
static void ad_bg_tick(u32 now)                                   /* from slice, menus only: start a background run when one is due */
{
    sddns_io io;
    if (g_nt_on || g_rc || !g_net || !S.ip || ST.state != OD_NET_UP || nt_session_busy() || in_game()) return;
    if (!g_ad_next) { g_ad_next = (now + AD_BG_FIRST_MS) | 1u; return; }
    if ((u32)(now - g_ad_next) > 0x7FFFFFFFu) return;             /* not due yet (the clock may wrap) */
    g_ad_next = now + AD_BG_RETRY_MS;                             /* a start that fails is tried again later */
    if (sddns_sdnet_bind(&io, &g_nt_ctx, &S, (u16)(49152u + ((now * 13u) & 0x3FFFu))) < 0) return;
    if (g_nt_seen && !g_nt_bg) {                                  /* keep what the player's test showed (the words 112..114) */
        nt_leg_result r;
        g_nt_fgw[0] = nt_state_word(&g_nt);
        nt_leg_get(&g_nt, NT_ROUTER, &r); g_nt_fgw[1] = nt_pack(&r);
        nt_leg_get(&g_nt, NT_INTERNET, &r); g_nt_fgw[2] = nt_pack(&r);
    }
    if (nt_start(&g_nt, &io, 0, 0, 0, NT_IP_CF1, NT_IP_G8, sdg_clock_us(), now ^ S.ip ^ 0x41444C31u) != 0) { sdnet_close(&S, g_nt_ctx.sock); return; }   /* router_ok 0: the internet leg only */
    g_nt_on = 1; g_nt_bg = 1;
}
static void nt_step(u32 now)                                     /* from slice: the polls of this frame, then the socket goes back when the run is over */
{
    u32 t, t1, k;
    (void)now;
    if (!g_nt_on) return;
    if (g_rc || !S.ip || nt_session_busy()) { nt_stop(); return; }
    t = sdg_clock_us();                                          /* microseconds: the reply is stamped when sdnet_poll hands it over, not at the next frame */
    nt_frame(&g_nt, t);
    for (k = 0; k < 64u; k++) {                                  /* bounded busy loop (nt_spin_us: at most NT_SPIN_MAX_US = 11 ms of the frame, only while a reply is expected; the test runs in the menu only) */
        if (nt_poll(&g_nt, t) != NT_RUN) { ad_harvest(); nt_close(); return; }
        if (g_nt_bg || !nt_spin_us(&g_nt, t)) return;            /* a background run: one poll per frame, never a busy loop */
        t1 = t;
        do { t = sdg_clock_us(); } while ((u32)(t - t1) < 250u); /* a poll step of about 0.25 ms */
        sdnet_poll(&S);
        t = sdg_clock_us();
    }
}
static u32 ms_net_test_start(void)
{
    sddns_io io;
    u32 now = clock_ms(0), port = 49152u + ((now * 7u) & 0x3FFFu);
    u32 router_alt = 0;
    int wii = sd_plat_ms_ticks() >= 55000u;
    if (g_nt_on && g_nt_bg) nt_stop();                           /* the player's test replaces a background run */
    if (g_nt_on) return 2;
    if (g_rc || ST.state == S3_NET_INIT || ST.state == S3_NET_LINK || ST.state == S3_NET_DHCP) return 5;   /* the link is coming up (a Reconnect, the start): NOT 'a search or a match is active' (cube pass: that false refusal about 10 s after Reconnect; nt_session_busy counts the bring-up states 1..3 as a session) */
    if (!g_net || !S.ip) return 3;
    if (nt_session_busy()) return 1;
    if (ST.state != OD_NET_UP) return 3;
    if (sddns_sdnet_bind(&io, &g_nt_ctx, &S, (u16)port) < 0 && sddns_sdnet_bind(&io, &g_nt_ctx, &S, (u16)(port ^ 0x155u)) < 0) return 4;
    if (S.dns && S.dns != S.gw && ((S.dns ^ S.ip) & S.mask) == 0) router_alt = S.dns;            /* the gateway runs no DNS service: the resolver DHCP gave, if it is a neighbour */
    if (nt_start(&g_nt, &io, !wii, S.gw, router_alt, NT_IP_CF1, NT_IP_G8, sdg_clock_us(), now ^ S.ip) != 0) { sdnet_close(&S, g_nt_ctx.sock); return 4; }
    g_nt_on = 1; g_nt_seen = 1; g_nt_bg = 0;
    return 0;
}
u32 ms_net_test_q(int i)                                         /* ms_net_cost 110 start, 111 cancel, 112 state word, 113 router, 114 internet (nettest.h) */
{
    nt_leg_result r;
    if (i == 110) return ms_net_test_start();
    if (i == 111) { if (g_nt_on && !g_nt_bg) nt_stop(); return 0; }
    if (i == 115) return ms_ad_word;                             /* the Auto delay word */
    if (!g_nt_seen) return 0;
    if (g_nt_bg) return g_nt_fgw[i - 112];                       /* a background run is in g_nt: the menu sees the player's last test */
    if (i == 112) return nt_state_word(&g_nt);
    nt_leg_get(&g_nt, i == 113 ? NT_ROUTER : NT_INTERNET, &r);
    return nt_pack(&r);
}
#endif
int od_core_state(void) { return C.state; }                      /* for the backend: the engine's session state */
u32 od_clock_ms(void) { return clock_ms(0); }                    /* the glue's one clock (the backend runs in another translation unit) */
#ifdef ON_BUILD    /* ON (online image: Ranked result report) */
/* The evidence of a finished game: the newest finalized frame this console has and the Slippi checksum it computed for that frame (the same value on both
   consoles: the engine compares them every frame).  1 = both valid; 0 = the checksum history does not hold the frame (cks 0). */
int od_core_final(u32 *frame, u32 *cks)
{
    s32 f = C.cur_fin;
    if (f > 0 && C.hist_frame[f & (S3_CKS_HIST - 1)] == f) { *frame = (u32)f; *cks = C.hist_cks[f & (S3_CKS_HIST - 1)]; return 1; }
    *frame = f > 0 ? (u32)f : 0u;
    *cks = 0;
    return 0;
}
#endif
#if defined(ON_BUILD) && defined(HUB_OVL_RES)
#include "gns_ovl.h"                                             /* Game Night spectate uploader: the resident hook entries live in menus/gns/gns_res.c */
extern volatile unsigned int gns_hot;                            /* 1 while ms/gns.ovl is loaded in AllM */
extern volatile unsigned int gns_whot;                           /* 1 while a WATCH session ("X: Last Game") is in force */
void gns_b0(const struct gns_view *v);
int gns_rx(const unsigned char *d, unsigned int n);
void gns_w_b0(const unsigned char *txb, unsigned char *rxb);
int gns_w_b3(unsigned char *msrb);
int gns_w_sels(const unsigned char **s0, const unsigned char **s1);
#endif
static void od_server_rx(u32 ip, u16 port, const u8 *d, u32 n)
{
    if (ip == od_server_ip(&od_o) && port == (u16)(od_o.cfg->server_port ? od_o.cfg->server_port : 43150u)) {
#if defined(ON_BUILD) && defined(HUB_OVL_RES)
#ifdef MS_MATCH_MSG
        if (n >= 3u && d[2] == 0xAFu && gns_rx(d, n)) return;                    /* MSG_FETCH reply that answers the match-time message check of the gns block; any other goes on to the hub client */
#endif
        if (n >= 3u && d[2] >= 0xBBu && d[2] <= 0xBFu && gns_rx(d, n)) return;   /* GN_SPEC .. GN_REC replies and the feed (reply bit | 0x3B..0x3F): the block checks them; they must not pile up in the hub client's inbox during the match */
#endif
        od_rx(&od_o, d, n);
    }
}
static int od_from_server(u32 ip, u16 port)
{
    return od_o.cfg && ip == od_server_ip(&od_o) && port == (u16)(od_o.cfg->server_port ? od_o.cfg->server_port : 43150u);
}
static volatile u32 g_rx_new;                                   /* S12: remote frames arrived since the last early check */
static void drain(void)
{
    static u8 pk[SDENET_MTU + 16];                               /* S7 */
    u32 ip;
    u16 port;
    int n, k = 0;
    s3_s32 rl0 = C.rlatest;                                      /* S12 */
    while (k++ < 16 && (n = sdnet_recvfrom(&S, g_sock, pk, sizeof pk, &ip, &port)) >= 0) {
        if (od_from_server(ip, port)) od_server_rx(ip, port, pk, (u32)n);   /* OD */
        else s3_core_rx_from(&C, ip, port, pk, (u32)n);
    }
#ifdef BR_BUILD
    k = 0;
    while (g_slp_sock >= 0 && k++ < 16 && (n = sdnet_recvfrom(&S, g_slp_sock, pk, sizeof pk, &ip, &port)) >= 0)
        if (!((ip ^ g_slp_ip) & 0xFFFFFF00u)) s3_core_rx_from(&C, ip, port, pk, (u32)n);   /* CARRIER POOL: the opponent's /24 (a carrier NAT pool may send from a neighbour) */   /* the opponent; anything else on the bridge's port is dropped (the matchmaker connection is closed) */
#endif
    if (C.rlatest != rl0) g_rx_new = 1;                          /* S12: only a flag (drain also runs from B0, an interrupt) */
}
static void poll_cb(void *ctx) { (void)ctx; if (g_locked) { sdnet_poll(&S); drain(); } }

static u32 g_card_cnt, g_card_tb, g_card_skip;                /* last card-command counter seen, when it moved, slices skipped in a row (at most 600 = 10 s) */
static u32 g_exi_skip;                                        /* consecutive slices skipped because a card transfer-complete interrupt was pending (Wii) */
#ifdef MM_FC
static u32 g_card_skip_n;                                     /* FRAME COST: network slices / B0 polls refused for a card transfer (Wii), counted */
#define FC_CSKIP() (g_card_skip_n++)
#else
#define FC_CSKIP() ((void)0)
#endif
static int lock(void)
{
    /* WII ONLY (kernel built with NIN_MS_EXIPUB, word 0xD3026690 = the ARM holds a transfer-complete interrupt back; 0xD3026560 = EXI_INT, thrown and not yet handled): a card transfer is finishing.
       Its completion interrupt would arrive after this slice and find the EXI routing / callback state our BBA transfers changed (ALPHA_OPEN_FINDINGS 41: 15 overlaps in a hung run), so wait a frame.
       At most 120 frames in a row (a stuck word must never switch the network off), and only when the word exists (the kernel marks it with 'EXPB' at 0xD3026694). */
    if (sd_plat_ms_ticks() >= 55000u && *(volatile u32 *)0xD3026694u == 0x45585042u && (*(volatile u32 *)0xD3026690u || *(volatile u32 *)0xD3026560u) && g_exi_skip < 120u) { g_exi_skip++; FC_CSKIP(); return 0; }
    if (sd_plat_ms_ticks() >= 55000u && *(volatile u32 *)0xD3026694u == 0x45585042u) {      /* a card transfer is going on (kernel word 0xD3026698 counts the card-side EXI commands): no BBA slice within 20 ms of the last one (finding 42: the hung runs show a card DMA read 7 ms before the first BBA select) */
        u32 cn = *(volatile u32 *)0xD3026698u, now = tbl();
        if (cn != g_card_cnt) { g_card_cnt = cn; g_card_tb = now; }
        if (now - g_card_tb < 20u * sd_plat_ms_ticks() && g_card_skip < 600u) { g_card_skip++; FC_CSKIP(); return 0; }
        g_card_skip = 0;
    }
    g_exi_skip = 0;
    if (!SDK_EXILock(g_bch, g_bdev, 0)) { ST.lock_fail++; return 0; }
    g_locked = 1;
    g_pvr = 0;                                                   /* PV: select the device again in this window */
    return 1;
}
static void unlock(void)
{
    if (g_sel) { SDK_EXIDeselect(g_bch); g_sel = 0; }
    /* the routing hand-back (select device 0 after the slice) is left out: it was the last command in the hang rings (ALPHA_OPEN_FINDINGS 43); g_bba_sel_used is only kept for the readout */
    g_bba_sel_used = 0;
    SDK_EXIUnlock(g_bch);
    g_locked = 0;
    ST.sock_drop = S.stats.sock_drop;
}

/* ---- S6b: rollback statistics block (watched by the harness) ---- */
enum {
    RB_MAGIC, RB_STORE, RB_HEAP_BEFORE, RB_HEAP_AFTER, RB_B1, RB_B1_FAIL, RB_B1_SUM, RB_B1_MAX, RB_B1_BASE, RB_B2,
    RB_B2_MISS, RB_B2_SUM, RB_B2_MAX, RB_COUNT, RB_RESIM, RB_TICK_SUM, RB_TICK_MAX, RB_HIST,          /* 17..24 hist k=0..7+ */
    RB_BYTES = RB_HIST + 8, RB_CELLS, RB_MAXLINES, RB_LOGDROPS, RB_SDERR, RB_PUB, RB_GAP, RB_B1_LINES, RB_ITERS,
    RB_ITER_SUM, RB_RESIM_SUM, RB_PRES_OVF, RB_B1_INACTIVE, RB_RES_MAX, RB_DSKIP, RB_LAST_K, RB_ITERS_RB, RB_ALLOCS,
    RB_ALLOC_FAIL, RB_MAX_K, RB_B1_RESIM_SUM, RB_TRACE_SUM, RB_LOGMAX, RB_HDRPRE, RB_XMAX, RB_FREECELLS, RB_ADDS,
    RB_REMOVES, RB_B1_STEP_SUM, RB_B1_STEP_N, RB_B1_STEP_MAX, RB_BASE_SUM, RB_BARMISS, RB_PUB_FRAME, RB_SF, RB_ODB,
    RB_PROLL_SUM, RB_PHASE, RB_PH_FRAME, RB_OSA_LAST, RB_OSF_LAST, RB_B0PH, RB_REHASH, RB_REHASH_DIFF, RB_RB_PUB,
    RB_EVTMAX, RB_PREMAX, RB_NETMAX, RB_REBUILDS,                                          /* S6C: 69..72 */
    RB_MEM, RB_MEM_FREE, RB_MEM_BIG, RB_MEM_SIZE, RB_MEM_MIN, RB_MEM_OFF_F, RB_N            /* S19 store fit: 74..79 (status word, free / largest cell
                                                                                              at match start, store bytes, free minimum in the match,
                                                                                              frame rollback went off) */
};
struct s6_rb { volatile u32 w[80]; };
struct s6_rb s6_rb __attribute__((section(".s6rb"), used)) = { { 0x53365242u } };
#define RB s6_rb.w

/* ---- S6C telemetry block (watched by the harness; layout TELN) ---- */
enum {
    T_MAGIC, T_STRIDE, T_CAP, T_B1_CAP, T_B1_SKIP, T_B1N = 5, T_B1T = 8, T_GAPLOADS = 11, T_GAPFR, T_GAPH = 13,
    T_RXBVIOL = 17, T_GFMIS, T_SWITCH, T_SPASS = 20, T_KT = 23, T_KTSUM = 31, T_CAPH = 32, T_LOGIC = 40, T_RESTORE,
    T_CAPCOST, T_WORK, T_BASEWORK, T_PASSES, T_LATE, T_WORKMAX, T_PERIODMAX, T_WH = 49, T_REHASH_N = 61, T_REHASH_DIFF,
    T_CANARY
};
struct s6c_tel { volatile u32 w[64]; };
struct s6c_tel s6c_tel __attribute__((section(".s6crb"), used)) = { { 0x53364354u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0x5A5AC0DEu } };
#define TL s6c_tel.w

/* ---- S6D telemetry block (watched by the harness; layout DELN) ---- */
enum {
    D_MAGIC, D_CAP, D_WANT, D_TOKENS, D_IDLE, D_BUDGET, D_PER, D_LATE, D_LATE_TICKS, D_RAISES, D_DROPS,
    D_STALLH = 11, D_WANTH = 19, D_CW_SUM = 27, D_CW_N, D_SFX_REQ, D_SFX_START, D_SFX_STOP, D_SFX_STOP_SL, D_SFX_REQ_RS,
    D_SFX_START_RS, D_SFX_LOGN, D_SFX_OVF, D_SFX_LINES, D_SFX_STATE, D_RB_LATE, D_RB_LASTWORK, D_RBH = 41, D_COSTPCT = 49,
    D_RB_N, D_PER_N, D_CANARY = 63
};
struct s6d_tel { volatile u32 w[64]; };
struct s6d_tel s6d_tel __attribute__((section(".s6drb"), used)) = { { 0x53364454u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0x5A5AC0DEu } };
#define DL s6d_tel.w
static struct s6d_cap g_cs;                                   /* S6D: adaptive cap v2 state */
static struct s6d_cap_cfg g_cc;
/* S6D TEST ONLY cost emulation: busy-wait (cost_pct - 100) % of dt on the Time Base */
static void cost_wait(u32 dt)
{
    u32 p = s3_cfg.cost_pct, w, t0;
    if (p <= 100u || p > 400u) return;
    w = dt / 100u * (p - 100u);
    t0 = tbl();
    while (tbl() - t0 < w) { }
    DL[D_CW_SUM] += w >> 8;
    DL[D_CW_N]++;
}
static int rb_active;                                         /* (tentative definition; see below) */
#define FRAME_TICKS sdg_frame_ticks()                         /* TB rate / 60 Hz: 675,000 on a GameCube (and Dolphin), 1,012,500 on a Wii */
static u32 g_stride = 1, g_stride_next = 1, g_cap_next = 7;
static u32 ema(u32 e, u32 x) { return e ? e - (e >> 3) + (x >> 3) : x; }          /* 1/8 exponential average */
static u32 hw(u32 t) { u32 p = s3_cfg.hw_pct ? s3_cfg.hw_pct : 100u; return (u32)s7_udiv64s((unsigned long long)t * p, 100u); }   /* the same quotient without libgcc __udivdi3 (s7_core.c) */
static u32 cfg_stride_max(void) { u32 m = s3_cfg.stride_max; return m < 1 ? 1 : m > 2 ? 2 : m; }   /* review fix 4: stride 3 = gap 2 breaks the 7-entry RXB bound; clamp at 2 (was 3) */

/* ---- sdshim backend ---- */
#ifdef MS_CARD_STRESS
static void stress_poll(void);
#define STRESS_POLL() do { if (S3CFG & 0x10u) stress_poll(); } while (0)
#define STRESS_STEP() stress_step()
#else
#define STRESS_POLL() ((void)0)     /* the card stress lab (cfg flag bit 4) is compiled in only with -DMS_CARD_STRESS: ~300 bytes less in every release engine */
#define STRESS_STEP() ((void)0)
#endif
static u32 g_b0_t;                                              /* S12: Time Base at the last B0 (VI pad alarm) */
/* S12: VI position.  B0 runs in the VI pad alarm, but it is delayed whenever interrupts were off at the retrace (B1/B2,
   our slices), so its Time Base overstates the time left.  The VI line is exact: the pad-alarm line is the smallest line
   seen at B0 over the last 32 calls, the field length the largest line seen. */
#define VIGetCurrentLine ((u32 (*)(void))0x80350294)
static u32 g_b0_line[32], g_b0_li, g_irq_line = 0xFFFFu, g_vmax;
static void vi_note_b0(void)
{
    u32 l = VIGetCurrentLine(), i, m = 0xFFFFu;
    if (l > 1000u) return;
    if (l > g_vmax) g_vmax = l;
    g_b0_line[g_b0_li++ & 31u] = l + 1u;
    for (i = 0; i < 32u; i++) if (g_b0_line[i] && g_b0_line[i] - 1u < m) m = g_b0_line[i] - 1u;
    g_irq_line = m;
}
static u32 vi_left(void)                                         /* ticks until the next pad alarm (0 = unknown) */
{
    u32 l = VIGetCurrentLine(), n;
    if (l > g_vmax && l <= 1000u) g_vmax = l;
    if (g_vmax < 200u || g_irq_line > g_vmax || g_b0_li < 32u) return 0;
    n = g_vmax + 1u;
    return ((g_irq_line + n - l) % n) * (FRAME_TICKS / n);
}
/* S16: glue gate.  Prediction needs a restorable past: while the engine is incomplete (a range was lost), has lost
   pending pre-images, or is nearly out of arena, the prediction cap is 0 (lockstep: every frame runs with confirmed
   inputs, so no rollback is needed).  g_cap_base = the configured cap (cap changes only between rollbacks). */
#define s16cfg_flags() (S16CFG)
static int in_game(void);
static void s16_tel_copy(void);
static u32 g_cap_base, g_gate_on, g_gate_run, g_gate_reb_f, g_probe_done;
static u32 g_mem_drain;                                        /* S19 store fit (match_store): prediction stops, the store is about to be freed */
static u32 ps_gp(void);                                       /* defined with the Stadium hooks below */
static void psc_free(void);                                   /* S19e: Stadium transformation file cache, defined with the Stadium hooks below */
#define PS_ARM 14                                              /* countdown frames before the request at which the lockstep window opens (> the rollback depth cap 7 + delay) */
#ifndef S12F_PS_VANILLA
#define S12F_PS_VANILLA 0x100000u
#endif
#define S12F_PS_NOCACHE 0x800000u                              /* TEST ONLY (s12_flags): no transformation-file cache (the old read path) */
#define S12F_PS_NOARM 0x400000u                                /* TEST ONLY (s12_flags): no countdown-armed lockstep window (A/B of the Slippi-parity switch) */
static u32 g_ps_switch;                                        /* S19c: the frame of the next Stadium transformation switch (R + PS_LAG), 0 = none */
extern u32 g_ps_tel[8];
static u32 be32p(const volatile u8 *p);
static void s19_first_capture(s32 frame);                      /* MEM_TOOSMALL: the first base capture could not hold the manifest */
static u32 g_b2_refused, g_b2_inj;                             /* review fix 3: a refused restore ended this match (be_savestate / b2_refused); g_b2_inj = the TEST injector fired */
static u32 s16_gate(void)
{
    u32 h;
    if ((s16cfg_flags() & (S16F_NOSTRICT | S16F_NOGATE)) || !sd_active || !in_game()) { g_gate_on = 0; g_gate_run = 0; return 0; }
    h = sd_health();
    if (!h) {
        if (g_gate_on) { g_gate_on = 0; g_gate_run = 0; }
        return 0;
    }
    g_gate_on = 1;
    YL[Y_S16_GATE_FRAMES]++;
    if (h & SD_H_INC) YL[Y_S16_GATE_INC]++;
    if (h & (SD_H_LOW | SD_H_LOST)) YL[Y_S16_GATE_LOW]++;
    return h;
}
#if defined(ON_BUILD) && defined(HUB_OVL_RES)
/* Game Night spectate uploader (gns_res.c / ms/gns.ovl): the engine's own rings, read only, handed over after s3_core_b0 updated them (only while the AllM block is loaded) */
static void gns_hook(const sdshim_u8 *txb)
{
    struct gns_view v;
    v.mask = S3_RING - 1u; v.ltag = C.ltag; v.lpad = &C.lpad[0][0]; v.lnewest = C.lnewest; v.rtag = C.rtag; v.rpad = &C.rpad[0][0]; v.rlatest = C.rlatest;
    v.hframe = C.hist_frame; v.hcks = C.hist_cks; v.hmask = S3_CKS_HIST - 1u; v.fin = C.cur_fin;
    v.frame = (s32)be32p(txb + 1); v.in_rb = rb_active != 0;
    {   static u8 sel2[2 * S7_SEL_LEN];                          /* both players' MATCH_SELECTIONS, side 0 then side 1 (the same bytes on both consoles): the watcher builds the real game's block from them */
        u32 i;
        const u8 *a = C.lsel, *b = C.rsel;
        if (C.side == 1) { a = C.rsel; b = C.lsel; }
        for (i = 0; i < S7_SEL_LEN; i++) { sel2[i] = a[i]; sel2[S7_SEL_LEN + i] = b[i]; }
        if (C.rules_have && C.rules.alt_mode == S7R_ALT_ON) { sel2[13] = 1; sel2[S7_SEL_LEN + 13] = 1; }   /* Frozen room: record altStageMode 1 (the watcher cannot see the rules) */
        v.lsel = C.rsel_have ? sel2 : 0;
    }
    gns_b0(&v);
}
#endif
/* PQ GUARD + PQ LOG (6, 7).  The pad hook (B0) runs in the VI interrupt and the
   game pushes the accepted frame into its raw pad queue (HSD_PadInit(5, ...), queue type 0: a push into a full queue DROPS the oldest entry,
   which the hook has already counted and sent: from then on this console applies every pad one frame early).  MM_PQ_GUARD: a call that finds
   4 entries waiting stalls (time-sync class, s3_core_b0 pq_hold) instead; same interrupt as the push, so no race; the logic thread only
   shrinks the queue, so no deadlock.  The counters below are always on (the record's PQ block, od_core_pq): one byte read per B0. */
#if defined(ON_BUILD) && !defined(MM_PQ_GUARD) && !defined(MM_PQ_GUARD_OFF)
#define MM_PQ_GUARD 1                                            /* ON in the online image (card / release): the Dolphin pair rig dropped 2-7 accepted frames and DIVERGED with a 200 ms logic block, the guard kept it IDENTICAL; -DMM_PQ_GUARD_OFF for an A/B build */
#endif
#define PQ_NUM (*(volatile u8 *)0x804C1F78u)                    /* HSD_PadLibData.qnum (5) */
#define PQ_COUNT (*(volatile u8 *)0x804C1F7Bu)                  /* HSD_PadLibData.qcount: raw entries the logic thread has not taken yet */
static u32 g_pq_ovf, g_pq_max;                                   /* this match (reset at match entry): accepted calls that met a full queue (= dropped frames), largest count seen at B0 */
#if defined(MM_FTSTATS) && defined(MS_RB_READOUT)
extern u32 g_sp_b0;                                              /* : time inside be_inputs, accumulated (lab readout) */
#endif
static int be_inputs(void *ctx, const sdshim_u8 *txb, sdshim_u8 *rxb)
{
    BC(0x60);
    g_b0_t = tbl();                                              /* S12: the next pad alarm is about one frame later */
    if (S12CFG & (S12F_EARLY | S12F_EARLY_DRY)) vi_note_b0();
    s6_rb.w[RB_B0PH]++;
    if (!rb_active) {                                            /* S6C: stride / cap change only between rollbacks */
        if (g_stride != g_stride_next) { g_stride = g_stride_next; TL[T_SWITCH]++; }
        if (S6CCFG & S6CF_ADAPT_CAP) C.cap = g_cap_next; else C.cap = g_cap_base;   /* S16: the gate may have set 0 */
        if (s16_gate()) { C.cap = 0; g_gate_run++; } else g_gate_run = 0;                     /* S16 */
        if (in_game() && (!sd_active || g_mem_drain)) C.cap = 0;   /* S19: no engine (store did not fit / released / draining): no prediction */
        if (g_b2_refused) C.cap = 0;                                  /* review fix 3: a refused restore ended the match; no prediction until the next match */
        {   /* S19c: lockstep across the Stadium transformation switch.  A rollback whose replay re-runs the switch frame (ps_done
               a second time: the stage models were built on the live heap, the restore puts back only the mirrored cells) hung the game in
               2 of 3 Dolphin runs of the Jigglypuff/G&W pair (frozen game loop, VI alive, the Wii freeze signature).  With cap 0 on the
               frames around the switch every frame up to the switch is confirmed and compared before the switch frame runs, so no later
               rollback can target a frame before it; cost: one stall of about RTT/2 per transformation. */
            s32 bf = (s32)be32p(txb + 1);
            if (g_ps_switch && bf >= (s32)g_ps_switch - 2 && bf <= (s32)g_ps_switch + 1) { C.cap = 0; g_ps_tel[6]++; }
            {   /* Slippi-PC parity: the switch is at request + PS_LAG (1, as Slippi-PC's synchronous stage-file load), so a replay can re-run the request and the
                   switch within a few frames: the lockstep window is armed from the stage's COUNTDOWN (state xDC 0, counter xD8 about to expire), not from the request, and
                   stays on while the state machine waits for / performs the switch (xDC 1, 2).  Every frame up to the request is then confirmed before it runs. */
                u32 gp = ps_gp();
                if (gp && !(S12CFG & (S12F_PS_VANILLA | S12F_PS_NOARM))) {
                    s32 xdc = (s32)(short)(W(gp + 0xDCu) >> 16), xd8 = (s32)W(gp + 0xD8u);
                    if ((xdc == 0 && xd8 <= (s32)PS_ARM) || xdc == 1 || xdc == 2) { C.cap = 0; g_ps_tel[6]++; }
                    else {   /* the RETURN to the normal stage: state 5 -> 6 destroys the transformed ground (Ground_801C4A08) and state 6 frees an archive
                                (grAnime_801C65B0) and draws the next countdown: neither may run twice. A replay of that frame asserted "archive":252
                                (real client, 200 ms / 20 ms / 5 % link, 2 of 2 runs, ROLLBACK_STATUS_2026-10-11): lockstep from PS_ARM frames before the end of state 5.
                                yakumono_param x14 = the length of state 5 (Ground_GetYakumonoParam, a plain getter). */
                        u32 yp = ((u32 (*)(void))0x801C49F8u)();
                        if (xdc == 6 || (xdc == 5 && ptr_like(yp) && xd8 >= (s32)W(yp + 0x14u) - (s32)PS_ARM)) { C.cap = 0; g_ps_tel[6]++; }
                    }
                }
            }
        }
    }
    TL[T_STRIDE] = g_stride;
    TL[T_CAP] = C.cap;
    u32 t0 = tbl(), dt;
    int lk = 0;
    (void)ctx;
    STRESS_POLL();
    BC(0x61);
    if (g_net && (lk = lock())) { sdnet_poll(&S); drain(); }
    else if (g_net) ST.b0_lockfail++;
    BC(0x62);
    {   u32 q = PQ_COUNT;
        if (q > g_pq_max) g_pq_max = q;
#ifdef MM_PQ_GUARD
        C.pq_hold = q >= 4u;                                     /* 4, not 5: UCF 0.84 reads the entry at qread - 1, which a 5th waiting entry overwrites */
#endif
        s3_core_b0(&C, txb, rxb);
        if (rxb[0] == 1u && q >= PQ_NUM) g_pq_ovf++;
    }
#if defined(ON_BUILD) && defined(HUB_OVL_RES)
    if (lk && gns_hot) gns_hook(txb);                            /* spectate upload: one word test per frame; the block runs at most one datagram per 12 frames */
    if (gns_whot) gns_w_b0(txb, rxb);                            /* watch session: the receive block of both ports from the server's feed (no lock needed: memory only) */
#endif
    BC(0x63);
    if (lk) unlock();
    BC(0x64);
    dt = tbl() - t0;
    if (dt > ST.b0_tick_max) ST.b0_tick_max = dt;
    ST.b0_tick_sum += dt;
#if defined(MM_FTSTATS) && defined(MS_RB_READOUT)
    g_sp_b0 += dt;
#endif
    return 0;
}
#if defined(ON_BUILD) && defined(HUB_OVL_RES)
/* WATCH session ("X: Last Game"; gns_res.c): the core in the loopback state, delay-only (seats 3: no prediction, no snapshot store), both ports remote */
int gns_engine_watch(int on)
{
    u32 i;
    if (on) {
        struct s7_rules r;
        if (C.state > S3_NET_DHCP) return -1;                    /* a session exists, is connecting or was lost: not now */
        r.flags = 0; r.alt_mode = S7R_ALT_CHOOSER; r.stocks = 0; r.time_s = 0; r.items = 0; r.n_stages = 0;      /* the rules a Game Night (room) match runs under (od_rules, mode room) */
        for (i = 0; i < 16u; i++) r.stages[i] = 0;
        C.seats = 3;
        s7_core_loopback(&C);
        ST.state = OD_NET_UP;                                    /* s7_core_loopback put 0xA0 there and s3_tick stops the whole network slice for states >= S3_NOBBA: the watcher's requests and the feed need it (first Dolphin watch runs: 5 sends, rc -1) */
        ms_rules_clear(&ms_rules);                               /* a watch session runs no match of ours or Slippi's */
        (void)s7_core_set_rules(&C, &r);
        C.lsel[3] = 1;                                           /* locked in: the watcher has no character pick */
        return 0;
    }
    C.seats = 0; C.state = S3_NET_WAIT; ST.state = OD_NET_UP; C.side = -1; ST.side = 0xFFu;
    for (i = 0; i < S7_SEL_LEN; i++) { C.lsel[i] = 0; C.rsel[i] = 0; }
    C.rsel_have = 0; C.rules_have = 0; C.items = 0; C.last_frame = 0; C.ever_up = 0; C.game_started = 0;
    return 0;
}
#endif
static int be_state(void *ctx, sdshim_u8 *msrb)
{
    (void)ctx;
#if defined(ON_BUILD) && defined(HUB_OVL_RES)
    if (gns_whot) {                                              /* watch session: the block of the REAL game when the server holds both selections, else the core's template + the demo words */
        const unsigned char *s0, *s1;
        int rc;
        if (gns_w_sels(&s0, &s1)) {
            u8 lk[S7_SEL_LEN], rk[S7_SEL_LEN];
            u32 i;
            int rs = C.state, rh = C.rsel_have;
            u32 ralt = C.rules.alt_mode;
            for (i = 0; i < S7_SEL_LEN; i++) { lk[i] = C.lsel[i]; rk[i] = C.rsel[i]; C.lsel[i] = s0[i]; C.rsel[i] = s1[i]; }
            if (s0[13] || s1[13]) C.rules.alt_mode = S7R_ALT_ON;   /* the real game was Frozen (altStageMode recorded by the uploader): the same block for this call */
            C.lsel[3] = lk[3]; C.rsel[3] = 1; C.rsel_have = 1; C.state = S3_SES_RUN;     /* for this one call: player A local on side 0, player B remote (the loopback state would put the cfg's fake peer there) */
            rc = s3_core_b3(&C, msrb);
            C.state = rs; C.rsel_have = rh; C.rules.alt_mode = ralt;
            for (i = 0; i < S7_SEL_LEN; i++) { C.lsel[i] = lk[i]; C.rsel[i] = rk[i]; }
        } else rc = s3_core_b3(&C, msrb);
        if (rc == 0 && gns_w_b3(msrb) < 0) { msrb[0] = 5; rc = 0; }
        return rc;
    }
#endif
    return s3_core_b3(&C, msrb);
}
static int be_sel(void *ctx, const sdshim_u8 *pstb)            /* S7: B5 SetMatchSelections */
{
    int lk = 0, rc;
    (void)ctx;
    if (g_net) lk = lock();
    rc = s7_core_b5(&C, pstb);
    if (lk) unlock();
    return rc;
}


#define HSD_MemAlloc ((void *(*)(u32))0x8037F1E4)
#define GLOBAL_FRAME (*(volatile u32 *)0x80479D60)      /* Slippi's loadGlobalFrame (Common.s:315) */
#define ODB_OFST (-18916)                                 /* OFST_R13_ODB_ADDR (Online.s) */
#define ODB_ROLLBACK_END_FRAME 0xE1
#define ODB_STABLE_FINALIZED_FRAME 0xAC9
#define ODB_ROLLBACK_IS_ACTIVE 0xDF

static u32 r13v(void) { u32 v; __asm__ volatile("mr %0, 13" : "=r"(v)); return v; }
static u32 odb(void) { u32 o = *(volatile u32 *)(r13v() + (u32)ODB_OFST); return o >= 0x80000000u && o < 0x81800000u ? o : 0; }
u32 s17_relax_odb(void);
static int s17_relax_hook_fn(const unsigned char *a, const unsigned char *p, const unsigned char *odb_, const unsigned char *rxb_, unsigned count, unsigned off, int frame, unsigned rd)
{
    return s17_relax_core(a, p, (unsigned)(unsigned long)odb_, (unsigned)(unsigned long)rxb_, count, off, frame, rd);
}
static u32 be32p(const volatile u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

/* MATCH SETUP CONFORMANCE: our modes build the match ONLY from negotiated facts, the two selections and constants.
   The negotiated facts are the MODE both tickets searched (the server pairs equal modes only) and, once the hub overlay ABI step carries it (MNC_SETUP_BLOCK, off in
   console builds today), the server's canonical setup block.  The mode table is the server's ms_core.match_setup for the same mode: Ranked (Grandmaster) / Unranked
   (Dojo) = Slippi's fixed modes (pause off, Pokemon Stadium transforms, the six-stage pool); Direct (Mano a mano) = Slippi Direct (pause allowed, the stage chooser's
   frozen toggle, any stage).  Giving the rules also turns the setup-digest check on (s7_core_set_rules). */
/* THE ONE PER-MATCH RECORD (ms_match_rules.h): written here (our route, MR-WRITE: the only match-time read of od_o.mode) and in br_engine_begin (Slippi route), cleared by end_session.
   Resident + overlays (gns_res.c) read it through the ms_rules_* accessors.  Starts zero = route NONE = nothing of ours runs. */
struct ms_match_rules ms_rules __attribute__((section(".data")));
typedef char ms_alt_numbers_match[(S7R_ALT_CHOOSER == 0 && S7R_ALT_OFF == 1 && S7R_ALT_ON == 2) ? 1 : -1];
typedef char ms_mode_numbers_match[(MR_MNC_RANKED == MNC_MODE_RANKED && MR_MNC_UNRANKED == MNC_MODE_UNRANKED && MR_MNC_DIRECT == MNC_MODE_DIRECT && MR_MNC_ROOM == MNC_MODE_ROOM) ? 1 : -1];

static void od_rules(void)
{
    struct s7_rules r;
    u32 i;
    int frozen = 0;
    r.flags = 0; r.alt_mode = S7R_ALT_CHOOSER; r.stocks = 0; r.time_s = 0; r.items = 0; r.n_stages = 0;
    for (i = 0; i < 16u; i++) r.stages[i] = 0;
#if defined(ON_BUILD) && defined(HUB_OVL_RES)
    frozen = od_o.m.gn_magic == MNC_GN_MAGIC && (od_o.m.gn_room[19] & 2u) != 0u;   /* the room's Stadium policy is Frozen (GNS_RF_FROZEN, set by the lobby hand-off): every Stadium game of the set runs frozen on both consoles (only a Game night record takes it) */
#endif
    (void)ms_rules_begin_ours(&ms_rules, od_o.mode, frozen);   /* MR-WRITE: an unknown mode writes nothing but the clear (touch nothing) */
#if defined(ON_BUILD) && defined(HUB_OVL_RES)
    if (ms_rules.mode == MR_MODE_GAMENIGHT && od_o.m.gn_magic == MNC_GN_MAGIC && od_o.m.gn_room[0] && (od_o.m.gn_room[19] & 0x40u)) r.flags |= S7R_ITEMS_ALL;   /* Wild And Crazy (GNS_RF_ITEMS 0x40, the lobby hand-off;): every item on, Very High, on both consoles */
#endif
#ifdef MS_LAB_ITEMS_ALL
    r.flags |= S7R_ITEMS_ALL;                                      /* LAB ONLY (option itemsall): the item soak runs every match of the pair rig with items */
#endif
    /* the setup block of our modes comes from the record only: Ranked / Unranked pause off + Stadium transforms, Game night pause off ( A6; the console had no field for it until
 our saw a Game Night match paused), Direct = pause allowed + the stage chooser's toggle */
    if (ms_rules_flag(&ms_rules, MR_F_SETUP_PAUSE_OFF)) r.flags |= S7R_PAUSE_OFF;
    r.alt_mode = ms_rules_alt(&ms_rules);                          /* 0 chooser / 1 transforms / 2 frozen = S7R_ALT_* (asserted above) */
#ifdef MNC_SETUP_BLOCK
    {
        const mnc *m = &od_o.m;
        if (m->su_have) {
            r.flags = (m->su_flags & 1u) ? S7R_PAUSE_OFF : 0u; r.alt_mode = m->su_alt; r.stocks = m->su_stocks; r.time_s = m->su_time; r.items = m->su_items;
            r.n_stages = m->su_n > 16u ? 16u : m->su_n;
            for (i = 0; i < r.n_stages; i++) r.stages[i] = m->su_stages[i];
        }
    }
#endif
#if defined(ON_BUILD) && defined(HUB_OVL_RES)
    if (od_o.m.gn_magic == MNC_GN_MAGIC) {
        u32 st = !od_o.m.gn_room[0] ? ((od_o.m.gn_room[19] & 0x40u) ? od_o.m.gn_room[3] : 0u) : (ms_rules.route == MR_ROUTE_OUR_SERVER && ms_rules.mode == MR_MODE_GAMENIGHT && !od_o.m.gn_room[13]) ? od_o.m.gn_room[14] : 0u;
        if (st) { r.n_stages = 1; r.stages[0] = (u16)st; }
    }   /* a Grandmaster SET's game: the stage the set's strikes / ban / pick decided is the match's only stage (the console has no setup block: MNC_SETUP_BLOCK is off) */
#endif
    (void)s7_core_set_rules(&C, &r);
}

static int in_game(void)
{
    u32 sc = SCENE_WORD;
    return (sc >> 24) == 8 && (sc & 0xFFu) == 2 && LOADING == 0;
}

#ifdef ON_BUILD
/* POST-GAME PEER GONE: before this, nothing watched the connection once the engine was RUN
   and a game was over: the opponent (or the PC bot, which serves ONE match and whose relay dies about 60 s later) left, B3 kept answering state 4 for ever
   and the console sat on the character / stage select with no way out (couch feedback, RC1 9efc6dc1).  Now:
   - SILENCE: in the online character / stage select a connected peer that has sent no datagram at all (ENet pings and acks count: a live peer sends some
     every 500 ms to 1 s) for OPP_SILENCE_MS is treated as gone (s7_core_peer_gone: state LOST).  Not applied in a match (B0's own stall / disconnect rules)
     nor in scene loads (neither console services ENet while it loads).  ENet's own timeout (5 to 30 s) is the backstop everywhere.
   - LOST while the connection was up: od_core_report turns it into OD_E_LEFT ("Opponent left"); the CSS shows it as the error box (B cancels, BA).
   - STAGE SELECT: it cannot show a box and has no connection check; its way out is B only while no stage was chosen. When the
     opponent is gone the scene is left by the glue the way B does it (clear 804D6CAF, gm_801A4B60 = 1 -> LOADING word 0x80479D64), Slippi's SSSSceneDecide then
     returns to the CSS (exit data byte 0), where the box is.
   - SESSION END: BA (cleanup, flag od_end_req), a lost session outside a match, and a new ticket naming another peer end the engine session
     (s7_core_end_session), so a new search works without a restart (before: "Restart the game to play someone else"). */
#define OPP_SILENCE_MS 10000u
#ifndef MS_NATPROBE
#define MS_NATPROBE 0                                            /* NAT PROBE datagram: off until the server's UDP wire port + 1 is open (room in the image) */
#endif
static u32 g_es_ip, g_es_port;                                   /* REJOIN: the address of the next opponent while a new ticket ends the old session */
static void end_session(u32 now)                                 /* the core back to "no session"; the glue's own state word is the "server step" value again (OD_NET_UP) */
{
    if (C.dst_ip) {                                              /* WHY-IT-FAILED record: what this peer session did, for the next ticket after a failed connect (in the RESIDENT od_o: the hub overlay's globals do not survive) */
        u32 *why = od_o.why;
        why[0] = C.enet.st.tx_dgrams; why[1] = C.enet.st.rx_dgrams; why[2] = C.enet.st.rx_drop_allow; why[3] = (C.enet.st.rx_drop_peer & 0xFFFFu) | ((u32)C.enet.st.rx_accept_pool << 16); why[4] = C.enet.st.connects_out;
        why[5] = C.punch_n; why[6] = C.enet.st.last_drop_ip; why[7] = (u32)C.enet.st.last_drop_port | (((now - C.punch_t0) >> 10) << 16); why[8] = 1u;
        if (C.ever_up && C.disconnected && C.last_frame > 0 && C.last_frame < 3600) {   /* BLACKOUT: a match that dropped in its first minute: word 5 bit 31 marks it */
            u32 lr = 0, k;
            for (k = 0; k < SDENET_MAX_PEERS; k++) if (C.enet.peer[k].ip == C.dst_ip && C.enet.peer[k].last_rx_ms - lr < 0x80000000u) lr = C.enet.peer[k].last_rx_ms;
            lr = lr ? (now - lr) / 100u : 0xFFFFu;
            why[5] = 0x80000000u | ((u32)C.last_frame << 16) | (lr > 0xFFFFu ? 0xFFFFu : lr);   /* frames played, 0.1 s since the peer's last datagram */
            why[6] = C.enet.st.rx_unseq;                         /* the peer's pad packets (unsequenced commands) received in the session */
            for (k = 0; k < 8; k++) od_o.why_mid[k] = od_o.mid[k];
            why[8] = od_o.mid_ok ? 2u : 1u;
        }
    }

#ifdef MS_RB_READOUT
    if (C.dst_ip) {                                              /* NT line (finding 63): what the peer connection of this session did, uploaded with the readout when the search ends (cross-network debugging) */
        extern void bh_nt_note(const u32 *v);
        u32 nt[7];
        nt[0] = C.dst_ip; nt[1] = C.dst_port; nt[2] = C.enet.st.tx_dgrams; nt[3] = C.enet.st.rx_dgrams; nt[4] = C.enet.st.rx_drop_allow; nt[5] = C.enet.st.rx_drop_peer; nt[6] = C.enet.st.connects_out;
        bh_nt_note(nt);
    }
#endif
    s7_core_end_session_to(&C, now, g_es_ip, g_es_port);          /* REJOIN: (ip, port) of the next opponent, 0 = none */
    ad_peer_reset(&g_adp);                                       /* AUTO DELAY FROM THE OPPONENT: the next session measures its own peer */
    ms_rules_clear(&ms_rules);                                    /* the match / session is over: nothing of ours (or Slippi's) is pending */
    ST.state = OD_NET_UP;
    ST.resv[3]++;
#ifdef BR_BUILD
    g_slp_sock = -1;                                             /* the match is over: drain must stop reading the bridge's socket.  Left set, the engine's drain ate the NEXT search's matchmaker
                                                                    replies (ip != g_slp_ip, "dropped") before the bridge saw them: second search of a boot = "Failed to connect to mm server" */
#endif
}
#define SSS_CHOSEN (*(volatile u8 *)0x804D6CAF)                  /* mnStageSel_804D6CAF: 2 = a stage was chosen */
/* telemetry in the free status words: 61 left_silence (silence verdicts), 62 left_sss (stage selects left by the glue), 63 left_ended (engine sessions ended: BA, lost, new peer) */
extern volatile u32 od_end_req;                                  /* on_backend.c: BA arrived */
static int online_menu_scene(void)
{
    u32 sc = SCENE_WORD;
    return (sc >> 24) == 8 && ((sc & 0xFFu) == 0 || (sc & 0xFFu) == 1) && LOADING == 0;
}
#define OPP_SILENCE_SLIPPI_MS 60000u   /* a Slippi-bridge session (g_slp_sock set by br_engine_begin): Slippi 3.7.0 has NO lobby silence timer (/9), ENet's own disconnect / timeout decides; our own 10 s rule stays for Multishine sessions (Mano, Dojo, Game Night).  60 s = a generous backstop only. */
#ifdef BR_BUILD
#define OPP_SILENCE_LIMIT() (g_slp_sock >= 0 ? OPP_SILENCE_SLIPPI_MS : OPP_SILENCE_MS)
#else
#define OPP_SILENCE_LIMIT() OPP_SILENCE_MS      /* a build without the bridge has no g_slp_sock (profiles s17-compat-on / -ond) */
#endif
static void peer_watch(u32 now)
{
    if (C.state == S3_SES_RUN && online_menu_scene() && s7_core_silence_ms(&C, now) > OPP_SILENCE_LIMIT()) { s7_core_peer_gone(&C); ST.resv[1]++; }
    if (C.state == S7_SES_LOST && (SCENE_WORD & 0xFFu) == 1u && (SCENE_WORD >> 24) == 8 && LOADING == 0) {   /* stage select with nobody left to play */
        SSS_CHOSEN = 0;
        LOADING = 1;
        ST.resv[2]++;
    }
    if (od_end_req && !in_game()) {                              /* BA: the player (or Slippi's cleanup) left the search / the connection.  A BA that arrives while the match scene still runs
 stays pending until the scene is left */
        if (C.state >= S7_SES_CONNECTING) end_session(now);
        od_end_req = 0;
    }
    if (C.state == S7_SES_LOST && online_menu_scene() == 0 && !in_game() && (SCENE_WORD >> 24) != 8) { end_session(now); }   /* back in the menus: the session is over */
    {   /* LEFT WHILE PAIRED: leaving Slippi's character screen while still connected sends no BA on the bridge route, the session stayed open in the menus and its
           drain kept the socket: our server went silent ("No server") until a restart.  Out of every online scene (no CSS, no stage select, no match) for 3 s = the session is over */
        static u32 out_since;
        if (C.state >= S7_SES_CONNECTING && C.state < S3_NOBBA && online_menu_scene() == 0 && !in_game() && (SCENE_WORD >> 24) != 8 && LOADING == 0) {
            if (!out_since) out_since = now | 1u;
            else if (now - out_since > 3000u) { end_session(now); out_since = 0; }      /* (counted by end_session in resv[3]) */
        } else out_since = 0;
    }
}
#endif

static u32 g_store;
static u32 g_pub_next, g_pub_match;
static u32 g_dbg_gp[16], g_dbg, g_dbg_done, g_dbg_tag[16], g_gph_f[64], g_gph[64], g_gp_checked, g_gp_dumps;
#define DBG_AUTO (s3_cfg.dbg_lo == 0xFFFFFFFFu)
static int rb_active;                    /* a load happened; the re-simulation is running */
static u32 g_pass_rb;                    /* S6C: rollback ticks inside the current rendered frame */
static s32 rb_g, rb_e;
static u32 rb_restore;                   /* S6D: B2 ticks of the running rollback */
static u32 g_in_logic, g_iter_seq;       /* S6D SFX log: inside an engine logic iteration; iteration counter */
static void sfx_put(u32 kind, u32 a, u32 b);
static void sfx_alloc(void);
#define SFX_N 12288u
static volatile u32 *g_sfx;
static u32 g_sfx_i, g_sfx_on, g_sfx_freeze, g_sfx_d;
static u32 rb_t0, rb_trace, rb_iters, g_cur_frame, g_it0;
static u8 g_pres[0x1000] __attribute__((aligned(32)));
static s32 g_b1r[16], g_b2r[8];
static u32 g_b1n, g_b2n;

static s32 load_g;
static u32 g_rest_k[8];                                         /* S12 (on S11): restore ticks EMA by rollback depth */
static u32 g_early_pending, g_rb_early;                          /* S12: next load is an early rollback; running one is */
static u32 g_early_est, g_early_t0, g_early_pass, g_render_skip_next, g_neg_early_done;
static u32 g_rs_end_t, g_rs_gap_have, g_b1_rb_acc;              /* S12 tick breakdown */
static u32 g_idle_acc, g_last_end_t;                             /* S12: idle before an early rollback; last logic end */
/* S10 state */
static u32 g_fresh;          /* nothing ran since the newest capture (post-frame capture or KEEP/RSET restore) */
static u32 g_loadcap;        /* a load happened and nothing was captured since (S6c rule: capture the first predicted frame) */
static u32 g_rbframe;        /* SKIPCAP: a load happened and the B1 that decides the rollback frame's own capture has not run yet (cleared by that B1) */
struct sd_cp sd_cp = { .limit = SD_CP_LIMIT };   /* SKIPCAP counters (sd_cappol.h); symbol address in the build map */
static s32 g_b1_seen = -1;   /* frame of the last B1 call (the frame about to run was predicted) */
static u32 g_pass_iters;     /* engine iterations in this rendered frame */
static u32 g_after_restore;  /* the next iteration starts right after a restore */
static u32 g_base_at_load;   /* sd_st.base_captures at the last load (base copy after a load: counted once) */
static u32 g_seed_end, g_seed_iter0, g_proc_steps, g_seed_have;   /* RNG seed at logic end / iteration start */
static u32 g_rb_id;          /* rollback counter (EQ profile timeline id) */
static int s10_keep(void) { return s10_cfg.rmode == 1u || s10_cfg.rmode == 2u; }
static int s10_post(void) { return (S10CFG & (S10F_POSTCAP | S10F_WPROF | S10F_FORCECAP)) != 0; }
#define RNG_PTR (*(volatile u32 *)0x804D5F94)           /* HSD_RandSeedPtr, normally 0x804D5F90 */
static u32 rng_now(void) { u32 p = RNG_PTR; return ptr_like(p) ? W(p) : 0; }
static u32 g_pe_seq;                                            /* S17 R1: engine iteration of the last pass-end attempt */
static u32 g_cs_lines, g_cs_pres, g_cs_static, g_cs_cell;       /* S17 R1: census of the current load */
static u32 g_cs_ln[16];
static u32 g_v[16], g_nv, g_v_loads, g_v_since, g_v_latched;                 /* S18 production chain-only: learned residual set, fresh loads seen, loads since the last new line, latch */                                         /* S17 R1 lab: the first residual lines of it */
static u32 g_ps_a[3], g_ps_l[3], g_np;                              /* S17 R1: preserve blocks of the current load (ODB, RXB, SSCB) */
static int s17_census(void);
static void s10_capture_done(void);
static void pass_mark(u32 t);
static int want_capture(s32 frame);
static void gap_fixup(s32 f, s32 g, const sdshim_u8 *ssrb);
static void sd_stats_copy(void)
{
    RB[RB_EVTMAX] = sd_st.evt_max; RB[RB_PREMAX] = sd_st.pre_max; RB[RB_NETMAX] = sd_st.net_max; RB[RB_REBUILDS] = sd_st.evt_rebuilds;
    RB[RB_BYTES] = sd_st.bytes; RB[RB_CELLS] = sd_st.cells; RB[RB_MAXLINES] = sd_st.max_lines;
    RB[RB_LOGDROPS] = sd_st.log_full_drops; RB[RB_SDERR] = sd_st.errors + sd_st.evt_overflow + sd_st.cell_overflow + sd_st.arena_full;
    RB[RB_RES_MAX] = sd_st.t_res_max; RB[RB_DSKIP] = sd_st.dirty_skip_bytes; RB[RB_LOGMAX] = sd_st.log_used_max;
    RB[RB_HDRPRE] = sd_st.hdr_pre; RB[RB_XMAX] = sd_st.extras_max; RB[RB_FREECELLS] = sd_ms.free_cells;
    RB[RB_ADDS] = sd_st.cell_adds; RB[RB_REMOVES] = sd_st.cell_removes; RB[RB_BARMISS] = sd_barrier_miss;
    {   /* S17: the engine's own kernel costs */
        u32 d;
        QL[Q_FLAGS] = s11_cfg.s17_flags; QL[Q_FLAGS_EFF] = sd_x17.flags_eff;
        QL[Q_G_PASSES] = sd_x17.g_passes; QL[Q_G_BUSY] = sd_x17.g_busy; QL[Q_G_FAIL] = sd_x17.g_fail;
        QL[Q_G_TICKS_LAST] = sd_x17.g_ticks_last; QL[Q_G_TICKS_SUM] = sd_x17.g_ticks_sum; QL[Q_G_TICKS_MAX] = sd_x17.g_ticks_max;
        QL[Q_G_LINES] = sd_x17.g_lines_sum; QL[Q_G_CMDS] = sd_x17.g_cmds_sum; QL[Q_G_BATCHES] = sd_x17.g_batches_sum; QL[Q_G_CHG] = sd_x17.g_chg_sum;
        QL[Q_H_PASSES] = sd_x17.h_passes; QL[Q_H_BUSY] = sd_x17.h_busy; QL[Q_H_FAIL] = sd_x17.h_fail;
        QL[Q_H_TICKS_LAST] = sd_x17.h_ticks_last; QL[Q_H_TICKS_SUM] = sd_x17.h_ticks_sum; QL[Q_H_TICKS_MAX] = sd_x17.h_ticks_max;
        QL[Q_H_PLAN_LAST] = sd_x17.h_plan_last; QL[Q_H_PLAN_SUM] = sd_x17.h_plan_sum; QL[Q_H_BYTES] = sd_x17.h_bytes_last;
        QL[Q_H_SEGS] = sd_x17.h_segs_last; QL[Q_H_CMDS] = sd_x17.h_cmds_sum;
        QL[Q_U_WHOLE] = sd_x17.u_whole; QL[Q_U_PART] = sd_x17.u_part; QL[Q_RF_HIT] = sd_x17.rf_hit; QL[Q_RF_MISS] = sd_x17.rf_miss;
        QL[Q_LOGZ] = sd_x17.logz_lines; QL[Q_DMA_ERR] = sd_x17.dma_err_flags; QL[Q_MFLUSH] = sd_x17.mflush_lines;
        { u32 i; for (i = 0; i < RXT_N; i++) QL[Q_RX + i] = s17_rx_tel[i]; }   /* S17 RELAX */
        QL[Q_DD_USED] = sd_dd.used; QL[Q_DD_FB] = sd_dd.fb_cap; QL[Q_DD_ENTS] = sd_dd.ents_all; QL[Q_DD_APPL] = sd_dd.ents_applied; QL[Q_DD_LINES] = sd_dd.lines;
        QL[Q_TCAP_SUM] = sd_x11.t_cap_sum; QL[Q_TCAP_N] = sd_x11.t_cap_n; QL[Q_TCAP_MAX] = sd_st.t_cap_max; QL[Q_TRES_MAX] = sd_st.t_res_max;
        QL[Q_CAPTURES] = sd_st.captures; QL[Q_BASE_CAPS] = sd_st.base_captures; QL[Q_RESTORES] = sd_st.restores;
        QL[Q_LINES_SUM] = sd_x11.lines_sum; QL[Q_UNDO_KB] = sd_x.undo_bytes_sum >> 10; QL[Q_FP_EFF] = sd_fp;
        for (d = 0; d < 8; d++) {
            QL[Q_REST_N + d] = sd_x17.rest_n[d]; QL[Q_REST_T + d] = sd_x17.rest_t[d];
            QL[Q_UNDO_T + d] = sd_x17.undo_t[d]; QL[Q_STEP1_T + d] = sd_x17.step1_t[d];
        }
    }
}

static void rb_finish(void)
{
    u32 dt = tbl() - rb_t0 - rb_trace;
    RB[RB_TICK_SUM] += dt;
    if (dt > RB[RB_TICK_MAX]) RB[RB_TICK_MAX] = dt;
    RB[RB_ITERS_RB] += rb_iters;
    rb_active = 0;
    g_rs_gap_have = 0;                                           /* S12 */
    if (rb_restore) ZL[Z_RS_REST_SUM] += rb_restore, ZL[Z_RS_REST_N]++;
    if (g_rb_early) {                                            /* S12: early rollback finished */
        u32 now = tbl();
        ZL[Z_E_DONE]++;
        ZL[Z_E_TICKS] += dt;
        ZL[Z_E_EST] += g_early_est;
        if (dt > g_early_est) { ZL[Z_E_EST_UNDER]++; ZL[Z_E_EST_ERR_SUM] += dt - g_early_est; }
        if (g_b0_t && now - g_b0_t > FRAME_TICKS) ZL[Z_E_LATE]++;   /* ran past the expected next pad alarm */
        {   u32 o = odb();                                       /* a VI pad alarm came during it: Slippi deferred the pad
                                                                    renew (PreventPadAlarmDuringRollback.asm) */
            if (o && *(volatile u8 *)(o + S12_ODB_SHOULD_FORCE_PAD_RENEW)) ZL[Z_E_VI_DURING]++;
        }
        if (S12CFG & S12F_EARLY_NORENDER) g_render_skip_next = 1;
        g_early_pass = 1;
        g_rb_early = 0;
    } else ZL[Z_RB_NORMAL_TICKS] += dt;
    { extern void s10_rb_end(void); s10_rb_end(); }              /* S10: EQ profile of this rollback */
    g_pass_rb += dt;                                             /* S6C: rollback work of this rendered frame */
    {   /* S6D: measured cost of one re-simulated frame (incl. its captures and the cost emulation) and a histogram of
           whole-rollback cost in quarter frames: <.25 .5 .75 1 1.25 1.5 2 2+ */
        s32 kt = rb_e - rb_g;
        u32 q = dt / (FRAME_TICKS / 4u);
        if (kt > 0 && dt > rb_restore) { DL[D_PER] = ema(DL[D_PER], (dt - rb_restore) / (u32)kt); DL[D_PER_N]++; }
        DL[D_RBH + (q < 6 ? q : q < 8 ? 6 : 7)]++;
        DL[D_RB_N]++;
    }
}

/* debug (cfg flags bit7): OS heap consistency check of the HSD heap after every B1/B2: free list address ordered,
   prev links consistent, no cycle; allocated list prev links consistent; free + allocated sizes = arena.  The first
   failure is reported (S6!K tag frame what cell). */
static u32 g_hk_reports;
static void hk_report(u32 tag, s32 frame, u32 what, u32 cell, u32 extra);
static void heap_check(u32 tag, s32 frame)
{
    u32 d = sd_hsd_desc(), c, prev = 0, n = 0, sum = 0;
    if (!d || g_hk_reports >= 3) return;
    for (c = W(d + 4); c; prev = c, c = W(c + 4)) {
        if (!ptr_like(c) || (c & 31)) { hk_report(tag, frame, 1, c, prev); return; }
        if (W(c) != prev) { hk_report(tag, frame, 2, c, W(c)); return; }
        if (prev && c <= prev) { hk_report(tag, frame, 3, c, prev); return; }
        if (prev && prev + W(prev + 8) > c) { hk_report(tag, frame, 4, c, prev); return; }
        sum += W(c + 8);
        if (++n > 5000) { hk_report(tag, frame, 5, c, n); return; }
    }
    prev = 0; n = 0;
    for (c = W(d + 8); c; prev = c, c = W(c + 4)) {
        if (!ptr_like(c) || (c & 31)) { hk_report(tag, frame, 6, c, prev); return; }
        if (W(c) != prev) { hk_report(tag, frame, 7, c, W(c)); return; }
        sum += W(c + 8);
        if (++n > 30000) { hk_report(tag, frame, 8, c, n); return; }
    }
    RB[RB_PH_FRAME] = sum;
}

/* S12 (on S11): HSD heap headroom per rendered frame (free-list walk, bounded): smallest free total, smallest largest
   free cell, last free total and the global frame of the minimum (Z_HEAP_*).  The YS / DL runs m4, m4b, m5, m14 ended
   in HSD allocation asserts. */
static void s12_heap_watch(void)
{
    u32 d = sd_hsd_desc(), c, n = 0, sum = 0, big = 0;
    if (!d) return;
    for (c = W(d + 4); ptr_like(c) && n < 2000u; c = W(c + 4), n++) {
        u32 sz = W(c + 8);
        sum += sz;
        if (sz > big) big = sz;
    }
    ZL[Z_HEAP_FREE_LAST] = sum;
    if (!ZL[Z_HEAP_FREE_MIN] || sum < ZL[Z_HEAP_FREE_MIN]) { ZL[Z_HEAP_FREE_MIN] = sum; ZL[Z_HEAP_MIN_FRAME] = GLOBAL_FRAME; }
    if (!ZL[Z_HEAP_BIG_MIN] || big < ZL[Z_HEAP_BIG_MIN]) ZL[Z_HEAP_BIG_MIN] = big;
}

/* ---- S6C: sparse capture schedule, gap loads, per-frame timing, adaptive stride and cap ---- */
#define ODB_SAVESTATE_FRAME 0x38D                                 /* Online.s:261 (s32, unaligned) */
#define ODB_STABLE_SAVESTATE_FRAME 0xAC5                          /* Online.s:272 */
#define ODB_RXB_ADDR 0xDB                                         /* Online.s:250 */
#define RXB_OPNT_FRAME_NUMS 26                                    /* Online.s:298: latest remote frame of remote 0 */
static void put_be32(u32 a, u32 v)
{
    volatile u8 *p = (volatile u8 *)a;
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}
/* capture at this B1?  Always when there is no capture since the last load (the first predicted frame after a load is
   the earliest frame a later rollback can target), else every `g_stride`-th frame after the newest capture. */
static int want_capture(s32 frame)
{
    s32 n;
    if (!sd_newest_le(0x7FFFFFFF, &n)) return 1;
    if (frame <= n) return s10_keep() ? !(frame == n && g_fresh) : 1;   /* S10: KEEP: the kept capture is still current */
    if (g_rbframe && !(rb_active && frame < rb_e) && sd_cp_skip_own(S17CFG, frame, n)) return 0;   /* SKIPCAP (opt-in): the rollback frame's own capture is deferred while the newest capture is < sd_cp.limit frames behind; the debt stays (g_loadcap) */
    if (g_loadcap) return 1;                                            /* S10: first capture after a load (S6c rule); review fix 1: tested BEFORE RBCAP_LAST, so the first predicted frame of a replay is always captured */
    if ((S10CFG & S10F_RBCAP_LAST) && s10_keep() && rb_active && frame < rb_e && frame - n < 2) return 0;   /* S10 RBCAP_LAST inside the replay: skip only while the newest capture is 1 frame behind (gap <= 1 always: the RXB holds 7 entries, a target at fin + 1 restored 2+ frames back needs more); was `frame - n >= 4` with the guard, unbounded without it */
    return frame - n >= (s32)g_stride;
}
/* B0 hook: the frame a load of target x would restore. x < frame: x already ran (newest capture
   <= x); x == frame: B1(frame) has not run yet, the same rule decides it (the stride only changes at a B0). */
static struct pc_delay g_pd;
static u32 g_pd_n;
/* GAME_RECORD facts: RTT samples of the current match in 16 ms buckets (the last bucket = 240 ms and more); a few adds per status refresh */
static u32 g_gs_rtt_n, g_gs_rtt_sum, g_gs_rtt_h[16], g_gs_delay;
static void pace_rtt_cb(void *ctx, u32 ms)
{
    (void)ctx;
    if (ms <= 4000u) { g_gs_rtt_n++; g_gs_rtt_sum += ms; g_gs_rtt_h[ms >> 4 > 15u ? 15u : ms >> 4]++; }
    if (!(S17CFG & S17F_PACE_DELAY) || ms > 4000u) return;
    if (++g_pd_n % 30u == 1u) pc_delay_sample(&g_pd, ms * 1000u);          /* about every 30th status refresh */
}
/* ---- AUTO INPUT DELAY (s7_cfg.s7_flags S7F_DELAY_AUTO, default OFF in every image) ----
   rtt_raw_hook feeds one RAW ENet round trip per acknowledgement (s7_core.c s3_core_rx_from) into the estimator; the delay is decided ONCE per match, at B3 (the
   core's delay_hook, the safe moment), and stays fixed for the whole match (the ASM reads it once at match setup).  The cfg delay is the fallback (too few
   samples: reason FEW, "no ping yet").  The delay is local: nothing about it goes on the wire, the two sides may differ (the tags carry F + delay).
   Telemetry: s17_tel words Q_DA_* (they overlap the census table, a lab-only block that is not run together with this) and the text a menu/slice can draw
   ("Delay 3 auto, ping 62 ms") in da_disp.text (menu-font characters only), seq counts the decisions. */
#ifdef MS_DA_ENGINE
static struct da_state g_da;
static struct da_result g_dar;
static u32 g_da_ip, g_da_port, g_da_total;
struct da_disp { u32 magic, seq, delay, ping_ms; char text[40]; };
struct da_disp da_disp __attribute__((used)) = { 0x44415950u, 0u, 0u, 0u, { 0 } };    /* 'DAYP'; symbol address in the build map */
static void da_rtt_cb(void *ctx, u32 us)
{
    (void)ctx;
    if (C.dst_ip != g_da_ip || C.dst_port != g_da_port) { da_reset(&g_da); g_da_ip = C.dst_ip; g_da_port = C.dst_port; }   /* another opponent: its own samples only */
    da_sample(&g_da, us);
    g_da_total++;
}
static u32 da_decide_cb(u32 cur)
{
    struct da_result r;
    u32 d, i;
    da_decide(&g_da, DA_FLOOR, da_opt_cap(s7_cfg.resv[0]), (da_s32)cur, &g_dar);
    r = g_dar;
    d = (u32)g_dar.delay;
    if ((s7_cfg.s7_flags & S7F_DA_SKEW) && C.side == 1) { d += 3u; if (d > DA_MAX) d = DA_MAX; r.delay = (da_s32)d; }   /* TEST: asymmetric on purpose */
    da_text(da_disp.text, (da_s32)sizeof da_disp.text, &r);
    da_disp.delay = d; da_disp.ping_ms = r.ping_ms; da_disp.seq++;
    QL[Q_DA_DELAY] = d; QL[Q_DA_PING] = r.ping_ms; QL[Q_DA_REASON] = (u32)r.reason; QL[Q_DA_NVALID] = r.n_valid; QL[Q_DA_CONF] = (u32)r.confident;
    QL[Q_DA_SPREAD] = r.spread_us; QL[Q_DA_RAW] = (u32)r.raw; QL[Q_DA_SAMPLES] = g_da_total; QL[Q_DA_DECISIONS] = da_disp.seq;
    for (i = 0; i < 4u; i++) QL[Q_DA_RING + i] = g_da.ring[i];                 /* the first 4 ring slots (us) for the log */
    return d;
}
#endif
/* ---- AUTO DELAY FROM THE OPPONENT (s7_cfg.s7_flags S7F_AD_PEER, default OFF in every image) ----
   While the setting is Auto, the delay is read from the round trips to the ACTUAL opponent when a good measurement exists (auto_delay_tab.h ad_peer_rtt: at least
   5 samples, retransmissions <= a quarter), else from the Auto word (the internet-leg estimate), else the default.  The samples are the raw ENet round trips of the
   acknowledgements the peer sends anyway (connect, selections, setup digest, ENet's 500 ms pings): nothing new goes on the wire, a Slippi peer (bridge) sees a stock
   ENet peer.  Collected only OUTSIDE the match scene (adp_arm, main thread): inside a match the receive path is exactly the flag-off path (rollback hot path
   untouched).  The decision is the delay hook's (B3: called on every match-state read of the character select, the ASM latches msrb[9] once in InitOnlinePlay).
   Which source decided: ms_ad_pick (symbol in the build map, never on screen); the game record's byte 45 keeps the frames only (no free bit). */
static u32 g_adp_ip, g_adp_port;
static struct sdl g_sdl;
static u32 g_car_ip, g_car_port, g_car_d, g_car_calm, g_sd_nr, g_sd_lr, g_sd_a0, g_gs_d0;   /* CARRY-OVER: the opponent, the delay in force at the end of the last Auto game, its calm flag; this game's raise count / last raise frame / Auto flag / start delay */
#if defined(SD_CARD_HOLE)
static int wii_col(void)                                         /* the table column of this console: 1 = Wii family on Wi-Fi or on a link kind that is not known, 0 = GameCube and a Wii whose vBBA positively published a WIRED interface (kind 2): its own delay does not change its CPU load, +1 only protects the peer from a jittery uplink */
{
    unsigned char pm[6]; unsigned int pk = 0; int ok;
    if (sd_plat_ms_ticks() < 55000u) return 0;
    ok = macpub(pm, &pk);
    return !(ok && pk == 2u);
}
#else
static int wii_col(void) { return sd_plat_ms_ticks() >= 55000u; }
#endif
volatile u32 sd_why[8] __attribute__((used));                    /* why the last raise decision went the way it did: [0] frame [1] delay [2] stalled calls [3] RTT p95 ms [4] ceiling [5] raises this match [6] refusals (symbol in the build map; the readout builds print it) */
static u32 g_sd_auto, g_ovr_ip, g_ovr_port, g_ig_prev, g_sd_end;   /* g_sd_end: the delay in force after a raise (0 = none this match); the game record's delay byte; cleared at the next match entry (a later B3 rewrites g_gs_delay with the START value) */
volatile u32 ms_ad_pick[4] __attribute__((used));                /* the last resolve: [0] source << 24 | samples (16 bits) << 8 | frames; [1] peer RTT ms (0 = none); [2] the Auto word; [3] losses (retransmissions + junk) */
static void adp_rtt_cb(void *ctx, u32 us)
{
    (void)ctx;
    if (C.dst_ip != g_adp_ip || C.dst_port != g_adp_port) { ad_peer_reset(&g_adp); g_adp_ip = C.dst_ip; g_adp_port = C.dst_port; }   /* another opponent: its own samples only */
    ad_peer_add(&g_adp, us / 1000u, C.enet.st.tx_retx);
}
static int in_game(void);
static void adp_arm(void)                                        /* from slice (main thread), before the drain: the sample hook only while no match scene runs */
{
    { u32 ig = (u32)in_game(); if (ig && !g_ig_prev && (s3_cfg.delay & AD_SW) && (s3_cfg.delay & 0xFFu)) { g_ovr_ip = C.dst_ip; g_ovr_port = C.dst_port; } g_ig_prev = ig; }   /* a chip value (switch On) belongs to the opponent of this match */
    if ((s7_cfg.s7_flags & (S7F_AD_PEER | S7F_DELAY_AUTO)) != S7F_AD_PEER) return;   /* off, or the old engine Auto owns the hook */
    C.rtt_raw_hook = (in_game() || LOADING) ? 0 : adp_rtt_cb;     /* off from the match scene's load on: the B0 / interrupt drains of the match never see it */
}
/* ---- STALL-AWARE AUTO DELAY ----
   While the delay is Auto (g_sd_auto, set at B3), s7_core.c asks raise_hook at every advanced frame of a 2-seat match; auto_delay_tab.h sdl_tick counts the stalled B0 calls
   of 300-frame windows and says "raise" at 8 or more.  The raise is the ASM's own ring surgery at the one safe moment (inside the B0 exchange, before the ASM reads its delay
   ring): ODB_DELAY_BUFFER holds `delay` pads of PAD_REPORT_SIZE (12) bytes, ODB_DELAY_BUFFER_INDEX is the slot read now and overwritten after; one slot is inserted at the WRITE end
   (the slots from the index up move up by one, the new slot repeats the newest pad), the index follows its slot and ODB_DELAY_FRAMES grows by one.  The frames the ring already holds
   play unchanged (their tags are on the wire), so nothing diverges.  The core fills the one tag hole with the same repeated pad.  Offsets: Online.s (0x21 / 0x22 / 0x23). */
static u32 stall_raise_cb(void *ctx, u32 frame, u32 delay)
{
    u32 o;
    (void)ctx;
    if (!g_sd_auto || !sdl_tick(&g_sdl, frame, ST.stall_calls, delay)) return 0;
    {   /* the link must support it (sdl_ceiling): p95 of the match's RTT samples; why_log = {frame, delay, stalled calls so far, p95 ms, ceiling, raises done, refusals} for the diag / readouts */
        u32 acc = 0, k, p95 = 0, cl;
        for (k = 0; k < 16u && g_gs_rtt_n; k++) { acc += g_gs_rtt_h[k]; if (acc * 100u >= g_gs_rtt_n * 95u) { p95 = (k + 1u) * 16u; break; } }
        cl = sdl_ceiling(g_gs_d0, p95, wii_col());
        sd_why[0] = frame; sd_why[1] = delay; sd_why[2] = ST.stall_calls; sd_why[3] = p95; sd_why[4] = cl;
        if (delay >= cl) { sd_why[6]++; return 0; }
    }
    o = odb();
    if (!o || !sdl_ring_raise((volatile u8 *)o, delay)) return 0;
    g_sd_nr++; g_sd_lr = frame; sd_why[5] = g_sd_nr;
    g_gs_delay = g_sd_end = delay + 1u;                          /* the game record's delay byte = the delay at the END of the match */
    return 1u;
}
static u32 pace_delay_cb0(void *ctx, u32 cur);
static u32 pace_delay_cb(void *ctx, u32 cur)                     /* latched for the game record */
{
    static u32 pm_last;                                          /* the peer estimate of the last call outside the match scene (a B3 inside the match keeps it: no sort there) */
    int wii = wii_col();
    u32 pm = 0, src, d, sw = cur & AD_SW;
    cur &= 0xFFu;                                                /* the word's bit 8 = the Settings "Auto delay" switch is ON; bits 0-7 the frames, 0 = Auto */
    if (sw && cur && g_ovr_ip && (C.dst_ip != g_ovr_ip || C.dst_port != g_ovr_port)) { s3_cfg.delay = AD_SW; cur = 0u; }   /* the CSS chip's value was for ONE opponent: a new one goes back to Auto */
    if (!(sw && cur)) g_ovr_ip = 0u;
    g_sd_auto = (cur == AD_AUTO) && !(s7_cfg.s7_flags & S7F_DELAY_AUTO);   /* stalls may raise the delay only while it is Auto (a set value, the chip's value and the old engine Auto stay fixed) */
    if (!in_game()) sdl_reset(&g_sdl);                          /* a B3 inside the match must not restart the stall window */
    if ((s7_cfg.s7_flags & S7F_AD_PEER) && cur == AD_AUTO) {
        if (!in_game()) pm_last = (C.dst_ip == g_adp_ip && C.dst_port == g_adp_port) ? ad_peer_rtt(&g_adp, C.enet.st.tx_retx) : 0u;
        pm = pm_last;
    }
    d = pace_delay_cb0(ctx, ad_resolve_src(cur, ms_ad_word, pm, wii, &src));   /* ONLINE DELAY AUTO: the setting word 0 = Auto -> the peer's / the Auto word's frames (every mode and both backends pass here, B3) */
    if (g_sd_auto && g_car_d && C.dst_ip == g_car_ip && C.dst_port == g_car_port) d = sdc_start(d, g_car_d, g_car_calm);   /* the next game of a set: the last game's delay, one less when it was calm (never below the Auto start); a new opponent starts from the Auto value */
    g_gs_delay = d;
#ifdef MS_ENGINE_REPORT                                          /* lab telemetry (which source decided); a release does not keep it */
    ms_ad_pick[0] = (src << 24) | ((u32)g_adp.n << 8) | (d & 0xFFu); ms_ad_pick[1] = pm; ms_ad_pick[2] = ms_ad_word;
    ms_ad_pick[3] = g_adp.n ? (C.enet.st.tx_retx - g_adp.retx0) + g_adp.junk : 0u;
#else
    (void)src;
#endif
    return d;
}
static u32 pace_delay_cb0(void *ctx, u32 cur)
{
    s32 d;
    (void)ctx;
#ifdef MS_DA_ENGINE
    if (s7_cfg.s7_flags & S7F_DELAY_AUTO) return da_decide_cb(cur);          /* AUTO DELAY wins over the S18 pacing delay */
#endif
    if (!(S17CFG & S17F_PACE_DELAY)) return cur;
    d = pc_delay_commit(&g_pd, 1);                                          /* B3 = a safe moment: between games */
    QL[Q_PACE_DELAY] = (u32)d; QL[Q_PACE_N] = g_pd.n;
    return (g_pd.n >= PC_MIN_N && d >= 1) ? (u32)d : cur;                  /* no estimate yet: the cfg delay */
}
/* S18 gap guard, review fix 2: cap at g + 6, g = the newest
   COMPLETE capture <= the ASM's own oldest rollback target (ODB_SAVESTATE_FRAME while ODB_SAVESTATE_IS_PREDICTING); no cap while the ASM is not
   predicting.  The old rule (g <= fin + 1) deadlocked in lockstep (S16 gate) and hid the inputs a replay needed.
   Model: . */
#define ODB_SAVESTATE_IS_PREDICTING 0x38C                         /* Online.s:260 (u8) */
static s32 latest_cap_cb(void *ctx, s3_s32 fin)
{
    s32 g, ss;
    u32 o;
    (void)ctx; (void)fin;
    if ((S17CFG & S17F_NOGUARD) || !sd_active) return 0;
    o = odb();
    if (!o || !*(volatile u8 *)(o + ODB_SAVESTATE_IS_PREDICTING)) return 0;   /* not predicting: no rollback target exists yet */
    ss = (s32)be32p((volatile u8 *)(o + ODB_SAVESTATE_FRAME));
    if (!sd_newest_ok_le(ss, &g)) return 0;
    return g + 6;
}
static s3_s32 load_frame_cb(void *ctx, s3_s32 x, s3_s32 frame)
{
    s32 g;
    (void)ctx;
    if (!sd_active) return x;
    if (x >= frame && (sd_have(frame) || want_capture(frame))) return x;   /* S10: post-frame capture already taken */
    return sd_newest_ok_le(x, &g) ? g : x;                       /* S16: the newest COMPLETE capture <= x */
}
static void gap_fixup(s32 f, s32 g, const sdshim_u8 *ssrb)
{
    u32 o = odb(), rxb, gap = (u32)(f - g);
    (void)ssrb;
    TL[T_GAPLOADS]++;
    TL[T_GAPFR] += gap;
    TL[T_GAPH + (gap > 4 ? 3 : gap - 1)]++;                         /* gap 1, 2, 3, 4+ */
    if ((s32)GLOBAL_FRAME != g) TL[T_GFMIS]++;                       /* the restore must bring the global frame to g */
    if (!o) return;
    rxb = be32p((volatile u8 *)(o + ODB_RXB_ADDR));
    if (ptr_like(rxb) && (s32)be32p((volatile u8 *)(rxb + RXB_OPNT_FRAME_NUMS)) - g > 6) {
        TL[T_RXBVIOL]++;
        hk_report(0x56494F4Cu, f, (u32)be32p((volatile u8 *)(rxb + RXB_OPNT_FRAME_NUMS)), (u32)g, GLOBAL_FRAME);   /* S18 diagnostic: "S6! K 56494F4C frame latest g global_frame ..." */
    }
    if (S6CCFG & S6CF_NO_ODB_FIX) return;                 /* negative test: the ASM re-simulates from f on g's state */
    put_be32(o + ODB_SAVESTATE_FRAME, (u32)g);
    put_be32(o + ODB_STABLE_SAVESTATE_FRAME, (u32)g);
}

/* rendered-frame timing: s3_tick runs at the top of the scene loop (gm_801A4D34); the loop then waits for the pad
   (0x801A4DA8) and runs the engine iterations (StartEngineLoop: B2/B1 first, then s6n_iter) and the render.
   idle = first engine work - end of s3_tick; work = period - idle; rollback work = rb_finish sums. */
static u32 g_tick_start, g_tick_end, g_pass_have, g_pass_first, g_busy, g_calm, g_bw[8], g_bwi;
static void pass_mark(u32 t) { if (!g_pass_have) { g_pass_have = 1; g_pass_first = t; } }
static u32 umax8(const u32 *a) { u32 m = 0, i; for (i = 0; i < 8; i++) if (a[i] > m) m = a[i]; return m; }
static void pass_end(u32 now)
{
    u32 period, idle, work, base, b, w;
    if (!g_tick_start || !g_pass_have || !in_game()) return;      /* stalled frame (no engine iteration) or menus */
    period = now - g_tick_start;
    idle = g_pass_first - g_tick_end + g_idle_acc;              /* S12: + the idle before an early rollback */
    if (idle > period) idle = period;
    work = period - idle;
    base = work > g_pass_rb ? work - g_pass_rb : 0;
    TL[T_PASSES]++;
    TL[T_WORK] = ema(TL[T_WORK], work);
    TL[T_BASEWORK] = ema(TL[T_BASEWORK], base);
    /* late = the work did not fit in one frame (missed the next vblank).  The period measured at the loop top is not
       used for that: it moves by the difference of two frames' work (capture / no capture at stride 2 alone is ~19 %). */
    if (work > FRAME_TICKS) TL[T_LATE]++;
    if (work > TL[T_WORKMAX]) TL[T_WORKMAX] = work;
    if (period > TL[T_PERIODMAX]) TL[T_PERIODMAX] = period;
    b = work * 10u / FRAME_TICKS;
    TL[T_WH + (b > 11 ? 11 : b)]++;
    TL[T_SPASS + g_stride - 1]++;
    g_bw[g_bwi++ & 7] = base;
    if (S6CCFG & S6CF_ADAPT_STRIDE) {
        w = hw(TL[T_WORK]);                                      /* smoothed: per-frame work alternates with the stride */
        if (work > FRAME_TICKS || w > s3_cfg.busy_hi * (FRAME_TICKS / 100u)) { g_busy++; g_calm = 0; }
        else {
            u32 proj = w, cc = hw(TL[T_CAPCOST]);                 /* work if the stride were one lower */
            if (g_stride > 1) proj += cc / (g_stride - 1) - cc / g_stride;
            if (proj < s3_cfg.busy_lo * (FRAME_TICKS / 100u)) { g_calm++; g_busy = 0; } else { g_busy = 0; g_calm = 0; }
        }
        if (g_busy >= (s3_cfg.hyst / 4 ? s3_cfg.hyst / 4 : 1) && g_stride < cfg_stride_max()) { g_stride_next = g_stride + 1; g_busy = 0; }
        else if (g_calm >= (s3_cfg.hyst ? s3_cfg.hyst : 1) && g_stride > 1) { g_stride_next = g_stride - 1; g_calm = 0; }
    }
    if (g_pass_rb && work > FRAME_TICKS) DL[D_RB_LATE]++;          /* S6D: frames made late by a rollback */
    if (g_pass_rb) DL[D_RB_LASTWORK] = work;
    if ((S6CCFG & S6CF_ADAPT_CAP) && !(S6DCFG & S6DF_CAP_V1)) {   /* S6D: cap v2 (s6d_cap.h) */
        u32 rest = TL[T_RESTORE] ? hw(TL[T_RESTORE]) : sdg_gc_ticks(81000u);    /* defaults before the first measurement: console */
        u32 per = DL[D_PER] ? hw(DL[D_PER]) : sdg_gc_ticks(130000u);           /* 2P estimates (restore ~2 ms, logic 2.69 ms + capture) */
        s6d_cap_step(&g_cs, &g_cc, hw(work), hw(TL[T_BASEWORK]), hw(umax8(g_bw)), rest, per);
        g_cap_next = g_cs.cap;
        DL[D_CAP] = g_cs.cap; DL[D_WANT] = g_cs.want; DL[D_TOKENS] = g_cs.tokens; DL[D_IDLE] = g_cs.idle;
        DL[D_BUDGET] = g_cs.budget; DL[D_LATE] = g_cs.late; DL[D_LATE_TICKS] = g_cs.late_ticks;
        DL[D_RAISES] = g_cs.raises; DL[D_DROPS] = g_cs.drops;
        DL[D_WANTH + (g_cs.want > 7 ? 7 : g_cs.want)]++;
    } else if (S6CCFG & S6CF_ADAPT_CAP) {                /* S6c rule: cap = (idle x budget - restore) / per-frame cost */
        u32 bw = hw(umax8(g_bw)), avail = bw < FRAME_TICKS ? (FRAME_TICKS - bw) / 100u * s3_cfg.budget_pct : 0;
        u32 rest = TL[T_RESTORE] ? hw(TL[T_RESTORE]) : sdg_gc_ticks(81000u);   /* defaults: hardware 2P estimates (~2 ms restore, */
        u32 lg = TL[T_LOGIC] ? hw(TL[T_LOGIC]) : sdg_gc_ticks(109000u);         /* 2.69 ms logic, 25 % capture), HARDWARE_RESULTS */
        u32 cc = TL[T_CAPCOST] ? hw(TL[T_CAPCOST]) : sdg_gc_ticks(169000u);
        u32 per = lg + cc / g_stride, cap = avail > rest ? (avail - rest) / per : 0, mx = s3_cfg.rb_depth > 7 ? 7 : s3_cfg.rb_depth;
        if (cap < s3_cfg.cap_min) cap = s3_cfg.cap_min;
        if (cap > mx) cap = mx;
        g_cap_next = cap;
    }
    TL[T_CAPH + (C.cap > 7 ? 7 : C.cap)]++;
}

/* Review fix 3: a refused restore (sd_restore != 0: no complete capture <= frame, pending pre-images lost,
   the journal revert would not fit = -5 from the engine's dry pass, never a half-done -3 any more) used to return to the ASM silently; the ASM then
   re-simulated from the savestate frame on the UN-restored memory (StartEngineLoop.asm:355-393): a desync of our own making, counted only
   (b2_miss; seen 3 of 11 Stadium runs).  Now the match ENDS: prediction stops (cap 0 for the rest of the match) and
   the core is marked disconnected, so the next B0 answers result 3 (Slippi's own disconnect path: "DISCONNECTED" on screen, clean end; the peer
   times out after its 7 s stall limit).  Visible in DIAG / the status words as RB_B2_MISS != 0 and one report line "S6!K 52465344 frame r g
   global_frame" ('RFSD').  The frames the ASM re-simulates before that B0 still run on un-restored memory, as before, but none of them is
   confirmed, sent as a result or compared.  Lockstep instead of a disconnect would keep a diverged game alive (Slippi's checksum would show
   DESYNC DETECTED later, or never): worse.  TEST ONLY opt-out S16F_B2MISS_CONT (s16_flags 0x80): continue as before, the negative control. */
static void b2_refused(s32 frame, s32 g, int r)
{
    hk_report(0x52465344u, frame, (u32)r, (u32)g, GLOBAL_FRAME);
    if (S16CFG & S16F_B2MISS_CONT) return;
    g_b2_refused = 1;
    C.cap = 0;
    C.disconnected = 1;
}

/* SSRB (Online.s:131-142): u8 cmd, s32 frame, {u32 addr, u32 len} x 3 (ODB, RXB, SSCB), u32 0 */
static void be_savestate(void *ctx, sdshim_u8 cmd, const sdshim_u8 *ssrb)
{
    s32 frame = (s32)be32p(ssrb + 1);
    u32 t0 = tbl(), dt, off, n;
    int r;
    (void)ctx;
    RB[RB_PHASE] = cmd == 0xB1 ? 0x10 : 0x20;
    RB[RB_PH_FRAME] = (u32)frame;
    pass_mark(t0);                                               /* S6C: first work of this rendered frame */
    if (cmd == 0xB1) {
        u32 b0 = sd_st.base_captures, si = g_stride - 1;
        RB[RB_B1]++;
        g_b1r[g_b1n++ & 15] = frame;
        if (!sd_active) { RB[RB_B1_INACTIVE]++; return; }
        TL[T_B1N + si]++;                                        /* S6C: B1 calls (= predicted frames) at this stride */
        g_b1_seen = frame;                                       /* S10: this frame is predicted */
        if ((s10_post() || s10_keep()) && sd_have(frame) && (g_fresh || s10_post())) {   /* S10: captured already */
            XL[X_B1_HAVE]++;
            TL[T_B1_SKIP]++;
            TL[T_B1T + si] += tbl() - t0;
            RB[RB_PHASE] = 0x12;
            if (g_rbframe && !(rb_active && frame < rb_e)) g_rbframe = 0;   /* SKIPCAP: captured already, the own-capture question is settled */
            return;
        }
        {   /* SKIPCAP: this B1 is the rollback frame's own capture decision (the first outside the replay after a load): count what the policy did, then the debt rule goes on */
            u32 own = g_rbframe && !(rb_active && frame < rb_e);
            s32 nw;
            int wc = want_capture(frame);
            if (own) {
                g_rbframe = 0;
                if (!wc && sd_newest_le(0x7FFFFFFF, &nw) && sd_cp_skip_own(S17CFG, frame, nw)) sd_cp.skipped++;
                else if (wc) sd_cp.own_taken++;
            }
            if (!wc) {                                           /* S6C: sparse capture: skip this frame */
            TL[T_B1_SKIP]++;
            TL[T_B1T + si] += tbl() - t0;
            RB[RB_PHASE] = 0x12;
            return;
            }
        }
        TL[T_B1_CAP]++;
        if (s10_post()) XL[X_B1_FALLBACK]++;                     /* S10: pre-frame capture (no post-frame one exists) */
        r = sd_capture(frame);
        if (!r) { s10_capture_done(); g_fresh = 0; }
        cost_wait(tbl() - t0);                                   /* S6D TEST ONLY cost emulation */
        dt = tbl() - t0;
        TL[T_B1T + si] += dt;
        TL[T_CAPCOST] = ema(TL[T_CAPCOST], dt);
        if (r) RB[RB_B1_FAIL]++;
        RB[RB_B1_SUM] += dt;
        if (dt > RB[RB_B1_MAX]) RB[RB_B1_MAX] = dt;
        if (sd_st.base_captures != b0) { RB[RB_B1_BASE]++; RB[RB_BASE_SUM] += dt; }
        if (sd_st.base_captures == 1u && sd_st.base_captures != b0) s19_first_capture(frame);   /* MEM_TOOSMALL check (S19 block below) */
        else { RB[RB_B1_STEP_SUM] += dt; RB[RB_B1_STEP_N]++; if (dt > RB[RB_B1_STEP_MAX]) RB[RB_B1_STEP_MAX] = dt; RB[RB_B1_LINES] += sd_st.last_lines; }
        if (rb_active) { RB[RB_B1_RESIM_SUM] += dt; g_b1_rb_acc += dt; }   /* S12 */
        sd_stats_copy();
        if (S3CFG & 0x80u) heap_check(1, frame);
        RB[RB_PHASE] = 0x11;
        return;
    }
    if (cmd != 0xB2)
        return;
    RB[RB_B2]++;
    g_b2r[g_b2n++ & 7] = frame;
    if (g_pub_match == ST.match_no && (u32)frame < g_pub_next) RB[RB_RB_PUB]++;
    /* preserve blocks: live values saved before the restore and written back after it (E: handleLoadSavestate) */
    g_np = 0;
    for (off = 5, n = 0; off + 8 <= 33; off += 8) {
        u32 a = be32p(ssrb + off), l = be32p(ssrb + off + 4), i;
        if (!a) break;
        if (n + l > sizeof g_pres) { RB[RB_PRES_OVF]++; return; }
        for (i = 0; i < l; i++) g_pres[n + i] = *(volatile u8 *)(a + i);
        n += l;
        if (g_np < 3u) { g_ps_a[g_np] = a; g_ps_l[g_np] = l; g_np++; }
    }
    {   /* S6C: the newest capture g <= frame (g < frame when frame was skipped by the sparse schedule) */
        s32 g = frame;
        if (sd_active && !sd_newest_ok_le(frame, &g)) g = frame;   /* none: sd_restore reports the miss (S16: ok = complete) */
        if (sd_active) {                                           /* S16: deeper than the newest plain capture <= frame */
            s32 gp = 0;
            if (sd_newest_le(frame, &gp) && gp != g) YL[Y_S16_DEEP]++;
        }
        sd_rmode = s10_cfg.rmode <= 2u ? s10_cfg.rmode : 0u;     /* S10: restore mode */
        if (sd_rmode == 2u && !(s10_post() && g_fresh)) { sd_rmode = 1u; XL[X_RES_DENIED]++; }   /* live == M outside R */
        sd_rs_verify = (S10CFG & S10F_RSVERIFY) ? 1u : 0u;
        if (sd_active && (S17CFG & S17F_CHAIN_PROD) && sd_rmode == 1u) {   /* S18 production chain-only */
            QL[Q_C_LOADS]++;
            if ((S17CFG & S17F_CHAIN_NEG) && g_fresh && sd_nslots() && g_v_loads >= 64u && QL[Q_C_LOADS] % 7u == 3u) {   /* TEST ONLY unlogged write, only after the set was learned */
                u32 gob = W(STATIC_P0 + 0xB0u), dat = ptr_like(gob) ? W(gob + 0x2Cu) : 0u;
                if (ptr_like(dat)) { W(dat + 0x1830u) ^= 0x00400000u; QL[Q_CH_NEG]++; }
            }
            if (!g_fresh) QL[Q_CH_DENY]++;
            else if (g_v_latched) QL[Q_CH_FB]++;
            else {
                int learning = g_v_loads < 32u || g_v_since < 16u, verify = learning || (g_v_loads % 16u) == 0u, ok = 1;
                u32 q, k;
                g_v_loads++;
                if (verify) {
                    int nres = s17_census();
                    QL[Q_C_VALID]++;
                    if (nres < 0 || nres > 16) ok = 0;
                    else {
                        u32 newl = 0;
                        for (q = 0; q < (u32)nres; q++) {
                            for (k = 0; k < g_nv && g_v[k] != g_cs_ln[q]; k++) ;
                            if (k == g_nv) { newl++; if (learning && g_nv < 16u) g_v[g_nv++] = g_cs_ln[q]; else ok = 0; }
                        }
                        if (newl && learning) g_v_since = 0; else g_v_since++;
                        if (!learning && newl) { g_v_latched = 1; QL[Q_CH_LATCH]++; hk_report(0x4C415443u, (s32)GLOBAL_FRAME, g_nv, g_cs_ln[0], (u32)nres); }   /* "S6!K LATC": a line outside V, chain-only is off for this match */
                        if (nres > 16 && learning) ok = 0;
                    }
                } else g_v_since++;
                if (ok) {
                    for (q = 0; q < g_nv; q++) sd_census_fix(g_v[q]);
                    sd_chain_only = 1; QL[Q_CH_N]++; QL[Q_CH_FIX]++;
                } else QL[Q_CH_FB]++;
            }
        } else
        if (sd_active && (S17CFG & (S17F_CENSUS | S17F_CHAIN | S17F_CHAIN_VERIFY))) {   /* S17 R1: census / chain-only decision (the engine consumes sd_chain_only in this restore) */
            if ((S17CFG & S17F_CHAIN_NEG) && g_fresh && sd_nslots() && QL[Q_C_LOADS] % 7u == 3u) {   /* TEST ONLY: an unlogged live write after the pass-end capture */
                u32 gob = W(STATIC_P0 + 0xB0u), dat = ptr_like(gob) ? W(gob + 0x2Cu) : 0u;
                if (ptr_like(dat)) { W(dat + 0x1830u) ^= 0x00400000u; QL[Q_CH_NEG]++; }
            }
            int nres = (S17CFG & (S17F_CENSUS | S17F_CHAIN_VERIFY)) ? s17_census() : 0;   /* -1 = no fresh capture (nothing known about live) */
            QL[Q_C_LOADS]++;
            if (S17CFG & (S17F_CENSUS | S17F_CHAIN_VERIFY)) {
                if (nres < 0) QL[Q_C_NOTFRESH]++;
                else {
                    u32 nr = (u32)nres;
                    QL[Q_C_VALID]++; QL[Q_C_LINES] += nr;
                    if (nr > QL[Q_C_MAX]) QL[Q_C_MAX] = nr;
                    QL[Q_C_H0 + (nr == 0 ? 0u : nr <= 2 ? 1u : nr <= 8 ? 2u : nr <= 32 ? 3u : 4u)]++;
                    if (!nr) QL[Q_C_ZERO]++;
                }
            }
            if ((S17CFG & S17F_CHAIN) && sd_rmode == 1u) {
                if (!g_fresh) QL[Q_CH_DENY]++;                                           /* something ran since the newest capture: the full restore runs */
                else if ((S17CFG & S17F_CHAIN_VERIFY) && nres != 0) {
                    if ((S17CFG & S17F_CHAIN_FIX) && nres > 0 && nres <= 16) {            /* the verified census found the whole residual set: repair it, then the chain is exact */
                        u32 q;
                        for (q = 0; q < (u32)nres; q++) sd_census_fix(g_cs_ln[q]);
                        sd_chain_only = 1; QL[Q_CH_N]++; QL[Q_CH_FIX]++;
                    } else QL[Q_CH_FB]++;                                                         /* residual lines (or no census): the full restore runs, counted */
                }
                else { sd_chain_only = 1; QL[Q_CH_N]++; }
            }
        }
        {
            u32 tr = tbl();
            if ((S3CFG & 8u) && C.side == 1 && frame >= (s32)s3_cfg.neg_frame && !g_b2_inj) { g_b2_inj = 1; r = -6; }   /* TEST ONLY (review fix 3 injector, cfg flags=8 neg_frame=N): side B refuses one restore on purpose: the match must end with result 3 */
            else
            r = sd_active ? sd_restore(g) : -2;
            tr = tbl() - tr;
            if (!r) {
                XL[X_LAST_RMODE] = sd_rmode;
                if (sd_rmode == 2u) { XL[X_T_RSET_SUM] += tr; XL[X_T_RSET_N]++; }
                else { XL[X_T_FULL_SUM] += tr; XL[X_T_FULL_N]++; }
                XL[X_RES_N] = sd_x.res_n; XL[X_RES_KEEP] = sd_x.res_keep; XL[X_RES_RSET] = sd_x.res_rset;
                XL[X_CMP_KB] = sd_x.cmp_bytes_sum >> 10; XL[X_COPY_KB] = sd_x.copy_bytes_sum >> 10;
                XL[X_UNDO_KB] = sd_x.undo_bytes_sum >> 10; XL[X_M_KB] = sd_x.m_bytes_sum >> 10;
                XL[X_RS_LINES] = sd_x.rs_lines_sum; XL[X_RS_MISS_LINES] = sd_x.rs_miss_lines;
                XL[X_RS_MISS_RANGES] = sd_x.rs_miss_ranges; XL[X_RS_MISS_RES] = sd_x.rs_miss_restores;
                XL[X_RS_MISS_A0] = sd_x.rs_miss_a0; XL[X_M_NOMAP] = sd_x.m_nomap; XL[X_KEPT_SUM] = sd_x.kept_slots_sum;
                XL[X_UNSORTED] = sd_x.unsorted;
                if (sd_x.copy_bytes_last > XL[X_COPY_MAX]) XL[X_COPY_MAX] = sd_x.copy_bytes_last;
                g_fresh = s10_keep() ? 1u : 0u;                  /* KEEP/RSET: live == M == state(g) now */
                if (s17_relax_on) s17_relax_restore(g);          /* S17 RELAX: patch #2 (history entries of frames accepted by the relaxed rule) */
                g_after_restore = 1;
                g_loadcap = 1;
                g_rbframe = 1; sd_cp.rollbacks++;                  /* SKIPCAP: the next B1 outside the replay is this rollback's own capture decision */
                g_base_at_load = sd_st.base_captures;
                g_rb_id++;
            }
        }
        load_g = g;
    }
    for (off = 5, n = 0; off + 8 <= 33; off += 8) {
        u32 a = be32p(ssrb + off), l = be32p(ssrb + off + 4), i;
        if (!a) break;
        for (i = 0; i < l; i++) *(volatile u8 *)(a + i) = g_pres[n + i];
        n += l;
    }
    if (!r && load_g != frame) gap_fixup(frame, load_g, ssrb); /* S6C: ODB savestate frame -> g, checks, counters */
    if (!r) cost_wait(tbl() - t0);                              /* S6D TEST ONLY cost emulation */
    dt = tbl() - t0;
    rb_restore = dt;                                            /* S6D */
    if (r) { RB[RB_B2_MISS]++; YL[Y_S16_UNSAFE_RB]++; b2_refused(frame, load_g, r); s16_tel_copy(); sd_stats_copy(); return; }   /* S16: a rollback the engine could not restore: the match ends (review fix 3) */
    TL[T_RESTORE] = ema(TL[T_RESTORE], dt);
    RB[RB_B2_SUM] += dt;
    if (dt > RB[RB_B2_MAX]) RB[RB_B2_MAX] = dt;
    RB[RB_COUNT]++;
    {
        u32 o = odb();
        s32 e = o ? (s32)be32p((volatile u8 *)(o + ODB_ROLLBACK_END_FRAME)) : frame, k, kt;
        u32 early = g_early_pending;                            /* S12: started by s12_idle (no new frame in it) */
        g_early_pending = 0;
        if (early) e += 1;                                      /* S12: it ends at the last simulated frame, so every
                                                                   iteration re-simulates: count it like frames g..e-1 */
        k = e - frame; kt = e - load_g;
        g_rb_early = early;
        {   u32 kk = k < 0 ? 0u : k > 7 ? 7u : (u32)k;          /* S12 */
            if (early) ZL[Z_E_DEPTH_H + kk]++; else { ZL[Z_RB_NORMAL]++; ZL[Z_E_RB_NORM_DEPTH_H + kk]++; }
            ZL[Z_T_RESTORE] += dt;
            g_rest_k[kk] = ema(g_rest_k[kk], dt);                /* S12 (on S11): restore cost by depth (undo steps) */
            if (kk >= 1u && kk <= 5u) ZL[Z_REST_K + kk - 1u] = g_rest_k[kk];
        }
        RB[RB_RESIM] += kt > 0 ? (u32)kt : 0;                   /* S6C: frames really re-simulated (gap included) */
        RB[RB_HIST + (k < 0 ? 0 : k > 7 ? 7 : k)]++;            /* ASM depth (end - requested frame), as S6b */
        RB[RB_LAST_K] = (u32)k;
        if (k > (s32)RB[RB_MAX_K]) RB[RB_MAX_K] = (u32)k;
        TL[T_KT + (kt < 0 ? 0 : kt > 7 ? 7 : kt)]++;            /* S6C: total re-simulation incl. gap */
        TL[T_KTSUM] += kt > 0 ? (u32)kt : 0;
        k = kt;
        rb_g = load_g;
        rb_e = e;
        rb_t0 = t0;
        rb_trace = 0;
        rb_iters = 0;
        rb_active = 1;
        { extern void s10_rb_begin(void); s10_rb_begin(); }      /* S10 */
        if (k <= 0) rb_finish();
    }
    sd_stats_copy();
    if (S3CFG & 0x80u) heap_check(2, frame);
    RB[RB_PHASE] = 0x21;
}

#ifdef BR_BUILD
/* SLIPPI HAND-OFF entry points for br_glue.c (the engine is the one in this file; the bridge owns the matchmaker and the menus).  begin runs in the network slice. */
int br_engine_begin(const void *m, int sock, u32 opp_ip)
{
    if (!g_locked) return -1;
    if (C.state >= S7_SES_CONNECTING) {                              /* a session from an earlier match (the opponent may be gone): the bridge has a NEW assignment, the old one is over */
#ifdef ON_BUILD
        if (in_game()) return -1;
        end_session(clock_ms(0));
#else
        return -1;
#endif
    }
    if (s7_core_slippi_match(&C, (const struct s7_slippi_match *)m)) return -1;
    ms_rules_begin_slippi(&ms_rules, ((const struct s7_slippi_match *)m)->fixed_rules != 0u);   /* MR-WRITE: the Slippi route: Slippi's own pause / LRAS / setup, every policy of ours off */
    g_slp_sock = sock; g_slp_ip = opp_ip; g_dst_ip = opp_ip;
    s3_core_start(&C, clock_ms(0));
    return 0;
}
/* review M1: the bridge dropped the assignment (peer budget out, re-ticket, error, cancel): the engine session toward the old opponent ends now, not at the next assignment.  A running match is left to the scene rules. */
void br_engine_end(void)
{
#ifdef ON_BUILD
    if (g_locked && C.state >= S7_SES_CONNECTING && !in_game()) end_session(clock_ms(0));
#endif
}
/* why the session was lost, for the bridge's error text (live debugging without RAM access): 1 = our silence rule fired (a connected peer sent nothing for OPP_SILENCE_MS), 0 = ENet reported the loss (disconnect or ENet timeout) */
int br_engine_left_silence(void) { return ST.resv[1] != 0u; }
void br_engine_why(u32 *o8) { s7_core_why(&C, o8); }             /* cause + counters of the lost session, for the code in the bridge's box */
int br_engine_state(void) { return C.state == S3_SES_RUN ? 1 : (C.state == S7_SES_LOST ? -1 : 0); }
/* ready flags for the bridge's B3 (sbr_app.c sbra_b3, SBRA_ENG_* bits): bit 0 = the remote player's lock-in of THIS game stands, bit 1 = ours, 0x80 = "the engine owns both flags".
   s7_core new_match clears both lock-ins when a game starts (Slippi: handleOnlineInputs frame 1 localSelections.Reset()), so after a game the online CSS reads "not ready" and waits
   for a new lock-in.  Before (return C.rsel_have): the remote flag and the bridge's own local flag stayed 1 for ever, the CSS found "both locked in" the moment it loaded after ANY
   game with a live opponent and started a rematch by itself (Splash "Now loading", a new match scene against a peer that was not playing): */
u32 br_engine_ready(void) { return ((C.rsel_have && C.rsel[3]) ? 1u : 0u) | (C.lsel[3] ? 2u : 0u) | 0x80u; }
int br_engine_b0(void *ctx, const unsigned char *txb, unsigned char *rxb) { return be_inputs(ctx, txb, rxb); }
int br_engine_b3(void *ctx, unsigned char *msrb) { return be_state(ctx, msrb); }
int br_engine_b5(void *ctx, const unsigned char *pstb) { return be_sel(ctx, pstb); }
#endif
#ifdef MM_FC
/* FRAME COST: the card and the disc as the GAME's SDK sees them (Melee NTSC 1.02, symbols): EXI control block Ecb[3] at 0x804A7C80 (0x40 each: state +0x0C, bit 0x10 = locked,
   locked device +0x18), a memory card is device 0 of channel 0 or 1 (our BBA is device 2 of channel 0, or channel 2 on a BCH2 Wii kernel); the DVD library's executing block at 0x804D7298
   (its DummyCommandBlock 0x804A75E0 = idle).  Sampled once per B0 (every video frame, also while the game thread is blocked) and at the end of a long block. */
static u32 fc_gflag;                                             /* bit 0 the card, bit 1 the disc was seen busy since the last game-thread checkpoint */
static u32 fc_card_busy(void)
{
    u32 ch;
    for (ch = 0; ch < 2u; ch++) if ((W(0x804A7C8Cu + 0x40u * ch) & 0x10u) && W(0x804A7C98u + 0x40u * ch) == 0u) return 1u;
    return 0u;
}
static u32 fc_disc_busy(void) { u32 e = W(0x804D7298u); return e && e != 0x804A75E0u; }
static u32 fc_phase(void) { return GLOBAL_FRAME >= FC_PH0; }
static int be_inputs_fc(void *ctx, const sdshim_u8 *txb, sdshim_u8 *rxb)
{
    u32 p = fc_go(FC_B0);
    int r;
    if (fc_on) {
        if (fc_card_busy()) { fc_gflag |= 1u; g_fc.card_s++; }
        if (fc_disc_busy()) { fc_gflag |= 2u; g_fc.dvd_s++; }
    }
    r = be_inputs(ctx, txb, rxb);
    fc_go(p);
    return r;
}
static void be_savestate_fc(void *ctx, sdshim_u8 cmd, const sdshim_u8 *ssrb)
{
    u32 p = fc_go(cmd == 0xB2 ? FC_REST : FC_CAP);
    if (fc_on) { if (cmd == 0xB2) g_fc.rb[fc_phase()]++; else g_fc.cap[fc_phase()]++; }
    be_savestate(ctx, cmd, ssrb);
    fc_go(p);
}
#define BE_INPUTS be_inputs_fc
#define BE_SAVESTATE be_savestate_fc
#else
#define BE_INPUTS be_inputs
#define BE_SAVESTATE be_savestate
#endif
static const struct sdshim_backend BACKEND = { 0, BE_INPUTS, be_state, 0, be_sel, 0, 0, 0, 0, 0, 0, BE_SAVESTATE, 0 };   /* S7: + B5 */

void s3_config(struct sdshim *s)
{
    sdshim_set_backend(s, &BACKEND);
    sdshim_set_delay(s, s3_cfg.delay);
    if (!ms_ad_word) ms_ad_word = ad_word(0u, sd_plat_ms_ticks() >= 55000u, 0);   /* ONLINE DELAY AUTO: valid from boot, 2 on a GameCube / 3 on a Wii until a measurement */
}

static int g_core_init;
static void core_init(void)
{
    if (g_core_init) return;
    g_core_init = 1;
    s7_core_init(&C, &s3_cfg, &s7_cfg, &s3_status, &s7_status, &s3_cks, send_cb, poll_cb, clock_us, clock_ms, 0);   /* S7 */
    if (s7_cfg.s7_flags & S7F_LOOPBACK) s7_core_loopback(&C);    /* S7: reference run, no network */
    /* S6C: sparse captures / adaptive modes: B0 asks which frame a load would restore; stride 1 without adaptive
       modes keeps the S6b B0 rule exactly (no hook) */
    g_stride = s3_cfg.stride < 1 ? 1 : s3_cfg.stride > 3 ? 3 : s3_cfg.stride;
    g_stride_next = g_stride;
    C.cap = s3_cfg.rb_depth > 7 ? 7 : s3_cfg.rb_depth;
    g_cap_base = C.cap;                                          /* S16 */
    g_cap_next = C.cap;
    C.latest_cap = latest_cap_cb;
    pc_delay_init(&g_pd, PC_DELAY_FLOOR, PC_DELAY_CAP);
    C.rtt_hook = pace_rtt_cb;
    C.delay_hook = pace_delay_cb;
    C.raise_hook = stall_raise_cb;
#ifdef MS_DA_ENGINE
    if (s7_cfg.s7_flags & S7F_DELAY_AUTO) { da_init(&g_da); C.rtt_raw_hook = da_rtt_cb; }   /* AUTO DELAY: raw samples only when asked for (no cost otherwise) */
#endif
    if (g_stride > 1 || (S6CCFG & (S6CF_ADAPT_STRIDE | S6CF_ADAPT_CAP))) C.load_frame = load_frame_cb;
    /* S6D: cap v2 starts at 3 (console: depth 2 fits, 3 is borderline) inside [cap_min, rb_depth] */
    g_cc.frame = FRAME_TICKS; g_cc.budget_pct = s3_cfg.budget_pct; g_cc.ovr_pct = s3_cfg.ovr_pct;
    g_cc.ovr_every = s3_cfg.ovr_every; g_cc.up = s3_cfg.cap_hyst >> 16; g_cc.dn = s3_cfg.cap_hyst & 0xFFFFu;
    g_cc.cap_max = C.cap; g_cc.cap_min = s3_cfg.cap_min > C.cap ? C.cap : s3_cfg.cap_min;
    s6d_cap_init(&g_cs, &g_cc, 3u);
    DL[D_COSTPCT] = s3_cfg.cost_pct;
    if ((S6CCFG & S6CF_ADAPT_CAP) && !(S6DCFG & S6DF_CAP_V1)) { C.cap = g_cs.cap; g_cap_next = C.cap; }
}

void s3_exi_entry(void *buf, u32 len, int is_write)
{
    u32 m = irq_off();
#ifdef MM_FC
    u32 fp = fc_go(FC_HOOK);
#endif
    core_init();
    sdshim_transfer(buf, len, is_write);
#ifdef MM_FC
    fc_go((m & 0x8000u) && fp == FC_IDLE ? FC_ENG : fp);        /* the game thread's first command after the loop-top wait: the engine loop has started */
#endif
    irq_restore(m);
}
#ifdef MM_FC
/* FRAME COST, game-thread checkpoints (loop top, iteration start / end): besides the category switch, the time since the previous game-thread checkpoint is checked for a BLOCK (>= 50 ms)
   and the block gets a cause: network EXI (half of it or more), disc, card, a stalled B0, rollback work, other.  The time base and the plain msr bits only (irq_off's Wii breadcrumb
   counter is not touched). */
static u32 fc_gtime, fc_gnet, fc_grb, fc_gst, fc_gcc, fc_wc, fc_iters;
static u32 fc_msr_off(void) { u32 m; __asm__ volatile("mfmsr %0" : "=r"(m)); __asm__ volatile("mtmsr %0" : : "r"(m & ~0x8000u) : "memory"); return m; }
static void fc_msr_set(u32 m) { __asm__ volatile("mtmsr %0" : : "r"(m) : "memory"); }
static u32 fc_cardcmd(void) { return fc_wc ? *(volatile u32 *)0xD3026698u : 0u; }      /* Wii kernel with the EXPB words: card-side EXI commands served (lock reads the same word) */
static void fc_snap(u32 now)
{
    fc_gtime = now; fc_gnet = fc_acc[FC_NTX] + fc_acc[FC_NRX]; fc_grb = fc_acc[FC_CAP] + fc_acc[FC_REST];
    fc_gst = ST.stall_calls; fc_gcc = fc_cardcmd(); fc_gflag = 0;
}
static void fc_window(u32 now, u32 cat)
{
    u32 gap = now - fc_gtime, b, net, rb;
    if (gap >= g_fc.th_b) {
        net = fc_acc[FC_NTX] + fc_acc[FC_NRX] - fc_gnet; rb = fc_acc[FC_CAP] + fc_acc[FC_REST] - fc_grb;
        if (fc_disc_busy()) fc_gflag |= 2u;
        if (fc_card_busy() || fc_cardcmd() != fc_gcc) fc_gflag |= 1u;
        b = net >= gap / 2u ? FC_B_NET : (fc_gflag & 2u) ? FC_B_DISC : (fc_gflag & 1u) ? FC_B_CARD : ST.stall_calls != fc_gst ? FC_B_STALL
          : (rb >= gap / 2u || cat == FC_REPLAY) ? FC_B_RB : FC_B_OTHER;
        fc_block(&g_fc, fc_phase(), GLOBAL_FRAME, gap, b, cat);
    }
    fc_snap(now);
}
static void fc_gt(u32 cat)                                       /* REND running when an iteration starts was time BETWEEN iterations: moved to ENG */
{
    u32 m, now, p;
    if (!fc_on) return;
    m = fc_msr_off(); now = tbl(); p = fc_cat;
    fc_acc[p] += now - fc_t; fc_t = now; fc_cat = cat; g_fc.cps++;
    if (cat == FC_HOOK && p == FC_REND) { fc_acc[FC_ENG] += fc_acc[FC_REND]; fc_acc[FC_REND] = 0; }
    fc_window(now, p);
    fc_msr_set(m);
}
#define FC_GT(c) fc_gt(c)
#else
#define FC_GT(c) ((void)0)
#endif

/* ---- trace ---- */
static u32 fnv_w(u32 h, u32 w)
{
    h ^= w >> 24; h *= 16777619u; h ^= (w >> 16) & 0xFFu; h *= 16777619u;
    h ^= (w >> 8) & 0xFFu; h *= 16777619u; h ^= w & 0xFFu; h *= 16777619u;
    return h;
}
static u32 hash_block(u32 h, u32 addr, u32 bytes)
{
    const volatile u32 *p = (const volatile u32 *)addr;
    u32 i, w;
    for (i = 0; i < bytes / 4; i++) { w = p[i]; if (!ptr_like(w)) h = fnv_w(h, w); }
    return h;
}
/* fighter data with the 8 AXDriver voice handles (fp+0x2144..0x2163, ft/types.h:1658-1665) skipped: audio-engine
   voice ids, not simulation state (they are the only fighter words that differ between the two sides, run r2_loc) */
static u32 hash_fighter(u32 h, u32 fd)
{
    h = hash_block(h, fd, 0x2144u);
    return hash_block(h, fd + 0x2164u, FIGHTER_SIZE - 0x2164u);
}
static u32 fighter(u32 stat, u32 off)
{
    u32 g = *(volatile u32 *)(stat + off), fd;
    if (!ptr_like(g)) return 0;
    fd = *(volatile u32 *)(g + 0x2C);
    return ptr_like(fd) ? fd : 0;
}
/* gameplay hash of a fighter: the full hash minus fields written by the render phase or by the camera tasks that run
   between re-simulated frames: the draw-path flags x2227_b7 / x2228_b0 (245-246/291-292, 
   README 4.1) and the camera-box screen projection x2188 / x218C with its offscreen flag x221F_b0 (ftLib_80086A8C,
   ft/ftlib.c:525-540: projected with the CObj of the last RENDER, so a re-simulated frame sees a different matrix;
   found by run p12_loc), and the bonus statistics fed by the HUD magnifier state (fp->x2140, ft/fighter.c:2582-2591 ->
   pl_80040330, and the StaticPlayer statistics block): results-screen data,
   found by runs d12_loc/d14_loc; and the LbShadow flag byte fp+0x20A4 (shadow drawing state, lb/types.h:1021), d14_loc */
/* S12: the per-word masks of hash_fighter_gp as one function (also used by the locator); returns 0 for a skipped word.
   S12F_GPMASK adds the render-written fighter bits listed in s12_gp_extra (README S12, "YS gameplay-hash gap"). */
static int gp_word(u32 kind, u32 i, u32 *w)
{
    if (i >= 0x2144u / 4 && i < 0x2164u / 4) return 0;
    if (i == 0x2224u / 4) *w &= ~0x00000001u;
    else if (i == 0x2228u / 4) *w &= ~0x80000000u;
    else if (i == 0x2188u / 4 || i == 0x218Cu / 4) *w = 0;      /* x2188: camera-box screen projection */
    else if (i == 0x221Cu / 4) *w &= ~0x00000080u;              /* x221F_b0: offscreen flag from it */
    else if (i == 0x2140u / 4) *w = 0;                             /* x2140: bonus stat, needs the HUD magnifier */
    else if (i == 0x20A4u / 4) *w &= 0x00FFFFFFu;                  /* x20A4 LbShadow flag byte (shadow drawing) */
    if (S12CFG & S12F_GPMASK) *w = s12_gp_extra_apply(kind, i, *w);   /* S12 */
    return !ptr_like(*w);
}
static u32 hash_fighter_gp(u32 h, u32 fd)
{
    u32 i, w;
    const volatile u32 *p = (const volatile u32 *)fd;
    u32 kind = p[1];                                              /* fp+4 FighterKind */
    for (i = 0; i < FIGHTER_SIZE / 4; i++) {
        w = p[i];
        if (gp_word(kind, i, &w)) h = fnv_w(h, w);
    }
    return h;
}
/* S12 TEST ONLY locator: the gameplay-hash scope of one fighter as 16 chunk hashes of 0x240 bytes (144 words) */
#define LOC_CH 16u
#define LOC_CW 144u
static void hash_fighter_chunks(u32 fd, u32 *out)
{
    u32 c, i, w, h;
    const volatile u32 *p = (const volatile u32 *)fd;
    u32 kind = p[1];
    for (c = 0; c < LOC_CH; c++) {
        h = 2166136261u;
        for (i = c * LOC_CW; i < (c + 1) * LOC_CW && i < FIGHTER_SIZE / 4; i++) {
            w = p[i];
            if (gp_word(kind, i, &w)) h = fnv_w(h, w);
        }
        out[c] = h;
    }
}

/* ---- S6b trace: per engine iteration (04 hook 0x801A4DE8, right after StartEngineLoop: the state at the START of
   Slippi frame F = global frame, after a savestate load if there was one), kept pending until the ODB stable finalized
   frame reaches F, then published once into a 32-entry ring watched by MemoryWatcher. ---- */
struct pend { u32 f, full, gp, core, cam; };
static struct pend g_pend[64];
static u32 g_pubf[64], g_pubgp[64], g_pubcore[64];             /* S6C: published hashes (rehash check) */

#define LOC_REC (68u + LOC_CW)                                   /* S12 locator record: frame, gp, 2 x 33, raw chunk */
static u32 *g_loc, g_loc_tried;                                  /* S12 TEST ONLY (HSD heap, outside the manifest) */
static void hash_state(u32 f, struct pend *e)
{
    u32 full = 2166136261u, gp = 2166136261u, core = 2166136261u, p, fd, k;
    static const u16 core_off[] = { 0x8, 0x10, 0x2C, 0x80, 0x84, 0xB0, 0xB4, 0x894, 0x1830 };
    for (p = 0; p < 4; p++) {
        u32 st = STATIC_P0 + p * STATIC_STRIDE;
        full = hash_block(full, st, STATIC_STRIDE);
        gp = hash_block(gp, st, 0xE8u);           /* StaticPlayer up to the stale-move queue (+0xBC..+0xE7); +0xE8..
                                                     is the results-screen statistics block (plActionStats, item logs,
                                                     bonus counters, pl/types.h:120-160), excluded from the gameplay hash */
        core = fnv_w(core, W(st + 0x8C));
        for (k = 0; k < 2; k++) {
            fd = fighter(st, k ? 0xB4 : 0xB0);
            if (!fd) continue;
            full = hash_fighter(full, fd);
            gp = hash_fighter_gp(gp, fd);
            { u32 i; for (i = 0; i < sizeof core_off / sizeof core_off[0]; i++) core = fnv_w(core, W(fd + core_off[i])); }
        }
    }
    e->f = f;
    if (g_loc) {                                                 /* S12 TEST ONLY locator record of this frame */
        u32 *r = g_loc + (f & 63u) * LOC_REC, q, kk;
        r[0] = f;
        for (q = 0; q < 2; q++) {
            u32 st = STATIC_P0 + q * STATIC_STRIDE, *o = r + 2 + q * 33u;
            o[0] = hash_block(2166136261u, st, 0xE8u);
            for (kk = 0; kk < 2; kk++) {
                u32 fdq = fighter(st, kk ? 0xB4 : 0xB0), c;
                if (fdq) hash_fighter_chunks(fdq, o + 1 + kk * LOC_CH);
                else for (c = 0; c < LOC_CH; c++) o[1 + kk * LOC_CH + c] = 0;
            }
        }
        if (S12CFG & S12F_LOCW) {                         /* raw words of chunk loc_sel */
            u32 sel = s12_cfg.loc_sel, fdw = fighter(STATIC_P0 + ((sel >> 12) & 3u) * STATIC_STRIDE, (sel >> 8) & 1u ? 0xB4 : 0xB0);
            u32 c0 = (sel & 15u) * LOC_CW, i;
            for (i = 0; i < LOC_CW; i++) r[68 + i] = fdw && c0 + i < FIGHTER_SIZE / 4 ? W(fdw + 4 * (c0 + i)) : 0;
        }
    }
    {   /* S10: with the trimmed re-simulation the seed at the START of a frame may differ (the skipped particle / HUD
           procs drew from it at priority >= 15 of the previous frame); it is dead there: Slippi's per-frame reseed
           (patched proc, p_link 7, priority 0: seed = rotl(global frame, 16) + ODB+7) overwrites it before any draw, which
           s10_invoke checks on every iteration (X_PRESEED_DRAWS must stay 0).  So trim runs hash the seed as 0. */
        u32 seed = ((S10CFG & S10F_TRIM) || (S12CFG & (S12F_TRIMX | S12F_TRIMCAM | S12F_C_ANY))) ? 0u : RNG_SEED;   /* S12: + its trims */
        e->full = fnv_w(full, seed);
        e->gp = fnv_w(gp, seed);
        e->core = fnv_w(core, seed);
    }
    e->cam = hash_block(2166136261u, 0x80452C68u, 0x39Cu);       /* game_camera */
}

static void loc_print(u32 f, u32 gp);                            /* S12 */
static void publish(u32 sf)
{
    u32 n = 0;
    if (g_pub_match != ST.match_no) { g_pub_match = ST.match_no; g_pub_next = 1; }
    if (sf + 64 < g_pub_next || (s32)sf < 1) return;
    if (sf >= g_pub_next + 64) { RB[RB_GAP] += sf - 63 - g_pub_next; g_pub_next = sf - 63; }
    while (g_pub_next <= sf && n++ < 64) {
        const struct pend *e = &g_pend[g_pub_next & 63];
        if (e->f == g_pub_next) {
            volatile u32 *r = s6_trace.e[g_pub_next & (TR_N - 1)];
            r[0] = 0xFFFFFFFFu;                                  /* invalid while rewritten (MemoryWatcher may sample) */
            r[1] = e->full; r[2] = e->gp; r[3] = e->core; r[4] = e->cam;
            r[0] = g_pub_next | ((ST.match_no & 0x7Fu) << 24);
            g_pubf[g_pub_next & 63] = g_pub_next; g_pubgp[g_pub_next & 63] = e->gp; g_pubcore[g_pub_next & 63] = e->core;
            if (g_loc) loc_print(g_pub_next, e->gp);              /* S12 TEST ONLY */
            RB[RB_PUB]++;
            RB[RB_PUB_FRAME] = g_pub_next;
            if (g_dbg && DBG_AUTO) {
                g_gph_f[g_pub_next & 63] = g_pub_next; g_gph[g_pub_next & 63] = e->gp;   /* S7: not sent on the wire */
            }
        } else {
            RB[RB_GAP]++;
        }
        g_pub_next++;
    }
}

/* debug locator: dump both fighters and the static player blocks as hex words (one OSReport line per 32 words);
   the last dump of a frame before it is finalized is its final state */
typedef void (*report_t)(const char *, ...);
#ifdef MS_ENGINE_REPORT          /* lab / FC builds keep the engine's OSReport lines; a release prints nowhere, so the calls (and the line building, now dead stores) are compiled out: engine size */
#define OSReport ((report_t)0x803456A8)
#else
#define OSReport(...) ((void)0)
#endif
static char g_lb[400];
static char *hx(char *p, u32 v) { int i; for (i = 28; i >= 0; i -= 4) { u32 d = (v >> i) & 15; *p++ = (char)(d < 10 ? '0' + d : 'a' + d - 10); } return p; }
#ifdef MS_ENGINE_REPORT
static void dump_block(u32 f, u32 tag, u32 a, u32 bytes)
{
    u32 off;
    for (off = 0; off < bytes; off += 128) {
        char *p = g_lb;
        u32 k;
        *p++ = 'S'; *p++ = '6'; *p++ = 'L'; *p++ = ' ';
        p = hx(p, f); *p++ = ' '; p = hx(p, tag); *p++ = ' '; p = hx(p, off); *p++ = ' ';
        for (k = 0; k < 32 && off + 4 * k < bytes; k++) p = hx(p, W(a + off + 4 * k));
        *p++ = '\n'; *p = 0;
        OSReport(g_lb);
    }
}
#else
static void __attribute__((unused)) dump_block(u32 f, u32 tag, u32 a, u32 bytes)
{
    (void)f; (void)tag; (void)a; (void)bytes;
}
#endif
/* the window is copied to a heap buffer at every iteration (cheap; the last copy of a frame before it is finalized is
   its final state) and printed in one go 120 frames after the window (the printing lags the game: debug runs only) */
#define DBG_REC (2u * 0x2400u + 4u * 0xE90u)
static void dbg_copy(u32 f)
{
    u32 p, base = g_dbg + (DBG_AUTO ? f % s3_cfg.dbg_n : f - s3_cfg.dbg_lo) * DBG_REC, i;
    if (DBG_AUTO) { g_dbg_tag[f % s3_cfg.dbg_n] = f; g_dbg_gp[f % s3_cfg.dbg_n] = g_pend[f & 63].gp; }
    for (p = 0; p < 2; p++) {
        u32 fd = fighter(STATIC_P0 + p * STATIC_STRIDE, 0xB0);
        for (i = 0; i < FIGHTER_SIZE / 4; i++) W(base + p * 0x2400u + 4 * i) = fd ? W(fd + 4 * i) : 0;
    }
    for (p = 0; p < 4; p++)
        for (i = 0; i < STATIC_STRIDE / 4; i++) W(base + 0x4800u + p * 0xE90u + 4 * i) = W(STATIC_P0 + p * STATIC_STRIDE + 4 * i);
}
static void dbg_print(void)
{
    u32 k, p;
    for (k = 0; k < s3_cfg.dbg_n; k++) {
        u32 base = g_dbg + k * DBG_REC;
        for (p = 0; p < 2; p++) {
            dump_block(s3_cfg.dbg_lo + k, 0x10 + p, base + p * 0x2400u, FIGHTER_SIZE);
            dump_block(s3_cfg.dbg_lo + k, 0x20 + p, base + 0x4800u + p * 0xE90u, STATIC_STRIDE);
        }
    }
}

/* hang watchdog, called from the pad alarm (VI interrupt, fn_800195FC): if no engine iteration ran for 180 fields
   inside the online match, print the interrupted context once (OSContext at *0x800000D4: srr0 +0x198, lr +0x84,
   r1 +4 and its back chain) and the snapshot engine's state */
extern u32 sd_lost_first[4];
#ifdef MS_ENGINE_REPORT
static u32 g_wd_iters, g_wd_same, g_wd_done;
static void wd_dump(char tag)                                     /* "S6!<tag>" line (+ one "S6!T" line per thread): the interrupted context and the thread queue */
{
    u32 ctx, sp, i;
    char *p;
    ctx = W(0x800000D4);
    p = g_lb;
    *p++ = 'S'; *p++ = '6'; *p++ = '!'; *p++ = tag; *p++ = ' ';
    p = hx(p, GLOBAL_FRAME); *p++ = ' '; p = hx(p, rb_active); *p++ = ' '; p = hx(p, (u32)rb_g); *p++ = ' '; p = hx(p, (u32)rb_e);
    if (ptr_like(ctx)) {
        *p++ = ' '; p = hx(p, W(ctx + 0x198)); *p++ = ' '; p = hx(p, W(ctx + 0x84));
        sp = W(ctx + 4);
        for (i = 0; i < 20 && ptr_like(sp); i++) { *p++ = ' '; p = hx(p, W(sp + 4)); sp = W(sp); }
    }
    *p++ = '\n'; *p = 0;
    OSReport(g_lb);
    {   /* stage smoke why the engine stalls, in one line: "S6!K 47415052 global_frame ss g predicting<<16|sd_health" (ss = the ASM's oldest rollback target, g = newest COMPLETE capture <= ss or -1,
           predicting = ODB_SAVESTATE_IS_PREDICTING, health = SD_H_* bits).  The Mute City / Great Bay hangs end with every game thread idle in the pad wait: the engine answers B0 "stall" for ever. */
        u32 o = odb(), pr = 0;
        s32 ss = 0, g = -1;
        if (o) { pr = *(volatile u8 *)(o + ODB_SAVESTATE_IS_PREDICTING); ss = (s32)be32p((volatile u8 *)(o + ODB_SAVESTATE_FRAME)); }
        if (!sd_active || !sd_newest_ok_le(ss, &g)) g = -1;
        hk_report(0x47415052u, (s32)GLOBAL_FRAME, (u32)ss, (u32)g, (pr << 16) | (sd_active ? sd_health() : 0xFFu));
        hk_report(0x4C4F5354u, (s32)GLOBAL_FRAME, sd_x16.lost_ranges, sd_x16.lost_bytes, sd_x16.inc_onsets);              /* "S6!K LOST frame lost_ranges lost_bytes inc_onsets ..." */
        hk_report(0x4C535431u, (s32)GLOBAL_FRAME, sd_lost_first[0], sd_lost_first[1], sd_lost_first[2]);                    /* "S6!K LST1 frame site lo bytes seq": the first loss */
        hk_report(0x4C535432u, (s32)GLOBAL_FRAME, sd_lost_first[3], sd_x16.inc_caps, (sd_x16.rebuilds_ok << 16) | (sd_x16.rebuilds_fail & 0xFFFFu));
    }
    {   /* every thread of the active-thread queue (__OSActiveThreadQueue 0x800000DC, link +0x2FC): state, srr0, lr,
           back chain (OSThread starts with its OSContext) */
        u32 t = W(0x800000DC), n = 0;
        while (ptr_like(t) && n++ < 12) {
            p = g_lb;
            *p++ = 'S'; *p++ = '6'; *p++ = '!'; *p++ = 'T'; *p++ = ' ';
            p = hx(p, t); *p++ = ' '; p = hx(p, (u32)*(volatile u16 *)(t + 0x2C8)); *p++ = ' ';
            p = hx(p, W(t + 0x198)); *p++ = ' '; p = hx(p, W(t + 0x84)); *p++ = ' '; p = hx(p, W(0x800000E4));
            sp = W(t + 4);
            for (i = 0; i < 16 && ptr_like(sp); i++) { *p++ = ' '; p = hx(p, W(sp + 4)); sp = W(sp); }
            *p++ = '\n'; *p = 0;
            OSReport(g_lb);
            t = W(t + 0x2FC);
        }
    }
}
#define S12F_SCENE_WD 0x1000000u        /* s12_flags (diagnostic): report a frozen online scene (see s6n_watchdog) */
#define SCENE_FRAMES (*(volatile u32 *)0x80479D58)               /* gm_80479D58: the scene frame counter */
static u32 g_sf_scene, g_sf_last, g_sf_same, g_sf_done;
void s6n_watchdog(void)
{
    if ((S12CFG & S12F_SCENE_WD) && (SCENE_WORD >> 24) == 8) {   /* (ROLLBACK_LOCKIN row 44): the post-game character select of a long game froze in Dolphin (scene frame counter stopped, VI interrupts alive):
                                                                    the same interrupted-context dump as below, "S6!F", once per scene, when the counter has not moved for 30 s */
        u32 sf = SCENE_FRAMES;
        if (SCENE_WORD != g_sf_scene) { g_sf_scene = SCENE_WORD; g_sf_done = 0; g_sf_same = 0; g_sf_last = sf; }
        else if (sf != g_sf_last) { g_sf_last = sf; g_sf_same = 0; }
        else if (!g_sf_done && ++g_sf_same >= 1800) { g_sf_done = 1; wd_dump('F'); }
    }
    if (!in_game() || g_wd_done) return;
    if (RB[RB_ITERS] != g_wd_iters) { g_wd_iters = RB[RB_ITERS]; g_wd_same = 0; return; }
    if (++g_wd_same < 180) return;
    g_wd_done = 1;
    wd_dump('H');
}
#else
void s6n_watchdog(void) { }                                      /* release: the watchdog's only effect is the "S6!" dump, which prints nowhere */
#endif


u32 s17_relax_odb(void) { return odb(); }
u32 s17_relax_busy(void) { return g_in_logic; }

/* ---- S17 R1: pass-end capture and the live-against-mirror census ---- */
#define CS_TAB 7u
static void s17_cs_cb(u32 line, u32 kind)
{
    u32 i;
    for (i = 0; i < g_np; i++)
        if (line + 32u > g_ps_a[i] && line < g_ps_a[i] + g_ps_l[i]) { g_cs_pres++; return; }
    if (g_cs_lines < 16u) g_cs_ln[g_cs_lines] = line;
    g_cs_lines++;
    if (kind) g_cs_cell++; else g_cs_static++;
    for (i = 0; i < CS_TAB; i++) {
        if (QL[Q_C_TAB + 2 * i] == line) { QL[Q_C_TAB + 2 * i + 1]++; return; }
        if (!QL[Q_C_TAB + 2 * i]) { QL[Q_C_TAB + 2 * i] = line; QL[Q_C_TAB + 2 * i + 1] = 1; return; }
    }
}
/* census of one load: returns the residual line count, -1 when no fresh capture exists */
static int s17_census(void)
{
    u32 n;
    if (!sd_active || !g_fresh || !sd_nslots()) return -1;
    g_cs_lines = g_cs_pres = g_cs_static = g_cs_cell = 0;
    sd_census(s17_cs_cb);
    n = g_cs_lines;
    QL[Q_C_PRES] += g_cs_pres; QL[Q_C_STATIC] += g_cs_static; QL[Q_C_CELL] += g_cs_cell;
    return (int)n;
}
static void s17_pe(void)
{
    u32 m, f, t0, dt;
    int r;
    if (!sd_active || !in_game() || rb_active || g_early_pending || g_in_logic || g_pe_seq == g_iter_seq) return;
    g_pe_seq = g_iter_seq;
    f = g_cur_frame;
    if (g_fresh || g_b1_seen != (s32)f || !want_capture((s32)f + 1) || (S10CFG & S10F_POSTCAP)) { QL[Q_PE_SKIP]++; return; }
    m = irq_off();                                              /* the capture is as exclusive as B1 and B2 (no pad alarm inside) */
    t0 = tbl();
    r = sd_capture((s32)f + 1);
    dt = tbl() - t0;
    irq_restore(m);
    cost_wait(dt);
    TL[T_CAPCOST] = ema(TL[T_CAPCOST], dt);
    RB[RB_B1_SUM] += dt;
    if (dt > RB[RB_B1_MAX]) RB[RB_B1_MAX] = dt;
    if (!r) { s10_capture_done(); g_fresh = 1; QL[Q_PE_N]++; } else { RB[RB_B1_FAIL]++; QL[Q_PE_FAIL]++; }
    sd_stats_copy();
}
static void s10_iter_start(u32 f);
static void s10_iter_end(u32 f);
void s6n_iter(void)
{
    u32 t0 = tbl(), f, o;
    if (!in_game()) return;
    FC_GT(FC_HOOK);                                              /* FRAME COST: our iteration hook */
    pass_mark(t0);                                               /* S6C */
    if (g_rs_gap_have && rb_active) {                            /* S12: Slippi loop + camera tasks + B1 between frames */
        u32 gp = t0 - g_rs_end_t;
        ZL[Z_T_GAP] += gp > g_b1_rb_acc ? gp - g_b1_rb_acc : 0u; ZL[Z_T_GAP_N]++;
        ZL[Z_T_CAP_RB] += g_b1_rb_acc;
    }
    g_rs_gap_have = 0;
    RB[RB_PHASE] = 0x30;
    f = GLOBAL_FRAME;
    g_cur_frame = f;
    s10_iter_start(f);                                           /* S10 */
    g_iter_seq++;                                                /* S6D SFX log: one record per engine iteration */
    g_in_logic = 1;
    sfx_put(1u, (u32)rb_active, (u32)rb_e);
    RB[RB_ITERS]++;
    if (rb_active) rb_iters++;
    if (!(S3CFG & 4u)) {
        hash_state(f, &g_pend[f & 63]);
        if (g_pub_match == ST.match_no && f < g_pub_next) {           /* a frame already published runs again */
            RB[RB_REHASH]++;
            if (g_pubf[f & 63] == f) {                            /* S6C: it must hash exactly as published */
                TL[T_REHASH_N]++;
                if (g_pubgp[f & 63] != g_pend[f & 63].gp || g_pubcore[f & 63] != g_pend[f & 63].core) TL[T_REHASH_DIFF]++;
            }
            if (g_dbg && DBG_AUTO && g_gph_f[f & 63] == f && g_gph[f & 63] != g_pend[f & 63].gp) {
                u32 s = f % s3_cfg.dbg_n, b = g_dbg + s * DBG_REC;
                RB[RB_REHASH_DIFF]++;
                if (g_gp_dumps < 2 && g_dbg_tag[s] == f) {
                    g_gp_dumps++;
                    dump_block(f, 0x40, b, FIGHTER_SIZE); dump_block(f, 0x41, b + 0x2400u, FIGHTER_SIZE);
                    dump_block(f, 0x50, b + 0x4800u, STATIC_STRIDE); dump_block(f, 0x51, b + 0x4800u + 0xE90u, STATIC_STRIDE);
                    dump_block(f, 0x52, b + 0x4800u + 0x1D20u, STATIC_STRIDE); dump_block(f, 0x53, b + 0x4800u + 0x2BB0u, STATIC_STRIDE);
                    dbg_copy(f);
                    dump_block(f, 0x10, b, FIGHTER_SIZE); dump_block(f, 0x11, b + 0x2400u, FIGHTER_SIZE);
                    dump_block(f, 0x20, b + 0x4800u, STATIC_STRIDE); dump_block(f, 0x21, b + 0x4800u + 0xE90u, STATIC_STRIDE);
                    dump_block(f, 0x22, b + 0x4800u + 0x1D20u, STATIC_STRIDE); dump_block(f, 0x23, b + 0x4800u + 0x2BB0u, STATIC_STRIDE);
                }
            }
        }
        if (g_dbg && (DBG_AUTO || (f >= s3_cfg.dbg_lo && f < s3_cfg.dbg_lo + s3_cfg.dbg_n))) dbg_copy(f);
        if (g_dbg && !DBG_AUTO && !g_dbg_done && f >= s3_cfg.dbg_lo + s3_cfg.dbg_n + 120) { g_dbg_done = 1; dbg_print(); }
        o = odb();
        if (o) {
            s32 sf = (s32)be32p((volatile u8 *)(o + ODB_STABLE_FINALIZED_FRAME));
            RB[RB_SF] = (u32)sf;
            RB[RB_ODB] = o;
            if (sf > 0) publish((u32)sf);
        }
        ST.trace_frames++;
    }
    g_it0 = tbl();
    RB[RB_PHASE] = 0x31;
    t0 = g_it0 - t0;
    RB[RB_TRACE_SUM] += t0;
    if (rb_active) { rb_trace += t0; ZL[Z_T_TRACE_RB] += t0; }   /* S12 */
#ifdef MM_FC
    if (fc_on) { if (rb_active && (s32)f < rb_e) g_fc.rep[fc_phase()]++; else fc_iters++; }
    FC_GT(rb_active && (s32)f < rb_e ? FC_REPLAY : FC_LOGIC);   /* FRAME COST: the game's logic of frame f from here to s6n_end */
#endif
}

#ifdef MS_ENGINE_REPORT
static void hk_report(u32 tag, s32 frame, u32 what, u32 cell, u32 extra)
{
    char *p = g_lb;
    g_hk_reports++;
    *p++ = 'S'; *p++ = '6'; *p++ = '!'; *p++ = 'K'; *p++ = ' ';
    p = hx(p, tag); *p++ = ' '; p = hx(p, (u32)frame); *p++ = ' '; p = hx(p, what); *p++ = ' ';
    p = hx(p, cell); *p++ = ' '; p = hx(p, extra); *p++ = ' '; p = hx(p, RB[RB_COUNT]); *p++ = ' '; p = hx(p, RB[RB_B1]);
    *p++ = '\n'; *p = 0;
    OSReport(g_lb);
}
#else
static void __attribute__((unused)) hk_report(u32 tag, s32 frame, u32 what, u32 cell, u32 extra)

{
    (void)tag; (void)frame; (void)what; (void)cell; (void)extra;
    g_hk_reports++;
}
#endif

/* LoadState.asm "frame not found" spin (`b 0` followed by `subi r6,r6,1`): redirect it to s6n_lsfail_stub so the
   case is reported (it spins with interrupts off, so no watchdog can see it) */
extern char s6n_lsfail_stub[];
static u32 g_ls_patched;
static void patch_lsfail(void)
{
    u32 a;
    if (g_ls_patched) return;
    g_ls_patched = 1;
    for (a = SD_B_LSLO; a < SD_B_LSHI; a += 4)
        if (W(a) == 0x48000000u && W(a + 4) == 0x38C6FFFFu) {
            W(a) = 0x48000000u | (((u32)s6n_lsfail_stub - a) & 0x03FFFFFCu);
            SDK_DCFlushRange((void *)a, 4);
            __asm__ volatile("icbi 0,%0; sync; isync" : : "r"(a) : "memory");
            RB[RB_PROLL_SUM] = a;
            return;
        }
}
/* Star Fox team conversation (Corneria / Venom; ftFx_AppealS, grCorneria_801E277C; stage sweep): a line is left when the voice clip is over, and the game asks
   the AUDIO ENGINE (Ground_801C54DC -> lbAudioAx_80023710, real time, outside the snapshot), so the two consoles (and a frame and its resimulation) ended each line at different frames.  The one call in
   grCorneria_801E277C (0x801E298C, `bl 0x801C54DC`) is redirected here: the line counts as playing while the frames since it started (60 - gv->timer; gv = gp + 0xC4: state +0, timer +2, sound_id +8) are
   below the clip's length, measured once per sound id (table below; frames of 1/60 s).  A sound id not in the table: 150 frames.  Only the call site of the conversation is changed: every other user of
   Ground_801C54DC keeps the audio query.  SF_MEASURE (lab build): the real query is used and "S6!Q sound_id frames" is printed when it first says "over". */
#define SF_FN      0x801E277Cu
#define SF_FN_LEN  0x2F0u
#define SF_QUERY   0x801C54DCu
static const u32 sf_len[] = {                                  /* (sound_id << 12) | frames, from the measurement (SF_MEASURE) */
    0x1870C05Fu,
    0x1870D08Bu,
    0x1870E0E3u,
    0x1870F097u,
    0x187100A4u,
    0x187110A3u,
    0x18712061u,
    0x187130A2u,
    0x18714096u,
    0x1871507Du,
    0x18716087u,
    0x18717087u,
    0x187180BAu,
    0x187190BEu,
    0x1871A0BCu,
    0x1871B08Du,
    0x1871C0C9u,
    0x1AE200B1u,
    0x1AE21091u,
    0x1AE22082u,
    0x1AE2308Du,
    0x1AE240E6u,
    0x1AE250A2u,
    0x1AE26088u,
    0x1AE270FDu,
    0x1AE2809Fu,
    0x1AE290B9u,
    0x1AE2A077u,
    0x1AE2B051u,
    0x1AE2C07Bu,
    0x1AE2D064u,
    0x1AE2E079u,
    0x1AE2F09Fu,
    0x1AE30051u
};
int s6n_sf_playing(u32 gp, s32 i)
{
    s32 n = 60 - (s32)(short)*(volatile unsigned short *)(gp + 0xC6u);
    u32 sid = *(volatile u32 *)(gp + 0xCCu), k, len = 150u;
#ifdef SF_MEASURE
    {   int r = ((int (*)(u32, s32))SF_QUERY)(gp, i);
        if (!r) { char *p = g_lb; *p++ = 'S'; *p++ = '6'; *p++ = '!'; *p++ = 'Q'; *p++ = ' '; p = hx(p, sid); *p++ = ' '; p = hx(p, (u32)n); *p++ = '\n'; *p = 0; OSReport(g_lb); }
        return r;
    }
#endif
    (void)i;
    for (k = 0; k < sizeof sf_len / sizeof sf_len[0]; k++)
        if ((sf_len[k] >> 12) == sid) { len = sf_len[k] & 0xFFFu; break; }
    return n < (s32)len;
}
static u32 g_sf_patched;
static void patch_sfconv(void)
{
    u32 a;
    if (g_sf_patched) return;
    for (a = SF_FN; a < SF_FN + SF_FN_LEN; a += 4) {
        u32 w = W(a);
        if ((w & 0xFC000003u) == 0x48000001u && ((a + (u32)(((s32)((w & 0x03FFFFFCu) << 6)) >> 6)) == SF_QUERY)) {
            s32 d = (s32)((u32)s6n_sf_playing - a);
            if (d < -0x02000000 || d > 0x01FFFFFC) return;       /* out of bl range (+-32 MB; the engine is about 23 MB above the game code): keep the audio query */
            W(a) = 0x48000001u | ((u32)d & 0x03FFFFFCu);
            SDK_DCFlushRange((void *)a, 4);
            __asm__ volatile("icbi 0,%0; sync; isync" : : "r"(a) : "memory");
            g_sf_patched = 1;
            return;
        }
    }
}
#ifndef MS_ENGINE_REPORT
void s6n_lsfail(s32 frame, u32 sscb) { (void)frame; (void)sscb; }   /* release: the "S6!L" dump prints nowhere */
#else
void s6n_lsfail(s32 frame, u32 sscb)
{
    u32 i, o = odb();
    char *p = g_lb;
    *p++ = 'S'; *p++ = '6'; *p++ = '!'; *p++ = 'L'; *p++ = ' ';
    p = hx(p, (u32)frame); *p++ = ' '; p = hx(p, GLOBAL_FRAME); *p++ = ' ';
    if (ptr_like(sscb)) { p = hx(p, *(volatile u8 *)sscb); *p++ = ':'; for (i = 0; i < 7; i++) { p = hx(p, be32p((volatile u8 *)(sscb + 2 + 4 * i))); *p++ = ','; } }
    *p++ = ' '; *p++ = 'b'; *p++ = '1';
    for (i = 0; i < 16; i++) { *p++ = ','; p = hx(p, (u32)g_b1r[(g_b1n + i) & 15]); }
    *p++ = ' '; *p++ = 'b'; *p++ = '2';
    for (i = 0; i < 8; i++) { *p++ = ','; p = hx(p, (u32)g_b2r[(g_b2n + i) & 7]); }
    if (o) {
        *p++ = ' '; p = hx(p, be32p((volatile u8 *)(o + ODB_ROLLBACK_END_FRAME)));
        *p++ = ' '; p = hx(p, be32p((volatile u8 *)(o + ODB_STABLE_FINALIZED_FRAME)));
        *p++ = ' '; p = hx(p, be32p((volatile u8 *)(o + 0xADA)));
        *p++ = ' '; p = hx(p, be32p((volatile u8 *)(o + 0x3)));
        *p++ = ' '; p = hx(p, be32p((volatile u8 *)(o + 0x38D)));
        *p++ = ' '; p = hx(p, be32p((volatile u8 *)(o + 0xAC5)));
    }
    *p++ = '\n'; *p = 0;
    OSReport(g_lb);
}
#endif

/* The ASM is about to load a savestate frame that its own SSCB ring (7 entries, SaveState.asm) does not hold -> LoadState.asm spins forever (`b 0`): the guest freezes (VI counter stops; the peer
   stalls, S6!H, and disconnects).  Stage sweep EVERY guest freeze seen in the pair rig (8 of 8, Big Blue / Final Destination / Venom, both rigs) printed S6!L on the frozen console;
   the ring held frames 0x729..0x72e and 0x724 while the ASM asked for 0x730 (the engine had not asked for the two newest saves).  Fix: the target is moved back to the newest ring frame <= it (what
   gap_fixup does for a sparse capture: ODB_SAVESTATE_FRAME and ODB_STABLE_SAVESTATE_FRAME), the rollback then re-simulates two more frames and B2 restores the newest complete capture <= that frame.
   ZL[Z_E_ERR] += 0x1000000 per fix.  Print the ASM's view (up to 3 times per boot) when it happens. */
static u32 g_sscb_reports;
static void check_sscb(u32 tag)
{
    u32 o = odb(), sscb, i;
    s32 s, fr[7];
    int found = 0;
#ifdef MS_ENGINE_REPORT
    char *p;
#else
    (void)tag;
#endif
    if (!o) return;
    if (!*(volatile u8 *)(o + ODB_ROLLBACK_IS_ACTIVE) || !*(volatile u8 *)(o + 0xE0)) return;
    s = (s32)be32p((volatile u8 *)(o + 0x38D));
    if (s >= (s32)GLOBAL_FRAME) return;                        /* no load (frame == / > savestate frame) */
    sscb = be32p((volatile u8 *)(o + 0x3A4));
    if (!ptr_like(sscb)) return;
    for (i = 0; i < 7; i++) { fr[i] = (s32)be32p((volatile u8 *)(sscb + 2 + 4 * i)); if (fr[i] == s) found = 1; }
    if (found) return;
    {   s32 best = -1;
        for (i = 0; i < 7; i++) if (fr[i] <= s && fr[i] > best) best = fr[i];
        if (best > 0) {
            put_be32(o + ODB_SAVESTATE_FRAME, (u32)best);
            put_be32(o + ODB_STABLE_SAVESTATE_FRAME, (u32)best);
            ZL[Z_E_ERR] += 0x1000000u;
        }
    }
#ifdef MS_ENGINE_REPORT
    if (g_sscb_reports >= 3) return;
    g_sscb_reports++;
    p = g_lb;
    *p++ = 'S'; *p++ = '6'; *p++ = '!'; *p++ = 'S'; *p++ = ' ';
    p = hx(p, tag); *p++ = ' '; p = hx(p, (u32)s); *p++ = ' '; p = hx(p, GLOBAL_FRAME); *p++ = ' ';
    p = hx(p, *(volatile u8 *)sscb); *p++ = ':';
    for (i = 0; i < 7; i++) { p = hx(p, (u32)fr[i]); *p++ = ','; }
    *p++ = ' '; *p++ = 'b'; *p++ = '1';
    for (i = 0; i < 16; i++) { *p++ = ','; p = hx(p, (u32)g_b1r[(g_b1n + i) & 15]); }
    *p++ = ' '; *p++ = 'b'; *p++ = '2';
    for (i = 0; i < 8; i++) { *p++ = ','; p = hx(p, (u32)g_b2r[(g_b2n + i) & 7]); }
    *p++ = ' '; p = hx(p, (u32)be32p((volatile u8 *)(o + ODB_ROLLBACK_END_FRAME))); *p++ = ' ';
    p = hx(p, (u32)be32p((volatile u8 *)(o + ODB_STABLE_FINALIZED_FRAME))); *p++ = ' ';
    p = hx(p, (u32)be32p((volatile u8 *)(o + 0xADA))); *p++ = ' ';            /* volatile finalized */
    p = hx(p, (u32)be32p((volatile u8 *)(o + 0x3)));                           /* ODB_FRAME */
    *p++ = '\n'; *p = 0;
    OSReport(g_lb);
#endif
}

/* end of the logic of one engine iteration (04 bl at 0x801A4FF0) */
void s6n_end(void)
{
    u32 dt;
    if (!in_game()) return;
    FC_GT(FC_HOOK);                                              /* FRAME COST: end of the game's logic */
    RB[RB_PHASE] = 0x40;
    cost_wait(tbl() - g_it0);                                   /* S6D TEST ONLY cost emulation (logic) */
    g_in_logic = 0;                                             /* S6D SFX log */
    dt = tbl() - g_it0;
    if (rb_active && (s32)g_cur_frame < rb_e && (S12CFG & S12F_RSTIME)) {   /* S12: frame j of the rollback */
        s32 j = (s32)g_cur_frame - rb_g;
        u32 jj = j < 0 ? 0u : j > 7 ? 7u : (u32)j;
        ZL[Z_RS_SUM + jj] += dt; ZL[Z_RS_N + jj]++;
        if (dt > ZL[Z_RS_MAX + jj]) ZL[Z_RS_MAX + jj] = dt;
        g_rs_end_t = tbl(); g_rs_gap_have = 1; g_b1_rb_acc = 0;
    }
    if (rb_active && (s32)g_cur_frame < rb_e) RB[RB_RESIM_SUM] += dt;
    else { RB[RB_ITER_SUM] += dt; TL[T_LOGIC] = ema(TL[T_LOGIC], dt); }   /* S6C: logic cost estimate */
    g_stk_ring[g_cur_frame & 63u] = s12_stocks_now();            /* S12F_STOCK_KO */
    s10_iter_end(g_cur_frame);                                  /* S10: post-frame capture, EQ profile (before rb_finish) */
    g_last_end_t = tbl();                                       /* S12: render estimate */
    if (rb_active && (s32)g_cur_frame >= rb_e - 1) rb_finish();
    check_sscb(1);
    RB[RB_PHASE] = 0x41;
    FC_GT(FC_REND);                                              /* FRAME COST: render (or the next iteration: moved to ENG at its start) */
}

/* S6b: snapshot store for the match (first scene-loop pass of the online in-game scene) */
static u32 heap_free(void)
{
    u32 d = sd_hsd_desc(), c, n = 0, g = 0;
    if (!d) return 0;
    for (c = W(d + 4); ptr_like(c) && g++ < 10000; c = W(c + 4)) n += W(c + 8);
    return n;
}

/* ---- S19 store fit ----------------------------------------------
   HSD_MemAlloc ASSERTS when the HSD heap cannot satisfy a request: it never returns 0. So the
   engine never asks it for a block that does not fit: hsd_room walks the current heap's free list (OSAllocFromHeap first fit: a request
   of n bytes needs one free cell of at least round32(n + 0x20)) and safe_alloc only calls HSD_MemAlloc when the block fits AND `keep`
   bytes stay free for Melee.  The store is sized to what the heap has (S19_KEEP left to Melee: its own in-match allocations measured
   164-212 KB on the six legal stages, hard-quartile runs); when even S19_MIN_STORE does not fit, rollback is OFF for this match:
   prediction cap 0 (delay-only lockstep, every frame with confirmed inputs: nothing to roll back), status word RB_MEM, an OSReport line
   and (online image) a short on-screen notice.  In the match the heap is watched every frame: if Melee's own use brings the free heap
   under S19_LOW, prediction stops (cap 0), and once every predicted frame is confirmed the store is FREED back to Melee (rollback off).
   TEST ONLY flags: S12F_MEM_NOFIT (the store never fits), S12F_MEM_LOW (the low-heap release from match frame 1500). */
#define HSD_Free ((void (*)(void *))0x8037F1B0)
#define S12F_MEM_NOFIT 0x20000u          /* TEST ONLY (s12_flags): the store never fits: rollback off for every match (the visible fallback) */
#ifdef MM_RB_OFF                                  /* TEST BUILD only (RELEASE_TEST=rb_off): the store never fits, so every match runs without rollback (delay only) */
static volatile u32 g_nofit_now = 1u;          /* read at run time so the compiler keeps the rollback paths */
#else
static volatile u32 g_nofit_now = 0u;
#endif
#define S12_NOFIT_NOW (g_nofit_now != 0u)
#define S12F_HSD_EAT 0x2000000u          /* TEST ONLY (s12_flags): see store_fit */
#define S12F_MEM_LOW 0x40000u            /* TEST ONLY (s12_flags): from match frame S19_TEST_LOW_F the HSD heap counts as low (release path) */
#define S19_KEEP 0x48000u                /* HSD heap left to Melee after the store (288 KB; Melee's own in-match use <= 212 KB measured) */
#define S19_MIN_STORE 0x110000u          /* smaller store: rollback off (fixed tables ~350 KB compact + a mirror of 0.7 MB is the least that works) */
#define S19_TEST_KEEP 0x38000u           /* TEST ONLY tables after the store may take at most 64 KB of S19_KEEP (224 KB stay free) */
#define S19_SMALL_STORE 0x200000u        /* below 2 MB (Pokemon Stadium) the log share drops to S19_SHRUNK_LOG_PCT; a store shrunk less (FoD) keeps 32 % */
#define S19_MIRROR_TARGET 0x118000u       /* small store: mirror bytes kept (Stadium manifest peak 1.03 MB + S16 reserve ~56 KB + margin) */
#define S19_FIXED_SMALL 0x5B500u          /* the engine's fixed tables of a small store */
#define S19_PS_MIRROR 0x178000u          /* shrunk Stadium store: mirror bytes kept (two Ice Climbers: manifest high water 1.31 MB + S16 reserve 0.10 MB + margin) */
#define S19_BB_MIRROR 0x1B8000u          /* shrunk Big Blue store: same for two Ice Climbers (1.8 MB high water) */
#define S19_BB_ALLM_KEEP 0x80000u        /* Big Blue keeps 512 KB of AllM after the store (no transformation cache there; two Ice Climbers need the full 3.3 MB store, the 1 MB keep gave 2.76 MB) */
#define S19_SHRUNK_LOG_PCT 14u          /* least log share of a small store (default 32 %): the mirror must hold the match AND stay above the S16 reserve (a LOW gate mid-match diverges, S16 finding 3) */
#define S19_LOW 0xC000u                  /* in match: less free HSD heap than this -> release the store */
#define S19_TEST_LOW_F 1500u            /* S12F_MEM_LOW: the release is forced from this match frame on */
/* Big Blue memory pass: Big Blue's manifest does not fit the configured store's mirror (2P Ice Climbers: wants
   1.75 MB, has 1.54 MB; the 7 ranges left out are fighter animation buffers fp->x59C / x5A0, 0x8000 each, fighter_x59C_alloc_data, keyed and rewritten in the match, so no exclusion).
   Big Blue (Gr_Kind 0x13) asks for S19_BIGBLUE_EXTRA more; the fit rule (S19_KEEP, S19_MIN_STORE, S19d) is unchanged, and the log keeps the configured store's bytes (match_store) so the
   whole extra goes to the mirror.  Every other stage asks for s3_cfg.store_size exactly as before. */
#define S19_BIGBLUE_EXTRA 0x90000u       /* 576 KB: mirror 1.54 -> 2.11 MB (Big Blue 2P Ice Climbers, bspam: high water 1.81 MB = +16 %, see STAGE_SMOKE "Big Blue memory pass 2") */
/* Poke Floats memory pass: the stage's mirror high water reaches 1.506 - 1.514 MB of the configured
   1.541 MB arena by frame ~2,300; from then on free arena < sd_reserve (SD_H_LOW), the S16 gate holds prediction at 0 and the rest of the match is lockstep (no rollback, a stall per RTT).
   Poke Floats (Gr_Kind 0x11) asks for S19_FLOATS_EXTRA more, same rule as Big Blue. */
#define S19_FLOATS_EXTRA 0x40000u       /* 256 KB: mirror 1.54 -> 1.80 MB (+19 % over the 1.514 MB high water of 2P Ice Climbers; heap minimum stays about 2.0 MB) */
/* Mute City (Gr_Kind 0x12, stage sweep): mirror high water 1.518 - 1.520 MB of the 1.540 MB arena in a 3,500-frame match; the S16 gate closed (7,900 - 8,200 engine calls) and the match ran in lockstep (20 - 41 rollbacks).  Same extra as Poke Floats. */
#define S19_MUTECITY_EXTRA 0x40000u     /* 256 KB */
/* Venom (Gr_Kind 0x0F, PC soak hc-s1, STAGE_SWEEP doc): in 8:00 matches the mirror high water reached 1.45 - 1.50 MB of the 1.540 MB arena in 3 of 7 full-length runs and the S16 gate closed for 10,000 - 47,000 engine calls (lockstep).  Same extra as Mute City. */
#define S19_VENOM_EXTRA 0x40000u        /* 256 KB */
#define S19_ITEMS_EXTRA 0x80000u        /* 512 KB more for a Wild And Crazy match (items on, up to 39 item objects alive, 5-8 KB each): the soak closed the S16 gate on Brinstar, Rainbow Cruise, Green Greens, Mute City */
/* Pokemon Stadium (Gr_Kind 0x10, STAGE_SWEEP doc): mirror high water 1.4465 MB = 94 % of the 1.540 MB arena in 7 full-length PC runs while the gate closes near 1.50 MB; the transformations add objects, an 8:00 match with items can pass it.  Heap minimum with the store live is about 0.9 MB, so 256 KB more is affordable. */
#define S19_STADIUM_EXTRA 0x40000u      /* 256 KB */
#define S19_FIXED_FULL 0x6F200u          /* the engine's fixed tables of a full store (cells 8192, plan, statics, journal, events, extras: 455,168 B) */
enum { MEM_NONE, MEM_FULL, MEM_SHRUNK, MEM_NOFIT, MEM_INITFAIL, MEM_RELEASED, MEM_DRAIN, MEM_TOOSMALL };   /* MEM_TOOSMALL: the first base capture lost ranges (the mirror cannot hold the manifest): the S16 gate would hold lockstep silently for the whole match; now the store is drained and released like MEM_RELEASED and the notice shows */
#define MEM_MAGIC 0x4D450000u            /* 'ME' | state (low byte) | 0x100 TEST forced */
u32 s19_rb_off;                          /* nonzero while rollback is off for memory in this match (the online image's notice reads it) */
u32 s19_rb_info[4];                      /* S19d: for the notice: [0] free KB, [1] largest cell KB (both at the decision), [2] KB the engine needs, [3] state */
static u32 g_mem_forced, g_mem_rbc, g_mem_quiet, g_mem_reason;
/* ---- S19d: the store from AllM when the HSD heap is short (s19_fit.h s19_place, host-tested) ------------------------------
   The first real console match on Pokemon Stadium (RC1 a204e22e, Fox vs Kirby) ran rollback-off (MEM_NOFIT: Kirby's copy data and
   Stadium leave the HSD heap under S19_MIN_STORE + S19_KEEP).  AllM (lbHeap heap 4, type 2 = lbMemory best-fit heap, persistent for the online
   major scene, holds the preload cache) has about 4 MB free during a Stadium match and nothing allocates from it mid-match (S12A line).  With
   s12_flags S12F_STORE_ALLM the store comes from AllM whenever that gives a larger store than the HSD heap would (the HSD heap keeps it when it
   holds the configured size: nothing changes on stages with room).  Never an assert: the gap list is walked first (lbMemory_80014FC8 asserts
   without a fitting gap or a free handle), the block is freed only while the heap exists and still lists it.  Opt-in until the pair rig has
   proved it on Stadium. */
#define S12F_STORE_ALLM 0x200000u        /* s12_flags: the store may come from AllM (heap 4) when the HSD heap is short */
#define S19D_ALLM_KEEP 0x100000u         /* AllM free bytes left after the store (1 MB) */
#define LB_HEAP(i) (0x80431FA0u + 0x10u + 0x1Cu * (u32)(i))   /* lbHeap_80431FA0.heap_array[i]: id +0, handle +4, start +8, size +C, type +10, transient +14, status +18 (0 = created) */
#define LBMEM_FREE_HEAD (*(volatile u32 *)(0x804318B0u + 0x62Cu))   /* lbMemory_804318B0.free_mem: a handle for one more block */
#define LBMEM_ALLOC ((u32 (*)(u32, u32))0x80014FC8u)   /* lbMemory_80014FC8(heap handle, size) -> block {next +0, lo +4, size +8} */
#define LBMEM_FREE ((void (*)(u32, u32))0x800150F0u)   /* lbMemFreeToHeap(heap handle, block lo) */
static u32 g_store_allm, g_store_allm_h; /* the store is an AllM block; the heap handle it was taken from */
static int allm_room(u32 *total, u32 *big)   /* free gaps of AllM (heap 4): 0 = not usable (not created, not an lbMemory heap, no handle left) */
{
    u32 d = LB_HEAP(4), h, cur, it, n = 0, t = 0, b = 0;
    *total = 0; *big = 0;
    if (W(d + 0x18u) != 0u || W(d + 0x10u) != 2u) return 0;
    h = W(d + 4u);
    if (!ptr_like(h) || !LBMEM_FREE_HEAD) return 0;
    cur = W(h + 4u);                                             /* heap start; blocks are sorted by address, the heap handle's +8 is the end */
    it = W(h + 0xCu);
    for (;;) {
        u32 end = ptr_like(it) ? W(it + 4u) : W(h + 8u), gap = end > cur ? end - cur : 0u;
        t += gap;
        if (gap > b) b = gap;
        if (!ptr_like(it)) break;
        cur = W(it + 4u) + W(it + 8u);
        it = W(it);
        if (++n > 4096u) return 0;
    }
    *total = t; *big = b;
    return 1;
}
static int allm_lists(u32 h, u32 lo)         /* the heap still lists the block (lbMemFreeToHeap asserts otherwise) */
{
    u32 it = W(h + 0xCu), n = 0;
    while (ptr_like(it) && n++ < 4096u) { if (W(it + 4u) == lo) return 1; it = W(it); }
    return 0;
}
static void store_free(void)                 /* the store back to its heap: HSD_Free for the HSD heap; the AllM block only while that heap exists and lists it */
{
    if (g_store <= 1u) return;
    if (g_store_allm) {
        u32 d = LB_HEAP(4);
        if (W(d + 0x18u) == 0u && W(d + 4u) == g_store_allm_h && allm_lists(g_store_allm_h, g_store)) LBMEM_FREE(g_store_allm_h, g_store);
    } else HSD_Free((void *)g_store);
    g_store_allm = 0;
}
static void hsd_room(u32 *total, u32 *big)
{
    u32 d = sd_hsd_desc(), c, n = 0, b = 0, g = 0;
    if (d)
        for (c = W(d + 4); ptr_like(c) && g++ < 10000; c = W(c + 4)) {
            u32 sz = W(c + 8);
            n += sz;
            if (sz > b) b = sz;
        }
    *total = n; *big = b;
}
static void *safe_alloc(u32 n, u32 keep)
{
    u32 t, b;
    hsd_room(&t, &b);
    if (!n || s19_fit_bytes(t, b, keep) < n) return 0;
    return HSD_MemAlloc(n);
}
static void mem_note(u32 state, u32 a, u32 b)
{
#ifdef MS_ENGINE_REPORT
    char *p = g_lb;
#endif
    RB[RB_MEM] = MEM_MAGIC | (g_mem_forced ? 0x100u : 0u) | state;
#ifndef MS_ENGINE_REPORT
    (void)a; (void)b;
#else
    *p++ = 'S'; *p++ = '1'; *p++ = '2'; *p++ = 'M'; *p++ = ' ';
    p = hx(p, RB[RB_MEM]); *p++ = ' '; p = hx(p, GLOBAL_FRAME); *p++ = ' '; p = hx(p, a); *p++ = ' '; p = hx(p, b);
    *p++ = '\n'; *p = 0;
    OSReport(g_lb);
#endif
}
static void rb_off(u32 state, u32 a, u32 b)      /* rollback off for the rest of this match */
{
    sd_active = 0;
    rb_active = 0;
    s19_rb_off = state;
    RB[RB_MEM_OFF_F] = GLOBAL_FRAME;
    s19_rb_info[0] = a >> 10; s19_rb_info[1] = b >> 10; s19_rb_info[2] = (S19_MIN_STORE + S19_KEEP + S19_HDR) >> 10; s19_rb_info[3] = state;   /* S19d: the notice shows them */
    mem_note(state, a, b);
}
#ifdef MS_LAB_ITEMS_ALL
#define ITEMS_MATCH 1                            /* LAB (itemsall): every match of the pair rig has items, on the Slippi route too (rules_have is 0 there) */
#else
#define ITEMS_MATCH (C.rules_have && (C.rules.flags & S7R_ITEMS_ALL))
#endif
static u32 store_want(void)                      /* the store this match asks for (Big Blue / Poke Floats memory passes) */
{
    u32 gk = W(0x8049E6C8u + 0x88u);
    return s3_cfg.store_size + (gk == 0x13u ? S19_BIGBLUE_EXTRA : gk == 0x11u ? S19_FLOATS_EXTRA : gk == 0x12u ? S19_MUTECITY_EXTRA : gk == 0x0Fu ? S19_VENOM_EXTRA : gk == 0x10u ? S19_STADIUM_EXTRA : 0u)
           + ((ITEMS_MATCH && gk != 0x13u) ? S19_ITEMS_EXTRA : 0u);   /* Wild And Crazy: every item on; Big Blue has no heap left to give */
}
/* the store size for this match (0 = none fits) */
static u32 store_fit(void)
{
    u32 t, b;
    hsd_room(&t, &b);
    if (S12CFG & S12F_HSD_EAT) { t = t > 0x380000u ? t - 0x380000u : 0u; b = b > 0x380000u ? b - 0x380000u : 0u; }   /* TEST ONLY: the HSD heap looks 3.5 MB smaller to the fit (the console's heap is short, Dolphin's is not), so S19d triggers */
    RB[RB_MEM_FREE] = t; RB[RB_MEM_BIG] = b;
    if (S12_NOFIT_NOW || (S12CFG & S12F_MEM_NOFIT)) { g_mem_forced = 1; return 0; }   /* TEST ONLY */
    return s19_store_size(t, b, store_want(), S19_KEEP, S19_MIN_STORE);
}
/* every frame of the match: free HSD heap under S19_LOW -> stop predicting, then free the store (drain = every predicted frame confirmed) */
static void mem_watch(u32 f)
{
    u32 t, b;
    if (g_store <= 1u || s19_rb_off) return;
    hsd_room(&t, &b);
    if (!RB[RB_MEM_MIN] || t < RB[RB_MEM_MIN]) RB[RB_MEM_MIN] = t;
    if (!g_mem_drain) {
        int low = t < S19_LOW;
        if ((S12CFG & S12F_MEM_LOW) && f >= S19_TEST_LOW_F) { low = 1; g_mem_forced = 1; }   /* TEST ONLY */
        if (!low) return;
        g_mem_drain = 1;
        g_mem_rbc = RB[RB_COUNT]; g_mem_quiet = 0;
        RB[RB_MEM] = MEM_MAGIC | (g_mem_forced ? 0x100u : 0u) | MEM_DRAIN;
        return;                                                  /* be_inputs sets cap 0 from now on */
    }
    if (RB[RB_COUNT] != g_mem_rbc) { g_mem_rbc = RB[RB_COUNT]; g_mem_quiet = 0; return; }   /* a rollback ran: count again */
    {   /* a predicted frame is still unconfirmed: a rollback may come. keyed on the ASM's own predicting flag; the old test
           `C.rlatest < f` never passed in lockstep (the remote pad of the NEXT frame is not in yet at the loop top), so a drained store was
           never released (MEM_TOOSMALL stayed in state 6 on Stadium Sheik/Pikachu, s17i / s17im runs) */
        u32 o = odb();
        if (rb_active || (o && *(volatile u8 *)(o + ODB_SAVESTATE_IS_PREDICTING))) { g_mem_quiet = 0; return; }
    }
    if (++g_mem_quiet < 8u) return;                              /* 8 quiet lockstep passes (the S16 rebuild rule: rollback limit 7): every
                                                                    mispredicted frame has been rolled back before the store goes */
    rb_off(g_mem_reason ? g_mem_reason : MEM_RELEASED, t, f);
    store_free();                                                /* S19d: HSD or AllM */
    g_store = 1;                                                 /* tried for this match: never allocated again before the scene ends */
}
/* MEM_TOOSMALL: the first base capture lost ranges (the mirror of a shrunk store cannot hold this match's
   manifest; Stadium Sheik/Pikachu: 44 ranges, 284 KB).  The S16 gate then held lockstep for the whole match without a word.  Now the store is
   drained like the low-heap path and released with state MEM_TOOSMALL: the heap goes back to Melee and the online image's notice shows. */
static void s19_first_capture(s32 frame)
{
    if (!(sd_health() & SD_H_INC) || g_mem_drain || g_store <= 1u) return;
    g_mem_drain = 1; g_mem_reason = MEM_TOOSMALL; g_mem_rbc = RB[RB_COUNT]; g_mem_quiet = 0;
    RB[RB_MEM] = MEM_MAGIC | (g_mem_forced ? 0x100u : 0u) | MEM_DRAIN;
    hk_report(0x544F4F53u, frame, sd_x16.lost_ranges, sd_x16.lost_bytes, RB[RB_MEM_SIZE]);   /* "S6!K 544F4F53 frame lost_ranges lost_bytes store" */
    hk_report(0x4C535431u, frame, sd_lost_first[0], sd_lost_first[1], sd_lost_first[2]);       /* stage smoke "S6!K LST1 frame site lo bytes": the first range the store could not hold */
    hk_report(0x4D495252u, frame, sd_x16.arena_bytes, sd_x16.arena_hi, sd_x16.m_top);            /* "S6!K MIRR frame arena_bytes arena_hi m_top": the mirror arena the manifest had to fit in */
}
#ifdef MS_RB_READOUT
#include "on_bhold.h"   /* RBR_* word indices */
/* B0 console readout (LAB ONLY, MANIFEST option rbread): the numbers of the last match for
   the post-game CSS line (menus/on/on_bhold.c rbr_tick): store size and placement, capacity peaks (cells list, extras ring, journal, events,
   mirror high water), cell_overflow, lost_ranges, HSD and AllM free at the store decision and their minimum over the match (sampled every
   16 frames; the HSD heap is walked as mem_watch does, AllM as allm_room does).  Read-only: no engine input, no decision reads these words. */
u32 rbr_w[RBR_N];
#ifdef MS_RB_RUNTIME
u32 rbr_enable;          /* rbread=rt: the readout is compiled in and runs only while this is 1 (Settings > System > Rollback readout, the card's Options file, ml_glue.c settings_apply) */
#define RBR_ON() (rbr_enable)
#else
#define RBR_ON() 1u
#endif
/* fix (cube, real-client match): the peaks were read from sd_st / sd_x16 at the scene change, but a second, empty pass through the match
   scene (sd_init zeroes both) came first and the CSS showed C0 X0 J0 E0 A0.  Now the peaks are LATCHED into rbr_pk every 16 match frames (max-merge, a
   re-init cannot lower them) and published at the scene change only when the match took captures; an empty pass keeps the last record (RBR_EMPTY counts
   them).  Rollbacks and depths per match are deltas of RB_COUNT / RB_HIST (no new write in the rollback path). */
static u32 rbr_sd, rbr_hmin, rbr_amin, rbr_a0, rbr_caps, rbr_frames, rbr_cnt0, rbr_h0[8], rbr_pk[7];
/* B2 timing lines: the engine's own restore clocks of this match (sd_x17, zeroed by sd_init, monotonic while the
   store lives), latched with the peaks and printed as averages in microseconds: depth 1 and depth 2 restores (count, step 1, step 2 undo, whole
   restore) and the H plan / H stream per restore pass.  Read-only like the rest of the readout. */
static u32 rbr_tl[11];
static void rbr_ps_fill(u32 *w);                                  /* Stadium cache words (defined with the psc code below) */
static void rbr_start(void)
{
    u32 t, b, k;
    rbr_sd = 0; rbr_caps = 0; rbr_frames = 0;
    for (k = 0; k < 7u; k++) rbr_pk[k] = 0;
    for (k = 0; k < 11u; k++) rbr_tl[k] = 0;
    rbr_cnt0 = RB[RB_COUNT];
    for (k = 0; k < 8u; k++) rbr_h0[k] = RB[RB_HIST + k];
    hsd_room(&t, &b); rbr_hmin = t;
    if (!allm_room(&t, &b)) t = 0;
    rbr_a0 = t; rbr_amin = t;
}
static void rbr_latch(void)
{
    u32 v[7], k;
    if (!rbr_sd) return;
    v[0] = sd_st.cells_max; v[1] = sd_st.xring_max; v[2] = sd_st.jring_max; v[3] = sd_st.evt_max;   /* the order of RBR_CELLS .. RBR_ARENA_HI */
    v[4] = sd_st.cell_overflow; v[5] = sd_x16.lost_ranges; v[6] = sd_x16.arena_hi;
    for (k = 0; k < 7u; k++) if (v[k] > rbr_pk[k]) rbr_pk[k] = v[k];
    if (sd_st.captures > rbr_caps) rbr_caps = sd_st.captures;
    if (sd_x17.rest_n[1] + sd_x17.rest_n[2] >= rbr_tl[0] + rbr_tl[5]) {   /* B2 timing: the clocks of the live store (a re-init would restart them at 0) */
        rbr_tl[0] = sd_x17.rest_n[1]; rbr_tl[1] = sd_x17.step1_t[1]; rbr_tl[2] = sd_x17.undo_t[1]; rbr_tl[3] = sd_x17.rest_t[1];
        rbr_tl[5] = sd_x17.rest_n[2]; rbr_tl[6] = sd_x17.step1_t[2]; rbr_tl[7] = sd_x17.undo_t[2]; rbr_tl[8] = sd_x17.rest_t[2];
        rbr_tl[4] = sd_x17.h_passes; rbr_tl[9] = sd_x17.h_plan_sum; rbr_tl[10] = sd_x17.h_ticks_sum;
    }
}
static u32 rbr_mt(void)                                         /* time base ticks per ms: 40500 on a GameCube, 60750 on a Wii (the readout once divided by 40500 on both: Wii times were 1.5 times too long) */
{
    u32 mt = sd_plat_ms_ticks();
    return mt ? mt : 40500u;
}
static u32 rbr_us(u32 t, u32 n)                                   /* average of t over n in microseconds (time base ticks per ms from the OS clock word) */
{
    u32 mt = sd_plat_ms_ticks();
    if (!n) return 0;
    if (!mt) mt = 40500u;                                         /* GameCube bus 162 MHz / 4 */
    return (t / n) * 1000u / mt;
}
extern u32 g_ps_lastk, g_ps_lastf, g_ps_wms;
static u32 g_ft_prev, g_ft_max, g_ft_maxf, g_ft_n100, g_ft_n500, g_ft_k, g_ft_wms;
#ifdef MM_FTSTATS
/*  (LAB readout, only with -DMM_FTSTATS=1 in an rbread build): the frame period of the whole match as mean / median / p99 / max in 0.1 ms, how many frames took >= 20 ms (a missed 60 Hz frame
   is 33 ms), from a 0.2 ms histogram up to 51 ms.  The CSS line "FS" shows them.  Read only, like the rest of the readout. */
u32 rbr_mm[8];                                   /* mean, p50, p99, max (0.1 ms), frames >= 20 ms, frames counted */
static u32 g_fs_sum, g_fs_n, g_fs_slow, g_fs_max;
static unsigned short g_fs_h[256];
static void rbr_fs_reset(void) { u32 i; g_fs_sum = g_fs_n = g_fs_slow = g_fs_max = 0; for (i = 0; i < 256u; i++) g_fs_h[i] = 0; }
static void rbr_fs(u32 ticks)
{
    u32 d = ticks / (rbr_mt() / 10u), b = d / 2u;                          /* 0.1 ms units (40.5 MHz); histogram bins of 0.2 ms */
    g_fs_sum += d; g_fs_n++;
    if (d >= 200u) g_fs_slow++;
    if (d > g_fs_max) g_fs_max = d;
    g_fs_h[b > 255u ? 255u : b]++;
}
/*  (LAB readout, MM_FTSTATS only): where a LONG pass (>= SP_MIN_MS between two scene-loop passes) went.  The scene loop passes once per GAME frame: when B0 answers "skip" (the opponent's
   input is not here: TriggerSendInput.asm SKIP_INPUT) the pad handler returns early and the loop waits for the next vblank's pad, so a wait for the peer shows here as ONE long pass with S stalled B0 calls inside.
   The interval between two rbr_ft stamps is split with stamps at the start and the end of s3_tick_body: A = the rest of the previous body after its stamp (hub / gns / core slice / network), B = outside the
   body (the game: logic, B0, B1/B2 captures and restores, render, the vblank / pad wait), C = the part of this body before its stamp (match_store, mem_watch ...).  Z, K, R = the B0 time, the capture time and the
   restore time inside the interval, S = stalled B0 calls (the opponent's input was missing), Y = the time-sync part of them.  Times in 0.1 ms.  Events 0..3 = the first four passes of >= SP_MIN_MS, event 4 = the
   longest; each event is {match frame, interval, A, B, C, Z, K, R, S, Y}; rbr_sp[60] = the number of events, [61] = passes >= 500 ms.  Read only. */
#define SP_MIN_MS 60u
#define SP_W 12u                                 /* + Y2/O udp out and I udp in of the interval (Wii: vBBA status block words 12 / 13; zero on a GameCube) */
u32 rbr_sp[64];
u32 g_sp_b0;                                     /* cumulative time inside be_inputs (time base ticks) */
static u32 g_sp_bs, g_sp_b0p, g_sp_b1p, g_sp_rp, g_sp_sp, g_sp_yp, g_sp_uop, g_sp_uip;
static void rbr_net(u32 *uo, u32 *ui)               /* Wii only: the vBBA's udp out / in counters (status block words 12 / 13), 0 elsewhere */
{
    volatile u32 *sb = (volatile u32 *)0xD31BE000u;
    *uo = *ui = 0u;
    if (*(volatile u32 *)0x800000F8u > 200000000u && sb[126] == 0x56425854u) { *uo = sb[12]; *ui = sb[13]; }
}
static void rbr_sp_note(u32 frame, u32 t, u32 dt)
{
    u32 d = dt / (rbr_mt() / 10u), ev[SP_W], i, rt = sd_x17.rest_t[1] + sd_x17.rest_t[2];
    ev[0] = frame; ev[1] = d;
    ev[2] = (g_tick_end - g_ft_prev) / (rbr_mt() / 10u); ev[3] = (g_sp_bs - g_tick_end) / (rbr_mt() / 10u); ev[4] = (t - g_sp_bs) / (rbr_mt() / 10u);
    ev[5] = (g_sp_b0 - g_sp_b0p) / (rbr_mt() / 10u); ev[6] = (RB[RB_B1_SUM] - g_sp_b1p) / (rbr_mt() / 10u); ev[7] = (rt - g_sp_rp) / (rbr_mt() / 10u);
    ev[8] = ST.stall_calls - g_sp_sp; ev[9] = ST.sync_stalls - g_sp_yp;
    { u32 uo, ui; rbr_net(&uo, &ui); ev[10] = uo - g_sp_uop; ev[11] = ui - g_sp_uip; }
    if (dt >= 500u * rbr_mt()) rbr_sp[61]++;
    if (rbr_sp[60] < 4u) for (i = 0; i < SP_W; i++) rbr_sp[SP_W * rbr_sp[60] + i] = ev[i];
    rbr_sp[60]++;
    if (d > rbr_sp[4u * SP_W + 1u]) for (i = 0; i < SP_W; i++) rbr_sp[4u * SP_W + i] = ev[i];
}
u32 rbr_xs[8];                                  /* XE line: ops, total time (0.1 ms), average (us), longest (us), x_delay time (0.1 ms), imm ops, dma ops, selects */
/* IO line (WII_PERFORMANCE_STUDY_2026-11-04 part 2, Wii only, nothing on a GameCube): per-match deltas of the vBBA kernel's published status block (patch 0001 struct vg_status, words 0..127 at
   0xD31BE000): [0] sendto completions that took >= 50 ms (x_tx_hist buckets 3..7, words 105..109; the first version summed from word 104 = from 10 ms), [1] average sendto submit -> completion us (words 99 / 100), [2] ARM main-loop passes >= 5 ms,
   [3] longest pass ms since boot, [4] passes in the match (words 122 / 124 / 125: the NIN_MS_PASSHIST kernel; 0 on a kernel without it).  Read only, like the rest of the readout. */
u32 rbr_io[8];
static u32 rbr_io0[8];
static void gs_io_take(u32 *v);                                  /* below (ON_BUILD | MM_FTSTATS): the kernel words of the game record's IO block, shared with this line */
static void rbr_io_begin(void) { gs_io_take(rbr_io0); }
static void rbr_io_end(void)
{
    u32 v[8], n;
    gs_io_take(v);
    n = v[0] - rbr_io0[0];
    rbr_io[0] = v[2] - rbr_io0[2]; rbr_io[1] = n ? (v[1] - rbr_io0[1]) / n : 0u; rbr_io[2] = v[3] - rbr_io0[3]; rbr_io[3] = v[5]; rbr_io[4] = v[6] - rbr_io0[6];
}
static void rbr_fs_end(void)
{
    rbr_io_end();
    {   u32 mt = rbr_mt(), n = rbr_xe[0];
        rbr_xs[0] = n; rbr_xs[1] = rbr_xe[1] / (mt / 10u); rbr_xs[2] = n ? (rbr_xe[1] / n) * 1000u / mt : 0u; rbr_xs[3] = rbr_xe[2] * 1000u / mt;
        rbr_xs[4] = rbr_xe[3] / (mt / 10u); rbr_xs[5] = rbr_xe[4]; rbr_xs[6] = rbr_xe[5]; rbr_xs[7] = rbr_xe[6];
    }
    u32 i, acc = 0, p50 = 0, p99 = 0;
    for (i = 0; i < 256u; i++) {
        acc += g_fs_h[i];
        if (!p50 && acc * 2u >= g_fs_n) p50 = i * 2u;
        if (acc * 100u >= g_fs_n * 99u) { p99 = i * 2u; break; }
    }
    rbr_mm[0] = g_fs_n ? g_fs_sum / g_fs_n : 0u; rbr_mm[1] = p50; rbr_mm[2] = p99; rbr_mm[3] = g_fs_max; rbr_mm[4] = g_fs_slow; rbr_mm[5] = g_fs_n;
}
#endif
/* FRAME TIME (lab readout, FT line): the time between two B0 calls (one per match frame); the longest, the match frame it happened at, how many took >= 100 / >= 500 ms, the Stadium request kind near it,
   the longest ps_wait.  Read only. */
u32 rbr_pr[16];                                /* PR line: peer ip, port, mean / min / max ENet round trip (ms) over the match, samples; [12..15] the slip tracker of rbr_ds (J D M); [6..11] the DS line (the Slippi ASM's desync detection, below) */
static u32 g_pr_sum, g_pr_n;
static u32 ds_u32(u32 a) { volatile const u8 *b = (volatile const u8 *)a; return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | b[3]; }
/* DS line (the Wii-vs-Cube Pokemon Stadium desyncs): the frame at which the ASM's checksum compare (StartEngineLoop.asm HARD_DESYNC_DETECTED) raised ODB_IS_DESYNC_STATE_DISPLAYED, first time in the match.
   F our match frame then, O ODB_FRAME, Z ODB_STABLE_FINALIZED_FRAME, R the remote checksum entry's frame (RXB_OPNT_DESYNC_ENTRY[0], the frame that did not agree), C its checksum, L our local entry for that frame (0 = not in the ring).
   The two consoles' lines side by side date the divergence (R) and show whether the sums differ.  Offsets from third_party/slippi-ssbm-asm Online/Online.s: ODB flag +18, frame +3, stable finalized +2761, RXB address +219, local entry ring +2791 (21 x 24 bytes); RXB desync entry +2 (frame, checksum).  Read only. */
static void rbr_ds(u32 frame, u32 reset)
{
    u32 o, rx, i, rf;
    static int g_ds_base;
    if (reset) { rbr_pr[6] = rbr_pr[7] = rbr_pr[8] = rbr_pr[9] = rbr_pr[10] = rbr_pr[11] = 0; rbr_pr[12] = rbr_pr[13] = rbr_pr[14] = rbr_pr[15] = 0; g_ds_base = 0x7FFFFFFF; }
    o = odb();
    /* SLIP tracker: the ASM's frame
       (ODB_FRAME, the number inputs and checksums are tagged with) minus our match frame, relative to its value at the first sample.  J = the first match frame where it differed, D = the difference then
       (negative: 1000 + the size), M = the largest size seen.  Both consoles' J / D side by side date a frame-numbering slip. */
    if (o && frame >= 3u) {
        int dl = (int)ds_u32(o + 3u) - (int)frame, df;
        if (g_ds_base == 0x7FFFFFFF) g_ds_base = dl;
        df = dl - g_ds_base;
        if (df && !rbr_pr[12]) { rbr_pr[12] = frame; rbr_pr[13] = df > 0 ? (u32)df : 1000u + (u32)(-df); }
        if ((u32)(df < 0 ? -df : df) > rbr_pr[14]) rbr_pr[14] = (u32)(df < 0 ? -df : df);
    }
    if (rbr_pr[6]) return;
    if (!o || !*(volatile u8 *)(o + 18u)) return;
    rbr_pr[6] = frame ? frame : 1u; rbr_pr[7] = ds_u32(o + 3u); rbr_pr[8] = ds_u32(o + 2761u);
    rx = ds_u32(o + 219u);
    if (rx >= 0x80000000u && rx < 0x81800000u) { rf = ds_u32(rx + 2u); rbr_pr[9] = rf; rbr_pr[10] = ds_u32(rx + 6u); }
    else rf = 0;
    for (i = 0; i < 21u; i++) if (rf && ds_u32(o + 2791u + 24u * i) == rf) { rbr_pr[11] = ds_u32(o + 2791u + 24u * i + 4u); break; }
}
static void rbr_pr_sample(u32 reset)
{
    u32 r;
    if (reset) { g_pr_sum = g_pr_n = 0; rbr_pr[2] = rbr_pr[3] = rbr_pr[4] = 0; }
    rbr_pr[0] = C.dst_ip; rbr_pr[1] = C.dst_port;
    if (C.active < 0) return;
    r = C.enet.peer[C.active].rtt;
    g_pr_sum += r; g_pr_n++;
    if (!rbr_pr[3] || r < rbr_pr[3]) rbr_pr[3] = r;
    if (r > rbr_pr[4]) rbr_pr[4] = r;
    rbr_pr[2] = g_pr_sum / g_pr_n; rbr_pr[5] = g_pr_n;
}
static void rbr_ft(u32 frame)
{
    u32 t = tbl(), dt;
    if (frame <= 2u) { g_ft_prev = 0; g_ft_max = g_ft_maxf = g_ft_n100 = g_ft_n500 = g_ft_k = 0; g_ps_wms = 0;
#ifdef MM_FTSTATS
        rbr_fs_reset(); rbr_io_begin();
        { u32 i; for (i = 0; i < 8u; i++) rbr_xe[i] = 0; }
        { u32 i; for (i = 0; i < 64u; i++) rbr_sp[i] = 0; }
#endif
    }
    if (g_ft_prev && frame > 2u) {
#ifdef MM_FTSTATS
        rbr_fs(t - g_ft_prev);
        rbr_pr_sample(frame == 3u);
        rbr_ds(frame, frame == 3u);
        if (t - g_ft_prev >= SP_MIN_MS * rbr_mt()) rbr_sp_note(frame, t, t - g_ft_prev);
#endif
        dt = (t - g_ft_prev) / rbr_mt();
        if (dt >= 100u) g_ft_n100++;
        if (dt >= 500u) g_ft_n500++;
        if (dt > g_ft_max) { g_ft_max = dt; g_ft_maxf = frame; g_ft_k = (frame >= g_ps_lastf ? frame - g_ps_lastf : g_ps_lastf - frame) <= 4u ? g_ps_lastk : 0u; }
    }
    g_ft_prev = t;
#ifdef MM_FTSTATS
    g_sp_b0p = g_sp_b0; g_sp_b1p = RB[RB_B1_SUM]; g_sp_rp = sd_x17.rest_t[1] + sd_x17.rest_t[2]; g_sp_sp = ST.stall_calls; g_sp_yp = ST.sync_stalls; rbr_net(&g_sp_uop, &g_sp_uip);
#endif
    g_ft_wms = g_ps_wms;
}
static void rbr_watch(void)
{
    u32 t, b;
    rbr_ft(GLOBAL_FRAME);
    rbr_frames++;
    if (GLOBAL_FRAME & 15u) return;
    rbr_latch();
    hsd_room(&t, &b); if (t < rbr_hmin) rbr_hmin = t;
    if (allm_room(&t, &b) && t < rbr_amin) rbr_amin = t;
}
static void rbr_end(void)
{
    u32 k, mk = 0;
    rbr_latch();
    if (rbr_w[RBR_MATCH] && !rbr_pr[6] && (rbr_frames < 120u || (rbr_sd && !rbr_caps))) { rbr_w[RBR_EMPTY]++; return; }   /* an empty pass (< 2 s in the match scene, or a store that never captured): keep the last match's record.  NOT when the Slippi ASM's desync flag fired in this match (DS line, rbr_pr[6]): a game that desyncs in its first 2 s is exactly the record we need */
    rbr_w[RBR_MATCH]++;
#ifdef MM_FTSTATS
    rbr_fs_end();
#endif
    rbr_w[RBR_FT_MS] = g_ft_max; rbr_w[RBR_FT_F] = g_ft_maxf; rbr_w[RBR_FT_N] = g_ft_n100 | (g_ft_n500 << 16); rbr_w[RBR_FT_K] = g_ft_k | (g_ft_wms << 8);
    rbr_ps_fill(&rbr_w[RBR_PM0]); rbr_w[RBR_PSCALLS] = 0;           /* Stadium cache state at the match end, before psc_free runs in the CSS */
    rbr_w[RBR_STAGE] = W(0x8049E6C8u + 0x88u);
    rbr_w[RBR_STORE] = rbr_sd ? RB[RB_MEM_SIZE] : 0u;
    rbr_w[RBR_MEM] = RB[RB_MEM] & 0xFFu;
    rbr_w[RBR_PLACE] = !rbr_sd ? 0u : (RB[RB_MEM] & 0x200u) ? 2u : 1u;
    for (k = 0; k < 7u; k++) rbr_w[RBR_CELLS + k] = rbr_pk[k];
    rbr_w[RBR_HSD0] = RB[RB_MEM_FREE]; rbr_w[RBR_HSDMIN] = rbr_hmin;
    rbr_w[RBR_ALLM0] = rbr_a0; rbr_w[RBR_ALLMMIN] = rbr_amin;
    rbr_w[RBR_RB] = RB[RB_COUNT] - rbr_cnt0;
    for (k = 0; k < 8u; k++) { rbr_w[RBR_H0 + k] = RB[RB_HIST + k] - rbr_h0[k]; if (rbr_w[RBR_H0 + k]) mk = k; }
    rbr_w[RBR_MAXK] = mk;
    rbr_w[RBR_T1N] = rbr_tl[0]; rbr_w[RBR_T1S] = rbr_us(rbr_tl[1], rbr_tl[0]); rbr_w[RBR_T1U] = rbr_us(rbr_tl[2], rbr_tl[0]); rbr_w[RBR_T1R] = rbr_us(rbr_tl[3], rbr_tl[0]);
    rbr_w[RBR_T2N] = rbr_tl[5]; rbr_w[RBR_T2S] = rbr_us(rbr_tl[6], rbr_tl[5]); rbr_w[RBR_T2U] = rbr_us(rbr_tl[7], rbr_tl[5]); rbr_w[RBR_T2R] = rbr_us(rbr_tl[8], rbr_tl[5]);
    rbr_w[RBR_TP] = rbr_us(rbr_tl[9], rbr_tl[4]); rbr_w[RBR_TH] = rbr_us(rbr_tl[10], rbr_tl[4]);
}
#define RBR_START() do { if (RBR_ON()) rbr_start(); } while (0)
#define RBR_WATCH() do { if (RBR_ON() && in_game()) rbr_watch(); } while (0)
#define RBR_END() do { if (RBR_ON()) rbr_end(); } while (0)
#define RBR_SD_OK() (rbr_sd = 1u)
#else
#define RBR_START() ((void)0)
#define RBR_WATCH() ((void)0)
#define RBR_END() ((void)0)
#define RBR_SD_OK() ((void)0)
#endif
#if defined(ON_BUILD) || (defined(MS_RB_READOUT) && defined(MM_FTSTATS))
/* The Wii vBBA kernel's published status block (patch 0001 struct vg_status, 0xD31BE000, 'VBXT' at word 126, ext_ver word 80 >= 1): [0] sendto completions (word 99), [1] their summed submit -> completion
   us (100), [2] completions >= 50 ms (x_tx_hist buckets 3..7 = words 105..109), [3] ARM main-loop passes >= 5 ms (122), [4] >= 20 ms (123), [5] the longest pass in ms since boot (124), [6] passes (125),
   [7] 1 = the block was there.  Words 122..125 are written only by a NIN_MS_PASSHIST kernel (q34ph), 0 otherwise.  All 0 on a GameCube / Dolphin (the bus clock test comes first: the address is never read
   there).  Cumulative since boot: the users take deltas (game record IO block, readout IO line).  Eight uncached loads. */
static void gs_io_take(u32 *v)
{
    volatile u32 *sb = (volatile u32 *)0xD31BE000u;
    u32 k;
    for (k = 0; k < 8u; k++) v[k] = 0;
    if (*(volatile u32 *)0x800000F8u <= 200000000u || sb[126] != 0x56425854u || sb[80] < 1u) return;
    v[0] = sb[99]; v[1] = sb[100];
    for (k = 105u; k < 110u; k++) v[2] += sb[k];
    v[3] = sb[122]; v[4] = sb[123]; v[5] = sb[124]; v[6] = sb[125]; v[7] = 1u;
}
#endif
#ifdef ON_BUILD
/* GAME_RECORD facts of the match in the match scene (major 8, minor 2): baselines at entry, a snapshot at exit (the record is built right after the game, before or after the scene change) */
static u32 g_gs_n, g_gs_in, g_gs_c0, g_gs_h0[8], g_gs_snap[12];
static u32 g_gs_st0, g_gs_tp, g_gs_o17;                         /* WII_PERFORMANCE_STUDY_2026-11-04 (record words 6 / 7, both consoles): stalled B0 calls at match entry, the time base of the previous pass, passes whose period exceeded 17 ms */
static u32 g_gs_go, g_gs_go_f, g_gs_go_st, g_gs_go_rb;         /* the game-over capture (od_core_go): 1 once the game's own flag (ODB_IS_GAME_OVER) was seen in this match scene; the ODB frame, the stalled B0 calls and the rollbacks at that moment */
static u32 g_gs_pqh0, g_gs_pq[3];                               /* PQ block (od_core_pq): guard stalls at match entry; snapshot: ovf, max, guard stalls of the match */
static u32 g_gs_slip84, g_gs_slipgo;                            /* PQ block: slip sampled at match frame 84 and at the game-over flag, (u8) value | 0x100 once taken */
static s32 pq_slip(void)                                         /* ODB_FRAME - 1 - global frame - queue count: a constant on an aligned console (the pair rig read -1 at its iteration hook on both sides), +1 per dropped queue entry (rig: -1 -> +1 after 2 drops, -> +6 after 7); interrupts off: the hook may push between the reads */
{
    u32 m = irq_off(), o = odb();
    s32 v = o ? (s32)be32p((volatile u8 *)(o + S12_ODB_FRAME)) - 1 - (s32)GLOBAL_FRAME - (s32)PQ_COUNT : 0;
    irq_restore(m);
    return v < -127 ? -127 : v > 126 ? 126 : v;
}
static u32 g_gs_io0[8], g_gs_io1[8];                            /* the kernel words (gs_io_take) at match entry and at the last snapshot: the record's IO block (od_core_io) */
static void gs_fill(u32 *w)
{
    u32 k, n = RB[RB_COUNT] - g_gs_c0, sum = 0, mx = 0, acc = 0, p95 = 0, tot = g_gs_rtt_n, st = ST.stall_calls - g_gs_st0;
    for (k = 0; k < 8u; k++) { u32 h = RB[RB_HIST + k] - g_gs_h0[k]; sum += h * k; if (h) mx = k; }
    w[0] = 1u; w[1] = n; w[2] = mx; w[3] = n ? sum * 100u / n : 0u;
    w[4] = tot ? g_gs_rtt_sum / tot : 0u;
    for (k = 0; k < 16u && tot; k++) { acc += g_gs_rtt_h[k]; if (acc * 100u >= tot * 95u) { p95 = (k + 1u) * 16u; break; } }
    w[5] = p95;
    w[6] = st > 65535u ? 65535u : st;                           /* stall frames = B0 calls that waited for the peer's pads (s7_core.c stall_calls, one per video frame while stalled) */
    w[7] = g_gs_o17 > 0xD500u ? 0xD500u : g_gs_o17;             /* "frames over 17 ms" (the record's name) = scene-loop passes whose period exceeded 25 ms since, 17 ms before (one pass per game frame; gs_tick); capped below the DESYNC LOCATOR marker 0xD501 (on_backend.c gr_record, DZ_MARK) */
    w[8] = g_sd_end ? g_sd_end : g_gs_delay;                    /* the delay in force at the end: the last raise's value, else the one latched at B3 */
    gs_io_take(g_gs_io1);
    g_gs_pq[0] = g_pq_ovf; g_gs_pq[1] = g_pq_max; g_gs_pq[2] = C.pq_holds - g_gs_pqh0;
}
static void gs_tick(u32 sc)
{
    u32 k;
    if ((sc >> 24) == 8u && (sc & 0xFFu) == 2u) {
        u32 t = tbl();
        if (!g_gs_in) {
            g_gs_in = 1; g_gs_c0 = RB[RB_COUNT]; g_gs_rtt_n = g_gs_rtt_sum = 0;
            for (k = 0; k < 8u; k++) g_gs_h0[k] = RB[RB_HIST + k];
            for (k = 0; k < 16u; k++) g_gs_rtt_h[k] = 0;
            g_sd_end = 0; sd_why[6] = 0; g_sd_nr = 0; g_sd_lr = 0; g_sd_a0 = g_sd_auto; g_gs_d0 = g_gs_delay; g_gs_st0 = ST.stall_calls; g_gs_o17 = 0; g_gs_tp = 0; g_gs_go = 0;
            g_pq_ovf = 0; g_pq_max = 0; g_gs_pqh0 = C.pq_holds; g_gs_slip84 = 0; g_gs_slipgo = 0;
            gs_io_take(g_gs_io0);
        }
        if (g_gs_tp) { u32 mt = sd_plat_ms_ticks(); if (!mt) mt = 40500u; if (t - g_gs_tp > 25u * mt) g_gs_o17++; }   /* 25 ms (one and a half frame periods = a picture was really repeated), was 17 ms: the game waits for the retrace, so a real slow frame takes 33 ms, and 17 ms counted the ordinary jitter of where in the pass this runs (a Wii showed 40 % in games with almost no stalls on either side); the record field keeps its name */   /* 40500 ticks per ms on a GameCube, 60750 on a Wii (finding 30) */
        g_gs_tp = t;
        if (!g_gs_go) {                                          /* the first pass that sees the game's own game-over flag: later passes (results, CSS) keep counting frames, the record must not */
            u32 o = odb();
            if (o && *(volatile u8 *)(o + S12_ODB_IS_GAME_OVER)) {
                s32 gf = (s32)be32p((volatile u8 *)(o + S12_ODB_FRAME));
                g_gs_go = 1; g_gs_go_f = gf > 0 ? (u32)gf : 1u; g_gs_go_st = ST.stall_calls - g_gs_st0; g_gs_go_rb = RB[RB_COUNT] - g_gs_c0;
                g_gs_slipgo = 0x100u | (u8)pq_slip();
                if (g_sd_a0) { g_car_ip = C.dst_ip; g_car_port = C.dst_port; g_car_d = g_sd_end ? g_sd_end : g_gs_d0; g_car_calm = sdc_calm(g_gs_go_st, g_gs_go_f, g_sd_nr, g_sd_lr); }   /* an Auto game ended: the next game of this set starts from here (pace_delay_cb) */
                else g_car_d = 0;
            }
        }
        if (!g_gs_slip84 && GLOBAL_FRAME >= 84u) g_gs_slip84 = 0x100u | (u8)pq_slip();   /* frame 84: after the start freeze (tags 78+), before the first possible input */
        if ((++g_gs_n & 7u) == 0u) gs_fill(g_gs_snap);          /* a few dozen instructions every 8th pass; the exit below takes the final values */
    } else if (g_gs_in) { gs_fill(g_gs_snap); g_gs_in = 0; if (!g_gs_go) g_car_d = 0; }   /* a game that ended without the game-over flag (blackout, disconnect, abort) carries no delay into the next one */    /* the snapshot stays until the next match */
}
/* the facts of the newest match (hub.ovl's game record): 9 words, [0] = 1 when valid */
void od_core_stats(u32 *w)
{
    u32 k;
    for (k = 0; k < 12u; k++) w[k] = g_gs_snap[k];
    w[10] = ST.dz_first; w[11] = ST.dz_lcks;                /* DESYNC LOCATOR (read live: the status words stay until the next game's first frame): the first mismatching checksum frame (0 = none) and our checksum of it */
}
/* The GAME_RECORD's trailing IO block of the newest match ( "IO block", ms_records.IO: the Wii counters go to the server, not on the screen):
   23 bytes, big-endian: id 1, length 21, flags (1 the kernel status block was there, 2 its pass words are written = a NIN_MS_PASSHIST kernel, 4 the longest pass grew during the match), sendto completions u32,
   of them >= 50 ms u16, average sendto us u32, ARM passes >= 5 ms u16, >= 20 ms u16, longest pass ms since boot u16, passes u32.  flags 0 = no data (GameCube, Dolphin, a kernel without the block):
   the counters are 0.  Called by the resident hub's core_stats (hub_res.c) right before the overlay builds the record. */
static void gs_p32(unsigned char *p, u32 v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static void gs_p16(unsigned char *p, u32 v) { if (v > 65535u) v = 65535u; p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
void od_core_io(unsigned char *p)
{
    const u32 *a = g_gs_io0, *b = g_gs_io1;
    u32 n = b[0] - a[0], f = 0;
    if (b[7] && a[7]) f = 1u | (b[6] ? 2u : 0u) | (b[5] > a[5] ? 4u : 0u);
    if (!f) n = 0;
    p[0] = 1; p[1] = 21; p[2] = (unsigned char)f;
    gs_p32(p + 3, n); gs_p16(p + 7, f ? b[2] - a[2] : 0u); gs_p32(p + 9, n ? (b[1] - a[1]) / n : 0u);
    gs_p16(p + 13, f ? b[3] - a[3] : 0u); gs_p16(p + 15, f ? b[4] - a[4] : 0u); gs_p16(p + 17, f ? b[5] : 0u); gs_p32(p + 19, f ? b[6] - a[6] : 0u);
}
/* The GAME_RECORD's trailing game-over block (ms_records.GO: frames / final_frame keep counting on the results and CSS screens, so the record could not say
   how long the GAME was): 10 bytes, big-endian: id 2, length 8, game_over_frame u32 (ODB_FRAME when the game-over flag first set; Slippi counts from -123, so about 60 per second of play plus 123), stall frames u16 and
   rollbacks u16 counted from the match scene entry to that moment (saturated).  byte 0 = 0 (no block) when the flag was never seen in the newest match scene.  Called by the resident hub's core_stats (hub_res.c). */
void od_core_go(unsigned char *p)
{
    u32 k;
    for (k = 0; k < 10u; k++) p[k] = 0;
    if (!g_gs_go) return;
    p[0] = 2; p[1] = 8;
    gs_p32(p + 2, g_gs_go_f); gs_p16(p + 6, g_gs_go_st); gs_p16(p + 8, g_gs_go_rb);
}
/* The GAME_RECORD's trailing PQ block (ms_records.PQ): 9 bytes: id 3, length 7, pq_ovf u8 (accepted hook
   frames that met a full pad queue = frames the game dropped; must be 0 with the guard), pq_max u8 (largest queue count at B0), lag_hold u16 (B0 calls stalled by the guard),
   slip s8 at match frame 84, slip s8 at the game-over flag (ODB_FRAME - 1 - global frame - queue count; 0x7F = not sampled), dz_shift s8 (at the first checksum mismatch: the k in
   -3..+3 whose stored local checksum of frame F + k equals the peer's checksum of F; 0 = no mismatch, 0x7F = none matched).  Called by the resident hub's core_stats (hub_res.c). */
void od_core_pq(unsigned char *p)
{
    p[0] = 3; p[1] = 7;
    p[2] = (unsigned char)(g_gs_pq[0] > 255u ? 255u : g_gs_pq[0]); p[3] = (unsigned char)(g_gs_pq[1] > 255u ? 255u : g_gs_pq[1]); gs_p16(p + 4, g_gs_pq[2]);
    p[6] = g_gs_slip84 ? (unsigned char)g_gs_slip84 : 0x7Fu; p[7] = g_gs_slipgo ? (unsigned char)g_gs_slipgo : 0x7Fu;
    p[8] = (unsigned char)(ST.dz_first ? ST.dz_shift : 0u);
}
#define GS_TICK(sc) gs_tick(sc)
#else
#define GS_TICK(sc) ((void)0)
#endif
static void match_store(void)
{
    u32 sc = SCENE_WORD, sz;
    GS_TICK(sc);
    if ((sc >> 24) != 8 || (sc & 0xFFu) != 2) {
        if (g_store) {
#ifdef SD_LAB_HG
            if (g_store > 1u) {   /* LAB ONLY ( B): engine writes between the end of the match scene and this first pass of the next one.
                                     "S6!K HGON detections log_entries events scene_word" and "S6!K HGRG store lo hi live_cells": live_cells = allocated cells of the
                                     (re-created) HSD heap that overlap the written range [lo, hi) */
                extern u32 sd_hg_w[8];
                u32 d = sd_hsd_desc(), c, n = 0, k = 0, lo = sd_hg_w[3], hi = sd_hg_w[4];
                if (d && hi > lo)
                    for (c = W(d + 8); ptr_like(c) && k++ < 10000u; c = W(c + 4))
                        if (c < hi && c + W(c + 8) > lo) n++;
                hk_report(0x48474F4Eu, (s32)sd_hg_w[6], sd_hg_w[1], sd_hg_w[2], sd_hg_w[5]);
                hk_report(0x48475247u, (s32)g_store, lo, hi, n);
                for (k = 0; k < 8u; k++) sd_hg_w[k] = 0;
            }
#endif
            RBR_END();                                           /* B0 readout (lab builds only) */
            if (g_store_allm) store_free();                      /* S19d: AllM is persistent for the major scene: the block must go back (the HSD heap dies with the scene, never freed here) */
            sd_active = 0; g_store = 0; rb_active = 0; g_dbg = 0; g_sscb_reports = 0; g_sfx = 0; g_sfx_on = 0;
        }
        psc_free();                                              /* S19e: the AllM cache blocks of the Stadium transformation files go back with the match */
        g_store_allm = 0;
        s19_rb_off = 0; g_mem_drain = 0; g_mem_forced = 0; g_mem_reason = 0; g_ps_switch = 0;   /* S19: per match */
        return;
    }
    if (g_store || LOADING != 0 || !s3_cfg.rb_depth || !s3_cfg.store_size) return;
    if (!s7_core_rollback_allowed(&C)) return;                   /* DELAY-ONLY LOBBIES: 3-4 human seats = lockstep, no snapshot store is allocated (the core also forces depth 0) */
    RB[RB_HEAP_BEFORE] = heap_free();
    RB[RB_MEM_MIN] = 0; RB[RB_MEM_OFF_F] = 0;
    RBR_START();                                                 /* B0 readout (lab builds only) */
    sz = store_fit();                                            /* S19: never ask HSD_MemAlloc for a block that does not fit */
    g_store_allm = 0;
    if ((S12CFG & S12F_STORE_ALLM) && !S12_NOFIT_NOW && !(S12CFG & S12F_MEM_NOFIT) && sz < (store_want() & ~31u)) {   /* S19d */
        u32 at, ab, s2 = 0;
        if (allm_room(&at, &ab) &&
            s19_place(RB[RB_MEM_FREE], RB[RB_MEM_BIG], at, ab, store_want(), S19_KEEP, S19_MIN_STORE, (W(0x8049E6C8u + 0x88u) == 0x10u ? 0x170000u : W(0x8049E6C8u + 0x88u) == 0x13u ? S19_BB_ALLM_KEEP : S19D_ALLM_KEEP) /* S19e: on Stadium 1.44 MB of AllM stay free for the transformation-file cache (1.12 MB + 256 KB) */, 1, &s2) == 4) {
            u32 h = W(LB_HEAP(4) + 4u), blk = LBMEM_ALLOC(h, s2);
#ifdef MS_ENGINE_REPORT
            char *p = g_lb;
#endif
            g_store = ptr_like(blk) ? W(blk + 4u) : 0u;
            if (g_store) { g_store_allm = 1; g_store_allm_h = h; sz = s2; }
#ifdef MS_ENGINE_REPORT
            *p++ = 'S'; *p++ = '1'; *p++ = '2'; *p++ = 'N'; *p++ = ' ';   /* "S12N store size allm_free allm_big hsd_free": the store came from AllM */
            p = hx(p, g_store); *p++ = ' '; p = hx(p, s2); *p++ = ' '; p = hx(p, at); *p++ = ' '; p = hx(p, ab); *p++ = ' '; p = hx(p, RB[RB_MEM_FREE]);
            *p++ = '\n'; *p = 0;
            OSReport(g_lb);
#endif
        }
    }
    RB[RB_MEM_SIZE] = sz;
    if (!g_store_allm) g_store = sz ? (u32)HSD_MemAlloc(sz) : 0u;
    RB[RB_HEAP_AFTER] = heap_free();
    RB[RB_STORE] = g_store;
    if (!g_store) { RB[RB_ALLOC_FAIL]++; g_store = 1; rb_off(MEM_NOFIT, RB[RB_MEM_FREE], RB[RB_MEM_BIG]); return; }   /* 1 = tried for this match */
    RB[RB_ALLOCS]++;
    patch_lsfail();
    patch_sfconv();
    { extern u32 *sd_crumb; sd_crumb = (u32 *)&RB[RB_OSA_LAST]; }
    sd_melee_init();
    { extern void s10_match_start(void); s10_match_start(); }   /* S10 */
    sd_mc.flags = s3_cfg.sd_flags & 0xFFu;
    sd_mc.deny = s3_cfg.deny;
    sd_verify = (s3_cfg.sd_flags >> 8) & 1u;
    {   /* S11: fingerprint capture options (read by sd_init / every capture) */
        extern void s11_match_start(void);
        s11_match_start();
    }
    if (sz < S19_SMALL_STORE && !s11_cfg.s16_log_pct) {          /* S19: a small store: the mirror gets S19_MIRROR_TARGET, the log the rest */
        u32 rest = sz > S19_FIXED_SMALL ? sz - S19_FIXED_SMALL : 0u, pct = S19_SHRUNK_LOG_PCT;
        if (rest > S19_MIRROR_TARGET) pct = (rest - S19_MIRROR_TARGET) / (rest / 100u + 1u);
        sd_log_pct = pct < S19_SHRUNK_LOG_PCT ? S19_SHRUNK_LOG_PCT : pct > 32u ? 32u : pct;
    }
    else if ((W(0x8049E6C8u + 0x88u) == 0x10u || W(0x8049E6C8u + 0x88u) == 0x13u) && sz < (store_want() & ~31u) && !s11_cfg.s16_log_pct) {   /*, Ice Climbers on Stadium / Big Blue: the AllM store is cut (Stadium 2.3 MB by the transformation-cache keep), at the default 32 % log the mirror (1.26 MB) is below the manifest high water (1.31 MB) + S16 reserve (0.10 MB), the LOW gate holds and the match plays in lockstep: the mirror gets S19_PS_MIRROR / S19_BB_MIRROR, the log the rest (about 20 %) */
        u32 fx = sz < 0x280000u ? S19_FIXED_SMALL : S19_FIXED_FULL, rest = sz > fx ? sz - fx : 0u, pct = 32u;
        u32 mt = W(0x8049E6C8u + 0x88u) == 0x13u ? S19_BB_MIRROR : S19_PS_MIRROR;
        if (rest > mt) pct = (rest - mt) / (rest / 100u + 1u);
        sd_log_pct = pct < S19_SHRUNK_LOG_PCT ? S19_SHRUNK_LOG_PCT : pct > 32u ? 32u : pct;
    }
    else if (sz > (s3_cfg.store_size & ~31u) && !s11_cfg.s16_log_pct) {   /* Big Blue memory pass: a store above the configured size keeps the configured log (32 % of it, rounded up) */
        u32 r0 = (s3_cfg.store_size & ~31u) - S19_FIXED_FULL, r = sz - S19_FIXED_FULL;
        sd_log_pct = (32u * r0 + r - 1u) / r;
    }
    if (sd_init(g_store, sz) != 0) {                             /* S19: no engine -> no prediction, and the block goes back to Melee */
        RB[RB_ALLOC_FAIL]++;
        rb_off(MEM_INITFAIL, sz, 0);
        store_free();                                            /* S19d: HSD or AllM */
        g_store = 1;
        return;
    }
    RB[RB_MEM] = MEM_MAGIC | (g_store_allm ? 0x200u : 0u) | (sz < (store_want() & ~31u) ? MEM_SHRUNK : MEM_FULL);   /* 0x200 = the store is in AllM (S19d) */
    RBR_SD_OK();
    YL[Y_TABLE_B] = sd_x11.fp_table_bytes; YL[Y_LFP_B] = sd_x11.lfp_bytes;   /* S11 */
    YL[Y_MIRROR_B] = sd_x11.mirror_bytes; YL[Y_LOG_ENTS] = sd_x11.log_ents;
    sfx_alloc();                                                 /* S6D TEST ONLY SFX log (after the store) */
    if (s3_cfg.dbg_n && s3_cfg.dbg_n <= (DBG_AUTO ? 16u : 64u)) {
        u32 i;
        g_dbg = (u32)safe_alloc(s3_cfg.dbg_n * DBG_REC, S19_TEST_KEEP);   /* S19: 0 when it does not fit (the locator then stays off) */
        g_dbg_done = 0; g_gp_checked = 0; g_gp_dumps = 0;
        for (i = 0; i < 16; i++) g_dbg_tag[i] = 0;
        for (i = 0; i < 64; i++) g_gph_f[i] = 0;
    }
}

/* ---- scene-loop slice (interrupts off, EXI lock held) ---- */
static void slice(void)
{
    u32 now = clock_ms(0);
    int rc, i;
    switch (ST.state) {
    case S3_NET_INIT:
        rc = bba_init_step(&B, now);
        if (rc == BBA_PENDING) break;
        g_init_rc = (u32)rc & 0xFFu;
        g_exi_id = rc == BBA_OK ? BBA_EXI_ID : bba_read_id(&B);   /* one more read of the id on a failure: what the adapter really answers (clone, emulation) */
        if (rc == BBA_E_NODEV) ST.state = S3_NOBBA;
        else if (rc != BBA_OK) ST.state = S3_FAIL_INIT;
        else { ST.state = S3_NET_LINK; g_link_frames = 0; if (B.wii_fast && g_bch == 2u) { g_pvr = 0; B.pv_xfer = x_pv_xfer; B.pv = SDK_EXISelect(2, 1, EXI_FREQ_32MHZ) ? 1 : 0; g_pvr = B.pv; g_pvp = B.pv ? 2u : 1u; } }   /* WII: the PV fast path when the kernel answers its device */
        break;
    case S3_NET_LINK:
        B.link = bba_reg_in8(&B, BBA_NWAYS);
        if (B.link & (BBA_NWAYS_LS10 | BBA_NWAYS_LS100)) {
            /* OD: no side from the MAC table; the side and the peer come from the server's ticket (s7_core_set_peer) */
            (void)i;
            bba_make_nic(&B, &NIC);
            sdnet_init(&S, &NIC, clock_ms, 0);
            {   /* PORT PER BOOT: a fresh local game port 43201..44199 unless the patched config says OD_CFGF_FIXPORT (the server and the opponent learn it from the ticket's source) */
                u16 lp = (u16)s3_cfg.local_port;
                if (lp == 43200u && !(od_o.cfg && (od_o.cfg->flags & OD_CFGF_FIXPORT))) lp = OD_GAME_PORT((u32)clock_ms(0) ^ ((u32)NIC.mac[5] << 8) ^ NIC.mac[4]);
                g_sock = sdnet_bind(&S, lp, 0, 0);
                od_o.lan_port = lp;
                if (B.wii_fast) { extern u32 s7_punch_mode; s7_punch_mode = (od_o.cfg && (od_o.cfg->flags & OD_CFGF_RING)) ? 2u : 0u; }   /* RING: a Wii sends no port sweep unless patched */
            }
#ifdef BR_BUILD
            br_link_up(&S);
#else
            od_dns_link_up();
#endif
            sdnet_dhcp_start(&S);
            g_net = 1;
            ST.state = S3_NET_DHCP;
        } else if (++g_link_frames > 900) ST.state = S3_FAIL_LINK;
        break;
    case S3_NET_DHCP:
        sdnet_poll(&S);
        /* the lease can arrive before the backend exists (od_o.cfg still NULL: od_net_up reads od_o.cfg->flags; with the MMU on that load faults): wait one more slice */
        if (sdnet_dhcp_state(&S) == SDNET_DHCP_BOUND && od_o.cfg) { ST.ip = S.ip; od_net_up(&od_o, S.ip, S.mask); od_set_dns(&od_o, OD_DNS_OBJ(), S.dns); ST.state = OD_NET_UP; if (g_rc == 2) { g_rc = 3; g_rc_t = now; } }   /* OD: the core starts when the ticket names a peer */
        else if (sdnet_dhcp_state(&S) == SDNET_DHCP_BOUND) { }
        else if (sdnet_dhcp_state(&S) == SDNET_DHCP_FAILED) sdnet_dhcp_start(&S);
        break;
    default:                                                  /* session states (core owns ST.state once it was started) */
        adp_arm();                                            /* AUTO DELAY FROM THE OPPONENT (S7F_AD_PEER): samples outside the match scene only */
        sdnet_poll(&S);
        drain();
#ifdef SD_CARD_HOLE
        nt_step(now);                                         /* Settings > Network status > Test connection: one poll of the probe run (a bare flag test when none runs) */
        ad_bg_tick(now);                                      /* ONLINE DELAY AUTO: the background internet-leg run when due (menus only) */
#endif
        {   /* OD: server step, then the hand-over to the engine's session */
            u32 pip, pport;
            int pside;
#ifdef SD_CARD_HOLE
            while (ob_n) {                                       /* OTA: datagrams the menu thread queued (od_send), sent here with the lock held */
                (void)sdnet_sendto(&S, g_sock, od_server_ip(&od_o), (u16)(od_o.cfg->server_port ? od_o.cfg->server_port : 43150u), ob_buf[ob_head], (u16)ob_len[ob_head]);
                ob_head = (u8)((ob_head + 1) % OB_N); ob_n--;
            }
#endif
            od_tick(&od_o, now);
            if (MS_NATPROBE && od_o.nat_probe) {                                  /* NAT PROBE: one datagram to the server's wire port + 1, right after the ticket, from the same socket: the server compares
                                                                    the outside ports of the two and predicts a sequential router's next port for the opponent (an old server simply never answers) */
                u8 pk[12]; unsigned k;
                od_o.nat_probe = 0;
                pk[0] = 'M'; pk[1] = 'S'; pk[2] = 'N'; pk[3] = 'P';
                for (k = 0; k < 8; k++) pk[4 + k] = od_o.cfg->dev[k];
                (void)sdnet_sendto(&S, g_sock, od_server_ip(&od_o), (u16)((od_o.cfg->server_port ? od_o.cfg->server_port : 43150u) + 1u), pk, 12u);
            }
            if (od_take_peer(&od_o, &pip, &pport, &pside)) {
                int fresh = 1;
                if (C.state >= S7_SES_CONNECTING) {                  /* a session exists (a second search after a match or a cancel) */
#ifdef ON_BUILD
                    /* EVERY new ticket starts a fresh session.  Before: "same peer, session alive: the rematch continues on it".
                       But a session that still LOOKS alive proves nothing about the peer's process: the lab bot (and any real player who restarted, switched networks or
                       sat out the post-game silence rule) comes back as a NEW ENet host on the same address and port; our old ENet peer ignores its CONNECT for ever
                       (host test test_restart: node 0 stays RUN, the restarted node 1 stays CONNECTING), the lock-in/stage select then runs on a dead
                       session and the match ends with Slippi's DISCONNECTED screen or never starts.  Slippi itself builds a new netplay client for every search.  The old
                       peer is told with a DISCONNECT (best effort) and the connect attempt repeats every second until the peer is ready, so both sides converge. */
                    g_es_ip = pip; g_es_port = pport; end_session(now); g_es_ip = g_es_port = 0;   /* REJOIN: no DISCONNECT into the next opponent's new connection */
#else
                    fresh = 0;
                    if (C.dst_ip != pip || C.dst_port != pport) { od_o.phase = OD_ERR; od_o.err = OD_E_SESSION; }   /* same peer: the session is still the right one */
#endif
                }
                if (fresh) {
                    if (s7_core_set_peer(&C, pside, pip, pport) == 0) { od_rules(); g_dst_ip = pip; g_dst_port = pport; s3_core_start(&C, now); }
                    else { od_o.phase = OD_ERR; od_o.err = OD_E_PEER; }
                }
            }
            if (od_o.phase >= OD_CONNECT) {
                int ph0 = od_o.phase;
                od_core_report(&od_o, now, C.state);
                if (ph0 == OD_CONNECT && od_o.phase == OD_WAIT_NET) end_session(now);   /* the peer connection failed and od retries with a fresh ticket: the engine session is over (od_conn_retry) */
            }
        }
#ifdef ON_BUILD
        peer_watch(now);
#endif
        s3_core_tick(&C, now);
        BC(0x33);
        break;
    }
#ifdef BR_BUILD
    br_slice(ST.state);
#endif
}

#ifdef MS_CARD_STRESS
/* Card stress (cfg flags bit4): emulates the SDK CARD code that holds EXILock(0) for a while.  Every `period` frames the
   game thread takes EXILock(0, 0) (memory card device), does real card traffic (read ID, read status) and keeps the lock
   for `hold` frames with interrupts ENABLED, so B0 (VI interrupt) and the scene-loop slice find the channel busy.  (Its counters, status words 48/49/51/52, are gone: the desync locator uses them.) */
#define STRESS_PERIOD(f) ((((f) >> 8) & 0xFFu) ? (((f) >> 8) & 0xFFu) : 30u)
#define STRESS_HOLD(f) ((((f) >> 16) & 0xFu) ? (((f) >> 16) & 0xFu) : 3u)
static u32 g_str_cnt, g_str_t0;
static volatile int g_str_have;
/* release is time based and also called from B0 (VI interrupt): real CARD code releases the channel from the EXI
   completion callback, independent of the scene loop (which does not run while the match is stalled). */
static void stress_poll(void)
{
    if (g_str_have && tbl() - g_str_t0 >= STRESS_HOLD(S3CFG) * FRAME_TICKS) {
        g_str_have = 0; SDK_EXIUnlock(0); g_str_cnt = 0;
    }
}
static void stress_step(void)
{
    u32 f = S3CFG, tmp __attribute__((aligned(4)));
    if (!(f & 0x10u)) return;
    if (g_str_have) { stress_poll(); return; }
    if (++g_str_cnt < STRESS_PERIOD(f) || ST.state < S7_SES_CONNECTING) return;   /* S7 */
    if (!SDK_EXILock(0, 0, 0)) return;
    if (SDK_EXISelect(0, 0, 4)) {                              /* 16 MHz, device 0 = memory card */
        tmp = 0;
        if (SDK_EXIImm(0, &tmp, 2, EXI_WRITE, 0)) SDK_EXISync(0);          /* cmd 0x0000: read ID */
        tmp = 0;
        if (SDK_EXIImm(0, &tmp, 4, EXI_READ, 0)) SDK_EXISync(0);
        SDK_EXIDeselect(0);
        if (SDK_EXISelect(0, 0, 4)) {
            tmp = 0x83000000u;                                    /* read status */
            if (SDK_EXIImm(0, &tmp, 2, EXI_WRITE, 0)) SDK_EXISync(0);
            tmp = 0;
            if (SDK_EXIImm(0, &tmp, 1, EXI_READ, 0)) SDK_EXISync(0);
            SDK_EXIDeselect(0);
        }
    }
    g_str_t0 = tbl(); g_str_have = 1;
}
#endif

/* ---- S6D TEST ONLY: SFX log (cfg s6d_flags bit1).  Record = 4 words:
     w0 = kind << 28 | in_logic << 27 | resim << 26 | iteration seq (26 bits), w1 = global frame of the iteration,
     w2, w3 = kind 1 ITER: rb_active, rollback end frame; 2 REQ: sound id, track | channel << 16;
              3 START (really started, instance id known): sound id, instance id; 4 STOP: instance id, caller LR.
   The buffer (HSD heap, allocated at match start) is frozen at global frame cfg.sfx_dump and printed with OSReport. ---- */
static void sfx_put(u32 kind, u32 a, u32 b)
{
    u32 m, i;
    volatile u32 *r;
    if (!g_sfx_on || !g_sfx) return;
    m = irq_off();
    i = g_sfx_i;
    if (i < SFX_N) {
        g_sfx_i = i + 1;
        r = g_sfx + 4u * i;
        r[0] = (kind << 28) | (g_in_logic << 27) | ((rb_active && (s32)g_cur_frame < rb_e) ? 1u << 26 : 0u) | (g_iter_seq & 0x3FFFFFFu);
        r[1] = g_cur_frame;
        r[2] = a;
        r[3] = b;
        DL[D_SFX_LOGN] = i + 1;
    } else DL[D_SFX_OVF]++;
    irq_restore(m);
}
static int resim_now(void) { return rb_active && (s32)g_cur_frame < rb_e; }
void s6d_sfx_req(u32 id, u32 vol, u32 pan, u32 track, u32 ch)   /* pre-hook AXDriver_8038CFF4 (0x8038CFF4) */
{
    (void)vol; (void)pan;
    if (!in_game()) return;
    DL[D_SFX_REQ]++;
    if (resim_now()) DL[D_SFX_REQ_RS]++;
    sfx_put(2u, id, (track & 0xFFFFu) | (ch << 16));
}
void s6d_sfx_start(u32 inst, u32 id, u32 track)                 /* 0x8038D29C: the start really happened */
{
    (void)track;
    if (!in_game()) return;
    DL[D_SFX_START]++;
    if (resim_now()) DL[D_SFX_START_RS]++;
    sfx_put(3u, id, inst);
}
void s6d_sfx_stop(u32 inst, u32 lr)                             /* pre-hook lbAudioAx_800236B8 (SFX_StopSFXInstance) */
{
    if (!in_game()) return;
    DL[D_SFX_STOP]++;
    if (lr >= SD_B_SFXLO && lr < SD_B_SFXHI) DL[D_SFX_STOP_SL]++;   /* called from the patched code area (Slippi ASM) */
    sfx_put(4u, inst, lr);
}
static void sfx_alloc(void)                                     /* match start (match_store) */
{
    g_sfx = 0; g_sfx_i = 0; g_sfx_on = 0; g_sfx_d = 0; g_sfx_freeze = 0; DL[D_SFX_STATE] = 0;
    if (!(S6DCFG & S6DF_SFX_LOG)) return;
    g_sfx = (volatile u32 *)safe_alloc(SFX_N * 16u, S19_TEST_KEEP);   /* S19 */
    g_sfx_on = g_sfx != 0;
}
#ifndef MS_ENGINE_REPORT
static void sfx_dump_step(void) { }                              /* release: the SFX dump lines print nowhere */
#else
static void sfx_dump_step(void)
{
    u32 t0, k, j;
    char *p;
    if (!g_sfx || DL[D_SFX_STATE] == 2u) return;
    if (DL[D_SFX_STATE] == 0u) {
        if (!in_game() || (s32)GLOBAL_FRAME < (s32)s3_cfg.sfx_dump) return;
        g_sfx_on = 0; g_sfx_freeze = g_sfx_i; g_sfx_d = 0; DL[D_SFX_STATE] = 1u;
    }
    t0 = tbl();
    while (g_sfx_d < g_sfx_freeze && tbl() - t0 < sdg_gc_ticks(200000u)) {      /* ~30 % of a frame per rendered frame */
        p = g_lb;
        *p++ = 'S'; *p++ = 'F'; *p++ = 'X'; *p++ = ' ';
        p = hx(p, g_sfx_d);
        for (k = 0; k < 6u && g_sfx_d < g_sfx_freeze; k++, g_sfx_d++)
            for (j = 0; j < 4u; j++) { *p++ = ' '; p = hx(p, g_sfx[4u * g_sfx_d + j]); }
        *p++ = '\n'; *p = 0;
        OSReport(g_lb);
        DL[D_SFX_LINES]++;
    }
    if (g_sfx_d >= g_sfx_freeze) {
        p = g_lb;
        *p++ = 'S'; *p++ = 'F'; *p++ = 'X'; *p++ = '!'; *p++ = ' ';
        p = hx(p, g_sfx_freeze); *p++ = ' '; p = hx(p, DL[D_SFX_OVF]); *p++ = ' '; p = hx(p, s3_cfg.sfx_dump);
        *p++ = '\n'; *p = 0;
        OSReport(g_lb);
        DL[D_SFX_STATE] = 2u;
    }
}
#endif

#if defined(MS_QCHAT) && defined(ON_BUILD)
/* QUICK CHAT: Slippi's D-pad chat on the online character screen (menus/qc/qc_gc.c).  The transport is this engine's own ENet session with the
   opponent (s7_core.c: 0x84 in, s7_core_chat_tx out); the session flag follows the engine session from the assignment on (Slippi: slippi_netplay exists), the 1v1 indices are the absolute ports. */
extern void qc_iter(void);
extern void qc_session(int up, int local_idx, int remote_idx, const char *own_name, const char *opp_name, int direct);
int qc_tx_hook(const unsigned char *d, unsigned int len) { return s7_core_chat_tx(&C, d, len); }
void qc_tx_cancel_hook(void) { s7_core_chat_cancel(&C); }
static void qc_pump(void)
{
    static int up __attribute__((section(".data")));
    /* The session flag is up from the assignment on (the core started, the peer may still be connecting): Slippi's slippi_netplay exists as soon as the opponent is found, and the
       bridge route used to wait for the ENet connection.  A message pressed in
       that span waits in the core for the connection (s7_core_chat_tx / s3_core_tick); LOST or no core = down. */
    int now = C.state == S3_SES_RUN || C.state == S7_SES_CONNECTING;
    if (now != up) {
        int me = C.side > 0 && C.side < 4 ? C.side : 0;
        up = now;
        qc_session(now, me, me ? 0 : 1, od_o.m.name, od_o.opp_name, ms_rules_is_direct(&ms_rules));
    }
    qc_iter();
}
#endif

#ifdef MM_FC
/* FRAME COST, the pass boundary (top of the scene loop, s3_tick) and the match: the pass that ends here goes into the accumulators (IDLE becomes STALL when a B0 call stalled in it or it
   ran no new frame); the match starts with the first pass in the match scene and ends at the game's own game-over flag or when the scene is left; its readout words then wait in one of
   two upload slots (the oldest is replaced) for the hub overlay's LAB_DIAG step (on_bhold.c bh_diag_lines). */
u32 fc_slot[2][FC_W];
u32 fc_pend[2];                                                  /* sequence number of the readout in slot k, 0 = free */
static u32 fc_seq, fc_pt, fc_pst, fc_done, fc_lf0, fc_sk0, fc_cc0;
#ifdef ON_BUILD
static void gs_io_take(u32 *v);
static u32 fc_io0[8];
#endif
static void fc_end(u32 go)
{
    u32 h[12], k, o = odb(), plat = sd_plat_ms_ticks() >= 55000u ? 2u : 1u;
    volatile u32 *sb = (volatile u32 *)0xD31BE000u;
    for (k = 0; k < 12u; k++) h[k] = 0;
    if (plat == 2u && sb[126] == 0x56425854u && sb[121] == 1u) plat = 3u;   /* Wii U (the q34ph kernel's word 121) */
    h[0] = plat; h[1] = ST.match_no; h[2] = W(0x8049E6C8u + 0x88u); h[3] = GLOBAL_FRAME; h[4] = o ? *(volatile u8 *)(o + 33u) : 0u;
    h[5] = (go ? 1u << 24 : 0u) | (fc_wc ? 1u << 25 : 0u);
#ifdef ON_BUILD
    {   u32 v[8], n;
        gs_io_take(v);
        if (v[7] && fc_io0[7]) {
            n = v[0] - fc_io0[0];
            h[5] |= 1u << 26;
            h[6] = n ? (v[1] - fc_io0[1]) / n : 0u; h[7] = v[2] - fc_io0[2]; h[8] = v[3] - fc_io0[3]; h[9] = v[4] - fc_io0[4]; h[10] = v[5]; h[11] = 1u | (v[6] ? 2u : 0u);
        }
    }
#endif
    g_fc.card_cmd = fc_cardcmd() - fc_cc0;
    g_fc.lockfail = ST.slice_skip + ST.b0_lockfail - fc_lf0;
    g_fc.skip_card = g_card_skip_n - fc_sk0;
    g_fc.path = (B.pv ? 2u : B.wii_fast ? 1u : 0u) | (g_pvp << 4) | ((u32)B.pv_why << 8) | ((u32)B.pv_bad0 << 16) | ((u32)B.pv_bad1 << 24);
    g_fc.pvc = fc_pair(B.st_pv_tx, B.st_pv_rx);
    { extern unsigned int ms_ld_stat(void) __attribute__((weak)); g_fc.ldst = ms_ld_stat ? ms_ld_stat() : 0u; }
    g_fc.pvbuf = (u32)B.dmabuf;
    g_fc.pvd[0] = ((u32)B.pv_dump[0] << 24) | ((u32)B.pv_dump[1] << 16) | ((u32)B.pv_dump[2] << 8) | B.pv_dump[3];
    g_fc.pvd[1] = ((u32)B.pv_dump[4] << 24) | ((u32)B.pv_dump[5] << 16) | ((u32)B.pv_dump[6] << 8) | B.pv_dump[7];
    k = !fc_pend[0] ? 0u : !fc_pend[1] ? 1u : (fc_pend[0] < fc_pend[1] ? 0u : 1u);
    fc_words(&g_fc, fc_slot[k], h);
    fc_pend[k] = ++fc_seq;
}
static void fc_tick(u32 now)
{
    u32 m = fc_msr_off(), ig = in_game(), k;
    if (fc_on) {
        u32 p = fc_cat, o = odb(), s0, ph;
        fc_acc[p] += now - fc_t; fc_t = now; g_fc.cps++;
        fc_window(now, p);
        if (!ig || (o && g_fc.passes[0] + g_fc.passes[1] > 60u && *(volatile u8 *)(o + S12_ODB_IS_GAME_OVER))) {
            fc_end(ig); fc_on = 0; fc_done = ig;                 /* game over: nothing more until the scene is left */
        } else {
            s0 = tbl(); ph = fc_phase();
            if (ST.stall_calls != fc_pst || !fc_iters) { fc_acc[FC_STALL] += fc_acc[FC_IDLE]; fc_acc[FC_IDLE] = 0; }
            g_fc.stall[ph] += ST.stall_calls - fc_pst;
            fc_pass(&g_fc, ph, fc_acc, fc_pt ? now - fc_pt : 0u, fc_ops, fc_oplong, fc_iops);
            g_fc.self += tbl() - s0;
        }
    } else if (!ig) fc_done = 0;
    if (!fc_on && ig && !fc_done) {                              /* a new match */
        fc_init(&g_fc, sd_plat_ms_ticks());
        fc_wc = sd_plat_ms_ticks() >= 55000u && *(volatile u32 *)0xD3026694u == 0x45585042u;
        fc_cc0 = fc_cardcmd(); fc_lf0 = ST.slice_skip + ST.b0_lockfail; fc_sk0 = g_card_skip_n;
#ifdef ON_BUILD
        gs_io_take(fc_io0);
#endif
        fc_on = 1;
    }
    if (fc_on) {
        fc_pt = now;
        for (k = 0; k < FC_NC; k++) fc_acc[k] = 0;
        fc_ops = fc_oplong = fc_iters = fc_iops = 0;
        fc_cat = FC_HOOK; fc_t = now; fc_pst = ST.stall_calls;
        fc_snap(now);
    }
    fc_msr_set(m);
}
#define FC_TICK(t) fc_tick(t)
#else
#define FC_TICK(t) ((void)0)
#endif
static void s3_tick_body(void);
void s3_tick(void)                                               /* S6C: per rendered frame timing around the S6b body */
{
    u32 now = tbl();
    if (g_pass_have && g_last_end_t && now - g_last_end_t < 2u * FRAME_TICKS && in_game() && !g_early_pass)
        ZL[Z_RENDER_EMA] = ema(ZL[Z_RENDER_EMA], now - g_last_end_t);   /* S12: render pass = last logic end .. loop top */
    if (g_early_pass) {                                          /* S12: that pass held only an early rollback (no new
                                                                    frame): it belongs to the same display frame */
        g_early_pass = 0;
        if (g_pass_have && g_pass_first > g_tick_end) g_idle_acc += g_pass_first - g_tick_end;
        FC_GT(FC_HOOK);
        s3_tick_body();
        FC_GT(FC_IDLE);
        g_tick_end = tbl();
        g_pass_have = 0;
        return;
    }
    FC_TICK(now);                                                /* FRAME COST: the pass that ends here (test build MM_FC only) */
    pass_end(now);
    s3_tick_body();
    FC_GT(FC_IDLE);                                              /* FRAME COST: the loop-top wait for the pad / VI */
    g_tick_start = now;
    g_tick_end = tbl();
    g_pass_have = 0;
    g_pass_rb = 0;
    g_pass_iters = 0;                                           /* S10 */
    g_idle_acc = 0;                                             /* S12 */
    if ((S12CFG & S12F_RSTIME) && in_game()) s12_heap_watch();   /* S12 (on S11): HSD heap headroom */
    {   u32 i; for (i = 0; i < 8; i++) DL[D_STALLH + i] = C.stall_hist[i]; }   /* S6D */
}

/* S19b TEST ONLY (s12_flags 0x80000): Pokemon Stadium state log.  Once per scene-loop pass in a match, for map ground objects 0..3
   (stage_info.map_gobjs, 0x8049E6C8 + 0x180; Ground user data at GObj +0x2C, union at +0xC4, gr/types.h grStadium_GroundVars /
   grStadium_Display), print "S12P frame map dc de d8 xc4 | e4 ee e0" whenever a watched word changes.  dc = transformation state
   (grStadium_801D4548: 0 countdown, 1 waiting for the async file load, 2.. switch), de = type, d8 = countdown; e4/ee/e0 = the jumbotron state. */
#define S12F_PSLOG 0x80000u
static u32 g_ps_last[4][3], g_ps_allm;                                     /* [1] unused */
/* ---- S19b: deterministic Pokemon Stadium transformations  ---------------------
   Vanilla: the transformation state machine (grStadium_801D4548, state xDC of map ground 2) requests the transformation file with an ASYNC
   disc read (lbFile_80016580, callback fn_801D4220 clears xC4_b1 when the read lands) and switches to the new stage on the first frame its
   poll (grStadium_801D42B8) sees the flag clear.  That frame depends on the disc (measured in a rollback-free Dolphin pair: 3,737 on one
   console, 3,738 on the other for the same request at 3,732), so the stages differ from there; the callback also writes game state outside
   any frame (a rollback can restore the flag to "loading" after the read landed).  Fix (console-safe, no Dolphin help):
   - 04 `bl ps_request` replaces `bl lbFile_80016580` at 0x801D4754: the request frame R goes into the stage ground (+0xF8, inside the
     snapshot); the read is issued with our callback (it calls the original and marks the read done in a glue word OUTSIDE the snapshot); a
     re-simulated request for the file already in flight is not issued twice;
   - 04 `bl ps_done` replaces `bl grStadium_801D42B8` at 0x801D4760: "loaded" is answered from the FRAME, not from the disc: false before
     R + PS_LAG, true at R + PS_LAG on both consoles; if the read has not landed by then (slow disc) the game waits for it right there, the way
     Melee's own synchronous loader does (lbFile_8001668C waitForDisc: lb_800195D0 in the loop).  The flag is cleared and the original
     builds the archive.  A rollback that re-runs this frame finds the buffer already relocated by HSD_ArchiveParse: it is un-relocated first.
   s12_flags 0x100000 (TEST ONLY) = vanilla behaviour (A/B). */
#define S12F_PS_VANILLA 0x100000u
#ifndef PS_LAG
#define PS_LAG 1u                               /* frames from the request to the switch: 1 = Slippi-PC parity (its synchronous stage-file load switches on the next frame; ps_done waits for the disc read right there: a hitch of ~0.1-0.3 s per transformation on a console).  Was 60 (ROLLBACK_STATUS_2026-10-11: with 60 a console vs the real client desynced 340-450 frames after the first transformation) */
#endif
#define PS_REQ_OFF 0xF8u                        /* spare word of the stadium ground's user data (vars end at +0xEC; Ground is 0x204) */
typedef void (*ps_cb_t)(int, int, void *, int);
#define PS_LBFILE ((void (*)(const char *, void *, u32 *, ps_cb_t, void *))0x80016580)
#define PS_FILESIZE ((u32 (*)(const char *))0x800163D8)         /* lbFileGetSize */
#define PS_CB_ORIG ((ps_cb_t)0x801D4220)
#define PS_DONE_ORIG ((int (*)(void))0x801D42B8)
#define PS_PUMP ((void (*)(void))0x800195D0)                    /* lb_800195D0, what waitForDisc calls while it waits */
static volatile u32 g_ps_busy;                  /* a transformation read is in flight (set at issue, cleared by ps_cb) */
static u32 g_ps_name, g_ps_dst, g_ps_parsed;    /* file and buffer of the read; buffer whose archive was relocated since its last read */
u32 g_ps_tel[8];                                /* TEST telemetry: issued, skipped re-requests, waits, wait polls, unrelocations, vanilla */
static u32 ps_gp(void)   /* stage_info.map_gobjs[2] user data; 0 off Pokemon Stadium (grkind 0x10). the stage check was missing, so on every stage whose map_gobjs[2] data read xDC = 0 and xD8 <= PS_ARM (Fountain of Dreams, Final Destination, Yoshi's Story) the countdown-armed lockstep window held cap 0 for the whole match: no prediction, no capture, no rollback (console and Dolphin) */
{
    u32 g;
    if (W(0x8049E6C8u + 0x88u) != 0x10u) return 0u;
    g = W(0x8049E848u + 8u);
    return ptr_like(g) ? W(g + 0x2Cu) : 0u;
}
#ifdef MS_ENGINE_REPORT
static void ps_note(char k, u32 x)            /* "S12T <k> frame x issued skipped waits polls unreloc" (one line per request / switch) */
{
    char *p = g_lb;
    *p++ = 'S'; *p++ = '1'; *p++ = '2'; *p++ = 'T'; *p++ = ' '; *p++ = k; *p++ = ' ';
    p = hx(p, GLOBAL_FRAME); *p++ = ' '; p = hx(p, x);
    { u32 i; for (i = 0; i < 5; i++) { *p++ = ' '; p = hx(p, g_ps_tel[i]); } }
    *p++ = '\n'; *p = 0;
    OSReport(g_lb);
}
#else
static void __attribute__((unused)) ps_note(char k, u32 x)
{
    (void)k; (void)x;
}
#endif
static void ps_cb(int a, int b, void *buf, int cancel) { PS_CB_ORIG(a, b, buf, cancel); g_ps_busy = 0; }
u32 g_ps_lastk, g_ps_lastf, g_ps_wms;                  /* lab readout (FT line): kind of the last Stadium request (1 cache, 2 cache from a moved block, 3 disc, 4 cache unusable -> disc, 5 vanilla), its frame, the longest ps_wait in ms */
static void ps_wait(void)
{
    u32 t0;
    if (!g_ps_busy) return;
    g_ps_tel[2]++;
    t0 = tbl();
#ifdef MM_FC
    fc_gflag |= 2u;                                              /* FRAME COST: a disc wait inside the frame (block cause) */
#endif
    while (g_ps_busy) { g_ps_tel[3]++; PS_PUMP(); }
    { u32 mt = sd_plat_ms_ticks(); t0 = (tbl() - t0) / (mt ? mt : 40500u); }      /* (rbr_mt only exists with the readout option) */
    if (t0 > g_ps_wms) g_ps_wms = t0;
}
/* ---- S19e: Stadium transformation file cache -------------------------------------------------------------------------------
   Slippi-PC switches the stage on the frame after the request (its stage-file load is synchronous); a console waiting for the disc inside that frame
   (PS_LAG 1) broke the pair rig (5 of 6 runs diverged ~100 frames after the first transformation, ROLLBACK_STATUS_2026-10-11), while a lag long enough for
   the read to land (10) was IDENTICAL.  So the four GrPs<k>.dat files (1.12 MB) are read ONCE per Stadium match, early, into AllM blocks (heap 4: 3-4 MB
   idle in a match), and ps_request answers from RAM (copy + the original callback: no disc, no wait, lag 1 is exact on every console).  No AllM room
   (S19d store took it, 256 KB are always left): no cache, the old read path (a wait at lag 1). */
#define PSC_N 4
static u32 g_psc_rec[PSC_N], g_psc_sz[PSC_N];            /* the lbMemory RECORD of each block (stable) and its rounded size: lbMemory's compaction (lbDvd_CleanupPreloadHeap -> lbMemory_8001529C) MOVES every block above a gap and updates the record's address, so the address is only valid when read from the record */
u32 g_psc_tel[8];                                           /* TEST telemetry (S19f): [0] blocks freed, [1] blocks not listed any more at the free (moved heap / record reused: left alone), [2] cache read still in flight after the bounded wait (blocks kept), [3] served from a moved block, [4] a request that fell back to the disc path (block moved / compaction running), [5] fill invalid (a block moved during the reads), [6] psc_free calls with blocks, [7] last frame of a free */
static u32 g_psc_lo[PSC_N], g_psc_len[PSC_N], g_psc_h, g_psc_st, g_psc_k;   /* (g_psc_k: next file to read) */ /* AllM data address (= block lo), file length; heap handle; 0 idle, 1 cached, 2 no cache this match */
static volatile u32 g_psc_busy;
static void psc_cb(int a, int b, void *buf, int cancel) { (void)a; (void)b; (void)buf; (void)cancel; g_psc_busy = 0; }
static int psc_idx(const char *n)
{
    if (n && n[0] == 'G' && n[1] == 'r' && n[2] == 'P' && n[3] == 's' && n[4] >= '1' && n[4] <= '4' && n[5] == '.') return n[4] - '1';
    return -1;
}
#ifdef MS_RB_READOUT
static void rbr_ps_fill(u32 *w)                                   /* the readout's PM / PS lines (on_bhold.h): [0] busy flags, [1] state, [2] blocks held, [3..9] g_psc_tel[0..6] */
{
    u32 k, n = 0;
    for (k = 0; k < PSC_N; k++) if (g_psc_rec[k]) n++;
    w[0] = g_psc_busy | (g_ps_busy << 1); w[1] = g_psc_st; w[2] = n;
    for (k = 0; k < 7u; k++) w[3 + k] = g_psc_tel[k];
}
#endif
static u32 psc_live(u32 k)                              /* S19f: the CURRENT data address of cache block k, 0 unless AllM still lists our record with our size (a compaction moves the block and rewrites the record's address: the address remembered at allocation goes stale) */
{
    u32 d = LB_HEAP(4), it, n = 0, rec = g_psc_rec[k];
    if (!rec || W(d + 0x18u) != 0u || W(d + 4u) != g_psc_h) return 0;
    it = W(g_psc_h + 0xCu);
    while (ptr_like(it) && n++ < 4096u) {
        if (it == rec) return W(rec + 8u) == g_psc_sz[k] ? W(rec + 4u) : 0u;
        it = W(it);
    }
    return 0;
}
static void psc_free(void)
{
    u32 k, any = 0, t0 = tbl(), lim = sdg_us_ticks(2000000u), stuck = 0;
    while (g_psc_busy) {                                /* a cache read still in flight (scene change): let it land before its block goes back; BOUNDED (2 s): a read the disc layer dropped must never freeze the first frame after the match */
        if (tbl() - t0 > lim) { stuck = 1; g_psc_busy = 0; g_psc_tel[2]++; break; }
        PS_PUMP();
    }
    for (k = 0; k < PSC_N; k++) if (g_psc_rec[k]) {
        u32 lo = stuck ? 0u : psc_live(k);              /* freed through the record's own address (it may have been moved), never through a remembered one; with a read in flight the blocks are kept (a leak is better than a DVD write into freed memory) */
        any = 1;
        if (lo) { LBMEM_FREE(g_psc_h, lo); g_psc_tel[0]++; } else g_psc_tel[1]++;
        g_psc_rec[k] = 0; g_psc_sz[k] = 0; g_psc_lo[k] = 0; g_psc_len[k] = 0;
    }
    if (any) { g_psc_tel[6]++; g_psc_tel[7] = GLOBAL_FRAME; }
    for (k = 0; k < PSC_N; k++) { g_psc_lo[k] = 0; g_psc_len[k] = 0; }
#ifdef MS_RB_READOUT
    rbr_ps_fill(&rbr_w[RBR_PS0]); rbr_w[RBR_PSCALLS]++;              /* readout: psc_free finished (its last statement) */
#endif
    g_psc_st = 0; g_psc_k = 0;
}
static void psc_step(void)                              /* one asynchronous step per scene-loop pass: never waits (a wait inside the game loop broke the pair rig) */
{
    static const char *const nm[PSC_N] = { "GrPs1.dat", "GrPs2.dat", "GrPs3.dat", "GrPs4.dat" };
    u32 k, at, ab, need = 0, sz[PSC_N];
    char *p = g_lb;
    if (g_psc_st == 0) {                                 /* size, allocate the four blocks; the reads follow one per pass */
        g_psc_st = 2;
        if (!allm_room(&at, &ab)) return;
        for (k = 0; k < PSC_N; k++) { g_psc_len[k] = PS_FILESIZE(nm[k]); sz[k] = (g_psc_len[k] + 0x40u) & ~31u; need += sz[k]; }
        if (need + 0x40000u > at) { for (k = 0; k < PSC_N; k++) g_psc_len[k] = 0; return; }
        g_psc_h = W(LB_HEAP(4) + 4u);
        for (k = 0; k < PSC_N; k++) {
            u32 blk;
            if (!allm_room(&at, &ab) || ab < sz[k]) { psc_free(); g_psc_st = 2; return; }
            blk = LBMEM_ALLOC(g_psc_h, sz[k]);
            if (!ptr_like(blk)) { psc_free(); g_psc_st = 2; return; }
            g_psc_rec[k] = blk; g_psc_sz[k] = sz[k]; g_psc_lo[k] = W(blk + 4u);
        }
        g_psc_k = 0; g_psc_st = 3;
        return;
    }
    if (g_psc_st != 3 || g_psc_busy) return;
    if (g_psc_k >= PSC_N) {
        u32 bad = 0;
        for (k = 0; k < PSC_N; k++) if (psc_live(k) != g_psc_lo[k]) bad = 1;   /* S19f: a block moved while its read was in flight: the data landed at the old address, the cache is not valid (the blocks still go back at the match end) */
        if (bad) { g_psc_tel[5]++; g_psc_st = 2; return; }
        g_psc_st = 1;
        *p++ = 'S'; *p++ = '1'; *p++ = '2'; *p++ = 'C'; *p++ = ' '; p = hx(p, GLOBAL_FRAME); *p++ = ' '; p = hx(p, g_psc_len[0] + g_psc_len[1] + g_psc_len[2] + g_psc_len[3]);
        *p++ = '\n'; *p = 0;
        OSReport(g_lb);
        return;
    }
    {   u32 len = 0;
        g_psc_busy = 1;
        if (psc_live(g_psc_k) != g_psc_lo[g_psc_k]) { g_psc_busy = 0; g_psc_tel[5]++; g_psc_st = 2; return; }   /* S19f: moved since the last pass */
        PS_LBFILE(nm[g_psc_k], (void *)g_psc_lo[g_psc_k], &len, psc_cb, 0);
        g_psc_k++;
    }
}
void ps_request(const char *name, void *dst, u32 *size, ps_cb_t cb, void *args)
{
    u32 gp = ps_gp();
    if ((S12CFG & S12F_PS_VANILLA) || !gp) { g_ps_tel[5]++; g_ps_lastk = 5; g_ps_lastf = GLOBAL_FRAME; PS_LBFILE(name, dst, size, cb, args); return; }
    W(gp + PS_REQ_OFF) = GLOBAL_FRAME;
    g_ps_switch = GLOBAL_FRAME + PS_LAG;         /* S19c: also for a re-simulated request (R restored from the snapshot) */
    {   /* S19e: answered from the cache (also a re-simulated request: the copy restores a buffer whose archive was already relocated) */
        int ci = psc_idx(name);
        u32 src = (g_psc_st == 1 && ci >= 0 && g_psc_len[ci]) ? psc_live((u32)ci) : 0u;   /* S19f: the block's address from its record (a compaction may have moved it since the fill) */
        if (src && W(0x804318B0u + 0x6D0u) == 0u) {      /* ... and no compaction copy running (lbMemory_804318B0.x6A0_mgr.size): a moved block is whole once the copy is done */
            u32 n = g_psc_len[ci], i;
            const u32 *sw = (const u32 *)src;
            u32 *dw = (u32 *)dst;
            if (src != g_psc_lo[ci]) g_psc_tel[3]++;
            g_ps_lastk = src != g_psc_lo[ci] ? 2u : 1u; g_ps_lastf = GLOBAL_FRAME;
            ps_wait();
            for (i = 0; i < (n >> 2); i++) dw[i] = sw[i];
            for (i = n & ~3u; i < n; i++) ((u8 *)dst)[i] = ((const u8 *)src)[i];
            *size = n;
            g_ps_parsed = 0;
            PS_CB_ORIG(0, 0, dst, 0);
            ps_note('C', (u32)name);
            return;
        }
        g_ps_lastk = (g_psc_st == 1 && ci >= 0 && g_psc_len[ci]) ? 4u : 3u; g_ps_lastf = GLOBAL_FRAME;
        if (g_psc_st == 1 && ci >= 0 && g_psc_len[ci]) g_psc_tel[4]++;   /* block moved away / compaction running: this request takes the disc path below (old behaviour) */
    }
    if (g_ps_busy && g_ps_name == (u32)name && g_ps_dst == (u32)dst) { *size = PS_FILESIZE(name); g_ps_tel[1]++; ps_note('S', (u32)name); return; }   /* re-simulated request */
    ps_wait();                                  /* another read still in flight (a rollback changed the choice): let it land first */
    g_ps_busy = 1; g_ps_name = (u32)name; g_ps_dst = (u32)dst; g_ps_parsed = 0;
    g_ps_switch = GLOBAL_FRAME + PS_LAG;         /* S19c: lockstep window around the switch (be_inputs) */
    g_ps_tel[0]++;
    PS_LBFILE(name, dst, size, ps_cb, args);
    ps_note('R', (u32)name);
}
static void ps_unrelocate(u32 buf)              /* undo HSD_ArchiveParse's Locate: data words += data base, listed in the relocation table */
{
    u32 data = buf + 0x20u, dsz = W(buf + 4u), n = W(buf + 8u), t = data + dsz, i;
    if (dsz >= 0x50000u || n > 0x10000u) return;
    for (i = 0; i < n; i++) { u32 off = W(t + 4u * i); if (off < dsz) W(data + off) -= data; }
    g_ps_tel[4]++;
}
int ps_done(void)
{
    u32 gp = ps_gp(), dst;
    if ((S12CFG & S12F_PS_VANILLA) || !gp) return PS_DONE_ORIG();
    if ((s32)(GLOBAL_FRAME - W(gp + PS_REQ_OFF)) < (s32)PS_LAG) return 0;
    ps_wait();
    *(volatile u8 *)(gp + 0xC4u) &= (u8)~0x40u;  /* xC4_b1 (bit 1 of the first byte): "loading" off, whatever a restore left there */
    dst = W(gp + 0xCCu);
    if (g_ps_parsed == dst) ps_unrelocate(dst);  /* a rollback re-runs the switch on the same buffer */
    g_ps_parsed = dst;
    ps_note('D', W(gp + PS_REQ_OFF));
    if (g_ps_parsed == dst && g_ps_tel[4]) g_ps_tel[7]++;   /* S19c telemetry: the switch frame re-run by a replay (must stay 0 with the lockstep window) */
    return PS_DONE_ORIG();
}
u32 g_ps_fn[2];                                 /* keeps the two hook targets linked (--gc-sections); written by ps_log */

/* FROZEN FINAL DESTINATION (policy mfd_decide in ms_match_rules.h): at the first tick of a match on the FD ground the fd-freeze switch word (0x8021AAE0,
   `mr r3,r30` <-> `b +8`) is written when the stage select's Z toggle is on, or FD came up by a Random pick (core flag s7_stage_rand); the retail word is read from memory before the first write
   and written back when the match is over (or on any other stage).  Local screen option, nothing synced (DesyncProofBGTransformations restores the RNG seed around the background think), the
   Slippi route never.  One decision per match, no per-frame state, so rollback re-simulation has nothing to repeat. */
extern s3_u8 s7_stage_rand;
static u8 g_fd_dec;                                              /* 1 = this match was decided */
static u32 g_fd_ret;                                             /* the word we replaced (0 = nothing written) */
static void fd_write(u32 w)
{
    volatile u32 *p = (volatile u32 *)MFD_SWITCH_ADDR;
    if (*p == w) return;
    *p = w;
    SDK_DCFlushRange((void *)p, 4);
    __asm__ volatile("icbi 0,%0; sync; isync" : : "r"(p) : "memory");
}
u32 ms_frozen_kind;                                              /* 1 = Frozen Final Destination, 2 = Frozen Pokemon Stadium (mfd_kind), set at the first tick of a match: the match-start notice (on_bhold.c bh_note_tick) says it */
static u32 mfd_tog(u32 site, u32 magic)                          /* body + 8 of the C2 whose `b` is at site; with a magic only when it is at body + 16 */
{
    u32 a = mfd_body_addr(site, W(site));
    return a >= 0x80000000u && a < 0x81800000u - 32u && (!magic || W(a + 16u) == magic) ? *(volatile u8 *)(a + 8u) : 0u;
}
u32 ms_sss_kind;                                                 /* the stage select's frozen label (on_bhold.c bh_sss_tick): 1 = Final Destination hovered and FD frozen, 2 = Pokemon Stadium hovered and frozen */
static void fd_step(void)
{
    u32 gk, fd, h = *(volatile u8 *)0x804D6CAEu;                 /* hovered stage icon */
    ms_sss_kind = LOADING == 0 && (SCENE_WORD >> 24) == 2u && (SCENE_WORD & 0xFFu) == 1u         /* the offline Vs Melee stage select */
        ? (h == 25u && mfd_tog(MFD_INJ_CURSOR, MFD_MAGIC) ? 1u : h == 18u && mfd_tog(MFD_INJ_PS, 0) && !ms_rules_slippi(&ms_rules) ? 2u : 0u) : 0u;
    if (!mfd_scene(SCENE_WORD, LOADING)) {                       /* online AND offline Vs Melee (major 2: the old online-only test left FD transforming in Couch / Vs Melee) */
        if (g_fd_ret) { fd_write(g_fd_ret); g_fd_ret = 0; }
        g_fd_dec = 0; ms_frozen_kind = 0;
        return;
    }
    if (g_fd_dec) return;
    g_fd_dec = 1;
    gk = W(0x8049E6C8u + 0x88u);
    fd = mfd_decide(gk, mfd_tog(MFD_INJ_CURSOR, MFD_MAGIC), s7_stage_rand, ms_rules_slippi(&ms_rules));   /* the cursor C2: ours (flag ms-fd-toggle: magic at +16, toggle byte at +8) or Slippi's (no magic: no FD toggle) */
    ms_frozen_kind = mfd_kind(gk, fd, mfd_tog(MFD_INJ_PS, 0), ms_rules_slippi(&ms_rules));
    if (fd) {
        if (!g_fd_ret && W(MFD_SWITCH_ADDR) != MFD_WORD_ON) g_fd_ret = W(MFD_SWITCH_ADDR);
        fd_write(MFD_WORD_ON);
    } else if (g_fd_ret) { fd_write(g_fd_ret); g_fd_ret = 0; }
}
static void ps_log(void)
{
    u32 i;
    g_ps_fn[0] = (u32)ps_request; g_ps_fn[1] = (u32)ps_done;
    if (!in_game()) { g_ps_allm = 0; return; }
    if (W(0x8049E6C8u + 0x88u) == 0x10u && (g_psc_st == 0 || g_psc_st == 3) && !rb_active && GLOBAL_FRAME >= 3u && ps_gp() && !(S12CFG & (S12F_PS_VANILLA | S12F_PS_NOCACHE))) psc_step();   /* S19e: Pokemon Stadium, once per match */
    if ((S12CFG & S12F_PSLOG) && !g_ps_allm && GLOBAL_FRAME >= 60u) {   /* once per match: AllM (preload heap 4) free bytes, HSD heap free */
        u32 h = W(0x80431FA0u + 0x10u + 0x1Cu * 4u + 4u), t, bg;
        char *p = g_lb;
        g_ps_allm = 1;
        hsd_room(&t, &bg);
        *p++ = 'S'; *p++ = '1'; *p++ = '2'; *p++ = 'A'; *p++ = ' ';
        p = hx(p, ptr_like(h) ? ((u32 (*)(u32))0x80014F7Cu)(h) : 0u); *p++ = ' '; p = hx(p, W(0x80431FA0u + 0x10u + 0x1Cu * 4u + 0xCu));
        *p++ = ' '; p = hx(p, t); *p++ = ' '; p = hx(p, W(0x8049E6C8u + 0x88u)); *p++ = '\n'; *p = 0;
        OSReport(g_lb);
    }
    if (!(S12CFG & S12F_PSLOG) || !in_game() || W(0x8049E6C8u + 0x88u) != 0x10u) return;   /* grkind 0x10 = Gr_Kind_PStadium */
    for (i = 0; i < 4; i++) {
        u32 g = W(0x8049E848u + 4u * i), gp, a, b, c;
        if (!ptr_like(g) || !ptr_like(gp = W(g + 0x2Cu))) continue;
        a = W(gp + 0xDCu); b = W(gp + 0xD8u); c = (W(gp + 0xE4u) & 0xFFFF0000u) | (W(gp + 0xECu) & 0xFFFFu);
        if (a != g_ps_last[i][0] || c != g_ps_last[i][2]) {      /* state / jumbotron changes (countdown ticks are printed with them) */
            char *p = g_lb;
            g_ps_last[i][0] = a; g_ps_last[i][2] = c;
            *p++ = 'S'; *p++ = '1'; *p++ = '2'; *p++ = 'P'; *p++ = ' ';
            p = hx(p, GLOBAL_FRAME); *p++ = ' '; p = hx(p, i); *p++ = ' '; p = hx(p, a); *p++ = ' '; p = hx(p, b); *p++ = ' '; p = hx(p, c);
            *p++ = ' '; p = hx(p, W(gp + 0xC4u)); *p++ = ' '; p = hx(p, rb_active);
            *p++ = '\n'; *p = 0;
            OSReport(g_lb);
        }
    }
}

/* Game night's pause callback (the game calls it from its pause check every match frame, in the game logic, so both consoles and every rollback re-run see the same
   synced pads): Slippi's ClientPause (Online/Core/InitPause.asm) without the pause itself.  A human port with stocks holding L+R+A and Start ends the game as
   Slippi's LRAS does: match struct 0x8046B6A0 +1 the quitter, +8 = 7 (LRAS exit), +0x24D5 = 0x1E (short GAME!), the HUD hidden.  Start alone does nothing.  The return value matters: -1, or the game opens its real pause for the port returned. */
static s32 gn_quit_cb(void)
{
    u32 port;
    for (port = 0; port < 4u; port++) {
        u32 held;
        if (((u32 (*)(u32))0x8003241Cu)(port) != 0u) continue;            /* PlayerBlock_LoadSlotType: humans only */
        if (((u32 (*)(u32))0x80033BD8u)(port) == 0u) continue;            /* PlayerBlock_LoadStocksLeft: out of stocks cannot quit */
        held = W(0x804C1FACu + port * 68u);
        if ((held & 0x1160u) == 0x1160u) {                                 /* L 0x40, R 0x20, A 0x100, Start 0x1000 */
            *(volatile u8 *)(0x8046B6A0u + 0x1u) = (u8)port;
            *(volatile u8 *)(0x8046B6A0u + 0x8u) = 7u;
            *(volatile u8 *)(0x8046B6A0u + 0x24D5u) = 0x1Eu;
            ((void (*)(void))0x802F3394u)();                               /* Pause_HideHUD */
            break;
        }
    }
    return -1;                                                             /* as ClientPause: -1 = the game does not pause */
}

static void s3_tick_body(void)
{
    u32 t0, dt, m;
#ifdef MM_BOOTLOOP
    /* TEST BUILD (RELEASE_TEST=bootloop): reboot the game 60 s after its first tick (35 s hung inside OSResetSystem while the title was still loading cards; the counter wraps at 70 s), counting boots in an ARM-owned word (0xD30266A0, the Wii kernel logs it) so a Wii can hunt its
       title hangs unattended: the loop stops at the first hang and the count is the number of clean boots before it (ALPHA_OPEN_FINDINGS 50).  Melee's own reset path as soft_reset in ml_glue.c. */
    {   static u32 bl_t0;
        u32 tn = tbl();
        if (!bl_t0) bl_t0 = tn | 1u;
        if (sd_plat_ms_ticks() >= 55000u && tn - bl_t0 > 60000u * sd_plat_ms_ticks()) {
            *(volatile u32 *)0xD30266A0u = *(volatile u32 *)0xD30266A0u + 1u;
            ((void (*)(void)) 0x80027DBC)();                        /* lbAudioAx_80027DBC */
            while (((int (*)(void)) 0x8001B6F8)() == 11) { }        /* lbCardNew_CompleteNextTask */
            ((void (*)(int)) 0x8034EBD0)(0);                        /* VISetPostRetraceCallback(0) */
            ((void (*)(int)) 0x8034EB8C)(0);                        /* VISetPreRetraceCallback(0) */
            ((void (*)(int)) 0x80350100)(1);                        /* VISetBlack */
            ((void (*)(void)) 0x8034FF78)();                        /* VIFlush */
            ((void (*)(void)) 0x8034F314)();                        /* VIWaitForRetrace */
            ((void (*)(void)) 0x8034F314)();
            ((void (*)(int, int, int)) 0x8034844C)(0, 0, 0);        /* OSResetSystem */
        }
    }
#endif
#if defined(MM_FTSTATS) && defined(MS_RB_READOUT)
    g_sp_bs = tbl();                                             /* : start of this body */
#endif
    match_store();
    ps_log();                                                    /* S19b TEST ONLY */
    fd_step();                                                   /* frozen Final Destination (see fd_step) */
    if (in_game()) ms_pause_policy(&ms_rules, (volatile u8 *)0x8046DB6Au, (volatile u32 *)(0x8046DB68u + 0x40u), (u32)gn_quit_cb);   /* the match's rules record decides (pause_policy.h): Grandmaster: no pause, no quit; Game night: no pause screen, L+R+A+Start still quits (option 1); the Slippi route and no record: untouched */
    if (in_game()) mem_watch(GLOBAL_FRAME);                     /* S19 */
    RBR_WATCH();                                                 /* B0 readout (lab builds only): in a match, every 16 frames */
    sfx_dump_step();                                             /* S6D TEST ONLY (counted as work of this frame) */
    { extern void s10_prof_step(void); s10_prof_step(); }       /* S10 TEST ONLY profiles */
    { extern void s11_site_step(void); s11_site_step(); }       /* S11 TEST ONLY allocation-site profile */
    if (in_game()) check_sscb(2);
    /* S7: the S6b debug-locator hash exchange over our own wire is removed (Slippi packets only) */
    STRESS_STEP();
#ifdef ON_BUILD
    { extern void bh_tick(void); bh_tick(); }                   /* ON: hold B 3 s to leave the CSS, hint and bar (menus/on/on_bhold.c) */
    { extern void bh_sss_tick(void); bh_sss_tick(); }
    { extern void bh_note_tick(void); bh_note_tick(); }         /* ON (S19): the in-match notice when rollback is off for memory (on_bhold.c) */
    { extern void bh_ping_tick(u32 active, u32 acks, u32 rtt, u32 var); bh_ping_tick(C.active >= 0 ? 1u : 0u, C.enet.st.rx_acks, C.active >= 0 ? C.enet.peer[C.active].rtt : 0u, C.active >= 0 ? C.enet.peer[C.active].rtt_var : 0u); }   /* ON: the in-match "Ping: N ms" hint, display only (on_bhold.c bh_ping_tick) */
#ifdef MS_QCHAT
    qc_pump();                                                   /* QUICK CHAT: the D-pad chat on the online character screen (menus/qc/qc_gc.c) */
#endif
#endif
#ifdef ON_BUILD    /* ON: the search's "no network" timeout (OD_E_NONET after net_ms) must also run when the adapter never comes up (no BBA: S3_NOBBA, init / link failure) or has no address yet */
    if (od_o.cfg && ((ST.state >= S3_NOBBA && ST.state != S7_SES_LOST) || ST.state <= S3_NET_DHCP)) od_tick(&od_o, clock_ms(0));   /* (od_o.cfg: the backend is set up by core_init below on the first call) the session states run od_tick from slice; od_tick sends nothing without an address */
#endif
#ifdef BR_BUILD
    br_tick(ST.state);                                           /* BR: the bridge's card poll and app-state byte, outside the lock */
#endif
    if (g_rc) {                                                  /* the full reconnect (ms_net_reconnect) */
        if (g_rc == 1) {
            u32 m = irq_off();
            g_net = 0; g_sock = -1;                              /* the b0 poll and the slice stop touching sdnet while the adapter is set up again */
#ifdef SD_CARD_HOLE
            ob_n = 0; ob_head = 0;                               /* queued datagrams belong to the old session */
#endif
            od_net_down(&od_o);                                  /* the hub client and the search wait for an address again */
#ifdef SD_CARD_HOLE
            if (g_nt_on) { nt_abort(&g_nt); g_nt_on = 0; }       /* a Test connection run dies with the stack (sdnet_init below clears its socket) */
#endif
            ST.ip = 0;
            ST.state = S3_NET_WAIT; g_link_frames = 0; g_relink = 0;
            g_rc = 2;
            irq_restore(m);
        } else if (g_rc == 2 && ST.state >= S3_NOBBA && ST.state != OD_NET_UP) g_rc = 0;     /* no adapter / no cable: the rows say No network, the cable probe goes on */
        else if (g_rc == 3 && (od_o.srv_ok_ms || (u32)(clock_ms(0) - g_rc_t) > RC_SERVER_MS)) g_rc = 0;   /* the server answered (or did not in time: the rows say No server) */
    }
    if (ST.state == S3_FAIL_LINK && ++g_relink >= 120u) { g_relink = 0; ST.state = S3_NET_LINK; g_link_frames = 840u; }   /* no cable at boot: look for the link again every 2 s (a 1 s probe), the adapter is already set up */
    if (ST.state >= S3_NOBBA && ST.state != S7_SES_LOST) return;   /* LOST (opponent gone) keeps the network slice running: peer_watch ends the session, the server step serves the next search (before: the slice stopped for good, nothing was sent again) */
    ST.calls++;
    BC(0x01);
    if (S3CFG & 1u) return;
    if (s7_cfg.s7_flags & S7F_LOOPBACK) { m = irq_off(); core_init(); irq_restore(m); return; }   /* S7: no network */
    m = irq_off();
    BC(0x02);
    core_init();
    if (ST.state == S3_NET_WAIT) {
        if (ST.calls < s3_cfg.start_frame) { irq_restore(m); return; }
        { extern u32 s7_stall_div, s7_new_div, s7_stall_min_ms; if (sd_plat_ms_ticks() >= 55000u) { s7_stall_div = 1u; s7_new_div = 1u; s7_stall_min_ms = 16u; } }      /* WII: a stalled frame re-sends its pad packet every third call, not every call (finding 54) */
        if (sd_plat_ms_ticks() >= 55000u && *(volatile u32 *)0xD302669Cu == 0x42434832u) { g_bch = 2u; g_bdev = 0u; }   /* WII kernel with the channel-2 BBA ('BCH2' at 0xD302669C): the adapter leaves the card's channel (finding 51) */
        if (bba_init_begin(&B, &OPS, 0, 0) != BBA_OK) { ST.state = S3_FAIL_INIT; irq_restore(m); return; }
        /* Wii receive savings OFF again while the 1130 no-IP report is being sorted out (ALPHA_OPEN_FINDINGS 48); to switch on, define MS_WII_RXFAST */
#if 1      /* burst read + batched RRP on (the hint stays dead: the d20t kernel publishes no RWP); bisect step 2 of ALPHA_OPEN_FINDINGS 48 */
        if (sd_plat_ms_ticks() >= 55000u) { B.rx_burst = 1; B.rx_hint = (volatile const u32 *)0xD3026670u; B.wii_fast = 1; }      /* WII: one DMA per received frame, RRP written in batches, idle polls answered by the kernel's published RWP; EXI CUT: a send in one DMA + one NCRA write, one RWP read per poll (never on a GameCube) */
#endif
        ST.state = S3_NET_INIT;
    }
    t0 = tbl();
    if (lock()) {
        BC(0x03);
        slice();
        BC(0x04);
        unlock();
        BC(0x05);
    } else ST.slice_skip++;
    irq_restore(m);
    BC(0x06);
    dt = tbl() - t0;
    if (dt > ST.tick_max) ST.tick_max = dt;
    if (ST.state >= S7_SES_CONNECTING && ST.state < S3_NOBBA)   /* S7 */ { ST.run_ticks++; ST.tick_run_sum += dt; }
}

/* ======================================================================================================== S10 */
#ifdef SD_NO_TESTPROF   /* the S10 TEST ONLY per-proc / write / input-equivalence profiles (s10_flags PPROF 2, WPROF 0x10, EQPROF 0x40; every product contract
                          has 0x101) can never switch on: constant 0, the compiler drops their code from the logic hooks */
static const u32 g_eq_on = 0, g_pp_on = 0, g_w_on = 0;
static u32 g_prof_state, g_prof_i, g_prof_j;
#else
static u32 g_eq_on, g_pp_on, g_w_on, g_prof_state, g_prof_i, g_prof_j;
#endif
static s32 g_mid_start[2];                                      /* motion id of both fighters at the iteration start */

/* ---- per-proc profile (TEST ONLY): by function (open addressing) and by p_link ---- */
#define PP_N 256u
struct pp { u32 fn, pl, n, t, rng, n_rs, t_rs, rng_rs, skip, reseed; };
static struct pp *g_pp;                                         /* HSD heap, PP_N entries */
struct pl { u32 n, t, rng, n_rs, t_rs, rng_rs, skip; };
static struct pl *g_pl;                                         /* 66 entries: 64 = logic outside procs before/between
                                                                   them, 65 = after the last proc (n counts iterations) */
static u32 g_seed_last;                                         /* seed after the last proc of this iteration */
static u32 g_reseeds;                                           /* procs whose call moved the seed by > 4096 LCG steps */
static u32 g_pp_full;
static struct pp *pp_get(u32 fn, u32 pl)
{
    u32 h = (fn >> 2) * 2654435761u, i, k;
    for (i = 0; i < PP_N; i++) {
        k = (h + i) & (PP_N - 1u);
        if (g_pp[k].fn == fn) return &g_pp[k];
        if (!g_pp[k].fn) { g_pp[k].fn = fn; g_pp[k].pl = pl; return &g_pp[k]; }
    }
    g_pp_full++;
    return 0;
}

typedef void (*proc_fn)(u32);
static const u32 S12_CLS[12][2] = {                              /* S12: {proc function, class flag} */
    { 0x8006D9ACu, S12F_C_DYN }, { 0x8026A788u, S12F_C_DYN },
    { 0x8002F360u, S12F_C_CAM }, { 0x8006D9ECu, S12F_C_CAM },
    { 0x801C461Cu, S12F_C_COSM }, { 0x8009F480u, S12F_C_COSM }, { 0x80021B04u, S12F_C_COSM }, { 0x800204C8u, S12F_C_COSM },
    { 0x802FED14u, S12F_C_COSM }, { 0x801A0E34u, S12F_C_COSM },
    { 0x8005BC50u, S12F_C_EFLIB }, { 0x801C9C40u, S12F_C_QUAKE } };
static u32 g_reseeded, g_seed_pre, g_reseed_fn;                 /* S10: Slippi's reseed proc of this frame has run */
/* replaces the `blrl` at 0x80390DFC in HSD_GObj_RunProcs: r3 = gobj; HSD_GObj_CurrentInvokedProc (0x804D7838) was
   stored just before; HSD_GObj p_link at +2. */
/* S12 (on S11): ifStock_804A1378 (.bss, 0x3FC; if/types.h:253): per player p the IfStockData at +0x204 + p * 0x54
; x0[10] / x0[11] are the two stock-steal animation slots. */
static int s12_steal_busy(void)
{
    u32 p;
    for (p = 0; p < 6u; p++) {
        const volatile u8 *d = (const volatile u8 *)(0x804A1378u + 0x204u + p * 0x54u);
        if (d[10] || d[11]) return 1;
    }
    return 0;
}
static u32 g_dyn_rs;                                            /* S12: inside a DYNB proc of a re-simulated frame */
void s10_invoke(u32 gobj)
{
    u32 proc = W(0x804D7838), fn = ptr_like(proc) ? W(proc + 0x14) : 0, pl, rs, t0, s0, dt, st;
    struct pp *e;
    if (!fn) return;                                            /* cannot happen (the original would crash) */
    if (!(S10CFG & (S10F_TRIM | S10F_PPROF)) && !(S12CFG & (S12F_TRIMX | S12F_C_ANY))) { ((proc_fn)fn)(gobj); return; }
    if (!in_game() || !g_in_logic) { ((proc_fn)fn)(gobj); return; }
    pl = ptr_like(gobj) ? *(volatile u8 *)(gobj + 2) : 64u;
    if (pl > 63u) pl = 63u;
    rs = rb_active && (s32)g_cur_frame < rb_e;
    if (!g_reseeded) {                                          /* S10: before this frame's reseed: no draw allowed */
        u32 s0 = rng_now();
        if (s0 != g_seed_pre) {
            if (s10_lcg_steps(g_seed_pre, s0, 64u) <= 64u) XL[X_PRESEED_DRAWS]++;
            g_seed_pre = s0;
        }
        if (fn == g_reseed_fn || (!g_reseed_fn && pl == 7u)) {   /* Slippi's reseed proc (learned on its first call) */
            u32 a = rng_now(), b2;
            ((proc_fn)fn)(gobj);
            b2 = rng_now();
            if (fn == g_reseed_fn || s10_lcg_steps(a, b2, 4096u) > 4096u) {
                g_reseed_fn = fn;
                g_reseeded = 1;
                g_seed_last = g_seed_pre = b2;
            }
            return;
        }
    }
    if (rs) XL[X_INV_RS_N]++; else XL[X_INV_N]++;
    if (rs && fn == 0x802F428Cu && (S10CFG & S10F_TRIM) && W(0x8046B6C8u) <= 6u) {   /* S12 fix of the S10 trim:
           ifTime_UpdateTimers fires the 5-second countdown exactly at 5.00 (205-225, loads a joint, frees the
           match timer); never skip it in the last 6 seconds of a timed match */
        ZL[Z_TIMER_KEEP]++;
        ((proc_fn)fn)(gobj);
        return;
    }
    if (rs && fn == 0x802F9410u && (S10CFG & S10F_TRIM) && (S12CFG & S12F_STOCK_KO) && s12_stocks_now() != g_stk_ring[(g_cur_frame - 1u) & 63u]) {   /* a stock changed since the end of the previous
           iteration, i.e. a KO happens in this re-simulated frame; this proc spawns the stock-loss effect (ifStock_802F8298 -> efSync_Spawn 0x474-0x476) */
        ((proc_fn)fn)(gobj);
        return;
    }
    if (rs && fn == 0x802F9410u && (S10CFG & S10F_TRIM) && s12_steal_busy()) {   /* S12 (on S11) fix of the S10
           trim: the stock HUD proc steps the team stock-steal animation slots, and gameplay reads them (gm/gmvs.c:590 ->
           ifStock_802F7EFC, if/ifstock.c:70-76: a steal is refused while both slots animate); never skip it then */
        ZL[Z_STEAL_KEEP]++;
        ((proc_fn)fn)(gobj);
        return;
    }
    if (rs && (S12CFG & S12F_C_ANY)) {                   /* S12: built-in trim classes */
        u32 i;
        for (i = 0; i < sizeof S12_CLS / sizeof S12_CLS[0]; i++)
            if (fn == S12_CLS[i][0] && (S12CFG & S12_CLS[i][1])) {
                ZL[Z_C_SKIP + i]++;
                XL[X_INV_SKIP]++;
                if (g_pp_on && (e = pp_get(fn, pl)) != 0) { e->skip++; if (e->n) XL[X_INV_T_SKIPPED_EST] += e->t / e->n; }
                if (g_pp_on) g_pl[pl].skip++;
                return;
            }
    }
    if (rs && (S12CFG & S12F_TRIMX)) {                   /* S12: more trim classes (cfg trim_fn[]) */
        u32 i;
        for (i = 0; i < 8; i++)
            if (fn == s12_cfg.trim_fn[i]) {
                ZL[Z_X_SKIP + i]++;
                XL[X_INV_SKIP]++;
                if (g_pp_on && (e = pp_get(fn, pl)) != 0) { e->skip++; if (e->n) XL[X_INV_T_SKIPPED_EST] += e->t / e->n; }
                if (g_pp_on) g_pl[pl].skip++;
                return;
            }
    }
    if (rs && (S10CFG & S10F_TRIM) &&
        (s10_in_mask(s10_cfg.trim_lo, s10_cfg.trim_hi, pl) || fn == s10_cfg.trim_fn[0] || fn == s10_cfg.trim_fn[1] ||
         fn == s10_cfg.trim_fn[2] || fn == s10_cfg.trim_fn[3])) {
        XL[X_INV_SKIP]++;
        if (g_pp_on && (e = pp_get(fn, pl)) != 0) { e->skip++; if (e->n) XL[X_INV_T_SKIPPED_EST] += e->t / e->n; }
        if (g_pp_on) g_pl[pl].skip++;
        return;
    }
    if (rs && (S12CFG & S12F_C_DYNB) && (fn == 0x8006D9ACu || fn == 0x8026A788u)) g_dyn_rs = 1;   /* S12: DYNB */
    if (!g_pp_on) { ((proc_fn)fn)(gobj); g_dyn_rs = 0; return; }
    s0 = rng_now();
    if (s0 != g_seed_last) {                                    /* RNG used by the logic outside procs since the last one */
        u32 g = s10_lcg_steps(g_seed_last, s0, 4096u);
        if (g <= 4096u) { if (rs) g_pl[64].rng_rs += g; else g_pl[64].rng += g; } else g_reseeds++;
    }
    t0 = tbl();
    ((proc_fn)fn)(gobj);
    dt = tbl() - t0;
    g_dyn_rs = 0;                                               /* S12 */
    st = rng_now();
    g_seed_last = st;
    st = st != s0 ? s10_lcg_steps(s0, st, 4096u) : 0u;
    if (st > 4096u) {                                           /* a reseed (Slippi's per-frame RNG seed), not draws */
        g_reseeds++;
        if ((e = pp_get(fn, pl)) != 0) e->reseed++;
        st = 0;
    }
    g_proc_steps += st;
    if (rs) { XL[X_INV_T_RS] += dt; XL[X_RNG_RS] += st; } else { XL[X_INV_T_NORM] += dt; XL[X_RNG_NORM] += st; }
    if (st && (S10CFG & S10F_TRIM) && s10_in_mask(s10_cfg.trim_lo, s10_cfg.trim_hi, pl)) XL[X_RNG_TRIMCLS] += st;
    if ((e = pp_get(fn, pl)) != 0) {
        if (rs) { e->n_rs++; e->t_rs += dt; e->rng_rs += st; } else { e->n++; e->t += dt; e->rng += st; }
    }
    if (rs) { g_pl[pl].n_rs++; g_pl[pl].t_rs += dt; g_pl[pl].rng_rs += st; } else { g_pl[pl].n++; g_pl[pl].t += dt; g_pl[pl].rng += st; }
}

/* ---- W profile (TEST ONLY): which manifest ranges differ from M at a logic start that follows a post-frame capture
   (outer = a render / main-loop pass ran in between; inner = between two iterations of one pass) ---- */
#define WP_N 192u
struct wp { u32 kind, key, size, a0, fo, lo_, fi, li; };       /* static: key = lo; cell: key = first word (class) */
static struct wp *g_wp;
static u32 g_wp_n, g_wp_full, g_w_outer, g_w_lines;
static void wprof_cb(u32 kind, const struct sd_range *r, u32 nl, u32 a0)
{
    u32 key = kind ? W(r->lo) : r->lo, size = r->hi - r->lo, i;
    if (kind && !(key >= 0x80003000u && key < 0x80500000u)) key = 0;   /* not a DOL-data pointer: class unknown */
    g_w_lines += nl;
    for (i = 0; i < g_wp_n; i++)
        if (g_wp[i].kind == kind && g_wp[i].key == key && g_wp[i].size == size) break;
    if (i == g_wp_n) {
        if (g_wp_n >= WP_N) { g_wp_full++; return; }
        g_wp[i].kind = kind; g_wp[i].key = key; g_wp[i].size = size; g_wp[i].a0 = a0 - r->lo;
        g_wp_n++;
    }
    if (g_w_outer) { g_wp[i].fo++; g_wp[i].lo_ += nl; } else { g_wp[i].fi++; g_wp[i].li += nl; }
}

/* ---- EQ profile (TEST ONLY): end-of-logic state per frame in the current timeline; a re-simulated frame is compared
   with the entry of the timeline it replaces ---- */
#define EQ_N 64u
#define FTI_OFF 0x620u                                          /* fp->input (ft/types.h:1403), 20 words */
#define FTI_END 0x68Cu                                          /* .. input timers up to fp+0x68B */
struct eqe { s32 f; u32 id, gp, gpn, inp[2][20]; s32 mid[2]; };
static struct eqe *g_eq;
#define EQM_N 96u
struct eqm { s32 mid; u32 a, b, c; };                           /* per motion id of the mispredicted fighter */
static struct eqm *g_eqm;
static u32 g_eqm_n, g_eq_mask[32][3];                           /* [input diff mask][class A/B/C] */
static u32 g_eq_frames, g_eq_gpd, g_eq_gpnd, g_eq_first, g_eq_firstk, g_eq_firstm, g_eq_firstf, g_eq_cdelay;
static u32 g_eq_cdh[8];                                         /* C: frames from the first input difference to the first gameplay difference */
static u32 hash_gp_end(u32 *gpn, u32 inp[2][20], s32 mid[2])
{
    u32 gp = 2166136261u, n = 2166136261u, p, k, i;
    for (p = 0; p < 4; p++) {
        u32 st = STATIC_P0 + p * STATIC_STRIDE;
        gp = hash_block(gp, st, 0xE8u);
        n = hash_block(n, st, 0xE8u);
    }
    for (k = 0; k < 2; k++) {
        u32 fd = fighter(STATIC_P0 + k * STATIC_STRIDE, 0xB0);
        mid[k] = -1;
        for (i = 0; i < 20; i++) inp[k][i] = 0;
        if (!fd) continue;
        gp = hash_fighter_gp(gp, fd);
        n = hash_block(n, fd, FTI_OFF);                          /* gpn: the gameplay hash without the input block ... */
        {
            u32 h2 = 2166136261u, j;
            const volatile u32 *q = (const volatile u32 *)fd;
            for (j = FTI_END / 4; j < FIGHTER_SIZE / 4; j++) {   /* ... with the gp masks of hash_fighter_gp */
                u32 w = q[j];
                if (j >= 0x2144u / 4 && j < 0x2164u / 4) continue;
                if (j == 0x2224u / 4) w &= ~0x00000001u;
                else if (j == 0x2228u / 4) w &= ~0x80000000u;
                else if (j == 0x2188u / 4 || j == 0x218Cu / 4) w = 0;
                else if (j == 0x221Cu / 4) w &= ~0x00000080u;
                else if (j == 0x2140u / 4) w = 0;
                else if (j == 0x20A4u / 4) w &= 0x00FFFFFFu;
                if (!ptr_like(w)) h2 = fnv_w(h2, w);
            }
            n = fnv_w(n, h2);
        }
        for (i = 0; i < 20; i++) inp[k][i] = W(fd + FTI_OFF + 4 * i);
        mid[k] = (s32)W(fd + 0x10);
    }
    *gpn = fnv_w(n, RNG_SEED);
    return fnv_w(gp, RNG_SEED);
}
static void eq_rb_begin(void) { g_eq_frames = g_eq_gpd = g_eq_gpnd = g_eq_first = 0; g_eq_cdelay = 0xFFFFFFFFu; }
static void eq_rb_end(void)
{
    u32 cls, i;
    if (!g_eq_on) return;
    XL[X_EQ_RB]++;
    if (!g_eq_frames) { XL[X_EQ_NOOLD]++; return; }
    cls = !g_eq_gpd ? 0u : !g_eq_gpnd ? 1u : 2u;                  /* A: nothing differed; B: input block only; C: more */
    XL[X_EQ_A + cls]++;
    XL[X_EQ_FRAMES] += g_eq_frames;
    g_eq_mask[g_eq_first ? (g_eq_firstm & 31u) : 0u][cls]++;
    if (cls == 2u && g_eq_cdelay != 0xFFFFFFFFu) g_eq_cdh[g_eq_cdelay > 7u ? 7u : g_eq_cdelay]++;
    if (g_eq_first) {
        for (i = 0; i < g_eqm_n && g_eqm[i].mid != (s32)g_eq_firstk; i++) ;
        if (i == g_eqm_n && g_eqm_n < EQM_N) { g_eqm[i].mid = (s32)g_eq_firstk; g_eqm[i].a = g_eqm[i].b = g_eqm[i].c = 0; g_eqm_n++; }
        if (i < g_eqm_n) { if (cls == 0u) g_eqm[i].a++; else if (cls == 1u) g_eqm[i].b++; else g_eqm[i].c++; }
    }
}

void s10_match_start(void)
{
    u32 i;
    g_b2_refused = 0; g_b2_inj = 0; g_fresh = 0; g_loadcap = 0; g_rbframe = 0; g_b1_seen = -1; g_after_restore = 0; g_seed_have = 0; g_rb_id = 0; g_reseed_fn = 0;
    g_prof_state = 0;
#ifndef SD_NO_TESTPROF
    g_eq_on = g_pp_on = g_w_on = 0;
#endif
    g_pp = 0; g_pl = 0; g_wp = 0; g_eq = 0; g_eqm = 0; g_wp_n = 0; g_eqm_n = 0;
    g_loc = 0; g_loc_tried = 0; g_early_pending = 0; g_rb_early = 0; g_early_pass = 0; g_render_skip_next = 0;   /* S12 */
    g_neg_early_done = 0; g_rx_new = 0;
    ZL[Z_FLAGS] = s12_cfg.flags;
    for (i = 0; i < 32; i++) g_eq_mask[i][0] = g_eq_mask[i][1] = g_eq_mask[i][2] = 0;
    for (i = 0; i < 8; i++) g_eq_cdh[i] = 0;
    XL[X_FLAGS] = s10_cfg.flags;
    sd_mc.rs_sel = s10_cfg.rs_sel;
    sd_mc.nwx = s10_cfg.nwx;
}
/* TEST ONLY tables live in HSD-heap blocks larger than SD_CELL_MAX (0x2400): such cells are not in the snapshot
   manifest, so a restore never rewinds the profile counters (found in run p1: smaller tables were rolled back) */
static void *prof_alloc(u32 n)
{
    u32 *p = (u32 *)safe_alloc(n < 0x2600u ? 0x2600u : n, S19_TEST_KEEP), i;   /* S19: 0 when it does not fit */
    if (p) for (i = 0; i < (n < 0x2600u ? 0x2600u : n) / 4; i++) p[i] = 0;
    return p;
}
static void s10_alloc(void)                                     /* first iteration of the match (after the store) */
{
#ifdef SD_NO_TESTPROF
    (void)prof_alloc;
#else
    u32 i;
    if ((S10CFG & S10F_PPROF) && !g_pp) {
        g_pp = (struct pp *)prof_alloc(PP_N * sizeof(struct pp));
        g_pl = (struct pl *)prof_alloc(66u * sizeof(struct pl));
        if (g_pp && g_pl) {
            g_pp_on = 1;
        } else XL[X_ERR] |= 1u;
    }
    if ((S10CFG & S10F_WPROF) && !g_wp) {
        g_wp = (struct wp *)prof_alloc(WP_N * sizeof(struct wp));
        if (g_wp) g_w_on = 1;
        else XL[X_ERR] |= 2u;
    }
    if ((S10CFG & S10F_EQPROF) && !g_eq) {
        g_eq = (struct eqe *)prof_alloc(EQ_N * sizeof(struct eqe));
        g_eqm = (struct eqm *)prof_alloc(EQM_N * sizeof(struct eqm));
        if (g_eq && g_eqm) { for (i = 0; i < EQ_N; i++) g_eq[i].f = -1; g_eq_on = 1; }
        else XL[X_ERR] |= 4u;
    }
#endif
}

/* S11 */
static u32 g_site_state, g_site_i;
u32 sd_site_enable(void) { return SD_SITES_ON && ((s11_cfg.cold & 1u) || s11_cfg.site_prof); }   /* allocation sites from boot on */
u32 sd_site_epoch(void) { return SCENE_WORD & 0xFF0000FFu; }   /* major, minor */
void s11_match_start(void)
{
    u32 i;
    sd_s17 = S17CFG & 0xFFFFu;                                  /* S17: engine kernels (0 = the S16 engine) */
    sd_build_bulk = (S12CFG & S12F_NOBULK) ? 0u : 1u;                  /* START_UP_STALL: one-sort manifest build (default on) */
    s17_relax_on = (unsigned)s17_relax_decide(S17CFG, C.side, s7_core_is_slippi(&C));   /* S17 RELAX: relaxed main-stick compare; OFF against a real Slippi client (D11) */
    s17_relax_neg = ((S17CFG & S17F_RELAX_NEG1) ? 1u : 0u) | ((S17CFG & S17F_RELAX_NEG2) ? 2u : 0u);
    g_nv = g_v_loads = g_v_since = g_v_latched = 0;
    if (S17CFG & S17F_PACE_MODE) {                     /* S18 pacing: mode selection at match start (never changed inside a game) */
        struct pc_cfg pcf, prev;
        prev.ts_mode = (s32)s7_cfg.ts_mode; prev.delay_floor = 2; prev.delay_cap = 7; prev.use_guard = 0; prev.equal_delay = 0; prev.peer_level = 0;
        pc_select(&pcf, ((S17CFG & S17F_PACE_CONSOLE) && !s7_core_is_slippi(&C)) ? PC_PEER_CONSOLE : PC_PEER_SLIPPI, 0u, 0, &prev);
        s7_cfg.ts_mode = (u32)pcf.ts_mode;
        QL[Q_PACE_MODE] = (u32)pcf.ts_mode;
    }
    s17_relax_reset();
    s12_relax_hook = s17_relax_on ? s17_relax_hook_fn : 0;
    sd_strict = (S16CFG & S16F_NOSTRICT) ? 0u : 1u;   /* S16 */
    sd_nofp_force = (S16CFG & S16F_NOFP_FORCE) ? 1u : 0u;
    sd_arena_limit = s11_cfg.s16_arena_kb << 10;
    sd_log_pct = s11_cfg.s16_log_pct;
    g_gate_on = 0; g_gate_run = 0; g_gate_reb_f = 0; g_probe_done = 0;
    sd_fp = s11_cfg.fp & 1u;
    sd_fp_bits = s11_cfg.fp_bits && s11_cfg.fp_bits < 64u ? s11_cfg.fp_bits : 64u;
    sd_fpv = s11_cfg.fpv;
    sd_fpv_fix = s11_cfg.fpv_fix & 1u;
    if (s11_cfg.fp & 2u) {                                       /* a fresh key per match */
        u32 t = tbl();
        sd_fp_key[0] = (t * 0x9E3779B9u) ^ 0x6A09E667u;
        sd_fp_key[1] = ((t >> 7) * 0x85EBCA6Bu) ^ GLOBAL_FRAME ^ 0xBB67AE85u;
    }
    for (i = 0; i < sizeof sd_x11 / 4; i++) ((u32 *)&sd_x11)[i] = 0;
    for (i = 0; i < sizeof sd_x11c / 4; i++) ((u32 *)&sd_x11c)[i] = 0;
    sd_cold = (s11_cfg.cold & 1u) ? (s11_cfg.cold & 3u) : 0u;    /* S11 part 2: cold-site mode (bit 1 TEST ONLY: no rescan after a restore) */
    sd_cold_n = s11_cfg.cold_n ? s11_cfg.cold_n : 8u;
    sd_cold_mark = sd_site_enable();                             /* S11: invalidation marks + unexplained-change profile */
    g_site_state = 0;
    YL[Y_FP] = s11_cfg.fp; YL[Y_BITS] = sd_fp_bits; YL[Y_FPV] = sd_fpv; YL[Y_S16_FLAGS] = s11_cfg.s16_flags;
    YL[Y_KEYA] = sd_fp_key[0]; YL[Y_KEYB] = sd_fp_key[1];
}

static void s16_tel_copy(void)                                  /* S16 */
{
    YL[Y_S16_LOST_RANGES] = sd_x16.lost_ranges; YL[Y_S16_LOST_BYTES] = sd_x16.lost_bytes;
    YL[Y_S16_REFUSED] = sd_x16.refused; YL[Y_S16_REB_OK] = sd_x16.rebuilds_ok; YL[Y_S16_REB_FAIL] = sd_x16.rebuilds_fail;
    YL[Y_S16_ARENA_HI] = sd_x16.arena_hi; YL[Y_S16_FP_LIM] = sd_x16.fp_lim; YL[Y_S16_FP_COV] = sd_x16.fp_cov_bytes;
    YL[Y_S16_INC_CAPS] = sd_x16.inc_caps; YL[Y_S16_INC_ONSETS] = sd_x16.inc_onsets;
    ZL[Z_S16_ARENA_B] = sd_x16.arena_bytes; ZL[Z_S16_FREE] = sd_x16.free_bytes; ZL[Z_S16_STEP_NEW_MAX] = sd_x16.step_new_max;
}

static void s11_tel_copy(void)
{
    YL[Y_LINES] = sd_x11.fp_lines; YL[Y_HIT] = sd_x11.fp_hit; YL[Y_HOLED] = sd_x11.fp_holed;
    YL[Y_VCAPS] = sd_x11.fpv_caps; YL[Y_VLINES] = sd_x11.fpv_lines; YL[Y_VCHG] = sd_x11.fpv_chg;
    YL[Y_VMISS] = sd_x11.fpv_miss; YL[Y_VMISS_CAPS] = sd_x11.fpv_miss_caps; YL[Y_VMISS_A0] = sd_x11.fpv_miss_a0;
    YL[Y_TAB_BAD] = sd_x11.fpv_tab_bad; YL[Y_FALSE] = sd_x11.fpv_false;
    YL[Y_SH8] = sd_x11.shadow[0]; YL[Y_SH12] = sd_x11.shadow[1]; YL[Y_SH16] = sd_x11.shadow[2];
    YL[Y_SH20] = sd_x11.shadow[3]; YL[Y_SH24] = sd_x11.shadow[4]; YL[Y_BASE_LINES] = sd_x11.fp_base_lines;
    YL[Y_TCAP_SUM] = sd_x11.t_cap_sum; YL[Y_TCAP_N] = sd_x11.t_cap_n;
    YL[Y_TCAPV_SUM] = sd_x11.t_cap_vsum; YL[Y_TCAPV_N] = sd_x11.t_cap_vn; YL[Y_LINES_SUM] = sd_x11.lines_sum;
    YL[Y_COLD_SKIP_LINES] = sd_x11c.skip_lines; YL[Y_COLD_CELLS] = sd_x11c.cells_cold; YL[Y_COLD_SCANS] = sd_x11c.scans;
    YL[Y_COLD_INVAL] = sd_x11c.inval; YL[Y_COLD_MISS] = sd_x11c.miss_lines; YL[Y_COLD_VLINES] = sd_x11c.vlines;
    YL[Y_SITES] = sd_ss.nsites; YL[Y_COLD_BYTES] = sd_x11c.bytes_cold_last;
    YL[Y_COLD_TOUCH] = sd_x11c.touch; YL[Y_COLD_RESCANS] = sd_x11c.rescans; YL[Y_COLD_MISS_CAPS] = sd_x11c.miss_caps; YL[Y_COLD_SKIP_CAPS] = sd_x11c.skip_caps;
}

/* S11 TEST ONLY: allocation-site profile at global frame site_frame (OSReport "S11H" header, "S11S" per site:
   idx lr1..lr5 allocs live_n live_bytes live_changed_n live_changed_bytes chg_sum age_sum dead_n dead_changed_n dead_age;
   "S11T": idx dead_chg_sum cold live_uchg dead_uchg (changes no invalidation explains)) */
static void s11_words(char tag, const u32 *v, u32 n)
{
    char *p = g_lb;
    u32 i;
    *p++ = 'S'; *p++ = '1'; *p++ = '1'; *p++ = tag; *p++ = ' ';
    for (i = 0; i < n; i++) { if (i) *p++ = ' '; p = hx(p, v[i]); }
    *p++ = '\n'; *p = 0;
    OSReport(g_lb);
}
#ifdef SD_NO_SITES   /* the site tables are compiled out (sd_melee.h): nothing to print; s11_cfg.site_prof is 0 in every product contract */
void s11_site_step(void) { (void)g_site_i; (void)s11_words; }
#else
void s11_site_step(void)
{
    u32 t0 = tbl(), v[16];
    if (!s11_cfg.site_prof || g_site_state >= 2u || !sd_active || !in_game() || (s32)GLOBAL_FRAME < (s32)s11_cfg.site_frame) return;
    if (g_site_state == 0u) {
        sd_site_aggregate();
        v[0] = GLOBAL_FRAME; v[1] = sd_st.captures; v[2] = sd_seq(); v[3] = sd_ss.nsites; v[4] = sd_ss.site_full; v[5] = sd_ss.wipes;
        v[6] = sd_ss.walk_bad; v[7] = sd_ss.unknown_n; v[8] = sd_ss.unknown_b; v[9] = sd_st.cells; v[10] = sd_st.bytes; v[11] = sd_ss.cold_listed;
        s11_words('H', v, 12);
        g_site_state = 1u; g_site_i = 1;
    }
    while (tbl() - t0 < sdg_gc_ticks(200000u) && g_site_i < SD_SITES) {
        const struct sd_site *e = &sd_sites[g_site_i];
        if (e->lr[0]) {
            v[0] = g_site_i; v[1] = e->lr[0]; v[2] = e->lr[1]; v[3] = e->lr[2]; v[4] = e->lr[3]; v[5] = e->lr[4]; v[6] = e->allocs;
            v[7] = e->live_n; v[8] = e->live_b; v[9] = e->live_chg_n; v[10] = e->live_chg_b; v[11] = e->chg_sum; v[12] = e->age_sum;
            v[13] = e->dead_n; v[14] = e->dead_chg_n; v[15] = e->dead_age;
            s11_words('S', v, 16);
            v[0] = g_site_i; v[1] = e->dead_chg_sum; v[2] = e->cold; v[3] = e->live_uchg; v[4] = e->dead_uchg;
            s11_words('T', v, 5);
        }
        g_site_i++;
    }
    if (g_site_i >= SD_SITES) g_site_state = 2u;
}
#endif

static void s10_capture_done(void)
{
    s11_tel_copy();                                              /* S11 */
    s16_tel_copy();                                              /* S16 */
    g_loadcap = 0;
    if (g_base_at_load != 0xFFFFFFFFu && sd_st.base_captures != g_base_at_load) XL[X_BASE_AFTER_LOAD]++;
    g_base_at_load = 0xFFFFFFFFu;
}

static void s10_iter_start(u32 f)
{
    u32 s = rng_now(), k;
    if (!g_pp && !g_wp && !g_eq && (S10CFG & (S10F_PPROF | S10F_WPROF | S10F_EQPROF)) && sd_active) s10_alloc();
    if (!g_loc && !g_loc_tried && (S12CFG & (S12F_LOC | S12F_LOCW)) && sd_active) {   /* S12 TEST ONLY */
        g_loc_tried = 1;
        g_loc = (u32 *)prof_alloc(64u * LOC_REC * 4u);
        if (g_loc) { u32 i; for (i = 0; i < 64u; i++) g_loc[i * LOC_REC] = 0xFFFFFFFFu; } else ZL[Z_E_ERR] |= 0x10000u;
    }
    if (g_seed_have && g_pass_iters == 0 && s != g_seed_end && !g_after_restore)
        XL[X_RNG_OUTER] += s10_lcg_steps(g_seed_end, s, 4096u);    /* RNG used outside the logic (render, main loop) */
    g_seed_iter0 = s;
    g_seed_last = s;
    g_reseeded = 0;
    g_seed_pre = s;
    g_proc_steps = 0;
    if (g_w_on && g_fresh && !g_after_restore && sd_active && f <= 0x7FFFFFFFu && g_prof_state == 0) {
        u32 nl;
        g_w_outer = g_pass_iters == 0;
        g_w_lines = 0;
        nl = sd_diff(wprof_cb);
        (void)nl;
        if (g_w_outer) { XL[X_W_OUT_N]++; XL[X_W_OUT_LINES] += g_w_lines; if (g_w_lines > XL[X_W_OUT_MAX]) XL[X_W_OUT_MAX] = g_w_lines; }
        else { XL[X_W_IN_N]++; XL[X_W_IN_LINES] += g_w_lines; if (g_w_lines > XL[X_W_IN_MAX]) XL[X_W_IN_MAX] = g_w_lines; }
    }
    if (g_eq_on)
        for (k = 0; k < 2; k++) { u32 fd = fighter(STATIC_P0 + k * STATIC_STRIDE, 0xB0); g_mid_start[k] = fd ? (s32)W(fd + 0x10) : -1; }
    g_fresh = 0;
    g_after_restore = 0;
    g_pass_iters++;
}

static void s10_iter_end(u32 f)
{
    u32 s = rng_now();
    int rs = rb_active && (s32)f < rb_e;
    g_seed_end = s;
    g_seed_have = 1;
    if (g_pp_on) {                                              /* RNG used by the logic after the last proc */
        u32 g = s != g_seed_last ? s10_lcg_steps(g_seed_last, s, 4096u) : 0u;
        if (g > 4096u) { g_reseeds++; g = 0; }
        if (rs) { g_pl[65].n_rs++; g_pl[65].rng_rs += g; g_pl[64].n_rs++; } else { g_pl[65].n++; g_pl[65].rng += g; g_pl[64].n++; }
    }
    if (g_eq_on) {
        struct eqe *e = &g_eq[f & (EQ_N - 1u)];
        u32 gpn, inp[2][20], k;
        s32 mid[2];
        u32 gp = hash_gp_end(&gpn, inp, mid);
        if (rs && e->f == (s32)f && e->id != g_rb_id) {          /* the replaced timeline ran this frame */
            u32 m = 0, kk = 0;
            g_eq_frames++;
            for (k = 0; k < 2; k++) { u32 d = s10_inp_diff(e->inp[k], inp[k]); if (d && !m) kk = k; m |= d; }
            if (m && !g_eq_first) { g_eq_first = 1; g_eq_firstm = m; g_eq_firstk = (u32)g_mid_start[kk]; g_eq_firstf = f; }
            if (gp != e->gp) g_eq_gpd = 1;
            if (gpn != e->gpn) { if (!g_eq_gpnd && g_eq_first) g_eq_cdelay = f - g_eq_firstf; g_eq_gpnd = 1; }
            if (gp != e->gp && gpn == e->gpn) XL[X_EQ_IN_ONLY_FR]++;
        }
        e->f = (s32)f; e->id = g_rb_id; e->gp = gp; e->gpn = gpn;
        for (k = 0; k < 2; k++) { u32 i; e->mid[k] = mid[k]; for (i = 0; i < 20; i++) e->inp[k][i] = inp[k][i]; }
    }
    /* post-frame capture of frame f + 1 */
    if (sd_active && s10_post() && in_game()) {
        int pred = g_b1_seen == (s32)f || rs || (S10CFG & (S10F_FORCECAP | S10F_WPROF));
        if (pred && want_capture((s32)f + 1)) {
            u32 t0 = tbl(), dt;
            int r = sd_capture((s32)f + 1);
            dt = tbl() - t0;
            cost_wait(dt);                                       /* S6D TEST ONLY cost emulation (as B1) */
            dt = tbl() - t0;
            TL[T_CAPCOST] = ema(TL[T_CAPCOST], dt);
            RB[RB_B1_SUM] += dt;
            if (dt > RB[RB_B1_MAX]) RB[RB_B1_MAX] = dt;
            if (rs) RB[RB_B1_RESIM_SUM] += dt;
            XL[X_POSTCAP]++;
            if (!r) { s10_capture_done(); g_fresh = 1; } else RB[RB_B1_FAIL]++;
            sd_stats_copy();
        }
    }
    /* S16: probe capture.  The first capture of a match is normally taken at the first predicted frame, i.e. after
       prediction has begun; if the engine cannot hold the manifest the first rollback would find no complete capture.  One
       capture at the first in-game frame end (the freeze phase is neutral lockstep) makes the engine's state known
       before any prediction: if it is incomplete, the gate holds the match in lockstep from the start. */
    if (sd_active && (S16CFG & S16F_PROBE) && !g_probe_done && in_game() && !rb_active && !sd_nslots() && !(S16CFG & (S16F_NOSTRICT | S16F_NOGATE))) {
        g_probe_done = 1;
        sd_capture((s32)f + 1);
        s16_tel_copy();
        sd_forget();        /* the probe is not kept: captures that no prediction follows pile up pre-images (S12 takes the first
                               capture at the first predicted frame; a probe kept for ~100 lockstep frames overflowed the pending
                               undo log and every later restore missed: runs v3_f1/v3_f2).  INC stays set when the probe lost something. */
        sd_stats_copy();
    }
    /* S16: gated (lockstep because the engine is incomplete / short of arena): try a forced rebuild every 30 frames, but
       only when no rollback can be needed: 8 consecutive lockstep frames (every earlier predicted frame has been
       confirmed or rolled back, the rollback limit is 7) and every remote frame up to f received. */
    if (sd_active && ((g_gate_on && (S16CFG & S16F_REBUILD) && g_gate_run >= 8u) || (S16CFG & S16F_TESTREB)) && in_game() && !rb_active && C.rlatest >= (s32)f &&
        (g_gate_reb_f == 0 || f - g_gate_reb_f >= 30u)) {
        int r = sd_rebuild((s32)f + 1);
        g_gate_reb_f = f ? f : 1u;
        YL[Y_S16_GATE_REB]++;
        if (r == 0) { s10_capture_done(); g_fresh = 1; }
        else sd_forget();                                       /* still incomplete: keep no captures while gated (see the probe) */
        sd_stats_copy();
    }
}

/* hooks for rb_finish / load */
void s10_rb_begin(void) { if (g_eq_on) eq_rb_begin(); }
void s10_rb_end(void) { eq_rb_end(); }

#ifndef SD_NO_TESTPROF   /* SD_NO_TESTPROF (card / hand-off profiles onbc, onb;, card hole): the S10 TEST ONLY profiles and their printer are compiled
                            out (-6.1 KB: g_nws 4 KB, s10_prof_step, nw_cb).  Their only entry, s10_prof_step, returns at once while s10_cfg.prof_frame is 0, which
                            every product contract leaves it; "TEST" row. */
/* ---- NW profile (TEST ONLY): ranges that never changed since the manifest was built ---- */
#define NWC_N 128u
struct nwc { u32 key, size, n0, b0, n1, b1; };                  /* cells by (class word, size): never changed / changed */
static struct nwc *g_nwc;
static u32 g_nwc_n, g_nw_sb, g_nw_cb, g_nw_tb, g_nw_ns;
static u32 g_nws[512][2], g_nws_n;                              /* never-changed statics (lo, hi), merged */
static void nw_cb(u32 kind, const struct sd_range *r)
{
    u32 sz = r->hi - r->lo - (r->hhi - r->hlo), never = (r->chg & ~SD_RS_BIT) == 0, key, i;
    g_nw_tb += sz;
    if (!kind) {
        if (!never) return;
        g_nw_sb += sz;
        if (g_nws_n && g_nws[g_nws_n - 1][1] == r->lo) g_nws[g_nws_n - 1][1] = r->hi;
        else if (g_nws_n < 512) { g_nws[g_nws_n][0] = r->lo; g_nws[g_nws_n][1] = r->hi; g_nws_n++; }
        g_nw_ns++;
        return;
    }
    if (never) g_nw_cb += sz;
    if (!g_nwc) return;
    key = W(r->lo);
    if (!(key >= 0x80003000u && key < 0x80500000u)) key = 0;
    for (i = 0; i < g_nwc_n && !(g_nwc[i].key == key && g_nwc[i].size == r->hi - r->lo); i++) ;
    if (i == g_nwc_n) { if (g_nwc_n >= NWC_N) return; g_nwc[i].key = key; g_nwc[i].size = r->hi - r->lo; g_nwc[i].n0 = g_nwc[i].b0 = g_nwc[i].n1 = g_nwc[i].b1 = 0; g_nwc_n++; }
    if (never) { g_nwc[i].n0++; g_nwc[i].b0 += sz; } else { g_nwc[i].n1++; g_nwc[i].b1 += sz; }
}

/* ---- printing (OSReport "S10x" lines, time-budgeted per rendered frame) ---- */
static char *p_put(char *p, char c1, char c2, char c3, char c4) { *p++ = c1; *p++ = c2; *p++ = c3; *p++ = c4; *p++ = ' '; return p; }
static void p_end(char *p) { *p++ = '\n'; *p = 0; OSReport(g_lb); XL[X_PROF_LINES]++; }
static void p_words(char tag, const u32 *v, u32 n)
{
    char *p = p_put(g_lb, 'S', '1', '0', tag);
    u32 i;
    for (i = 0; i < n; i++) { if (i) *p++ = ' '; p = hx(p, v[i]); }
    p_end(p);
}
void s10_prof_step(void)
{
    u32 t0 = tbl(), v[12];
    if (!s10_cfg.prof_frame || g_prof_state >= 9u || !in_game() || (s32)GLOBAL_FRAME < (s32)s10_cfg.prof_frame) return;
    if (g_prof_state == 0u) {                                  /* freeze: aggregate the NW profile once */
        g_prof_state = 1u; g_prof_i = 0; g_prof_j = 0;
        if (S10CFG & S10F_NWPROF) {
            if (!g_nwc) g_nwc = (struct nwc *)prof_alloc(NWC_N * sizeof(struct nwc));
            g_nwc_n = g_nw_sb = g_nw_cb = g_nw_tb = g_nw_ns = g_nws_n = 0;
            sd_each(nw_cb);
            XL[X_NW_STATIC_KB] = g_nw_sb >> 10; XL[X_NW_CELL_KB] = g_nw_cb >> 10; XL[X_NW_TOTAL_KB] = g_nw_tb >> 10;
        }
        v[0] = GLOBAL_FRAME; v[1] = s10_cfg.flags; v[2] = sd_st.captures; v[3] = sd_st.bytes; v[4] = sd_st.cells; v[5] = sd_st.statics;
        p_words('H', v, 6);
    }
    XL[X_PROF_STATE] = g_prof_state;
    while (tbl() - t0 < sdg_gc_ticks(200000u) && g_prof_state < 9u) {
        switch (g_prof_state) {
        case 1u:                                                /* per-proc table */
            if (!g_pp_on || g_prof_i >= PP_N) { g_prof_state = 2u; g_prof_i = 0; break; }
            if (g_pp[g_prof_i].fn) {
                struct pp *e = &g_pp[g_prof_i];
                v[0] = e->fn; v[1] = e->pl; v[2] = e->n; v[3] = e->t; v[4] = e->rng; v[5] = e->n_rs; v[6] = e->t_rs; v[7] = e->rng_rs; v[8] = e->skip; v[9] = e->reseed;
                p_words('P', v, 10);
            }
            g_prof_i++;
            break;
        case 2u:                                                /* per p_link */
            if (!g_pp_on || g_prof_i > 65u) { g_prof_state = 3u; g_prof_i = 0; break; }
            if (g_pl[g_prof_i].n || g_pl[g_prof_i].n_rs || g_pl[g_prof_i].skip) {
                struct pl *e = &g_pl[g_prof_i];
                v[0] = g_prof_i; v[1] = e->n; v[2] = e->t; v[3] = e->rng; v[4] = e->n_rs; v[5] = e->t_rs; v[6] = e->rng_rs; v[7] = e->skip;
                p_words('L', v, 8);
            }
            g_prof_i++;
            break;
        case 3u:                                                /* W profile */
            if (!g_w_on || g_prof_i >= g_wp_n) { g_prof_state = 4u; g_prof_i = 0; break; }
            { struct wp *e = &g_wp[g_prof_i]; v[0] = e->kind; v[1] = e->key; v[2] = e->size; v[3] = e->a0; v[4] = e->fo; v[5] = e->lo_; v[6] = e->fi; v[7] = e->li; p_words('W', v, 8); }
            g_prof_i++;
            break;
        case 4u:                                                /* NW statics */
            if (!(S10CFG & S10F_NWPROF) || g_prof_i >= g_nws_n) { g_prof_state = 5u; g_prof_i = 0; break; }
            v[0] = g_nws[g_prof_i][0]; v[1] = g_nws[g_prof_i][1]; p_words('N', v, 2);
            g_prof_i++;
            break;
        case 5u:                                                /* NW cells by class */
            if (!(S10CFG & S10F_NWPROF) || !g_nwc || g_prof_i >= g_nwc_n) { g_prof_state = 6u; g_prof_i = 0; break; }
            { struct nwc *e = &g_nwc[g_prof_i]; v[0] = e->key; v[1] = e->size; v[2] = e->n0; v[3] = e->b0; v[4] = e->n1; v[5] = e->b1; p_words('C', v, 6); }
            g_prof_i++;
            break;
        case 6u:                                                /* EQ by motion id */
            if (!g_eq_on || g_prof_i >= g_eqm_n) { g_prof_state = 7u; g_prof_i = 0; break; }
            v[0] = (u32)g_eqm[g_prof_i].mid; v[1] = g_eqm[g_prof_i].a; v[2] = g_eqm[g_prof_i].b; v[3] = g_eqm[g_prof_i].c; p_words('E', v, 4);
            g_prof_i++;
            break;
        case 7u:                                                /* EQ by input-difference mask, C delay histogram */
            if (!g_eq_on || g_prof_i >= 32u) {
                if (g_eq_on) p_words('D', g_eq_cdh, 8);
                g_prof_state = 8u; g_prof_i = 0; break;
            }
            if (g_eq_mask[g_prof_i][0] | g_eq_mask[g_prof_i][1] | g_eq_mask[g_prof_i][2]) {
                v[0] = g_prof_i; v[1] = g_eq_mask[g_prof_i][0]; v[2] = g_eq_mask[g_prof_i][1]; v[3] = g_eq_mask[g_prof_i][2]; p_words('M', v, 4);
            }
            g_prof_i++;
            break;
        default:                                                /* summary */
            { u32 i; for (i = 0; i < 12; i++) v[i] = XL[i + 2]; p_words('S', v, 12); }
            v[0] = g_nw_sb; v[1] = g_nw_cb; v[2] = g_nw_tb; v[3] = g_nw_ns; v[4] = g_nws_n; v[5] = g_wp_n; v[6] = g_wp_full; v[7] = g_pp_full; v[8] = g_reseeds;
            p_words('!', v, 9);
            g_prof_state = 9u;
            break;
        }
    }
    XL[X_PROF_STATE] = g_prof_state;
}
#else
void s10_prof_step(void) { (void)g_prof_i; (void)g_prof_j; }
#endif

/* ======================================================================================================== S12 */
/* ---- TEST ONLY locator lines (published = finalized frames only) ---- */
static void loc_print(u32 f, u32 gp)
{
    const u32 *r = g_loc + (f & 63u) * LOC_REC;
    u32 q, i, part;
    char *p;
    if (f < s12_cfg.loc_lo || f >= s12_cfg.loc_lo + 4096u) return;
    if (r[0] != f) { ZL[Z_LOC_FULL]++; return; }
    if (S12CFG & S12F_LOC)
        for (q = 0; q < 2; q++) {                               /* S12C frame q gp static k0[16] k1[16] */
            p = g_lb;
            *p++ = 'S'; *p++ = '1'; *p++ = '2'; *p++ = 'C'; *p++ = ' ';
            p = hx(p, f); *p++ = ' '; *p++ = (char)('0' + q); *p++ = ' '; p = hx(p, gp);
            for (i = 0; i < 33u; i++) { *p++ = ' '; p = hx(p, r[2 + q * 33u + i]); }
            *p++ = '\n'; *p = 0;
            OSReport(g_lb);
            ZL[Z_LOC_LINES]++;
        }
    if (S12CFG & S12F_LOCW)
        for (part = 0; part < 4u; part++) {                     /* S12W frame sel part words[36] */
            p = g_lb;
            *p++ = 'S'; *p++ = '1'; *p++ = '2'; *p++ = 'W'; *p++ = ' ';
            p = hx(p, f); *p++ = ' '; p = hx(p, s12_cfg.loc_sel); *p++ = ' '; *p++ = (char)('0' + part);
            for (i = 0; i < 36u; i++) { *p++ = ' '; p = hx(p, r[68 + part * 36u + i]); }
            *p++ = '\n'; *p = 0;
            OSReport(g_lb);
            ZL[Z_LOC_LINES]++;
        }
}

/* ---- Slippi's between-frame camera tasks (LoopEngineForRollback.asm FN_ExecCameraTasks, Common/FastForward/
   FunctionMacros.s), called instead of the render pass after an early rollback with S12F_EARLY_NORENDER ---- */
#define Camera_80030A50 ((u32 (*)(void))0x80030A50)              /* Camera_LoadCameraEntity: returns the camera */
#define Camera_8002A4AC ((void (*)(void))0x8002A4AC)
#define HSD_CObjSetCurrent ((int (*)(u32))0x80368458)
#define ftLib_80086A8C ((void (*)(u32))0x80086A8C)              /* Player_SetOffscreenBool */
static void s12_cam_tasks(void)
{
    u32 g, n = 0, list;
    ZL[Z_E_CAMTASKS]++;
    Camera_80030A50();
    Camera_8002A4AC();
    HSD_CObjSetCurrent(W(Camera_80030A50() + 0x28));
    list = W(r13v() - 0x3E74u);                                  /* lwz r3,-0x3E74(r13); first fighter gobj at +0x20 */
    if (!ptr_like(list)) return;
    for (g = W(list + 0x20); ptr_like(g) && n++ < 8; g = W(g + 8)) {
        u32 fd = W(g + 0x2C);
        if (ptr_like(fd) && !(*(volatile u8 *)(fd + 0x221F) & 0x10u)) ftLib_80086A8C(g);
    }
}

/* camera-task skip hooks (s12_hooks.S SKIPHOOK): idx 0 Camera_8002A4AC, 1 ftLib_80086A8C; lr = the caller.  Skipped
   only with S12F_TRIMCAM, inside a rollback, when called from the patched code area outside this image (Slippi's
   FN_ExecCameraTasks between two re-simulated frames). */
extern char __s12_image_lo[], __s12_image_hi[];
u32 s12_cam_skip(u32 idx, u32 lr)
{
    if (!(S12CFG & S12F_TRIMCAM) || !rb_active || !in_game()) return 0;
    if (lr < SD_B_CAMLO || lr >= SD_B_CAMHI) return 0;
    if (lr >= (u32)__s12_image_lo && lr < (u32)__s12_image_hi) return 0;
    ZL[Z_CAM_SKIP + (idx & 3u)]++;
    return 1;
}

/* S12 (on S11): DYNB, the dynamic-bone simulation lb_8001044C skipped in re-simulated frames. Called by
   s12_dyn_stub (s12_hooks.S, 04 `b` at the function entry) with the function's r3 (desc) and r7 (part).  Only inside
   Fighter_8006D9AC / Item_8026A788 of a re-simulated frame with S12F_C_DYNB (g_dyn_rs, set by s10_invoke); stage calls
 and every other frame run the original. What the skipped call would have done besides the dynamics
   state (DynamicsData list, the chain bones' local rotations, both outside the fighter struct): HSD_JObjSetMtxDirty +
   HSD_JObjSetupMatrix on the parent of the chain's first bone, which marks the
   parent subtree dirty (so Slippi's DynamicsFix C2 at 0x8009E090 recomputes every dirty fighter bone afterwards, as
   after the real call); that part is done here.  Returns 1 = skip (return to the caller), 0 = run the original. */
#define HSD_JObjSetMtxDirtySub ((void (*)(u32))0x803732E8)
#define HSD_JObjSetupMatrixSub ((void (*)(u32))0x80373078)
#define JOBJ_DIRTY(j) (!(W((j) + 0x14) & 0x800000u) && (W((j) + 0x14) & 0x40u))   /* HSD_JObjMtxIsDirty */
static u32 dyn_skip_ok(u32 desc, u32 part)
{
    u32 cur, jobj, parent, i;
    if (*(volatile u8 *)0x804D63B8u) return 0;                   /* lb_804D63B8: the original returns at once anyway */
    if (!ptr_like(desc) || part > 0xFFu) return 0;
    cur = W(desc);                                               /* DynamicsDesc.data */
    for (i = 0; ptr_like(cur) && i < part; i++) cur = W(cur + 0x90u);   /* DynamicsData.next */
    if (!ptr_like(cur)) return 0;
    jobj = W(cur);                                               /* desc.lb_unk0.jobj */
    if (!ptr_like(jobj)) return 0;
    parent = W(jobj + 0xCu);                                     /* HSD_JObj.parent */
    if (parent) {
        if (!ptr_like(parent)) return 0;
        if (!JOBJ_DIRTY(parent)) HSD_JObjSetMtxDirtySub(parent);
        if (JOBJ_DIRTY(parent)) HSD_JObjSetupMatrixSub(parent);
    }
    return 1;
}
u32 s12_dyn_skip(u32 desc, u32 part)
{
    if (!g_dyn_rs) return 0;
    if (!dyn_skip_ok(desc, part)) { ZL[Z_DYN_RUN]++; return 0; }   /* odd input: let the original decide */
    ZL[Z_DYN_SKIP]++;
    return 1;
}

/* render-skip hook (0x801A5030): the pass right after an early rollback shows the previous picture again */
u32 s12_render_skip(void)
{
    if (!g_render_skip_next) return 0;
    g_render_skip_next = 0;
    if (!in_game()) return 0;
    ZL[Z_E_NORENDER]++;
    s12_cam_tasks();
    return 1;
}

/* ---- early rollback start: called from the scene loop's wait loop (0x801A4DA8), main thread ---- */
static u8 g_rxs[S12_RXB_SIZE + 3] __attribute__((aligned(4)));
static u32 g_last_poll;
void s12_idle(void)
{
    u32 now, o, rxb, m, left, usable, est, k, rest, per, ren, pct, guard, poll;
    s32 nf, gf, g;
    struct s12_eck r;
    if (S17CFG & S17F_PE) s17_pe();                  /* S17 R1: the pass-end capture (once per pass, interrupts off inside) */
    if (!(S12CFG & (S12F_EARLY | S12F_EARLY_DRY)) || !in_game() || !sd_active || rb_active || g_early_pending) return;
    ZL[Z_E_POLLS]++;
    now = tbl();
    {   /* IDLE POLL GAP: cfg early_poll_us = Wii gap us << 16 | GameCube gap us, 0 halves = 1500 us on a Wii, 500 us on a GameCube, converted with
           the console's own time base (60.75 MHz on a Wii; the old constant 81/2 was the GameCube's 40.5 MHz, so the Wii polled every 333 us).  On a Wii the gap counts from the END of
           the previous poll: each poll's commands wait for the Nintendont ARM loop, and back-to-back polls kept it in its 20 us burst sleep for the whole VI wait (1253 readout:
           ~70 of 99 commands per frame were these polls), so the IOS network threads never got the 1 ms gaps the kernel's ADAPT rule (burst ends after 300 us) is there for. */
        u32 mt = sd_plat_ms_ticks(), us = s12_cfg.early_poll_us, wii = mt >= 55000u;
        if (!mt) mt = 40500u;
        us = wii ? us >> 16 : us & 0xFFFFu;
        if (!us) us = wii ? 1500u : 500u;
        poll = us * mt / 1000u;
        if (g_net && !(s7_cfg.s7_flags & S7F_LOOPBACK) && now - g_last_poll >= poll) {   /* main-thread network poll */
#ifdef MM_FC
            u32 fr0 = B.st.rx_frames, fa0 = fc_acc[FC_NIDLE], fi0 = fc_iops;
            fc_ip = 1;
#endif
            g_last_poll = now;
            m = irq_off();
            if (lock()) { sdnet_poll(&S); drain(); unlock(); ZL[Z_E_NETPOLLS]++; }
            if (wii) g_last_poll = tbl();
            irq_restore(m);
#ifdef MM_FC
            fc_ip = 0;
            if (fc_on) {                                        /* FRAME COST: a poll that delivered a frame is receive work, the rest is polling in the VI wait */
                g_fc.ipolls++;
                if (B.st.rx_frames != fr0) { u32 d = fc_acc[FC_NIDLE] - fa0; fc_acc[FC_NIDLE] -= d; fc_acc[FC_NRX] += d; fc_iops = fi0; }
            }
#endif
        }
    }
    if (!g_rx_new) return;
    m = irq_off();                                              /* no pad alarm (B0 / TriggerSendInput) in between */
    g_rx_new = 0;
    ZL[Z_E_RXNEW]++;
    o = odb();
    rxb = o ? be32p((volatile u8 *)(o + ODB_RXB_ADDR)) : 0;
    if (!o || !ptr_like(rxb)) { ZL[Z_E_ERR]++; irq_restore(m); return; }
    nf = (s32)be32p((volatile u8 *)(o + S12_ODB_FRAME));
    gf = (s32)GLOBAL_FRAME;
    if (nf != gf || nf < 2) { ZL[Z_E_SKIP_FRAME]++; irq_restore(m); return; }   /* an input was taken already */
    {   u32 i; for (i = 0; i < S12_RXB_SIZE; i++) g_rxs[i] = *(volatile u8 *)(rxb + i); }
    s12_core_rxb_inputs(&C, g_rxs, nf - 1, 0);                  /* the RXB the next B0 would build, up to frame nf-1 */
    ZL[Z_E_CHECKS]++;
    s12_early_check((u8 *)o, g_rxs, nf - 1, 0, &r);
    if (r.skip) {
        if (r.skip == S12_ESKIP_ACTIVE) ZL[Z_E_SKIP_ACTIVE]++;
        else if (r.skip == S12_ESKIP_NOTPRED) ZL[Z_E_SKIP_NOTPRED]++;
        else if (r.skip == S12_ESKIP_OVER) ZL[Z_E_SKIP_OVER]++;
        else ZL[Z_E_DENY_OTHER]++;
        irq_restore(m);
        return;
    }
    ZL[Z_E_MATCHED] += r.matched;
    if (!r.need) { irq_restore(m); return; }
    ZL[Z_E_NEED]++;
    /* budget: restore + k re-simulated frames (from the newest capture <= target, S6c gap) + the render or the camera
       tasks, measured EMAs in Dolphin ticks (no hw_pct: the time left is measured on this machine) */
    g = r.target;
    if (sd_newest_ok_le(r.target, &g) == 0) {                   /* S16: a complete capture is needed */
        s32 gp = 0;
        if (sd_newest_le(r.target, &gp)) { ZL[Z_S16_EARLY_DENY]++; irq_restore(m); return; }
        g = r.target;
    }
    k = (u32)(nf - g);
    rest = TL[T_RESTORE] ? TL[T_RESTORE] : sdg_gc_ticks(81000u);
    {   u32 kr = k > 7u ? 7u : k;                               /* S12 (on S11): the measured restore of this depth (the
                                                                   undo steps grow it, console ~0.55 ms per step) */
        if (g_rest_k[kr]) { rest = g_rest_k[kr]; ZL[Z_REST_K_USED]++; }
    }
    per = DL[D_PER] ? DL[D_PER] : sdg_gc_ticks(130000u);
    ren = (S12CFG & S12F_EARLY_NORENDER) ? sdg_gc_ticks(20000u) : (ZL[Z_RENDER_EMA] ? ZL[Z_RENDER_EMA] : sdg_gc_ticks(120000u));
    est = rest + k * per + ren;
    left = vi_left();                                           /* S12: from the VI line (exact) */
    if (!left) left = g_b0_t && g_b0_t + FRAME_TICKS - now < FRAME_TICKS ? g_b0_t + FRAME_TICKS - now : 0u;
    ZL[Z_VI_IRQ_LINE] = g_irq_line; ZL[Z_VI_VMAX] = g_vmax;
    pct = s12_cfg.early_pct ? s12_cfg.early_pct : 90u;
    guard = s12_cfg.early_guard ? s12_cfg.early_guard : sdg_ms_ticks(1u);   /* 1 ms = 40,500 ticks on a GameCube */
    usable = left / 100u * pct;
    usable = usable > guard ? usable - guard : 0u;
    if (est > usable && (S12CFG & S12F_EARLY_OVR) && est <= usable + FRAME_TICKS / 2u) {
        ZL[Z_E_OVR]++;                                           /* S12 (on S11): start anyway, the overrun of a rollback
                                                                    that would run at the next frame start lands here */
        usable = est;
    }
    if ((S12CFG & S12F_EARLY_DRY) || !(S12CFG & S12F_EARLY) || est > usable) {
        if (est > usable) ZL[Z_E_DENY_BUDGET]++;
        irq_restore(m);
        return;
    }
    {   u32 i; for (i = 0; i < S12_RXB_SIZE; i++) *(volatile u8 *)(rxb + i) = g_rxs[i]; }
    s12_early_check((u8 *)o, (u8 *)rxb, nf - 1, 1, &r);        /* commit: Slippi's engine starts it from this loop */
    if ((S12CFG & S12F_NEG_EARLY) && !g_neg_early_done && nf >= (s32)s12_cfg.loc_sel) {   /* NEGATIVE CONTROL */
        s32 lat = (s32)be32p((volatile u8 *)(rxb + S12_RXB_OPNT_FRAME_NUMS)), j = lat - r.target;
        if (j >= 0 && j < 7) { *(volatile u8 *)(rxb + S12_RXB_OPNT_INPUTS + 12 * j) ^= 0x01u; g_neg_early_done = 1; ZL[Z_E_NEG]++; }
    }
    g_early_pending = 1;
    g_early_est = est;
    g_early_t0 = now;
    ZL[Z_E_START]++;
    ZL[Z_E_LEFT_SUM] += left;
    if (!ZL[Z_E_IDLE_LEFT_MIN] || left < ZL[Z_E_IDLE_LEFT_MIN]) ZL[Z_E_IDLE_LEFT_MIN] = left;
    if (!ZL[Z_E_FIRST_FRAME]) ZL[Z_E_FIRST_FRAME] = (u32)nf;
    irq_restore(m);
}

#if defined(MS_TLS_PROBE) || defined(MS_REP_OVL)
/* LAB DIAGNOSTIC ONLY (MS_BLOB_OPTS tlsprobe=on): the sign-in overlay's TCP + TLS probe (menus only) gets the engine's sdnet and one extra
   poll from the menu thread (the 1 ms busy-poll variant).  Not compiled without the option: the release image is unchanged. */
struct sdnet *ms_tls_net(void) { return &S; }
static u32 g_tls_m; static int g_tls_own;
/* the overlay's TCP sends build the segment in S.txbuf and write the NIC: from the menu thread that needs the EXI lock and no slice/B0 in between (diag 472 on the cube: every such send was lost, RTO 1/2/4/8 s) */
int ms_tls_begin(void)
{
    u32 m = irq_off();
    if (!g_net) { irq_restore(m); return 0; }
    if (g_locked) g_tls_own = 0;
    else if (lock()) g_tls_own = 1;
    else { irq_restore(m); return 0; }
    g_tls_m = m;
    return 1;
}
void ms_tls_end(void)
{
    if (g_tls_own) unlock();
    g_tls_own = 0;
    irq_restore(g_tls_m);
}
void ms_tls_poll(struct sdnet *s)
{
    (void)s;
    if (!g_net) return;
    if (g_locked) { sdnet_poll(&S); drain(); }
    else if (lock()) { sdnet_poll(&S); drain(); unlock(); }
}
#endif
#ifdef MS_REP_OVL
/* the Slippi game-report overlay (menus/rep/rep_res.c) uses the helpers above and these: the layout check, the DHCP resolver, and the hook
   reset the resident performs whenever the overlay goes (also when the block can no longer be called) */
unsigned int ms_tls_net_size(void) { return (unsigned int)sizeof S; }
unsigned int ms_tls_dns(void) { return S.dns; }
void ms_tls_detach(void) { S.tcp_rx = 0; S.tcp_poll = 0; S.tcp = 0; }
#endif
