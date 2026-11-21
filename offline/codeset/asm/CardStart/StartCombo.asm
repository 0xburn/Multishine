# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x8022DD38
################################################################################
# Multishine card route, text-only profile. Own work; references: decomp mn/mnmain.c:2722
# mnMain_Scene_OnFrame (the exit sequence ends with data = gm_GetCurrentSceneExitData; data->pending_mode = GM_MENU; gm_801A4B60;).
# Opt-in flag card-start-combo.  The online major scene 8 exists on a stock disc only when the card loader has run the registration one-shot
# (offline/cardloader/card_export.py --oneshot 0x801A45B8) and the engine is resident, so this trigger is part of the card text-only profile only.
#
# While the main menu is up (mnMain_Scene_OnFrame runs once per frame there), holding L + R + Z + START on any controller sets the online mode
# byte to DIRECT (r13-0x5060 = 2, as Slippi's title-menu handler would) and leaves the scene towards Slippi's online major scene 8
# (the retail character select under it), exactly like the main menu leaves towards another mode. Nothing else is touched.
# At the hook the function has not saved anything yet (retail word 7C0802A6 mflr r0 is the replaced instruction, r0 = the caller's LR),
# there are no live volatile registers, so only r0 (LR copy) is kept across our two calls.

mflr r0                       # original instruction
lis r12, 0x804C
ori r12, r12, 0x1FAC          # HSD_PadMasterStatus (button word at +0, 0x44 per port)
li r11, 4
mtctr r11
ScLoop:
lwz r3, 0(r12)
andi. r3, r3, 0x1070          # START 0x1000 | L 0x40 | R 0x20 | Z 0x10
cmplwi r3, 0x1070
beq ScFire
addi r12, r12, 0x44
bdnz ScLoop
b ScExit
ScFire:
stwu r1, -16(r1)
stw r0, 20(r1)
li r3, 2
stb r3, -0x5060(r13)          # online mode DIRECT
lis r12, 0x801A
ori r12, r12, 0x4B9C          # gm_GetCurrentSceneExitData
mtctr r12
bctrl
li r4, 8
stb r4, 0(r3)                 # pending mode = Slippi online major scene
lis r12, 0x801A
ori r12, r12, 0x4B60          # gm_801A4B60: end the scene
mtctr r12
bctrl
lwz r0, 20(r1)
mtlr r0
addi r1, r1, 16
ScExit:
