# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x801AF3A0
# Codetype: 04
# Annotation: Multishine card: silent save creation (1/6, message layout US)
################################################################################
# Multishine offline (own code, opt-in flag card-silent-create, profile 'cardloader').
# Goal: when Slot A holds a healthy card with enough free space but no Melee save under the current
# name, create the save silently (exactly Melee's own "Yes" path) instead of asking
# "The Memory Card in Slot A has no saved Game Data. Create Game Data?".
# Needed by the stage-1 card loader: its copy table renames Melee's save (MultishineMelee), Melee
# re-enters its memory-card scene and finds no save under the new name.
#
# Decomp: , memory-card scene.
#   gm_801AF250 switches on lb_8001C87C (mount, check, open "SuperSmashBros0110290334"-style name).
#   taskOpen returns 4 only when the file is missing AND a directory entry AND enough blocks are free
#   (5 = not enough blocks, 6 = no free entry, 9 = damaged/encoding, 12 = sector size, 13/15 = no card).
#   case 4: gm_801AEE6C(1, 0xA, lines(0xA)) [Yes/No question], state 5.
#   state 5 Yes: gm_801AEE6C(0, 0xB, lines(0xB)) ["Creating Game Data"], state 6.
#   state 6: lb_8001C8BC creates+writes the save; ok -> message 0xC ("created"), state 7; fail -> 0xD, state 8.
#   state 7: waits for A/START, then leave data = 1, state 0x14 (leave scene), lbCardGame_SetCardStatus(0).
# Patch (six 04 words, only case 4 and state 7 change; every other result/message is untouched):
#   801AF3A0 lwz r5,0x7C(r30) -> 0x80(r30)   lines(0xA) -> lines(0xB), US table   (this file)
#   801AF3A8 lwz r5,0x20(r30) -> 0x24(r30)   lines(0xA) -> lines(0xB), JP table
#   801AF3AC li r3,1          -> li r3,0     no Yes/No buttons
#   801AF3B0 li r4,0xA        -> li r4,0xB   "Creating Game Data" instead of the question
#   801AF3B8 li r0,5          -> li r0,6     next state = create (state 6), as if Yes was chosen
#   801AFAD4 beq +0x14        -> b +0x20     state 7 continues without waiting for A (no sound)
# State 7 is only entered after a successful create in state 6, so the last word only removes the
# "Game Data has been created" acknowledgement.
lwz r5, 0x80(r30)
