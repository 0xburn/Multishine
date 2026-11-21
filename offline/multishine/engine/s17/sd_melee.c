/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : copy of; S11: allocation-site table (profile and cold-site mode, sd_melee.h).
 * copy of, S10: changes marked (render set for the RSET restore, never-written
 * exclusion list).
 * Melee (NTSC-U 1.02) manifest for the delta snapshot engine (original work).
 *
 * Same content as the S6 final manifest 0x33120540 (/): every allocated HSD-heap cell of at
 * most 0x2400 bytes except TObj image buffers, the bone-dynamics pool whole, the fighter animation buffers, the OS heap
 * metadata, the HSD pool headers (+ free-list links outside the cells), the UCF pad-buffer ring, and the game/HSD lines
 * of the measured statics and of .sdata/.sbss (s6_statgrp.h, deny mask = cfg).  Differences from S6:
 *   - built once per match and then kept up to date from allocator events (sd_core.c), not walked per capture;
 *   - OS heap: allocated-cell header links are covered by the allocator hooks' pre-images, free-cell headers by extras;
 *   - SDM_PADQ (Slippi B1/B2 mode): the raw pad queue (HSD_PadLibData + entries) is IN the manifest, as in Slippi's
 *     region list (StartEngineLoop.asm rewinds its read index after a load and expects the captured value);
 *   - SDM_ANIMBAR: animation buffers are barrier ranges (compared only after an ARAM transfer into them).
 */
#include "sd_snap.h"
#include "sd_melee.h"
#include "../s6/s6_statgrp.h"

#define REG32(a) (*(volatile u32 *)(a))
#define REG16(a) (*(volatile u16 *)(a))
#define REG8(a) (*(volatile u8 *)(a))
#define IN_MEM1(p) ((u32)(p) >= 0x80000000u && (u32)(p) < 0x81800000u)

#define A_PADLIB 0x804C1F78u
#define A_PLINK_HEAD 0x804D782Cu
#define A_HEAPARRAY 0x804D7360u
#define A_CURRENT_HEAP 0x804D5E00u
#define A_FIFOOBJ 0x804D76C4u
#define A_LB_PADTIMING 0x804329F0u
#define A_GM_LOOP 0x80479D58u
#define A_PDVB_FLAG 0x804DBB30u
#define A_SCENE_WORD 0x80479D30u                  /* gm state machine: [major, pending major, previous major, minor] */

struct sd_mcfg sd_mc = { 0, 0x3FFF, 0, 0 };
struct sd_mstats sd_ms;
u32 sd_pools[64], sd_npools;
u32 sd_ucf_body;

u32 sd_plat_time(void)
{
    u32 t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}

/* S17: time-base ticks per millisecond, from the OS bus clock word (0x800000F8, Hz; the time base runs at a quarter of it: 40.5 MHz on a
   GameCube, 60.75 MHz on a Wii) read at run time, with the plausibility window of offline/common/sdenet/sdenet_clock.c; 0 = unknown
   (the engine's DMA timeout then counts polls).  The engine never uses a time-base constant of its own. */
u32 sd_plat_ms_ticks(void)
{
    u32 bus = REG32(0x800000F8u);
    if (bus < 100000000u || bus > 300000000u)
        return 0;
    return bus / 4000u;
}

u32 sd_hsd_desc(void)
{
    u32 heaps = REG32(A_HEAPARRAY);
    s32 h = (s32)REG32(A_CURRENT_HEAP);
    if (!IN_MEM1(heaps) || h < 0 || h > 15)
        return 0;
    return heaps + 12 * (u32)h;
}

/* ---------------------------------------------------------------- exclusions (never in the manifest) */
static const u32 excl_fixed[][2] = {
    /* Slippi's own exclusion list (SlippiSavestate.cpp): sound, XFB/VI, scattered words (as ) */
    {0x804031A0, 0x24}, {0x80407FB4, 0x34C}, {0x80433C64, 0x1EE80}, {0x804A8D78, 0x17A68},
    {0x804C28E0, 0x399C}, {0x804D7474, 0x8}, {0x804D74F0, 0x50}, {0x804D7548, 0x4}, {0x804D7558, 0x24},
    {0x804D7580, 0xC}, {0x804D759C, 0x4}, {0x804D7720, 0x4}, {0x804D7744, 0x4}, {0x804D774C, 0x8},
    {0x804D7758, 0x8}, {0x804D7788, 0x10}, {0x804D77C8, 0x4}, {0x804D77D0, 0x4}, {0x804D77E0, 0x4},
    {0x804DE358, 0x80}, {0x804DE800, 0x70}, {0x804d6030, 0x4}, {0x804d603c, 0x4},
    /* Wii freeze root cause (q31): Slippi's list kept only __DSP_rude_task_pending (0x804D7218) and first/curr (0x804D7228) out, so a rollback restored __DSP_rude_task (0x804D721C) from an old snapshot (NULL) while the live pending flag was 1: __DSPHandler then ran __DSP_exec_task(curr, NULL) and loaded NULL->iram_mmem_addr (address 0xC): harmless on a Cube / in Dolphin (low memory is readable), an endless DSI loop inside the DSP interrupt on the Wii. The whole block (init flag, pending, rude, tmp, last, first, curr) stays live now. */
    {0x804d7210, 0x20},
    {0x804d7740, 0x4}, {0x804d7754, 0x4}, {0x804d77bc, 0x4}, {0x804de7f0, 0x10},
    {0x804c0980, 0x15F8},
    /* (ROLLBACK_LOCKIN row 44): HSD DevCom, the DVD -> ARAM relay that streams the music (and every other async file read) with real-time DVD/ARQ completions, and the
       music stream's own state.  Slippi's list above was written for a build with the music muted (PreventMusicAlarm/MuteMusic): our images stream the stage music, and the always-on
       sbss group {0x804D7800, 0x200} restored aramDC (0x804D7808) and devComRelayBufFlag[2] (0x804D780C) with every rollback.  A flag restored to "busy" after its completion callback
       already ran is never cleared again: after two such rollbacks the relay has no buffer left, the stream's busy flag HSD_Synth_804D7778 stays 1, and the next HSD_Synth_8038B5AC
       (the character select's music start) spins in `do {} while (HSD_Synth_804D7778 != 0)` for ever (frozen post-game CSS after a long game, 1,000+ rollbacks; absent with rollback off) */
    {0x804D77F0, 0x20},               /* HSD_DevCom sbss: free list, aramstate, dvdDC, 804D77FC[2], 804D7804, aramDC, devComRelayBufFlag (up to 0x804D780F; the GObj statics start at 0x804D7810) */
    {0x804C62A0, 0x80E0},             /* HSD_DevCom bss: devComARQR, devComStatus, the stream request table, the two relay buffers, the DVDFileInfo (statics group 7, denied by the default mask: kept out on purpose) */
    {0x804D7760, 0x28},               /* HSD_Synth stream state: node id, file, buffer indices, busy flag, volume, addresses (statics group 9, denied by the default mask: kept out on purpose) */
    {A_LB_PADTIMING + 0x50, 0x28},    /* OSAlarm of the pad alarm (linked into the OS alarm queue) */
    {A_GM_LOOP + 4, 4},               /* render counter: renders really happened */
    {A_PDVB_FLAG, 4},                 /* "Polling Drift Fix + VB" scratch word (timing dependent) */
    {0x804316C0, 0x1EC},              /* lbArq_804316C0: ARAM-DMA request lists; completions arrive in real time */
    {0x8046B1F8, 0x180},              /* pad rumble lists (walked in the VI interrupt; restoring them hung the console) */
    {0x804C22E0, 0x30},               /* rest of the pad rumble data */
    {A_PADLIB + 0x2C, 8},             /* HSD_PadLibData.rumble_info: free-list head of the same rumble nodes (S6b run d7) */
};
#define NXF (sizeof(excl_fixed) / sizeof(excl_fixed[0]))
static u32 xl[NXF + 4][2], nxl;

