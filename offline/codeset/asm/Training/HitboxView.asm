# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x800805F0
################################################################################
# Multishine training (own work; references: decomp ft/ftdrawcommon.c, ft/types.h, db/dbanim.c,
# gm/forward.h). Opt-in flag hitbox-view-training.
#
# Hitbox / hurtbox view in Training Mode, drawn by Melee's own collision debug draw.
#
# ftDrawCommon_800805C8 (ft/ftdrawcommon.c:81, the fighter render callback) draws the attack capsules x914[4], the
# hurt capsules, reflect / absorb / shield bubbles when fp->x21FC_flag.b6 (mask 0x02, ft/types.h:1703) is set; the
# developer debug menu sets that byte to 2 or 3 (db/dbanim.c:166-170). b7 (0x01) is "draw the model".
# Hook: 0x800805F0, retail word 880321FC (lbz r0,0x21FC(r3), r3 = fp), the load that line 101 tests.
# We OR 0x02 into the loaded register when the scene major is Training Mode (0x1C, gm/forward.h:48).
#
# Nothing is written to memory: the flag byte in the Fighter struct is unchanged, only this render pass sees the
# bit, so no snapshot, hash or checksum field changes. Never active outside Training Mode (Vs, online, 1P modes).
# Only r0 (the hooked load's own target) and r12 are used.

lbz r0, 0x21FC(r3)            # original instruction
lis r12, 0x8048
lbz r12, -0x62D0(r12)         # scene major (0x80479D30)
cmpwi r12, 0x1C               # GM_TRAINING
bne MsHbvExit
ori r0, r0, 0x02              # x21FC_flag.b6: draw collision capsules
MsHbvExit:
