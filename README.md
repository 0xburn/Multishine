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

## Licence

GPL-3.0-or-later (`LICENSE`); files marked `GPL-3.0-or-later OR GPL-2.0-only` may also be used under GPL-2.0-only.
Third-party notices: `NOTICE` and `SOURCE_OFFER.md`. Credits: `CREDITS.md`.