static void build_excl(void)
{
    u32 i, j;
    nxl = 0;
    for (i = 0; i < NXF; i++) {
        xl[nxl][0] = excl_fixed[i][0];
        xl[nxl][1] = excl_fixed[i][1];
        nxl++;
    }
    if (!(sd_mc.flags & SDM_PADQ)) {
        u32 q = REG32(A_PADLIB + 8), qn = REG8(A_PADLIB);
        xl[nxl][0] = A_PADLIB;
        xl[nxl++][1] = 0x34;
        if (IN_MEM1(q) && qn && qn <= 16) {
            xl[nxl][0] = q;
            xl[nxl++][1] = 0x30 * qn;
        }
    }
    {
        u32 fo = REG32(A_FIFOOBJ);
        if (IN_MEM1(fo) && IN_MEM1(REG32(fo))) {
            xl[nxl][0] = REG32(fo);
            xl[nxl++][1] = REG32(fo + 8);
        }
    }
    for (i = 1; i < nxl; i++)
        for (j = i; j > 0 && xl[j - 1][0] > xl[j][0]; j--) {
            u32 a = xl[j][0], l = xl[j][1];
            xl[j][0] = xl[j - 1][0];
            xl[j][1] = xl[j - 1][1];
            xl[j - 1][0] = a;
            xl[j - 1][1] = l;
        }
}

