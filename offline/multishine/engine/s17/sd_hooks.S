/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : copy of; S11: sd_osalloc_post gets r5 = the caller's r1. */
/* : allocator hooks for the delta snapshot engine (original work). Each is reached by a 04 code that writes
   `b <stub>` over the first instruction of the function:
     0x80343EF0  OSAllocFromHeap(heap, size)   sd_osalloc_stub: pre (allocated-list head pre-image) + post (event)
     0x80343FEC  OSFreeToHeap(heap, ptr)       sd_osfree_stub:  pre (header pre-images + event)
     0x8037AD48  HSD_ObjAllocInit(data, ...)   sd_objinit_stub: pool registry
     0x8037ABC8  HSD_ObjAlloc(data)            sd_hobjalloc_stub (S11, post): cold-cell invalidation
     0x8037AD20  HSD_ObjFree(data, obj)        sd_hobjfree_stub (S11): cold-cell invalidation
     0x80381FA8  hsdAllocMemPiece(size)        sd_hpiecealloc_stub (S11, post): cold-cell invalidation
     0x8038216C  hsdFreeMemPiece(mem, size)    sd_hpiecefree_stub (S11): cold-cell invalidation
   The displaced first instruction is executed by the stub, which then continues at entry + 4. */
        .text
        .globl  sd_osalloc_stub
sd_osalloc_stub:
        stwu    1,-32(1)
        mflr    0
        stw     0,36(1)
        stw     3,8(1)
        stw     4,12(1)
        bl      sd_osalloc_pre
        lwz     3,8(1)
        lwz     4,12(1)
        bl      sd_osalloc_orig
        stw     3,16(1)
        lwz     4,8(1)
        addi    5,1,32                  /* S11: r1 of the caller (stack walk for the allocation site) */
        bl      sd_osalloc_post
        lwz     3,16(1)
        lwz     0,36(1)
        mtlr    0
        addi    1,1,32
        blr
sd_osalloc_orig:
        .long   0x1C03000C              /* mulli r0,r3,12 (displaced) */
        lis     12,0x8034
        ori     12,12,0x3EF4
        mtctr   12
        bctr

/* pre-only hook: C function gets (r3, r4) of the hooked call; all argument registers and LR are preserved */
        .macro  PREHOOK name, cfn, insn, ret
        .globl  \name
\name:
        stwu    1,-0x40(1)
        stw     3,0x10(1)
        stw     4,0x14(1)
        stw     5,0x18(1)
        stw     6,0x1C(1)
        stw     7,0x20(1)
        stw     8,0x24(1)
        stw     9,0x28(1)
        stw     10,0x2C(1)
        mflr    0
        stw     0,0x30(1)
        bl      \cfn
        lwz     0,0x30(1)
        mtlr    0
        lwz     3,0x10(1)
        lwz     4,0x14(1)
        lwz     5,0x18(1)
        lwz     6,0x1C(1)
        lwz     7,0x20(1)
        lwz     8,0x24(1)
        lwz     9,0x28(1)
        lwz     10,0x2C(1)
        addi    1,1,0x40
        .long   \insn
        lis     12,(\ret)@h
        ori     12,12,(\ret)@l
        mtctr   12
        bctr
        .endm

        PREHOOK sd_osfree_stub, sd_osfree_pre, 0x7C0802A6, 0x80343FF0    /* OSFreeToHeap: mflr r0 */
        PREHOOK sd_objinit_stub, sd_objinit, 0x7C0802A6, 0x8037AD4C      /* HSD_ObjAllocInit: mflr r0 */
        PREHOOK sd_hobjfree_stub, sd_hobjfree, 0x80030004, 0x8037AD24    /* S11: HSD_ObjFree: lwz r0,4(r3) */
        PREHOOK sd_hpiecefree_stub, sd_hpiecefree, 0x7C0802A6, 0x80382170 /* S11: hsdFreeMemPiece: mflr r0 */

/* S11: post-hook: the original runs first (its displaced first instruction, then entry + 4, returning here), then the C
   function gets the returned pointer in r3; r3 is returned unchanged.  The frame is made before LR is touched. */
        .macro  POSTHOOK name, cfn, insn, ret
        .globl  \name
\name:
        stwu    1,-32(1)
        mflr    0
        stw     0,36(1)
        bl      \name\()_orig
        stw     3,16(1)
        bl      \cfn
        lwz     3,16(1)
        lwz     0,36(1)
        mtlr    0
        addi    1,1,32
        blr
\name\()_orig:
        .long   \insn
        lis     12,(\ret)@h
        ori     12,12,(\ret)@l
        mtctr   12
        bctr
        .endm

        POSTHOOK sd_hobjalloc_stub, sd_hobjalloc, 0x7C0802A6, 0x8037ABCC  /* S11: HSD_ObjAlloc: mflr r0 */
        POSTHOOK sd_hpiecealloc_stub, sd_hpiecealloc, 0x7C0802A6, 0x80381FAC /* S11: hsdAllocMemPiece: mflr r0 */
