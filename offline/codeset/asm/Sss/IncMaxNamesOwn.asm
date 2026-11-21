# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Multishine (own copy of Slippi's IncMaxNames.asm, GPL-3.0, flag ms-sss-guard): the same one instruction.  Harmless on the stock MnSlMap (ids 30 / 31 are only asked for by the guarded name modules, and only when MnSlMap is the patched one).
################################################################################
# Address: 0x80259fb4
# Will let us animate to our custom "Frozen Pokemon Stadium" matanim
################################################################################

cmpwi	r30, 32 # default is 30
