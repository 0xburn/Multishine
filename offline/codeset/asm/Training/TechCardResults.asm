# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x801A5C14
################################################################################
# Multishine training (own work; references: decomp gm/gmvsmelee.c:209-233 gmVsMelee_ExitVs,
# gm/gmvsmode.c:226 (Vs mode passes Results / SuddenDeath), gm/gmresult.c:140 gm_WasMatchCanceled, gm/forward.h
# MatchOutcome). Opt-in flag tech-card. Brings Melee's results screen back after a finished Vs match, so the tech card
# (TechCardDraw.asm) has a screen to appear on.
#
# Slippi's required "Salty Runback" (External/Salty Runback, C2 0x801A5B14, unchanged) sets r27, the next Vs-mode
# state, to 0 (CSS) unless A+B is held (2 = rematch); retail passes 4 (Results) there. Hook: 0x801A5C14 in
# gmVsMelee_ExitVs, retail word 7F63DB78 (mr r3,r27), the argument of gm_SetNextGameModeStateId when the match has a
# single winner. We pass 4 instead of 0 when the scene major is Vs Melee (2) and the match ended normally
# (MatchEnd.outcome, exit+0x10 in r30: 1 timeout, 2 elimination, 3 team elimination). A rematch (2), L+R+A+Start
# (no contest 7 / retry 8) and every other mode keep Slippi's flow. Melee's own results screen then returns to the
# CSS on A/Start as in retail. Only r3 and r12 are used.

mr r3, r27                    # original instruction
cmpwi r3, 0                   # Salty Runback chose the CSS
bne TcrExit
lis r12, 0x8048
lbz r12, -0x62D0(r12)         # scene major (0x80479D30)
cmpwi r12, 2                  # Vs Melee
bne TcrExit
lbz r12, 0x10(r30)            # MatchEnd.outcome
subi r12, r12, 1
cmplwi r12, 2                 # 1..3: ended normally
bgt TcrExit
li r3, 4                      # gmVsMode_State_Results
TcrExit:
