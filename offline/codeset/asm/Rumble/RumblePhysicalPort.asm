# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x80378430 # HSD_PadRumbleAdd
################################################################################
# Multishine online (own code, opt-in flag ms-rumble-port "make all game modes always respect rumble mode settings"):
# in an online match the Options > Rumble setting of the controller you PLAY WITH decides, not the setting of the slot you play as.
#
# Why: Melee gates a player's rumble with the per-slot flag set at match start from GetRumbleSettingOfPort(slot) (gm_LoadRumbleEnabled), and Slippi's
# HandleRumble.asm (PADControlMotor) then redirects the local player's slot to the physical controller port.  So the Off that counted online was the
# one of the SLOT port (default On), not of the port the controller is plugged into (a tester with Rumble Off for his port still felt it).  Slippi's own
# redirect cannot be used as the hook point on a Wii: Nintendont replaces PADControlMotor with its own stub there.
#
# This hook is the sink every in-game rumble request goes through (HSD_PadRumbleAdd(no, id, frame, pri, list), lb_80014574 -> here).  When the scene
# is the online in-game scene, the request is for the LOCAL player's slot (ODB_LOCAL_PLAYER_INDEX) and the setting of the PHYSICAL port
# (ODB_INPUT_SOURCE_INDEX, the port the controller is in) says Off, the request is dropped (returns 0, the "list full" answer the callers already
# ignore).  Every other scene runs the retail code unchanged.  Rumble is cosmetic: nothing here is synced or in a checksum.
# Part 2: a request for any slot that is NOT the local player's is dropped unconditionally in the
# online in-game scene (Slippi's HandleRumble rule; on a Wii Nintendont replaces PADControlMotor so that hook never ran and the remote slot's motor command
# landed on whichever pad sat in that channel).  On a real GameCube HandleRumble would drop it later too; the request now never reaches it (no double handling).
# No local player (ODB_LOCAL_PLAYER_INDEX > 3): retail code, nothing dropped.  A spectator with local index 2 in a 2-player match feels nothing (correct).
# Setting read as GetRumbleSettingOfPort does (0x8015ED3C): lwz save, -0x77C0(r13); lbz port + 0x1CC0(save).
# Only r8-r11 are used before the retail first instruction (mflr r0): r3 = no (u8, masked to 8 bits here) and r4..r7 (id, frame, pri, list) stay untouched.
.include "Common/Common.s"
.include "Online/Online.s"

getMinorMajor r8
cmpwi r8, SCENE_ONLINE_IN_GAME
bne ORIGINAL
lwz r8, OFST_R13_ODB_ADDR(r13)
cmpwi r8, 0
beq ORIGINAL
lbz r9, ODB_LOCAL_PLAYER_INDEX(r8)
cmplwi r9, 3
bgt ORIGINAL
clrlwi r11, r3, 24
cmpw r9, r11
beq LOCAL_SLOT
# Not the local player's slot: online you feel only your own fighter (Slippi's HandleRumble rule, moved here because Nintendont's PADControlMotor stub
# skips it).  Unconditional: no setting is read.  Returns 0 like the "list full" answer.
li r3, 0
blr

LOCAL_SLOT:
lbz r9, ODB_INPUT_SOURCE_INDEX(r8)
cmplwi r9, 3
bgt ORIGINAL
lwz r10, -0x77C0(r13)
cmpwi r10, 0
beq ORIGINAL
add r10, r10, r9
lbz r10, 0x1CC0(r10)
cmpwi r10, 0
bne ORIGINAL
li r3, 0
blr

ORIGINAL:
mflr r0