void sd_melee_rs_mark(void);                     /* S10 */
/* ---------------------------------------------------------------- statics: collect, sort, merge, subtract */
static const u32 statics_list[][2] = {      /* symbols.txt names and sizes (as ) */
    {0x80453080, 0x5760},   /* player_slots (StaticPlayer x6) */
    {0x8046B6A0, 0x2528},   /* controller / VsSceneState */
    {0x80452C68, 0x39C},    /* game_camera */
    {0x8049E6C8, 0x748},    /* stage_info */
    {0x80458888, 0x600},    /* mpLib_80458888 collision state */
    {0x80459278, 0x640},    /* dmg_log0/1 */
    {0x804C1FAC, 0x330},    /* HSD_PadMasterStatus/CopyStatus/GameStatus */
    {0x8046B0F0, 0x18},     /* gmMainLib_8046B0F0 */
    {0x80458FD0, 0x2C},     /* fighter_alloc_data */
    {0x804A0BE0, 0x2C},     /* item_alloc_data */
    {0x80458EB0, 0x2C},     /* efLib_AllocData */
    {0x804A0C64, 0x30C},    /* Item_804A0C64 .. it_804A0E70 */
    /* item soak: it_804A0F60[30], the table of loaded item Article pointers, filled the first time an item kind is used in a match and
       read at every spawn of that kind; the range above ended at 0x804A0F70, so 104 B of the table were outside the snapshot: a rollback across a first spawn left a pointer to an article the restored heap no longer holds
       (or none for one that it does), which the two consoles then disagree about (Onett with items on: self-healing mismatches mid match, 3 of about 15 runs, 0 of 6 without items). */
    {0x804A0F70, 0x68},     /* it_804A0F60[4..29] */
    {0x804CE38C, 0x58},     /* gobj_alloc_data, gobjproc_alloc_data */
    /* stage smoke mutable stage statics outside the measured groups (Kongo Jungle and Brinstar desynced from frame ~257 in Dolphin pairs; the
       fighters diverged through the stage objects). Kongo: grKg_803E188C[15] (.data, 0x18 each: rotation of the spinning platforms, rewritten every frame, :648).
       Brinstar: grZe_8049F140 .. grZe_8049F170[20] (.bss 0x8049F140-0x8049F440: bubble spawn positions and the 20-entry bubble table with the active flags, :146-148).
       Mute City: the car tables right behind it (desync at match frame 1301 with only Brinstar's range added). */
    {0x803E188C, 0x168},    /* grKg_803E188C */
    {0x8049F100, 0x900},    /* the stage .bss block 0x8049F100-0x8049F9FF: grZe_8049F140/158/170 (Brinstar bubbles), grMc_8049F440/4B8 (Mute City cars: position, speed, flags of 30 entries, :37/58), grGr_8049F9E0 (Green Greens); grPs_8049F040 below is already in the measured group */
    /* Venom: the Arwing table grVe_803E5348.arwing (arwing_gobj[3] at 0x803E5368, arwing_type[3] at 0x803E5374,
       :33-34, 251-405) and grVe_803E5380[3] (the spawn slot of each Arwing) are .data, rewritten at every Arwing spawn / despawn and read when the next type is drawn (`while (idx == arwing_type[0..2])`);
       they were outside the snapshot, so a rollback across a spawn left the tables of the two consoles different. */
    {0x803E5368, 0x24},     /* grVe_803E5348.arwing (0x803E5368-0x803E5380) + grVe_803E5380[3] (0x803E5380-0x803E538C) */
    /* Star Fox team conversation (Corneria / Venom, Fox or Falco special taunt; stage sweep): grCorneria_801E277C creates the conversation text with HSD_SisLib_803A611C / 803A5ACC (a text group,
       HSD_SisLib_804D1110[5] and HSD_SisLib_804D1124[5] = the per-group tables, .bss) and frees it with HSD_SisLib_803A5F50.  The measured statics end at 0x804D1100, so the two tables were outside the
       snapshot: a rollback across the creation / free left a group table that disagrees with the (restored) text objects and both consoles stalled about 30 frames after the first text (S6!H, 0 mismatches). */
    /* Corneria: grCn_803E1F70[5] / grCn_803E1FAC[5] (0x803E1F70-0x803E1FE8, 5 Vec3 each): the local and world positions of the five ship joints, rewritten every frame in the
       stage proc (`lb_8000B1CC(jobj, &F70[i], &FAC[i])`, :733) and read by the cannon / laser code (`sp40 = FAC[1]`, :2115, 2205) in other procs: a read that precedes the write sees the
       previous frame's value, which after a rollback is the newest frame's.  Outside the measured statics (they end at 0x803E1... only for the stage blocks added above). */
    {0x803E1F70, 0x78},     /* grCn_803E1F70[5] + grCn_803E1FAC[5] */
    {0x804D1110, 0x28},     /* HSD_SisLib_804D1110 (0x804D1110-0x804D1124) + HSD_SisLib_804D1124 (0x804D1124-0x804D1138) */
    /* Corneria: the same Arwing bookkeeping as Venom: arwing_gobjs[3] (0x803E1D68), arwing_types[3] (0x803E1D74), arwing_groups[3] (0x803E1D80), .data of the same table, rewritten at every Arwing
       spawn / despawn and read by the next type draw (`while (type_id == arwing_types[0..2])`, :422-446). Outside the snapshot, a rollback across a spawn left the two consoles with different tables: Corneria matches
       desynced at their KO end (items off included). */
    {0x803E1D68, 0x24},     /* arwing_gobjs / arwing_types / arwing_groups */
};
#define NCAND 320
static u32 cand[NCAND][3], ncand;           /* lo, hi, barrier */

static void cand_add(u32 lo, u32 len, u32 barrier)
{
    if (!len || ncand >= NCAND || !IN_MEM1(lo))
        return;
    cand[ncand][0] = lo & ~3u;
    cand[ncand][1] = (lo + len + 3) & ~3u;
    cand[ncand][2] = barrier;
    ncand++;
}

static void emit(u32 lo, u32 hi, u32 barrier)
{
    u32 i;
    /* subtract exclusions */
    for (i = 0; i < nxl && lo < hi; i++) {
        u32 xs = xl[i][0] & ~3u, xe = (xl[i][0] + xl[i][1] + 3) & ~3u;
        if (xe <= lo || xs >= hi)
            continue;
        if (xs > lo) {
            sd_add_static(lo, xs - lo);
            if (barrier)
                sd_barrier_range(lo, xs);
        }
        lo = xe;
    }
    if (lo < hi) {
        sd_add_static(lo, hi - lo);
        if (barrier)
            sd_barrier_range(lo, hi);
    }
}

/* animation buffers: {lo, hi, key address, key at the last capture}.  ftData_80085CD8/ftData_80085E50 (ft/ftdata.c
   1735-1840) refill fp->x59C / x5A0 (ARAM DMA or memcpy + in-place relocation) exactly when the animation id in
   fp->x5A4 / x5A8 changes, so the buffer is compared only at captures where its key changed. */
#define NANIM 8
static u32 anim[NANIM][4], nanim;

void sd_plat_pre_capture(void)
{
    u32 i;
    for (i = 0; i < nanim; i++) {
        u32 k = REG32(anim[i][2]);
        if (k != anim[i][3]) {
            anim[i][3] = k;
            sd_mark_dirty(anim[i][0], anim[i][1] - anim[i][0]);
            sd_ms.arq_calls++;
        }
    }
}

/* containing allocated cell (payload range) of p in the HSD heap, walking the allocated list */
static int cell_of(u32 p, u32 *lo, u32 *hi)
{
    u32 d = sd_hsd_desc(), c, g = 0;
    if (!d)
        return 0;
    for (c = REG32(d + 8); IN_MEM1(c) && g++ < 100000; c = REG32(c + 4))
        if (p >= c + 0x20 && p < c + REG32(c + 8)) {
            *lo = c + 0x20;
            *hi = c + REG32(c + 8);
            return 1;
        }
    return 0;
}

