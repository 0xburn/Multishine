/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Re-inject for the Wii route.  Pure C89 header, no library calls, included by ml_glue.c (menu overlay, card route) and by the host test.
 *
 * Problem: on a stock disc under Nintendont the Name Entry inject (ota/ms_inject_apply.h) is applied ONCE, by the PPC launcher at the game start.  The restart after an update is Melee's
 * own OSResetSystem(0,0,0) = __OSReboot: the game reads its boot image and apploader from the disc by itself and loads the DOL again through the kernel's disc emulation, so the launcher
 * (and with it the inject) is not in the path, and the player lands in stock Melee.
 *
 * Fix without touching the kernel: the kernel's own entry stub (FakeEntryLoad, copied to 0x80002F90 once at kernel start, the DOL's entry point is rewritten to it at every DOL load,
 * including a reload by __OSReboot) is the window between 'DOL loaded and patched' and 'game started', the same window the launcher uses.  Right before the reset we
 *   1. put a small position independent trampoline and a list of (address, word) pairs into the card work window (above the heap: nothing reclaims it before OSInit, which runs after the stub),
 *   2. point the stub's last instruction (blr) at the trampoline.
 * After the reload the stub reaches the trampoline: it writes the pairs (the hook words and the cave, the same words the launcher wrote the first time, the cave's one-shot word zero),
 * flushes every word, puts the stub's blr back (single shot: later resets are not touched) and returns through LR like the stub would have.
 * THE RESTART RUNS THE STUB TWICE (1182-1188 Wii logs: ramlog.log starts with 'DIP:App Switcher' = the boot image the game reads from the disc, patched by the kernel and entered through the stub with
 * LR = 0x81300000; only the second run, with LR = the game's entry 0x8000522C, comes after the game DOL was loaded).  The trampoline therefore acts only when LR < 0x81000000 and otherwise
 * returns at once, leaving the stub hooked (it stamps 0x524A0006).  Before that fix it applied the words in the first run, before the DOL load, and restored the stub: the DOL then loaded over them.
 * Everything is checked first: the stub must be the known one (7 signature words and the blr), and the live words at the hook sites must be exactly the inject's (so this only arms when
 * the running game IS an injected one, never on another disc image or variant).  Any 'no' = nothing is written = today's behaviour. */
#ifndef ML_REINJ_H
#define ML_REINJ_H

#include "ml_reinj_tab.h"

typedef unsigned int rj_u32;
#ifndef RJ_R
#define RJ_R(a) (*(volatile rj_u32 *)(a))
#endif

#define RJ_STUB_LO 0x80002E00u          /* the stub is at 0x80002F90 (100 bytes) in the q32 kernel; the search is wider for another build of the same stub */
#define RJ_STUB_HI 0x80003000u
#define RJ_STUB_BLR_OFF 96u             /* byte offset of the stub's last word (25 words) */
#define RJ_BLR 0x4E800020u
#define RJ_NCODE 49u                    /* trampoline words */
#define RJ_LISTOFF (RJ_NCODE * 4u)
#define RJ_NPAIRS (1u + RJ_NHOOKS + RJ_CAVE_NWORDS)        /* the stub's blr put back, the hook words, the cave */
#define RJ_DIAG0 0xD30267D0u            /* uncached MEM2 words the kernel's shutdump prints as 'FZ4: BC %08X at tbl %08X' (the engine's own breadcrumb words; an engine that never started leaves ours) */
#define RJ_DIAG1 0xD30267D4u
#define RJ_DIAG_RAN 0x524A0500u         /* 'RJ' 5: the trampoline ran: low byte = how many written words did not read back (0 = all held at the end of the stub window); the second word = the live word at 0x801A4578 (the progressive-scan prompt site, expected 48000028).  0x524A0004 armed, 1 live words differ, 2 window busy, 3 window out of range */
#define RJ_BYTES (RJ_LISTOFF + (RJ_NPAIRS + 1u) * 8u)      /* code + pairs + the terminating zero address */

/* the first 7 words of FakeEntryLoad (lis r3,0xD300; li r0,0x4DEA; stw r0,0x3420(r3); lwz r4,0x3420(r3); cmplw r4,r0; beq -8; mtlr r4) */
static const rj_u32 rj_stub_sig[7] = { 0x3C60D300u, 0x38004DEAu, 0x90033420u, 0x80833420u, 0x7C040040u, 0x4182FFF8u, 0x7C8803A6u };

/* address of the stub's last word, 0 when the known stub is not there */
__attribute__((unused)) static rj_u32 rj_find_stub_blr(void)
{
    rj_u32 a, i;
    for (a = RJ_STUB_LO; a + RJ_STUB_BLR_OFF < RJ_STUB_HI; a += 4u) {
        for (i = 0; i < 7u; i++)
            if (RJ_R(a + 4u * i) != rj_stub_sig[i]) break;
        if (i == 7u && RJ_R(a + RJ_STUB_BLR_OFF) == RJ_BLR)
            return a + RJ_STUB_BLR_OFF;
    }
    return 0;
}

/* 0 when the live game holds exactly the inject (every hook word and every cave word but the one-shot word), else 1 + the index of the first difference (hooks 0.., cave 100..) and the live word */
__attribute__((unused)) static rj_u32 rj_live_diff(rj_u32 *live)
{
    rj_u32 i;
    for (i = 0; i < RJ_NHOOKS; i++)
        if (RJ_R(rj_hooks[i][0]) != rj_hooks[i][1]) { *live = RJ_R(rj_hooks[i][0]); return 1u + i; }
    for (i = 0; i < RJ_CAVE_NWORDS; i++)
        if (i != RJ_CAVE_FLAG && RJ_R(RJ_CAVE_ADDR + 4u * i) != rj_cave[i]) { *live = RJ_R(RJ_CAVE_ADDR + 4u * i); return 101u + i; }
    return 0u;
}
__attribute__((unused)) static int rj_live_is_inject(void)
{
    rj_u32 l;
    return rj_live_diff(&l) == 0u;
}

/* the branch word 'b to' placed at 'from' (0 = out of reach: +-32 MB) */
__attribute__((unused)) static rj_u32 rj_branch(rj_u32 from, rj_u32 to)
{
    rj_u32 d = to - from;
    if (((d + 0x02000000u) & 0xFC000000u) != 0u || (d & 3u)) return 0;     /* reach and alignment */
    return 0x48000000u | (d & 0x03FFFFFCu);
}

/* the trampoline: position independent (finds itself with bl/mflr, keeps LR, uses r4-r6 and r12 only, all free at the stub's end), then the pairs.
 *     mflr r12 ; bl .+4 ; mflr r4 ; mtlr r12 ; addi r4,r4,LISTOFF-8
 *  L: lwz r5,0(r4) ; cmpwi r5,0 ; beq D; lwz r6,4(r4) ; stw r6,0(r5) ; dcbst 0,r5 ; sync; icbi 0,r5 ; addi r4,r4,8 ; b L
 *  D: sync; isync; blr */
typedef char rj_listoff_check[(RJ_LISTOFF == 196u) ? 1 : -1];      /* the addi above is LISTOFF - 8 = 188 */
static const rj_u32 rj_tramp_code[RJ_NCODE] = {
    0x7D8802A6u, 0x48000005u, 0x7C8802A6u, 0x7D8803A6u, 0x3CA08100u,
    0x7C0C2840u, 0x4180001Cu, 0x3CA0D302u, 0x60A567D0u, 0x3CC0524Au,
    0x60C60006u, 0x90C50000u, 0x4E800020u, 0x388400BCu, 0x7C872378u,
    0x80A40000u, 0x2C050000u, 0x41820020u, 0x80C40004u, 0x90C50000u,
    0x7C0028ACu, 0x7C0004ACu, 0x7C002FACu, 0x38840008u, 0x4BFFFFDCu,
    0x7C0004ACu, 0x4C00012Cu, 0x39000000u, 0x80A70000u, 0x2C050000u,
    0x41820020u, 0x80C70004u, 0x81250000u, 0x7C093040u, 0x41820008u,
    0x39080001u, 0x38E70008u, 0x4BFFFFDCu, 0x3CA0D302u, 0x60A567D0u,
    0x3CC0524Au, 0x60C60500u, 0x7CC64378u, 0x90C50000u, 0x3D20801Au,
    0x81294578u, 0x91250004u, 0x7C0004ACu, 0x4E800020u
};

/* write trampoline + pairs into w (RJ_BYTES bytes; the caller flushes it); blr = address of the stub's last word.  Returns the number of words written. */
__attribute__((unused)) static rj_u32 rj_build(volatile rj_u32 *w, rj_u32 blr, rj_u32 wbase)
{
    rj_u32 i, n = 0;
    for (i = 0; i < RJ_NCODE; i++) w[n++] = rj_tramp_code[i];
    w[n++] = blr; w[n++] = RJ_BLR;                                                  /* the stub is put back first: single shot */
    for (i = 0; i < RJ_NHOOKS; i++) { w[n++] = rj_hooks[i][0]; w[n++] = rj_hooks[i][1]; }
    for (i = 0; i < RJ_CAVE_NWORDS; i++) { w[n++] = RJ_CAVE_ADDR + 4u * i; w[n++] = i == RJ_CAVE_FLAG ? 0u : rj_cave[i]; }
    (void)wbase;
    w[n++] = 0u; w[n++] = 0u;
    return n;
}

/* ---- GameCube, stock disc  ----
 * The same restart (OSResetSystem(0,0,0) = __OSReboot) has no kernel stub on a GameCube: __OSReboot reads the disc's boot image to 0x81300000 and enters it (Run); the boot image runs the
 * apploader (init / main until the DOL is loaded / close), and its last handler does 0x813005E4 'blrl' = the apploader's close (returns the game entry in r3), 0x813005E8 'lis r4,0x8130',
 * 0x813005EC 'lis r5,2', 0x813005F0 'bl' a tail that clears 0x81300000.. and jumps to r3.  At 0x813005E8 the DOL is loaded and nothing of the game has run: the same window as the Wii stub.
 *   1. __OSReboot's 'bl Run' (0x803482F0, in the running DOL) -> bl STAGE1 (card work window).  STAGE1 runs after the boot image was read and before it starts: it hashes the boot image
 *      window 0x81300540..0x813005FF (rot5-xor, RJG_BOOT_HASH = the v1.02 disc's --boot-hash; only the hash is stored) and only on a match replaces the
 *      word at 0x813005E8 by 'b STAGE2', then tail-jumps to Run (LR and r3 kept: Run never returns).  Any other boot image = nothing patched = today's restart.
 *   2. STAGE2 'mtlr r3 ; b TRAMP': the Wii trampoline (rj_tramp_code) entered with LR = the game entry (< 0x81000000: it acts; anything else: stamp 6, nothing written), its two stamp
 *      addresses moved from the Wii's MEM2 words into the window and its two 'blr' exits pointed at TAIL.  It writes the pairs (BASE-MIN P1 / P2 / P3 / P8 and its cave with the one-shot word
 *      zero, ml_reinj_gc_tab.h) into the fresh DOL, each flushed.  TAIL 'lis r4,0x8130 ; b 0x813005EC' = the replaced instruction, then the boot image goes on exactly as before.
 *      Melee then boots into Name Entry by itself (the BASE-MIN disc's own path), the exploit fires, the card loader loads the NEW build.
 * Armed only when no Nintendont stub is there (the Wii path above), the live 'bl Run' word is the expected one (live P2 / P3 are NOT checked: after a re-inject they are BASE-MIN's in RAM).
 * The window (card work area, 0x817E4000..) is above everything the boot image (0x81300000..0x81700000 cleared at its end), the apploader (0x81200000) and the DOL touch; OSInit's ClearArena
 * clears it afterwards. */
#include "ml_reinj_gc_tab.h"
#define RJG_REBOOT_CALL 0x803482F0u     /* __OSReboot: bl Run(0x81300000) */
#define RJG_RUN 0x803480F8u             /* Run */
#define RJG_BOOT_SITE 0x813005E8u       /* boot image: 'lis r4,0x8130' right after the apploader's close returned the game entry in r3 */
#define RJG_BOOT_NEXT 0x813005ECu       /* where TAIL continues */
#define RJG_BOOT_HASH 0xB823119Eu       /* rot5-xor of the 48 boot image words at 0x81300540 (NTSC-U v1.02) */
#define RJG_S1_NCODE 25u
#define RJG_NPAIRS (RJG_NHOOKS + RJG_CAVE_NWORDS)
#define RJG_DIAGOFF (RJ_LISTOFF + (RJG_NPAIRS + 1u) * 8u)  /* two words: the trampoline's stamps (0x524A05NN, the live 0x801A4578 word) */
#define RJG_S2OFF (RJG_DIAGOFF + 8u)                       /* STAGE2: mtlr r3 ; b TRAMP */
#define RJG_TAILOFF (RJG_S2OFF + 8u)                       /* TAIL: lis r4,0x8130 ; b 0x813005EC */
#define RJG_S1OFF (RJG_TAILOFF + 8u)
#define RJG_BYTES (RJG_S1OFF + (RJG_S1_NCODE + 3u) * 4u)  /* + stage 1's hash stamp, expected hash, 'b STAGE2' */
#define RJG_S1_HASHSEEN (RJG_S1OFF + RJG_S1_NCODE * 4u)   /* stage 1 ran: the hash it computed */
/*     mflr r12 ; bl 1f; 1: mflr r9 ; mtlr r12 ; lis r5,0x8130 ; addi r6,r5,0x53C; li r7,0 ; li r8,48 ; mtctr r8
 *  2: lwzu r8,4(r6) ; rotlwi r7,r7,5 ; xor r7,r7,r8 ; bdnz 2b; stw r7,92(r9) ; lwz r8,96(r9) ; cmplw r7,r8 ; bne 3f
 *     lwz r8,100(r9) ; stw r8,0x5E8(r5) ; addi r6,r5,0x5E8 ; dcbst 0,r6 ; sync; icbi 0,r6 ; isync
 *  3: b Run (written by rjg_build) ; .long seen, hash, 'b STAGE2' */
static const rj_u32 rjg_s1_code[RJG_S1_NCODE] = {
    0x7D8802A6u, 0x48000005u, 0x7D2802A6u, 0x7D8803A6u, 0x3CA08130u, 0x38C5053Cu, 0x38E00000u, 0x39000030u, 0x7D0903A6u, 0x85060004u,
    0x54E7283Eu, 0x7CE74278u, 0x4200FFF4u, 0x90E9005Cu, 0x81090060u, 0x7C074040u, 0x40820020u, 0x81090064u, 0x910505E8u, 0x38C505E8u,
    0x7C00306Cu, 0x7C0004ACu, 0x7C0037ACu, 0x4C00012Cu, 0x48000000u
};

/* 1 when the GameCube path may arm: no Nintendont stub, __OSReboot's call is the expected 'bl Run' */
__attribute__((unused)) static int rjg_live_ok(void)
{
    if (rj_find_stub_blr() != 0u) return 0;
    if (RJ_R(RJG_REBOOT_CALL) != (rj_branch(RJG_REBOOT_CALL, RJG_RUN) | 1u)) return 0;
    /* No refusal for live BASE-MIN P2 / P3 words: a game that was itself started by this re-inject carries them in RAM only (the next __OSReboot loads the disc's stock DOL again),
       so refusing them made the second update restart after a power-on a plain Melee.  On a real BASE-MIN disc the same words are written over the same words. */
    return 1;
}

/* write trampoline + pairs + stamps + stage 2 + tail + stage 1 into w (RJG_BYTES bytes at address wbase; the caller flushes it).  Returns the address of stage 1 (0: out of reach). */
__attribute__((unused)) static rj_u32 rjg_build(volatile rj_u32 *w, rj_u32 wbase)
{
    rj_u32 i, n = 0, diag = wbase + RJG_DIAGOFF, s1 = wbase + RJG_S1OFF, s2 = wbase + RJG_S2OFF, tail = wbase + RJG_TAILOFF, brun, bs2, btr, bnext, bx12, bx48;
    brun = rj_branch(s1 + (RJG_S1_NCODE - 1u) * 4u, RJG_RUN);
    bs2 = rj_branch(RJG_BOOT_SITE, s2);
    btr = rj_branch(s2 + 4u, wbase);
    bnext = rj_branch(tail + 4u, RJG_BOOT_NEXT);
    bx12 = rj_branch(wbase + 12u * 4u, tail);
    bx48 = rj_branch(wbase + 48u * 4u, tail);
    if (!brun || !bs2 || !btr || !bnext || !bx12 || !bx48 || !rj_branch(RJG_REBOOT_CALL, s1)) return 0;
    for (i = 0; i < RJ_NCODE; i++) w[n++] = rj_tramp_code[i];
    w[7] = w[38] = 0x3CA00000u | (diag >> 16);                                     /* lis r5,diag@h (was the Wii's 0xD302) */
    w[8] = w[39] = 0x60A50000u | (diag & 0xFFFFu);                                 /* ori r5,r5,diag@l */
    w[12] = bx12;                                                                  /* both exits (blr) -> TAIL: the boot image goes on */
    w[48] = bx48;
    for (i = 0; i < RJG_NHOOKS; i++) { w[n++] = rjg_hooks[i][0]; w[n++] = rjg_hooks[i][1]; }
    for (i = 0; i < RJG_CAVE_NWORDS; i++) { w[n++] = RJG_CAVE_ADDR + 4u * i; w[n++] = i == RJG_CAVE_FLAG ? 0u : rjg_cave[i]; }
    w[n++] = 0u; w[n++] = 0u;
    w[n++] = 0u; w[n++] = 0u;                                                      /* stamps */
    w[n++] = 0x7C6803A6u; w[n++] = btr;                                            /* STAGE2: mtlr r3 ; b TRAMP */
    w[n++] = 0x3C808130u; w[n++] = bnext;                                          /* TAIL: lis r4,0x8130 ; b 0x813005EC */
    for (i = 0; i < RJG_S1_NCODE; i++) w[n++] = rjg_s1_code[i];
    w[n - 1u] = brun;
    w[n++] = 0u; w[n++] = RJG_BOOT_HASH; w[n++] = bs2;
    return s1;
}

#endif
