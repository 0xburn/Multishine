#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Host tests of the menu toasts and the Friends screen models: strict C89 of toast.c and frd.c, ASan/UBSan runs of
# tests/toast_test.c and tests/frd_test.c.  Called by run_tests.sh; runnable alone.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd); off=$(cd "$here/../../.." && pwd)
cc_bin="${CC:-cc}"; out="${TMPDIR:-/private/tmp}/sg-social-tests"; mkdir -p "$out"
"$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -c "$here/toast.c" -o "$out/toast.o"
"$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -I"$here/.." -c "$here/frd.c" -o "$out/frd.o"
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "$here/tests/toast_test.c" "$here/toast.c" -o "$out/toast_test"
"$out/toast_test"
SRC=("$here/frd.c" "$here/stb.c" "$here/gnt.c" "$here/sg_ui.c" "$here/sg_link.c" "$here/sg_delid.c" "$here/sg_mockup.c" "$here/sg_mocklist.c" "$here/gnb.c" "$off/qr/qr.c" "$off/net/tls/sdent.c" "$off/net/tls/sdcrypto.c" "$here/sgx.c" "$here/sgr.c" "$off/net/tls/sdc25519.c" "$off/client_net/mw2_crypto.c" "$off/payload_pack/ms_sha256.c")
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -I"$here/.." "$here/tests/frd_test.c" "${SRC[@]}" -o "$out/frd_test"
"$out/frd_test"
# the D-Pad quick-chat picker (qc_pick.h): its rules, and the same decisions as the chat lab picker (offline/chat/chat_core.c cc_ui_frame) on random pad sequences
"$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -fsyntax-only -x c "$here/qc_pick.h"
"${PYTHON:-python3.14}" "$off/chat/gen_chat_tables.py" header --out "$out/gen/chat_presets_gen.h"
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I"$out/gen" "$here/tests/qc_test.c" "$off/chat/chat_core.c" -o "$out/qc_test"
"$out/qc_test"
# the Friends net glue against a silent server / "session unknown", B cancel and the 12 s limit
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -I"$here/.." -I"$here" -I"$off/client_net" "$here/tests/frdn_test.c" "${SRC[@]}" "$here/frd_net.c" "$off/client_net/socc.c" "$off/client_net/mnc.c" "$off/client_net/mw2.c" -o "$out/frdn_test"
"$out/frdn_test"