void sd_plat_build_statics(void)
{
    u32 i, j, d, heads;
    build_excl();
    ncand = 0;
    for (i = 0; i < sizeof(statgrp_list) / sizeof(statgrp_list[0]); i++)
        if (statgrp_list[i][2] == 0xFF || !(sd_mc.deny & (1u << statgrp_list[i][2])))
            cand_add(statgrp_list[i][0], statgrp_list[i][1], 0);
    for (i = 0; i < sizeof(sdatagrp_list) / sizeof(sdatagrp_list[0]); i++)
        if (sdatagrp_list[i][2] == 0xFF || !(sd_mc.deny & (1u << sdatagrp_list[i][2])))
            cand_add(sdatagrp_list[i][0], sdatagrp_list[i][1], 0);
    cand_add(A_GM_LOOP, 0x40, 0);                /* loop control incl. Slippi's global frame (0x80479D60) */
    /* whole gameplay statics by symbol: the measured-chunk list misses chunks that
       the measured matches never wrote, e.g. StaticPlayer stale-move stats x904[] (pl/types.h:148), found by run p9_loc */
    for (i = 0; i < sizeof(statics_list) / sizeof(statics_list[0]); i++)
        cand_add(statics_list[i][0], statics_list[i][1], 0);
    if (sd_ucf_body)
        cand_add(sd_ucf_body + 4, 0x30, 0);      /* UCF pad-buffer ring (code-local data of the patched code) */
    d = sd_hsd_desc();
    if (d)
        cand_add(d, 12, 0);                      /* OS heap descriptor: free / allocated list heads */
    for (i = 0; i < sd_npools; i++)              /* HSD_ObjAllocData headers */
        if (IN_MEM1(sd_pools[i]) && !sd_covered(sd_pools[i]))
            cand_add(sd_pools[i], 0x2C, 0);
    {   /* bone-dynamics pool (lb_804D63A0, 0x140 x 0x98) whole, and its side block */
        u32 pool = REG32(0x804D63A0), sb = REG32(0x804D63A8);
        if (IN_MEM1(pool) && !sd_covered(pool))
            cand_add(pool, 0x140 * 0x98, 0);
        if (IN_MEM1(sb) && !sd_covered(sb))
            cand_add(sb, 0x1C0, 0);
    }
    {   /* HSD_SisLib text pool ( HSD_SisLib_803A6048: one HSD_MemAlloc of 0x4800 B in a match, 0x2400 on the CSS, 0xC000 in results): the allocator's free / used block lists and every HSD_Text
           live INSIDE it, while free_head / used_head (.sbss 0x804D7970 / 0x804D7974) are restored with the statics.  The block is above the 0x2400-byte cell limit, so a rollback restored the list heads
           but not the blocks (Star Fox team conversation on Corneria / Venom creates and frees texts: both consoles stalled about 30 - 250 frames into it, stage sweep). */
        u32 pool = REG32(0x804D796C), psz = REG32(0x804D7968);
        if (IN_MEM1(pool) && psz && psz <= 0xC000u && !sd_covered(pool))
            cand_add(pool, psz, 0);
    }
    {   /* stage collision geometry: groundCollVtx / groundCollLine / groundCollJoint (mp/mplib.c:83-85, HSD_MemAlloc'd
           at stage init with 2048 / 1536 / 256 entries, i.e. cells far above the 0x2400 cell limit).  Moving platforms
           (Yoshi's Story's Randall, ...) update them every frame; the used part ([0, count) of each, counts from the
           MapCollData at mpLib_804D64B4) is added.  Found by the Yoshi's Story harness run (hYStestD). */
        u32 cd = REG32(0x804D64B4), vtx = REG32(0x804D64B8), line = REG32(0x804D64BC), jnt = REG32(0x804D64C0);
        if (IN_MEM1(cd)) {
            u32 nv = REG32(cd + 4), nl = REG32(cd + 0xC), nj = REG32(cd + 0x28);
            if (IN_MEM1(vtx) && nv && nv <= 2048 && !sd_covered(vtx))
                cand_add(vtx, 0x18 * nv, 0);
            if (IN_MEM1(line) && nl && nl <= 1536 && !sd_covered(line))
                cand_add(line, 8 * nl, 0);
            if (IN_MEM1(jnt) && nj && nj <= 256 && !sd_covered(jnt))
                cand_add(jnt, 0x34 * nj, 0);
        }
    }
    heads = REG32(A_PLINK_HEAD);                 /* fighter animation buffers fp+0x59C / fp+0x5A0 (cells) */
    nanim = 0;
    if (IN_MEM1(heads)) {
        u32 g = REG32(heads + 4 * 8), guard = 0;
        while (IN_MEM1(g) && guard++ < 64) {
            u32 fp = REG32(g + 0x2C), k;
            if (IN_MEM1(fp))
                for (k = 0; k < 2; k++) {
                    u32 b = REG32(fp + 0x59C + 4 * k), lo, hi;
                    if (IN_MEM1(b) && !sd_covered(b) && cell_of(b, &lo, &hi)) {
                        cand_add(lo, hi - lo, (sd_mc.flags & SDM_ANIMBAR) ? 1 : 0);
                        if (nanim < NANIM) {   /* barrier key: the buffer's content is a function of fp+0x5A4/0x5A8 */
                            anim[nanim][0] = lo;
                            anim[nanim][1] = hi;
                            anim[nanim][2] = fp + 0x5A4 + 4 * k;
                            anim[nanim][3] = REG32(fp + 0x5A4 + 4 * k);
                            nanim++;
                        }
                    }
                }
            g = REG32(g + 8);
        }
    }
    if (sd_mc.flags & SDM_PADQ) {                /* raw pad queue (Slippi restores it) */
        u32 q = REG32(A_PADLIB + 8), qn = REG8(A_PADLIB);
        /* HSD_PadLibData up to +0x2C only: +0x2C is rumble_info (free-list head of the rumble nodes, :104,
           :26); the per-pad rumble lists it pairs with stay live (excluded), and restoring the head alone made
           HSD_PadRumbleFree spin with interrupts off at 0x80378740 (pair runs m2/m5/d7, rollback #60) */
        cand_add(A_PADLIB, 0x2C, 0);
        if (IN_MEM1(q) && qn && qn <= 16 && !sd_covered(q))
            cand_add(q, 0x30 * qn, 0);
    }
    /* sort by lo, merge overlaps (barrier only if every merged piece is a barrier) */
    for (i = 1; i < ncand; i++)
        for (j = i; j > 0 && cand[j - 1][0] > cand[j][0]; j--) {
            u32 a = cand[j][0], b = cand[j][1], c = cand[j][2];
            cand[j][0] = cand[j - 1][0]; cand[j][1] = cand[j - 1][1]; cand[j][2] = cand[j - 1][2];
            cand[j - 1][0] = a; cand[j - 1][1] = b; cand[j - 1][2] = c;
        }
    for (i = 0; i < ncand;) {
        u32 lo = cand[i][0], hi = cand[i][1], bar = cand[i][2];
        for (j = i + 1; j < ncand && cand[j][0] <= hi; j++) {
            if (cand[j][1] > hi)
                hi = cand[j][1];
            bar &= cand[j][2];
        }
        emit(lo, hi, bar);
        i = j;
    }
    /* pools whose free-list links lie outside the manifest: their links go into the extras */
    sd_ms.npools_uncov = 0;
    for (i = 0; i < sd_npools && i < 64; i++) {
        u32 p = sd_pools[i], f, g = 0, unc = 0;
        if (!IN_MEM1(p))
            continue;
        for (f = REG32(p + 4); IN_MEM1(f) && g++ < 20000; f = REG32(f))
            if (!sd_covered(f)) {
                unc = 1;
                break;
            }
        sd_ms.pools_uncov[i] = (u8)unc;
        sd_ms.npools_uncov += unc;
    }
    sd_melee_rs_mark();                          /* S10 */
}

