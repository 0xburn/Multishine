/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Nigzel / Bjerreman */
/*
 * Multishine Gecko list runner ("code handler"), written from scratch from the public Gecko code type description
 * (WiiRD / Gecko OS code type table; Dolphin's GCT format).  It replaces the third-party handler binary
 * that the Melee save exploit tutorial files shipped (C3).  No code of any other handler was read while writing it.
 *
 * Role.  The save exploit's loader 2 copies this image to 0x801910E0 (datacopy record "Gecko codehandler", 0xBA8 bytes) and calls
 * 0x801910E4 ONCE as an ordinary C function (no per-frame hook).  Word 0 of the image (0x801910E0) is DATA: the address of the Gecko list.
 * The list is the one the exploit copied from the save (default 0x817F4000; the generic/low layouts give another address at build time,
 * `-defsym LIST=...`).
 *
 * Supported code types (exactly the ones our lists use; anything else STOPS the run at that code, nothing after it is executed):
 *   00D0C0DE 00D0C0DE     list header (required, otherwise nothing runs)
 *   04AAAAAA VVVVVVVV     write 32-bit V to 0x80000000 | A  (A 25 bits, word aligned); data cache flushed and instruction cache invalidated for it
 *   22AAAAAA VVVVVVVV     if (word at 0x80000000 | A) != V  then run the following codes up to the matching E2, else skip them
 *   C0000000 NNNNNNNN     execute the next N 8-byte lines as PowerPC code (called with bctrl; the code ends with blr)
 *   E2000NNN 00000000     end N levels of "if" (low 8 bits of the first word; the second word must be 0: no else, no base/pointer change)
 *   F0000000 00000000     end of list (only the type byte is looked at)
 * Not supported on purpose (not used): 00/02 writes, 06/08/0A.. string/serial, base address / pointer offset, 20/24/26/28-2E compares, C2/C4,
 * the repeat/goto/gosub codes, E0, 'else'.
 *
 * ABI.  Entered like a function: LR = return, r1 = stack.  r3-r12, CTR, XER, CR0 are used freely (volatile); r27-r31 and LR are saved and restored.
 * Registers: r31 cursor (address of the current code), r30 skip depth (0 = executing), r29 line count of the running C0, r28 scratch address.
 */
        .section .text, "ax"
        .globl  ch_list_word
        .globl  ch_entry
        .ifndef LIST
        .set    LIST, 0x817F4000
        .endif
ch_list_word:                   /* +0x00  data word: address of the Gecko list */
        .long   LIST
ch_entry:                       /* +0x04  the exploit calls here */
        mflr    0
        stw     0, 4(1)
        stwu    1, -0x30(1)
        stmw    27, 0x1C(1)             /* r27..r31 -> 0x1C..0x2F */
        lis     12, 0x8019
        lwz     31, 0x10E0(12)          /* r31 = list address (word 0 of this image, at its final place 0x801910E0) */
        lwz     3, 0(31)
        lwz     4, 4(31)
        lis     5, 0x00D0
        ori     5, 5, 0xC0DE
        cmplw   3, 5
        bne     ch_done
        cmplw   4, 5
        bne     ch_done
        addi    31, 31, 8
        li      30, 0
ch_loop:
        lwz     3, 0(31)                /* first word */
        lwz     4, 4(31)                /* second word */
        rlwinm  5, 3, 8, 24, 31         /* type byte */
        cmpwi   5, 0xF0
        beq     ch_done
        cmpwi   30, 0
        bne     ch_skipping
        cmpwi   5, 0x04
        beq     ch_w32
        cmpwi   5, 0x22
        beq     ch_ifne
        cmpwi   5, 0xC0
        beq     ch_exec
        cmpwi   5, 0xE2
        beq     ch_endif
        b       ch_done                 /* unsupported type: stop */

ch_w32:                                 /* 04: write word, make it visible to instruction fetch too */
        rlwinm  28, 3, 0, 7, 29         /* A & 0x01FFFFFC */
        oris    28, 28, 0x8000
        stw     4, 0(28)
        dcbst   0, 28
        sync
        icbi    0, 28
        sync
        isync
        b       ch_next8

ch_ifne:                                /* 22: execute the following codes only when the word differs */
        rlwinm  28, 3, 0, 7, 29
        oris    28, 28, 0x8000
        lwz     6, 0(28)
        cmplw   6, 4
        bne     ch_next8                /* differs: condition true, keep executing */
        li      30, 1                   /* equal: skip to the matching E2 */
        b       ch_next8

ch_endif:                               /* E2 while executing: nothing is open that matters, just validate the form */
        cmpwi   4, 0
        bne     ch_done
        b       ch_next8

ch_exec:                                /* C0: run N lines of code that follow the header */
        mr      29, 4
        addi    28, 31, 8               /* code start */
        slwi    6, 29, 3                /* byte length */
        add     7, 28, 6                /* end */
        rlwinm  8, 28, 0, 0, 26         /* first cache line (32 bytes) */
ch_fl1:
        dcbst   0, 8
        addi    8, 8, 32
        cmplw   8, 7
        blt     ch_fl1
        sync
        rlwinm  8, 28, 0, 0, 26
ch_fl2:
        icbi    0, 8
        addi    8, 8, 32
        cmplw   8, 7
        blt     ch_fl2
        sync
        isync
        mtctr   28
        bctrl
        slwi    6, 29, 3
        add     31, 31, 6               /* skip the code lines; the header is skipped by ch_next8 below */
        b       ch_next8

ch_skipping:                            /* inside a false "if": only the structure matters */
        cmpwi   5, 0x04
        beq     ch_next8
        cmpwi   5, 0x22
        beq     ch_nest
        cmpwi   5, 0xC0
        beq     ch_skipc0
        cmpwi   5, 0xE2
        beq     ch_skipend
        b       ch_done
ch_nest:
        addi    30, 30, 1
        b       ch_next8
ch_skipc0:
        slwi    6, 4, 3
        add     31, 31, 6
        b       ch_next8
ch_skipend:
        cmpwi   4, 0
        bne     ch_done
        clrlwi  6, 3, 24                /* N levels */
        subf.   30, 6, 30
        bge     ch_next8
        li      30, 0                   /* never below zero */
ch_next8:
        addi    31, 31, 8
        b       ch_loop

ch_done:
        sync
        isync
        lmw     27, 0x1C(1)
        addi    1, 1, 0x30
        lwz     0, 4(1)
        mtlr    0
        blr
