<!-- Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later -->
# Multishine

Source of the Multishine card package: the engine, overlays, data pack, card download and save files that Multishine
puts on the memory card, and the tools that build and sign them.

Players: get Multishine at https://multishine.link/get

## Build

Prerequisites: devkitPro devkitPPC (`/opt/devkitpro`), `python3.14`, `git`, a host C compiler, and your own Melee NTSC v1.02 image.

```sh
./prepare.sh                              # pinned submodules and the codeset toolchain
RETAIL_ISO=/path/to/melee.iso ./build.sh  # build/release: engine, overlays, data pack, dev-signed
./test.sh                                 # host tests and lint
```

The card image `./build.sh` writes (`build/release/card/card.raw`) carries our own banner and icon (`tools/release/save_art.py`); the build stops if its loader save shares any bytes with your disc.

## Game recordings (local fork)

A Slippi Unranked or Direct game played on a console with a Lucky Stats key on the card (`tools/lucky/lucky_provision.py`) is recorded by Slippi's own recording code (codeset flag `ms-record`) and uploaded to the account's game data library after the game, from the menus or the idle online character screen. How it works and its limits: `offline/multishine/rec/rec_core.h` (format), `rec_gc.c` (memory, rollback, the drop rules), `menus/rep/rec_up.h` (upload). Host tests: `offline/multishine/rec/tests/run_tests.sh [game.slp ...]`.

## Licence

GPL-3.0-or-later (`LICENSE`); files marked `GPL-3.0-or-later OR GPL-2.0-only` may also be used under GPL-2.0-only.
Third-party notices: `NOTICE` and `SOURCE_OFFER.md`. Credits: `CREDITS.md`.
