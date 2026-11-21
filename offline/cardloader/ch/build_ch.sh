#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Nigzel / Bjerreman
# Builds the Multishine Gecko list runner: $OUT/ms_codehandler.bin = the image the exploit copies to 0x801910E0, padded to 0xBA8 bytes (the datacopy record's size).
# usage: build_ch.sh [LIST_ADDRESS]   (default 0x817F4000)
set -euo pipefail
d=$(cd "$(dirname "$0")" && pwd); root=$(cd "$d/../../.." && pwd)
P="${PPC_PREFIX:-/opt/devkitpro/devkitPPC/bin/powerpc-eabi}"
. "$root/offline/lr_safety.sh"
out="${OUT:-$root/build/offline/cardpkg/ch}"; mkdir -p "$out"
list="${1:-0x817F4000}"
"$P-as" -mgekko --defsym LIST="$list" -o "$out/ms_codehandler.o" "$d/ms_codehandler.S"
lr_verify "$out/ms_codehandler.o"
"$P-objcopy" -O binary -j .text "$out/ms_codehandler.o" "$out/raw.bin"
python3 - "$out/raw.bin" "$out/ms_codehandler.bin" <<'PY'
import sys
b = open(sys.argv[1], "rb").read()
assert len(b) <= 0xBA8, len(b)
open(sys.argv[2], "wb").write(b.ljust(0xBA8, b"\0"))
print("ms_codehandler: %d bytes of code, padded to 0xBA8" % len(b))
PY
