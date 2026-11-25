#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Host tests of the game recorder (rec_core).  Arguments: .slp files to replay through it as well.
set -euo pipefail
cd "$(dirname "$0")"
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cc -std=c99 -O1 -g -Wall -Wextra -Wno-misleading-indentation -fsanitize=address,undefined -o "$t/rec_test" rec_test.c msr_dec.c ../rec_core.c
"$t/rec_test"
if (($#)); then "$t/rec_test" "$@"; fi
