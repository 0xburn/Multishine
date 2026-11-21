# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x801AF3A8
# Codetype: 04
# Annotation: Multishine card: silent save creation (2/6, message layout JP)
################################################################################
# See SilentCreate1_LinesUS.asm. Retail: lwz r5,0x20(r30) (80BE0020), line count of message 0xA (JP).
lwz r5, 0x24(r30)
