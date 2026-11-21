/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* hub_sis: the one Melee text call of the hub overlay's menu toasts (hub_soc.c) that the soft-float C side cannot make itself.  Original work; the same stub as
   on_bhold_sis.S bh_sis_add (that one is resident and the overlay may not import it).
       int hs_sis_add(HSD_Text* t, const unsigned xy_bits[2], const char* fmt, const char* s)
           -> HSD_SisLib_803A6B98(t, x, y, fmt, s): add a subtext; f1/f2 = x/y (hard-float ABI), r4 = fmt, r5 = s, cr6 clear (no FP varargs).
   LR safety (offline/ rule 3): a tail call, LR still holds our caller's return address, no frame.  The two lfs reads are the only FPU instructions of the
   hub overlay (hub_ovl_pack.py allows them in this function only). */
        .text
        .globl  hs_sis_add
hs_sis_add:
        lfs     1,0(4)
        lfs     2,4(4)
        mr      4,5
        mr      5,6
        crxor   6,6,6
        lis     12,0x803A
        ori     12,12,0x6B98
        mtctr   12
        bctr
