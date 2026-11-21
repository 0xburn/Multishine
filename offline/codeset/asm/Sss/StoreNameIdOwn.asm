# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Multishine (own copy of Slippi's StoreNameId.asm, GPL-3.0, flag ms-sss-guard): the same code (maps name id 31 back to 18).  Harmless on the stock MnSlMap: id 31 is never produced there.
################################################################################
# Address: 0x80259fac
# SSS_CreateStageNameText saves the id its given, to data in the gobj
# so we must check if the id is 31 (Frozen) and set it back to 18 (Stadium)
################################################################################

.include "Common/Common.s"
.set ID_GRPS, 18

CODE_START:
  cmpwi r30, 31
  beq FROZEN_STADIUM

# original
  sth	r30, 0(r28)
  b EXIT

FROZEN_STADIUM:
  li r0, ID_GRPS
  sth	r0, 0(r28)

EXIT:
