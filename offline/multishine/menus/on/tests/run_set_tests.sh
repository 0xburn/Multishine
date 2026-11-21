#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Host tests of the settings record layer (on_set.c, ml_set.c): the pure value/record rules (ml/tests/mset_test.c) and the card layer against the fake
# memory card, 12 scenarios (plain and ASan/UBSan).  No emulator, no Docker.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); repo=$(cd "$here/../../../../.." && pwd)
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
on="$here/.."; od="$on/../od"; om="$on/../om"; ml="$on/../ml"; opt="$repo/offline/options"
n=$tmp/offline/s17/net; mkdir -p "$n"
cp "$on"/on_card.c "$on"/on_card.h "$on"/on_hist.c "$on"/on_set.c "$on"/on_cfgrec.c "$on"/on_cfgrec.h "$od"/od_glue.h "$om"/om_card.h "$om"/codehist.c "$om"/codehist.h \
   "$ml"/ml_set.c "$ml"/ml_set.h "$ml"/ml_rec_tab.h "$opt"/optstore.c "$opt"/optstore.h "$opt"/optengine.h "$on"/tests/set_test.c "$on"/tests/netsel_test.c "$om"/tests/omc_fake.inc "$om"/tests/omc_host.h "$n/"
for san in "" "-fsanitize=address,undefined"; do
  cc -std=gnu99 -DOD_DATA= -DOM_DATA= -DONS_SETTINGS -DCH_AT -Wall -Wextra -Werror -g $san -I"$n" -I"$repo/offline/client_net" -o "$tmp/set_test" "$n/set_test.c" "$n/ml_set.c" "$n/optstore.c" "$n/codehist.c" "$n/on_cfgrec.c"
  for sc in 0 1 2 3 4 5 6 7 8 9 10 11 13 14 15 16 17; do "$tmp/set_test" $sc; done
  cc -std=gnu99 -Wall -Wextra -Werror -g $san -I"$ml" -I"$opt" -o "$tmp/mset_test" "$ml/tests/mset_test.c" "$ml/ml_set.c" "$opt/optstore.c"
  "$tmp/mset_test"
  cc -std=gnu99 -Wall -Wextra -Werror -g $san -I"$ml" -I"$opt" -o "$tmp/mrec_test" "$ml/tests/mrec_test.c" "$ml/ml_set.c" "$opt/optstore.c"
  "$tmp/mrec_test"
done
# the shared CARD work area (ENGINE_MEMORY_BREAKDOWN steps 2a/2b) in its three placements: static array, loader area at a fixed address, HSD heap per operation (scenario 12 = refusals)
for mode in "-DMS_CARD_SHARED_WA" "-DMS_CARD_SHARED_WA -DMS_CARD_EXT_WA -DMS_CARD_WA_ADDR=0x817E4000u" "-DMS_CARD_SHARED_WA -DMS_CARD_EXT_WA"; do
  for san in "" "-fsanitize=address,undefined"; do
    cc -std=gnu99 -DOD_DATA= -DOM_DATA= -DONS_SETTINGS -DCH_AT $mode -Wall -Wextra -Werror -g $san -I"$n" -I"$repo/offline/client_net" -o "$tmp/set_test_wa" "$n/set_test.c" "$n/ml_set.c" "$n/optstore.c" "$n/codehist.c" "$n/on_cfgrec.c"
    for sc in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17; do "$tmp/set_test_wa" $sc; done
  done
  echo "work-area placement [$mode]: set scenarios 0-12 done"
done
# the lab/dev Settings > Online toggles (ONS_NETSEL = -DSBR_NETCFG on the card route with the settings layer): the two consumer rules, six scenarios
for san in "" "-fsanitize=address,undefined"; do
  cc -std=gnu99 -DOD_DATA= -DOM_DATA= -DONS_SETTINGS -DCH_AT -DSBR_NETCFG=1 -DSD_CARD_HOLE -Wall -Wextra -Werror -g $san -I"$n" -I"$repo/offline/client_net" -o "$tmp/netsel_test" "$n/netsel_test.c" "$n/ml_set.c" "$n/optstore.c" "$n/codehist.c" "$n/on_cfgrec.c"
  for sc in 0 1 2 3 4 5 6 7 8 9; do "$tmp/netsel_test" $sc; done
done
# a release build (no -DSBR_NETCFG) has no netsel symbol and no code for it, in the card layer, the settings layer, the record reader and the value rules
cc -std=gnu99 -DOD_DATA= -DOM_DATA= -DONS_SETTINGS -DSD_CARD_HOLE -DOMC_HOST -Os -Wall -Wextra -g -I"$n" -I"$repo/offline/client_net" -I"$om/tests" -c "$n/on_card.c" -o "$tmp/rel_card.o"
cc -std=c89 -Os -I"$n" -I"$repo/offline/client_net" -c "$n/on_cfgrec.c" -o "$tmp/rel_cfgrec.o"
cc -std=gnu99 -Os -I"$n" -I"$repo/offline/client_net" -c "$n/ml_set.c" -o "$tmp/rel_mlset.o"
if nm "$tmp/rel_card.o" "$tmp/rel_cfgrec.o" "$tmp/rel_mlset.o" | grep -Eq 'netsel|ons_change_(srv|mmr|mmk)|onr_decode_(host_)?lab|ms_set_(srv|mmr|mmk)|sbr_nc_|ml_cfg_reapply|onc_netsel'; then echo "release objects carry netsel symbols"; exit 1; fi
echo "release build: no netsel symbols in on_card, on_cfgrec, ml_set"
# the images that do not define ONS_SETTINGS must compile the card layer exactly as before (no reference to the settings code)
cc -std=gnu99 -DOD_DATA= -DOM_DATA= -DOMC_HOST -Wall -Wextra -g -I"$n" -I"$repo/offline/client_net" -c "$n/on_card.c" -o "$tmp/plain.o" -I"$om/tests"
if nm "$tmp/plain.o" | grep -q "ons_"; then echo "ons_ symbols in a build without ONS_SETTINGS"; exit 1; fi
echo "settings record layer host tests passed."
