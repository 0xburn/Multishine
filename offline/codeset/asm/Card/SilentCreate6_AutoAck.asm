# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x801AFAD4
# Codetype: 04
# Annotation: Multishine card: silent save creation (6/6, no acknowledgement after create)
################################################################################
# See SilentCreate1_LinesUS.asm. Retail: beq 0x801AFAE8 (41820014), skips state 7 while A/START is not
# pressed. Replaced by b 0x801AFAF4 (leave data = 1, state 0x14, lbCardGame_SetCardStatus(0)).
.long 0x48000020
