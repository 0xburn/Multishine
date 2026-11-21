/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* lbHeap_80015900 entry (04 `b om_heap_stub` at 0x80015900, original instruction `mflr r0`): call om_heap_policy (om_heap.c), then continue
   the original function at +4 with LR = its return address and the replaced `mflr r0` executed.  LR safety (offline/): the RA is
   stored in 4(SP0) before the frame is pushed; while LR holds anything else r1 = SP0 - 16 (own frame).  Same shape as the S14 stub. */
        .text
        .globl om_heap_stub
om_heap_stub:
        mflr    0
        stw     0, 4(1)
        stwu    1, -16(1)
        bl      om_heap_policy
        lwz     0, 20(1)
        addi    1, 1, 16
        mtlr    0
        mflr    0
        lis     12, 0x8001
        ori     12, 12, 0x5904
        mtctr   12
        bctr
