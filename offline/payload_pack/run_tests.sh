#!/bin/bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Host build of the freestanding payload decoder/verifier sketch: strict flags, ASan+UBSan, and the PPC object size if devkitPPC exists.
# Usage: offline/payload_pack/run_tests.sh BUILD_DIR   (prints "BIN path" for the test driver and "PPC ..." lines)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-${TMPDIR:-/tmp}/payload_pack_build}"
mkdir -p "$OUT"
CC="${CC:-cc}"
SRCS=("$HERE/ms_lz.c" "$HERE/ms_sha256.c" "$HERE/ms_pack.c")
for f in "${SRCS[@]}"; do
  $CC -std=c89 -pedantic -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion -Os -c "$f" -o "$OUT/$(basename "$f" .c).o" 2>&1 || exit 1
done
$CC -std=c99 -Wall -Wextra -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I"$HERE" "${SRCS[@]}" "$HERE/test_payload_pack.c" -o "$OUT/test_pp"
echo "BIN $OUT/test_pp"
size "$OUT"/ms_lz.o "$OUT"/ms_sha256.o "$OUT"/ms_pack.o 2>/dev/null | sed 's/^/HOSTSIZE /' || true
PPCGCC="${DEVKITPPC:-/opt/devkitpro/devkitPPC}/bin/powerpc-eabi-gcc"
if [ -x "$PPCGCC" ]; then
  for f in ms_lz ms_sha256 ms_pack; do
    "$PPCGCC" -std=c89 -Os -mcpu=750 -msoft-float -ffreestanding -fno-builtin -c "$HERE/$f.c" -o "$OUT/$f.ppc.o"
    echo "PPC $f $(${PPCGCC%gcc}size "$OUT/$f.ppc.o" | awk 'NR==2{print $1}')"
  done
else
  echo "PPC skipped (devkitPPC not found)"
fi
