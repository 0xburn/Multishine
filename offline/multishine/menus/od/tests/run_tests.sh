#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Host tests of the online-direct image's console code, no emulator, no Docker:
#   od_test      od_backend.c + od_glue.c + mnc/mw2 + om_card.c + codehist.c behind the real exi_shim and a fake memory card (plain and ASan/UBSan)
#   od_dns_test  the hub resolves a configured server NAME with the DHCP-provided resolver (od_glue.c + sddns.c, scripted network, virtual clock; plain and ASan/UBSan)
#      od_glue.c in a host driver against the REAL the Multishine server (two consoles pair; refusals; cancel; gates), strict C89 and PowerPC builds
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); repo=$(cd "$here/../../../../.." && pwd)
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
od="$here/.."; om="$od/../om"
# od_backend.c includes ../../exi_shim/sdshim.h (its place in the staged tree offline/s17/net): rebuild that layout
n=$tmp/offline/s17/net
mkdir -p "$n" "$tmp/offline/exi_shim"
cp "$od"/od_backend.c "$od"/od_glue.c "$od"/od_glue.h "$om"/om_card.c "$om"/om_card.h "$om"/codehist.c "$om"/codehist.h "$n/"
cp "$om"/tests/omc_fake.inc "$om"/tests/omc_host.h "$here/od_test.c" "$n/"
cp "$repo"/offline/exi_shim/sdshim.[ch] "$tmp/offline/exi_shim/"
cn="$repo/offline/client_net"
for san in "" "-fsanitize=address,undefined"; do
  cc -std=gnu99 -DOD_DATA= -DOM_DATA= -Wall -Wextra -g $san -I"$n" -I"$cn" -I"$repo/offline/net" -DSDDNS_NO_SDNET -o "$tmp/od_test" "$n/od_test.c" "$n/od_backend.c" "$n/od_glue.c" "$n/codehist.c" "$tmp/offline/exi_shim/sdshim.c" \
     "$cn/mnc.c" "$cn/mw2.c" "$cn/mw2_crypto.c" "$repo/offline/payload_pack/ms_sha256.c" "$repo/offline/net/sddns.c"
  "$tmp/od_test"
done
# the server NAME: od_glue.c + sddns.c behind a scripted DNS network and a virtual clock (plain and ASan/UBSan)
for san in "" "-fsanitize=address,undefined"; do
  cc -std=gnu99 -Wall -Wextra -Werror -g $san -DSDDNS_NO_SDNET -I"$cn" -I"$repo/offline/net" -o "$tmp/od_dns_test" "$here/od_dns_test.c" "$od/od_glue.c" "$repo/offline/net/sddns.c" \
     "$cn/mnc.c" "$cn/mw2.c" "$cn/mw2_crypto.c" "$repo/offline/payload_pack/ms_sha256.c"
  "$tmp/od_dns_test"
done
# the frozen Final Destination policy (ms_match_rules.h)
cc -std=c99 -Wall -Wextra -Werror -o "$tmp/mfd_test" "$here/mfd_test.c"
"$tmp/mfd_test"
if [[ -f "$here/test_od.py" ]]; then        # these three drive the real server: not in a tree without it
  python3 "$here/test_od.py"
  python3 "$here/test_od_reenrol.py"
  python3 "$here/test_od_guest.py"
  python3 "$here/test_od_link.py"             # Wired opponents only: needs the server with (WF_SERVER_DIR), skipped without it
else echo "test_od*.py: skipped (they need the server, which is not in this tree)"; fi
