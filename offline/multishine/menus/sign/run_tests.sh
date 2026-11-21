#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Host tests of the sign-in screens (offline/multishine/menus/sign): strict C89 build of the module, ASan/UBSan run of tests/sg_test.c, the mock flow
# dump and the PNG renderer; PowerPC size report when devkitPPC exists.   usage: run_tests.sh [OUTDIR for the PNGs]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); off=$(cd "$here/../../.." && pwd)
cc_bin="${CC:-cc}"; out="${TMPDIR:-/private/tmp}/sg-host-tests"; mkdir -p "$out"
SRC=("$here/sg_ui.c" "$here/sg_link.c" "$here/sg_delid.c" "$here/sg_mockup.c" "$here/sg_mocklist.c" "$here/gnb.c" "$here/frd.c" "$here/stb.c" "$here/gnt.c" "$off/qr/qr.c" "$off/net/tls/sdent.c" "$off/net/tls/sdcrypto.c" "$here/sgx.c" "$here/sgr.c" "$off/net/tls/sdc25519.c")
for f in "${SRC[@]}"; do "$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -DSG_LAB=1 -c "$f" -o "$out/$(basename "$f").o"; done
SRC+=("$off/client_net/mw2_crypto.c" "$off/payload_pack/ms_sha256.c")      # linked, not strict-C89 checked here (sdc25519 verify uses the SHA-512 of the wire crypto)
# the resident loader and hook (pointer <-> address mapped for a 64-bit host)
for f in "$here/sg_ovl.c" "$here/sg_hook.c"; do "$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -DSDNET_ENABLE_TCP=1 -include "$here/tests/sgo_host_map.h" -I"$off/net" -c "$f" -o "$out/$(basename "$f").o"; done
# the kept / prefetched overlay block: prefetch, park, reuse without a read, a changed image read again, no leaks
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSDNET_ENABLE_TCP=1 -include "$here/tests/sgo_host_map.h" -I"$off/net" -I"$here/.." \
  "$here/tests/sgh_park_test.c" "$here/sg_ovl.c" "$here/sg_hook.c" -o "$out/sgh_park_test"
"$out/sgh_park_test"
# the retail Tourney bracket snapshot model (gnt_tmb.c): strict C89, ASan/UBSan run
"$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -c "$here/gnt_tmb.c" -o "$out/gnt_tmb.o"
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "$here/tests/gnt_tmb_test.c" "$here/gnt_tmb.c" -o "$out/gnt_tmb_test"
"$out/gnt_tmb_test"
# the Event-view row reveal (evr.c): strict C89, the schedule with and without the stagger
"$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -c "$here/evr.c" -o "$out/evr.o"
for st in 1 0; do
  "$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DEV_STAGGER=$st "$here/tests/evr_test.c" "$here/evr.c" -o "$out/evr_test$st"
  "$out/evr_test$st"
done
for lab in 0 1; do      # release (no TCP and TLS row, no text naming a lab server) and lab
  "$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=$lab -I"$here/.." "$here/tests/sg_test.c" "${SRC[@]}" -o "$out/sg_test$lab"
  "$out/sg_test$lab"
done
# the card layout notice on the Update screen: present only when the overlay is built for the stock work area (0x817E4000), absent for generic (0x817D4000), low and no macro
for wa in 0x817E4000u 0x817D4000u 0x817B4000u ""; do
  "$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 ${wa:+-DMS_CARD_WA_ADDR=$wa} -DEXPECT_NOTICE=$([[ "$wa" == 0x817E4000u ]] && echo 1 || echo 0) -I"$here/.." "$here/tests/sg_notice_test.c" "${SRC[@]}" -o "$out/sg_notice_test"
  "$out/sg_notice_test"
done
# the Controller test screen (scripted pad structs: rest, OFF CENTRE, NOT CHANGING, min/max, Z freeze, B hold)
# release shape (SG_MOCKUPS 0): the Game Night browser still opens, takes input and draws
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -DSG_MOCKUPS=0 -I"$here" -I"$here/.." "$here/tests/sg_rel_test.c" "${SRC[@]}" "$off/client_net/mnc.c" "$off/client_net/mw2.c" -o "$out/sg_rel_test"
"$out/sg_rel_test"
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -I"$here/.." "$here/tests/sg_ctl_test.c" "${SRC[@]}" -o "$out/sg_ctl_test"
"$out/sg_ctl_test"
# Link This Console (sg_link.c): entropy pass -> code -> QR -> polls -> card -> Linked, every error, release and lab
for lab in 0 1; do
  "$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=$lab -I"$here/.." "$here/tests/sg_link_test.c" "${SRC[@]}" -o "$out/sg_link_test$lab"
  "$out/sg_link_test$lab"
