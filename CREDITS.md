<!-- Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later -->
# Credits

Multishine is made by Nigzel / Bjerreman.

It stands on a lot of other people's work:

- **Slippi.** Online Melee on real hardware exists because the Slippi team spent years on rollback netcode, matchmaking and the replay format. Multishine follows their protocol, and without them it would never have existed. The engine is built from Slippi's public code modules (`slippi-ssbm-asm`, GPL-3.0), the data pack carries Slippi's public game files unmodified (GPL), and the rank icons come from slippi-launcher (GPL-3.0). Please support Slippi: slippi.gg.
- **The Melee modding community.** The memory card exploit that starts Multishine was found by wParam in 2008 and developed through the 20XX Training Hack Pack, 20XX Tournament Edition, UCF and UnclePunch's Training Mode. Thank you to everyone who kept that work going.
- **melee-gci-compiler.** The loader save is built with practicaltas's melee-gci-compiler and its UCF code sets (MPL-2.0; see `SOURCE_OFFER.md`).
- **Rooms.** The Game Night screens are adapted from the Rooms work by attunegames, used with permission.
- **doldecomp.** The symbol table of the Melee decompilation project is used to check the code modules.
- **libogc and devkitPro.** The code is built with devkitPPC. Our network adapter code follows the behaviour of libogc's own driver. Thank you to the devkitPro and libogc contributors. Their licence notices are in `NOTICE` and `LICENSES/`.
- **ENet.** The netcode speaks a wire protocol compatible with Lee Salzman's ENet (MIT); no ENet code is included.
- **Aileron.** Text drawn by the build tools is set in Aileron by Sora Sagano, released as CC0.

Multishine is not affiliated with Nintendo or Slippi. No game data is included in this repository: you bring your own copy of Melee.