/* ---------------------------------------------------------------- cells */
#define CI_TOBJ 0x80405570u
#define NIMG 96
static u32 img[NIMG], nimg, in_cells;

static int is_classinfo(u32 w)
{
    u32 os, is;
    if (w < 0x803B9840u || w >= 0x804316C0u || (w & 3))
        return 0;
    os = REG16(w + 0x10);
    is = REG16(w + 0x12);
    return os >= 4 && os <= 0x400 && is >= 0x3C && is <= 0x100;
}

static int is_tobj(u32 info)
{
    u32 n;
    for (n = 0; n < 8 && is_classinfo(info); n++) {
        if (info == CI_TOBJ)
            return 1;
        info = REG32(info + 0x14);
    }
    return 0;
}

/* TObj image buffers (read-only texture data) are left out, as in S6 (S6M_IMGEXCL) */
static void mark_images(void)
{
    u32 d = sd_hsd_desc(), c, g = 0;
    nimg = 0;
    if (!d)
        return;
    for (c = REG32(d + 8); IN_MEM1(c) && g++ < 100000; c = REG32(c + 4)) {
        u32 lo = c + 0x20, hi = c + REG32(c + 8), a;
        if (hi - lo > SD_CELL_MAX)
            continue;
        for (a = lo; a + 0x60 <= hi; a += 32) {
            u32 w0 = REG32(a);
            if (is_classinfo(w0) && is_tobj(w0)) {
                u32 id = REG32(a + 0x58);
                if (IN_MEM1(id) && IN_MEM1(REG32(id)) && nimg < NIMG) {
                    u32 im = REG32(id), k;
                    for (k = 0; k < nimg && img[k] != im; k++)
                        ;
                    if (k == nimg)
                        img[nimg++] = im;
                }
            }
        }
    }
}

void sd_plat_build_cells(void)
{
    u32 d = sd_hsd_desc(), c, g = 0;
    build_excl();
    mark_images();
    in_cells = 1;
    if (d)
        for (c = REG32(d + 8); IN_MEM1(c) && g++ < 100000; c = REG32(c + 4))
            sd_add_cell(c + 0x20, c + REG32(c + 8));
    in_cells = 0;
    sd_ms.images = nimg;
}

int sd_plat_cell_ok(u32 lo, u32 hi, u32 *hlo, u32 *hhi)
{
    u32 i;
    if (hi <= lo || hi - lo > SD_CELL_MAX)
        return 0;
    if (in_cells)
        for (i = 0; i < nimg; i++)
            if (img[i] == lo)
                return 0;
    for (i = 0; i < nxl; i++) {           /* an exclusion inside the cell becomes its hole */
        u32 xs = xl[i][0] & ~3u, xe = (xl[i][0] + xl[i][1] + 3) & ~3u;
        if (xe <= lo || xs >= hi)
            continue;
        if (xs <= lo && xe >= hi)
            return 0;
        *hlo = xs > lo ? xs : lo;
        *hhi = xe < hi ? xe : hi;
        break;
    }
    return 1;
}

/* ---------------------------------------------------------------- S10: render set */
/* W profile, run a local work folder (FD, Fox vs Falco, loopback, post-frame capture every second
   frame, 1698 outer passes): the manifest statics and the cell sizes in which a line differed between the end of a logic
   iteration and the start of the next one (render, main loop, interrupts).  Statics: {lo, len} of the manifest range. */
static const u32 rs_w_static[][2] = {
    {0x804D0F00, 0x20C}, {0x804D7600, 0x120}, {0x804D7800, 0x200}, {0x8046B0F0, 0x108}, {0x804C2310, 0x2F4},
    {0x80407100, 0xA0}, {0x8040C300, 0x100}, {0x804D6340, 0xC0}, {0x804C1F78, 0x2C}, {0x80405440, 0x3C0},
    {0x804D08E0, 0x120}, {0x804C0880, 0x84}, {0x804A1E00, 0x200}, {0x80406E00, 0x200}, {0x80406200, 0x200},
    {0x80405E20, 0xE0}, {0x80406700, 0x100}, {0x80452C60, 0x6C58}, {0x804D5C60, 0x3C0}, {0x804D6440, 0xC20},
};
static const u32 rs_w_size[] = { 0x20, 0x40, 0xA0, 0xC0, 0x140, 0x400, 0xD00, 0x1800, 0x2400 };