done
# Device Identity and Delete This Console's Identity (sg_delid.c)
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -I"$here/.." "$here/tests/sg_delid_test.c" "${SRC[@]}" -o "$out/sg_delid_test"
"$out/sg_delid_test"
# the Mockups hub: every hub entry opened and left with scripted pad input, every mock state, tag / glyph / width checks
for lab in 0 1; do
  "$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=$lab -I"$here/.." "$here/tests/sg_mk_test.c" "${SRC[@]}" -o "$out/sg_mk_test$lab"
  "$out/sg_mk_test$lab" "$out/mk_flow$lab.txt"
done
# the Game Night browser model in LIVE mode: requests, answers, failures, keys, texts
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -I"$here/.." "$here/tests/gnb_test.c" "${SRC[@]}" "$off/client_net/mnc.c" "$off/client_net/mw2.c" -o "$out/gnb_test"
"$out/gnb_test"
# the stage prep screen model (gnb_prep.c, E1): a Sweaty / Grandmaster set from the first strike to the decided stage, the wait between games, the cursor and the busy guard, the clock, the texts, the lobby hooks
"$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -DSG_LAB=1 -c "$here/gnb_prep.c" -o "$out/gnb_prep.c.o"
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -I"$here/.." "$here/tests/gnb_prep_test.c" "${SRC[@]}" "$off/client_net/mnc.c" "$off/client_net/mw2.c" -o "$out/gnb_prep_test"
# a Grandmaster set on the browser (gnb_rk.c, 14.2, piece G2): the set views of a best of 3 -> go / wait / replay / result, the grid on the character step, hold B (strict C89 is gnb.c's check above: gnb_rk.c is part of it)
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -I"$here/.." "$here/tests/gnb_rk_test.c" "${SRC[@]}" "$off/client_net/mnc.c" "$off/client_net/mw2.c" -o "$out/gnb_rk_test"
"$out/gnb_rk_test"
"$out/gnb_prep_test"
# the SisLib pool dry run of the Event-view text guard (gnb_pool.h, GUI audit GN-01 / GN-02): retail allocator model, random churn, never a panic
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I"$here/.." "$here/tests/gnb_pool_test.c" -o "$out/gnb_pool_test"
"$out/gnb_pool_test"
# the Grandmaster standings model on a local mock ladder: paging, your row, states, glyphs, the text view
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -I"$here/.." "$here/tests/stb_test.c" "${SRC[@]}" -o "$out/stb_test"
"$out/stb_test" "$out/stb_flow.txt"
# the Tournament model (gnt.c): list, Create form, page, bracket arithmetic and view, the call, the text view
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -I"$here/.." "$here/tests/gnt_test.c" "$here/gnt.c" -o "$out/gnt_test"
# the bracket tree window (gnt_tree.c) on the demo cups (gnt_demo.c)
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -I"$here/.." "$here/tests/gnt_tree_test.c" "$here/gnt.c" "$here/gnt_tree.c" "$here/gnt_demo.c" -o "$out/gnt_tree_test"
"$out/gnt_tree_test" "$out/tree_figs.txt"
"$out/gnt_test"
# the Tournament reply parsers of the hub client (gnc_parse_tn) on hand-built wire replies and their mutations
"$cc_bin" -std=gnu99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Wno-unused-function -I"$off/client_net" "$off/client_net/tests/tn_parse_test.c" "$off/client_net/gnc.c" "$off/client_net/mnc.c" "$off/client_net/mw2.c" "$off/client_net/mw2_crypto.c" "$off/payload_pack/ms_sha256.c" -o "$out/tn_parse_test"
"$out/tn_parse_test"
# the menu toasts and the Friends screen
bash "$here/tests/run_social_tests.sh"
# the Update screen's state machine: strict C89 (pure, no libc), then the release objects must not name TCP, TLS or a lab server
"$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -I"$off/client_net" -c "$here/sgu_core.c" -o "$out/sgu_core.o"
"$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -I"$off/client_net" -c "$here/sgu_net.c" -o "$out/sgu_net.o"      # the network patch applier (tests/test_ngp.py runs it)
"$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -DSG_LAB=0 -c "$here/sg_ui.c" -o "$out/sg_ui_release.o"
if strings "$out/sg_ui_release.o" | grep -i -E "lab server|TLS|TCP"; then echo "release object names TCP, TLS or a lab server"; exit 1; fi
# the lab/dev Online rows Server / Slippi / In Use: strict C89 build, the row model test, and the release object (SG_NETSEL=0) must carry none of their text
"$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -DSG_LAB=0 -DSG_NETSEL=1 -c "$here/sg_ui.c" -o "$out/sg_ui_netsel.o"
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -DSG_NETSEL=1 -I"$here/.." "$here/tests/sg_netsel_test.c" "${SRC[@]}" -o "$out/sg_netsel_test"
"$out/sg_netsel_test"
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -DSG_LAB=0 -DSG_NETSEL=1 -DSG_SRV_COUNT=2 -I"$here/.." "$here/tests/sg_netsel_test.c" "${SRC[@]}" -o "$out/sg_netsel_test2"
"$out/sg_netsel_test2"
if strings "$out/sg_ui_release.o" | grep -E "Our Mock|Real Slippi|baked server|In Use|slippi.gg|real servers"; then echo "release object carries the netsel rows"; exit 1; fi
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -DSG_LAB=1 -I"$here/.." "$here/tests/sg_mock.c" "${SRC[@]}" -o "$out/sg_mock"
"$out/sg_mock" "$out/flow.txt"
[ "$(grep -c '^SCREEN ' "$out/flow.txt")" = 16 ]
python3 "$here/tests/sg_png.py" "$out/flow.txt" "${1:-$out/png}" >/dev/null
P=${PPC_PREFIX:-/opt/devkitpro/devkitPPC/bin/powerpc-eabi}
if [ -x "$P-gcc" ]; then
  F=(-Os -mcpu=750 -ffreestanding -fno-builtin -msdata=none -G0 -fno-pic -msoft-float -ffixed-lr -fno-jump-tables -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -Wall -Wextra -Werror)
  for f in "${SRC[@]}"; do "$P-gcc" "${F[@]}" -std=gnu99 -c "$f" -o "$out/ppc_$(basename "$f").o"; done
  for f in "$here/sg_ovl.c" "$here/sg_hook.c"; do "$P-gcc" "${F[@]}" -std=gnu99 -DSDNET_ENABLE_TCP=1 -I"$off/net" -c "$f" -o "$out/ppc_$(basename "$f").o"; done
  "$P-gcc" "${F[@]}" -std=gnu99 -I"$off/client_net" -c "$here/sgu_core.c" -o "$out/ppc_sgu_core.c.o"
  "$P-gcc" "${F[@]}" -std=gnu99 -I"$off/client_net" -c "$here/sgu_net.c" -o "$out/ppc_sgu_net.c.o"
  "$P-gcc" "${F[@]}" -std=gnu99 -c "$here/sgc.c" -o "$out/ppc_sgc.c.o"
  "$P-size" "$out"/ppc_*.o
  "$P-nm" -u "$out"/ppc_sg_ui.c.o | grep -v -E ' U (sdent_|sdc_|qr_|sgm_|sgl_|sgd_|sgr_|sgx_|sg_ident_|frd_|stb_|gnt_|_restgpr|_savegpr)' && { echo "unexpected undefined symbols"; exit 1; } || true
