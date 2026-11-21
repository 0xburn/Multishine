#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Fetch the pinned upstream sources: Project Slippi's GPL-3.0 codeset sources (slippi-ssbm-asm, 3.7.0 line) and
# melee-gci-compiler (MPL-2.0 exploit files of the loader save).  Then provision the codeset toolchain (Go + JLaferri/gecko,
# sha256-pinned; offline/codeset/tools.sh; set OFFLINE_FETCH=0 and GO_TARBALL / GECKO_SEED_DIR to stay offline).
set -euo pipefail
cd "$(dirname "$0")"
git submodule update --init third_party/slippi-ssbm-asm third_party/melee-gci-compiler
git submodule update --init vendor/melee     # doldecomp/melee: only its symbol table (config/GALE01/symbols.txt) is read, by the codeset audit
chmod +x third_party/melee-gci-compiler/mgc/pyiiasmh/bin/*/powerpc-eabi-* 2>/dev/null || true   # upstream ships its assembler binaries without the executable bit
bash offline/codeset/tools.sh > /dev/null
echo "submodules: $(git submodule status | awk '{print substr($1,1,9), $2}' | tr '\n' ' ')"