void sd_melee_rs_mark(void)          /* S10: called at the end of sd_plat_build_statics */
{
    u32 i;
    if (sd_mc.rs_sel & SDM_RS_ALL)
        sd_rs_static(0x80000000u, 0x81800000u);
    if (sd_mc.rs_sel & SDM_RS_W) {
        u32 d = sd_hsd_desc();
        for (i = 0; i < sizeof rs_w_static / sizeof rs_w_static[0]; i++)
            sd_rs_static(rs_w_static[i][0], rs_w_static[i][0] + rs_w_static[i][1]);
        if (d)
            sd_rs_static(d, d + 12);     /* the OS heap descriptor (allocations in the render pass), seen in W */
    }
}

int sd_plat_cell_rs(const struct sd_range *r, const u32 *mirror)
{
    u32 i, sz = r->hi - r->lo;
    (void)mirror;
    if (sd_mc.rs_sel & SDM_RS_ALL)
        return 1;
    if (sd_mc.rs_sel & SDM_RS_W)
        for (i = 0; i < sizeof rs_w_size / sizeof rs_w_size[0]; i++)
            if (rs_w_size[i] == sz)
                return 1;
    return 0;
}

/* ---------------------------------------------------------------- extras */
void sd_plat_extras_put(void)
{
    u32 d = sd_hsd_desc(), c, g = 0, i, n = 0;
    if (d)
        for (c = REG32(d + 4); IN_MEM1(c) && g++ < 4000; c = REG32(c + 4)) {
            sd_put_extra(c, 0x20);               /* free-cell headers (allocation splits/unlinks them) */
            n++;
        }
    sd_ms.free_cells = n;
    n = 0;
    if (sd_ms.npools_uncov)
        for (i = 0; i < sd_npools && i < 64; i++) {
            u32 p = sd_pools[i], f;
            if (!sd_ms.pools_uncov[i] || !IN_MEM1(p))
                continue;
            g = 0;
            for (f = REG32(p + 4); IN_MEM1(f) && g++ < 20000; f = REG32(f))
                if (!sd_covered(f)) {
                    sd_put_extra(f, 4);
                    n++;
                }
        }
    sd_ms.pool_links = n;
}

/* ---------------------------------------------------------------- allocator hooks (from sd_hooks.S) */
/* CSS corruption ( B): the hooks stay installed after the match.  Melee leaves the match scene,
   sets the next minor in the scene word, then (preloadState > lbDvd_80018CF4 > lbHeap_80015900 > HSD_CreateMainHeap) DESTROYS and RE-CREATES
   the HSD heap on the same arena and runs the next scene's on_enter (the character select loads its archives) BEFORE the glue's next
   scene-loop pass clears sd_active.  Every allocation of that on_enter went through these hooks with sd_active still 1: a 40-byte log entry
   (allocated-list head pre-image) and a 12-byte event per allocation, plus ST_FL marks from lbArq, all written into the old store, i.e. into
   memory the new heap had just handed to the character select.  The engine now ends itself at the first hook call after the scene word's
   major or minor differs from the scene the store was made in (latched by sd_melee_init, called right before sd_init); Melee changes the
   scene word before the heap is re-created. */
static u32 sd_scene0, sd_scene_on;                /* scene word of the match (major and minor are compared); sd_scene_on = latched */
#ifdef SD_LAB_HG
extern u32 sd_hg_w[8];                            /* LAB ONLY (sd_core.c): engine writes after the match scene ended */
#endif
static int scene_gone(void)
{
    u32 sc = REG32(A_SCENE_WORD);
    if (!sd_scene_on || !((sc ^ sd_scene0) & 0xFF0000FFu))
        return 0;
#ifdef SD_LAB_HG
    if (!sd_hg_w[0]) {
        sd_hg_w[0] = 1;
        sd_hg_w[5] = sc;
    }
    sd_hg_w[6]++;
#ifdef SD_LAB_HG_NOFIX
    return 0;                                     /* LAB ONLY negative control: keep writing (the old behaviour, counted) */
#endif
#endif
    sd_active = 0;                                /* the store is not ours any more: no engine write until the next sd_init */
    return 1;
}

void sd_osalloc_pre(u32 heap, u32 size)
{
    u32 d;
    (void)size;
    if (!sd_active || scene_gone() || heap != REG32(A_CURRENT_HEAP))
        return;
    d = sd_hsd_desc();
    if (d && IN_MEM1(REG32(d + 8)))
        sd_evt_alloc_pre(REG32(d + 8));
}

/* ---------------------------------------------------------------- S11: allocation sites */
#ifndef SD_NO_SITES
#include "s11_cold.h"                             /* cold-site list (generated from the site profiles) */
#endif
struct sd_site sd_sites[SD_SITES];
struct sd_site_stats sd_ss;
#ifndef SD_NO_SITES   /* SD_NO_SITES (product profiles): no allocation-site tables (88 KB), no site_alloc; sd_sites keeps 1 entry so the glue reporting loop links */
static u32 st_addr[SD_SITE_SLOTS];                /* payload address, 0 empty (linear probing, backward-shift delete) */
static u32 st_epoch = 0xFFFFFFFFu;
static u16 st_site[SD_SITE_SLOTS];
#endif
__attribute__((weak)) u32 sd_site_enable(void) { return 0; }
__attribute__((weak)) u32 sd_site_epoch(void) { return 0; }      /* the table starts over when this changes (scene) */

#ifndef SD_NO_SITES
static u32 st_hash(u32 a) { return ((a >> 5) * 0x9E3779B1u) >> 19; }   /* 13 bits */
/* CARD ROUTE: with -DSD_CARD_HOLE the Gecko bodies and this image live in the card hole, not at 0x804F0C20.
   The hole is always inside the loader ABI bounds [CL_HOLE_MIN, CL_HOLE_MAX) = [0x81700000, 0x817F0000) (offline/cardloader/cl/mscf.h), whatever base
   the exporter picked, so every "is this patched code" test below uses those bounds and never a base.  Without the define the constants are the
   historic ones (object code unchanged). */