fi
# The rounded plate of the toasts (toast_pill.h): Q16 <-> float bits, the fan geometry, the show-for-a-while rule
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I"$here/.." "$here/tests/toast_pill_test.c" -lm -o "$out/toast_pill_test"
"$out/toast_pill_test"
# The key badges of the footers, hints and toasts (keyhint.h, owner GUI pass): the parser over the real strings, the split at double spaces, the pair layout
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I"$here/.." "$here/tests/keyhint_test.c" -o "$out/keyhint_test"
"$out/keyhint_test"
# Website commands (sgc.c): strict C89 (pure), then the state machine against a scripted mailbox
"$cc_bin" -std=c89 -pedantic -Wall -Wextra -Werror -c "$here/sgc.c" -o "$out/sgc.o"
"$cc_bin" -std=c99 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "$here/tests/sgc_test.c" "$here/sgc.c" -o "$out/sgc_test"
"$out/sgc_test"
# Send Slippi Login (sgx.c, sgr.c): RFC vectors, the page's sealer against the console flow, then the real wire client against the real server.  Needs python3.14 with `cryptography`.
py="${SG_PYTHON:-python3.14}"
if [[ ! -f "$here/tests/test_sgr.py" ]]; then echo "Send Slippi Login server tests skipped (they need the server, which is not in this tree)"
elif "$py" -c 'import cryptography' 2>/dev/null; then
  "$py" "$here/tests/test_sgr.py"
  "$py" "$here/tests/test_sgr_wire.py"
  "$py" "$here/tests/test_sgc_wire.py"          # website commands: the real sgc.c + wire client against the real server
else echo "no Python with cryptography: Send Slippi Login tests skipped"; fi
echo "sign-in screens host tests passed (overlay loader: run_ovl_test.sh; Update row end to end: tests/test_sgu_e2e.py)."
"${PYTHON:-python3.14}" "$here/tests/test_qc_defaults.py"
"${PYTHON:-python3.14}" "$here/tests/test_frd_handoff_gate.py"      # the Friends hand-offs ask the glue before blanking the screen
if [[ -f "$here/tests/test_gnb_types.py" ]]; then "${PYTHON:-python3.14}" "$here/tests/test_gnb_types.py"; fi          # the room type texts of the console against (server side)
