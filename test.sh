#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Host tests that need neither a console, a disc nor a server, then the shared-code lock and lint.
set -uo pipefail
cd "$(dirname "$0")"
fails=()
step() { echo "== $*"; "$@" || fails+=("$*"); }
step bash offline/payload_pack/run_tests.sh
step bash offline/multishine/rec/tests/run_tests.sh
step python3.14 tools/release/test_check_release_shape.py
step python3.14 -m unittest -q tools/tests/test_payload_pack.py tools/tests/test_standard_save.py tools/tests/test_save_art.py
step bash offline/multishine/menus/od/tests/run_tests.sh
step bash offline/multishine/menus/sign/run_tests.sh
step bash offline/multishine/menus/on/tests/run_set_tests.sh
step python3.14 tools/lint_repo.py --licence "GPL-3.0-or-later,GPL-3.0-or-later OR GPL-2.0-only,GPL-3.0-only,MIT,CC0-1.0"
echo
if ((${#fails[@]})); then printf 'FAILED: %s\n' "${fails[@]}"; exit 1; fi
echo "all test steps passed"