#ifdef SD_CARD_HOLE
#define SD_BAKED_LO 0x81700000u
#define SD_BAKED_HI 0x817F0000u
#define SD_IN_BAKED(a) ((u32)(a) >= SD_BAKED_LO && (u32)(a) < SD_BAKED_HI)
#else
#define SD_BAKED_LO 0x804F0000u
#define SD_BAKED_HI 0x81000000u
#define SD_IN_BAKED(a) ((u32)(a) >= SD_BAKED_LO && (u32)(a) < SD_BAKED_HI)
#endif
#ifdef SD_CARD_HOLE
static int code_like(u32 a) { return ((a >= 0x80003000u && a < 0x80800000u) || SD_IN_BAKED(a)) && !(a & 3); }
#define BAKED_LO SD_BAKED_LO
#else
static int code_like(u32 a) { return a >= 0x80003000u && a < 0x80800000u && !(a & 3); }
#define BAKED_LO 0x804F0000u
#endif
/* S11: patched DOL text section (Gecko bodies + this image) starts at 0x804F0C20 */

static int key_eq(const u32 *a, const u32 *b)
{
    u32 k;
    for (k = 0; k < SD_SITE_DEPTH; k++)
        if (a[k] != b[k])
            return 0;
    return 1;
}

static u32 site_of_key(const u32 *lr)
{
    u32 i, h = 0;
    for (i = 0; i < SD_SITE_DEPTH; i++)
        h = h * 31u + lr[i];
    h = h % (SD_SITES - 1u) + 1u;
    for (i = 0; i < SD_SITES - 1u; i++, h = h % (SD_SITES - 1u) + 1u) {
        struct sd_site *e = &sd_sites[h];
        if (key_eq(e->lr, lr))
            return h;
        if (!e->lr[0]) {
            u32 k;
            for (k = 0; k < SD_SITE_DEPTH; k++)
                e->lr[k] = lr[k];
            for (k = 0; k < sizeof s11_cold_sites / sizeof s11_cold_sites[0]; k++)
                if (key_eq(s11_cold_sites[k], lr)) {
                    e->cold = 1;
                    sd_ss.cold_listed++;
                }
            sd_ss.nsites++;
            return h;
        }
    }
    sd_ss.site_full++;
    return 0;
}

static void st_wipe(void)
{
    u32 i;
    for (i = 0; i < SD_SITE_SLOTS; i++) st_addr[i] = 0;
    sd_ss.slots_used = 0;
}

static void st_put(u32 a, u32 site)
{
    u32 h = st_hash(a), i, e = sd_site_epoch();
    if (e != st_epoch) {                          /* new scene: the previous scene's heap is gone (no frees seen) */
        st_wipe();
        st_epoch = e;
    }
    if (sd_ss.slots_used >= SD_SITE_SLOTS * 3u / 4u) {   /* full within one scene: start over (counted) */
        st_wipe();
        sd_ss.wipes++;
    }
    for (i = 0; i < SD_SITE_SLOTS; i++, h = (h + 1u) & (SD_SITE_SLOTS - 1u)) {
        if (st_addr[h] == a) { st_site[h] = (u16)site; return; }
        if (!st_addr[h]) break;
    }
    sd_ss.slots_used++;
    st_addr[h] = a;
    st_site[h] = (u16)site;
}

static void st_del(u32 h)                         /* linear-probing delete without tombstones (backward shift) */
{
    u32 j = h, k;
    st_addr[h] = 0;
    sd_ss.slots_used--;
    for (;;) {
        j = (j + 1u) & (SD_SITE_SLOTS - 1u);
        if (!st_addr[j]) return;
        k = st_hash(st_addr[j]);
        /* move j back to h unless its home k lies cyclically in (h, j] */
        if (h <= j ? (h < k && k <= j) : (h < k || k <= j))
            continue;
        st_addr[h] = st_addr[j];
        st_site[h] = st_site[j];
        st_addr[j] = 0;
        h = j;
    }
}

static s32 st_find(u32 a)
{
    u32 h = st_hash(a), i;
    for (i = 0; i < SD_SITE_SLOTS && st_addr[h]; i++, h = (h + 1u) & (SD_SITE_SLOTS - 1u))
        if (st_addr[h] == a)
            return (s32)h;
    return -1;
}

static void site_alloc(u32 ptr, u32 sp)
{
    u32 lr[SD_SITE_DEPTH], c = sp, k, n, site;
    if (REG32(ptr - 0x20 + 8) - 0x20 > SD_CELL_MAX)
        return;
    for (k = 0; k < SD_SITE_DEPTH; k++)
        lr[k] = 0;
    for (k = 0, n = 0; k < SD_SITE_DEPTH && n < 3 * SD_SITE_DEPTH; n++) {   /* return address of frame k: LR save word of its caller */
        u32 a;
        if (!IN_MEM1(c) || (c & 3)) break;
        a = REG32(c + 4);
        if (!code_like(a)) break;
        if (a < BAKED_LO)                         /* S11: frames of patched code (Gecko bodies, our hook stubs) are skipped, so a
                                                     hooked allocator (sd_hpiecealloc_stub) does not shift the key */
            lr[k++] = a;
        if (REG32(c) <= c) break;                 /* back chain must go up */
        c = REG32(c);
    }
    if (!lr[0]) { sd_ss.walk_bad++; return; }
    site = site_of_key(lr);
    if (site) sd_sites[site].allocs++;
    st_put(ptr, site);
}

u32 sd_plat_cell_site(u32 lo)
{
    s32 h = st_find(lo);
    return h >= 0 ? st_site[h] : 0u;
}

int sd_plat_site_cold(u32 site) { return site && site < SD_SITES && sd_sites[site].cold; }

void sd_plat_cell_gone(const struct sd_range *r)
{
    u32 s = r->site & SD_SITE_IDX;
    if (!s || s >= SD_SITES) return;
    sd_sites[s].dead_n++;
    sd_sites[s].dead_chg_n += r->chg != 0;
    sd_sites[s].dead_chg_sum += r->chg;
    sd_sites[s].dead_age += sd_seq() - r->born;
    sd_sites[s].dead_uchg += (r->site & SD_SITE_UCHG) >> 16;
}

