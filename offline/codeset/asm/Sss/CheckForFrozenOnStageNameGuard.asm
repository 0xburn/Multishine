# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x80259e90
# Multishine (own code, flag ms-sss-guard; derived from Slippi's CheckForFrozenOnStageName.asm, GPL-3.0): the name id 31 (Frozen Pokemon Stadium) is only chosen when
# StageSelectTextProcGuard.asm has proven that MnSlMap is Slippi's patched one (its word at body + 8); on the stock MnSlMap there is no name animation 31.
################################################################################

.include "Common/Common.s"

.set SSS_HoveredIcon, 0x804d6cae # u8
.set SSS_CreateStageNameText, 0x80259ed8
.set SITE_TEXTPROC, 0x8025ac6c   # the hook of StageSelectTextProcGuard.asm: its body holds the 'patched MnSlMap' word at body + 8
.set OFS_PATCHED, 8
.set ID_GRPS, 18
.set OFST_HOVERED_ICON, -0x49F2

.set REG_FROZEN, 31
.set REG_NEW_ICON, 30

b CODE_START

CODE_START:
  backup
  lbz	r3, OFST_HOVERED_ICON(r13)
  mr REG_NEW_ICON, r3

  cmpwi REG_NEW_ICON, ID_GRPS
  bne EXIT

  # the patched MnSlMap must be there (published by StageSelectTextProcGuard.asm), else the plain icon
  computeBranchTargetAddress REG_FROZEN, SITE_TEXTPROC
  lwz r3, OFS_PATCHED(REG_FROZEN)
  cmpwi r3, 0
  beq EXIT

  # get our static toggle data
  computeBranchTargetAddress REG_FROZEN, INJ_FREEZE_STADIUM
  addi REG_FROZEN, REG_FROZEN, 0x8

  lwz r3, 0x0(REG_FROZEN)
  cmpwi r3, 0
  beq EXIT
  
  # we have frozen stadium toggled on
  li REG_NEW_ICON, 31

EXIT:
  mr r3, REG_NEW_ICON
  restore
