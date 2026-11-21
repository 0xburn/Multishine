# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x801BFA20
# Codetype: 04
# Annotation: Multishine offline boot: BootToCSS
################################################################################
# Multishine offline replacement for slippi-ssbm-asm "Online/Slippi Online Scene/boot.asm".
# Original (retail) instruction at 0x801BFA20 is "lbz r3,4(r31)" (887F0004): it loads the
# game mode passed to gm_ChangeGameModeAfterCurrentScene when the boot/title scene is left.
# Forcing the mode skips the opening movie/title and boots straight to the VS-mode character select screen.
# Unlike boot.asm this does no EXI traffic, no Online submenu selection and no GrPs preload.
li r3, 2