static void agg_cb(u32 kind, const struct sd_range *r)
{
    u32 s = r->site & SD_SITE_IDX, b = r->hi - r->lo;
    if (kind != 1) return;
    if (!s || s >= SD_SITES) { sd_ss.unknown_n++; sd_ss.unknown_b += b; return; }
    sd_sites[s].live_n++;
    sd_sites[s].live_b += b;
    sd_sites[s].live_chg_n += r->chg != 0;
    sd_sites[s].live_chg_b += r->chg ? b : 0;
    sd_sites[s].chg_sum += r->chg;
    sd_sites[s].age_sum += sd_seq() - r->born;
    sd_sites[s].live_uchg += (r->site & SD_SITE_UCHG) >> 16;
}

#endif /* !SD_NO_SITES */

#ifdef SD_NO_SITES
void sd_site_aggregate(void) {}
#else
void sd_site_aggregate(void)
{
    u32 i;
    for (i = 0; i < SD_SITES; i++)
        sd_sites[i].live_n = sd_sites[i].live_b = sd_sites[i].live_chg_n = sd_sites[i].chg_sum = sd_sites[i].age_sum =
            sd_sites[i].live_chg_b = sd_sites[i].live_uchg = 0;
    sd_ss.unknown_n = sd_ss.unknown_b = 0;
    sd_each(agg_cb);
}
#endif

u32 *sd_crumb;                                    /* optional breadcrumb words (net image): [0] alloc, [1] free */
void sd_osalloc_post(u32 ptr, u32 heap, u32 sp)
{
    if (sd_crumb) sd_crumb[0] = ptr;
#ifndef SD_NO_SITES
    if (heap == REG32(A_CURRENT_HEAP) && IN_MEM1(ptr) && sd_site_enable())   /* S11: before the sd_active check */
        site_alloc(ptr, sp);
#else
    (void)sp;
#endif
    if (!sd_active || scene_gone() || heap != REG32(A_CURRENT_HEAP) || !IN_MEM1(ptr))
        return;
    sd_evt_alloc(ptr, REG32(ptr - 0x20 + 8));
}

/* S11: HSD object pools (sysdolphin, HSD_ObjAllocData: +0x04 freehead, +0x0C free). HSD_ObjAlloc pops freehead
   (the caller then initialises the object), HSD_ObjFree writes the free-list link into the object: both write into the pool
   chunk (one heap cell), so the chunk is compared at the next capture even if its site is cold.  The same for the HSD
   memory pieces (hsdAllocMemPiece pops a per-size free list or splits a free piece; hsdFreeMemPiece writes the
   link into the piece; pieces are never given back to the OS heap, so their re-use shows no allocator event). */
void sd_hobjalloc(u32 obj)                        /* post-hook: the object HSD_ObjAlloc returned */
{
    if (sd_cold_mark && obj && !scene_gone())
        sd_touch(obj);
}

void sd_hpiecealloc(u32 mem)                      /* post-hook: the piece hsdAllocMemPiece returned */
{
    if (sd_cold_mark && mem && !scene_gone())
        sd_touch(mem);
}

void sd_hpiecefree(u32 mem)
{
    if (sd_cold_mark && mem && !scene_gone())
        sd_touch(mem);
}

void sd_hobjfree(u32 data, u32 obj)
{
    (void)data;
    if (sd_cold_mark && !scene_gone())
        sd_touch(obj);
}

void sd_osfree_pre(u32 heap, u32 ptr)
{
    if (sd_crumb) sd_crumb[1] = ptr;
#ifndef SD_NO_SITES
    if (heap == REG32(A_CURRENT_HEAP) && IN_MEM1(ptr) && sd_site_enable()) {   /* S11 */
        s32 h = st_find(ptr);
        if (h >= 0) st_del((u32)h);
    }
#endif
    if (!sd_active || scene_gone() || heap != REG32(A_CURRENT_HEAP) || !IN_MEM1(ptr))
        return;
    sd_evt_free(ptr);
}

void sd_objinit(u32 data)
{
    u32 i;
    for (i = 0; i < sd_npools; i++)
        if (sd_pools[i] == data)
            return;
    if (sd_npools < 64)
        sd_pools[sd_npools++] = data;
}

/* lbArq request (ARAM -> MEM1 transfer): destination range becomes dirty for the barrier ranges */
void sd_arq(u32 dst, u32 len)
{
    if (sd_active && !scene_gone())
        sd_mark_dirty(dst, len);
    sd_ms.arq_calls++;
}

/* Review M5: Kongo Jungle's platform table grKg_803E188C[15] (0x18 bytes each,.data at 0x803E188C) keeps unk10 (angular velocity, decays x0.99 per
   frame and is never zeroed) and unk14 from an earlier Kongo match of the same boot; stage init (grKongo_801D69B0) resets only unkC.  A console that played Kongo offline
   before going online therefore started the match with other platform motion than its opponent (desync from frame 1).  Both fields are zero in the boot image, so zeroing them
   at match start is the same state a fresh boot has, on every console. sd_kongo_reset is pinned by must_contain. */
static void sd_kongo_reset(void)
{
    u32 i;
    for (i = 0; i < 15; i++) {
        REG32(0x803E188Cu + i * 0x18u + 0x10u) = 0;
        REG32(0x803E188Cu + i * 0x18u + 0x14u) = 0;
    }
}

void sd_melee_init(void)
{
    u32 w = REG32(0x8006B460);
    sd_kongo_reset();
    sd_scene0 = REG32(A_SCENE_WORD);              /* the scene the store is made in (see scene_gone) */
    sd_scene_on = 1;
    if ((w & 0xFC000003) == 0x48000000) {      /* UCF Pad Buffer C2 hook: b body */
        s32 dd = (s32)(w & 0x03FFFFFC);
        if (dd & 0x02000000)
            dd -= 0x04000000;
        sd_ucf_body = 0x8006B460 + (u32)dd;
    }
}
