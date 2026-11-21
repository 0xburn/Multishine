#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# After a release (tools/release.sh SEQ --prod --update-dir DIR): make the card file the Nintendont shell downloads fresh, so a virtual card made from it starts on the SAME engine and overlay.
#   make_fresh_card_ota.sh SEQ UPDATE_DIR [--publish]
# Takes the newest 5-file card (dist/Multishine_Melee_card_*_5files/MemoryCardA.USA.raw), swaps in DIR/SEQ.mscf and DIR/ovl-SEQ.mscf (tools/card/card_refresh.py: signed bytes untouched, loader and the
# Melee save byte-identical), writes dist/Multishine_Melee_card_SEQ_5files/, signs crd-SEQ.mscf with the production key (SEQ must be above the last card number) and, with --publish, sends it to the server.
set -euo pipefail
seq=${1:?usage: make_fresh_card_ota.sh SEQ UPDATE_DIR [--publish]}; dir=${2:?update dir}; pub=${3:-}
here=$(cd "$(dirname "$0")" && pwd); repo=$(cd "$here/../.." && pwd); cd "$repo"
[[ -f "$dir/$seq.mscf" && -f "$dir/ovl-$seq.mscf" ]] || { echo "no $dir/$seq.mscf and $dir/ovl-$seq.mscf" >&2; exit 2; }
base=${BASE_CARD:-$(ls -d dist/Multishine_Melee_card_*_5files 2>/dev/null | grep -v "_${seq}_5files" | sort -t_ -k4 -n | tail -1)/MemoryCardA.USA.raw}      # BASE_CARD: an explicit 5-file base (the generic release day: tools/card/make_generic_base.py)
[[ -f "$base" ]] || { echo "no 5-file base card in dist/" >&2; exit 2; }
pubhex=$(grep '^prod-a ' tools/release/prod_pubkeys.txt | awk '{print $2}')
out=dist/Multishine_Melee_card_${seq}_5files; mkdir -p "$out" "dist/.crd_$seq"; rm -f "$out/MemoryCardA.USA.raw"
python3.14 tools/card/card_refresh.py --base "$base" --out "$out/MemoryCardA.USA.raw" --pub "$pubhex" --code "$dir/$seq.mscf" --ovl "$dir/ovl-$seq.mscf"
python3.14 tools/release/make_card_ota.py --card "$out/MemoryCardA.USA.raw" --seq "$seq" --key "$HOME/.multishine/prod/prod-a.key" --out-dir "dist/.crd_$seq"
if [[ $pub == --publish ]]; then echo "publishing is not part of this repository: dist/.crd_$seq/crd-$seq.mscf is built and signed" >&2; exit 2; else echo "built dist/.crd_$seq/crd-$seq.mscf (not published; add --publish)"; fi
