#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Build a DEV-signed release set of the Multishine card package: the Slippi codeset, then tools/release.sh with the lab target
# (the engine takes its server from the card record; no server is patched), the generic card layout by default:
#   OUT/update/SEQ.mscf (engine), ovl-SEQ.mscf (overlays), pack-SEQ.mscf (system-files data, only with SLIPPI_GAMEFILES),
#   and a card image + crd-SEQ.mscf signed with the published RFC 8032 test key.
#   ./build.sh [OUT]       default OUT=build/release; SEQ=${MS_SEQ:-900}; CL_LAYOUT=generic|stock
# Needs: ./prepare.sh, devkitPPC (/opt/devkitpro), python3.14, RETAIL_ISO (your own Melee NTSC 1.02 image: read for checks only, nothing of it
# is written into the outputs), the dev signing key ($HOME/.multishine/dev-signing.key, made by tools/card/build_card.sh rc1 --dev, outside the repo),
# optional SLIPPI_GAMEFILES (Slippi's public GameFiles/GALE01 folder) for pack-SEQ.mscf.  Production signing is not done here (README).
set -euo pipefail
cd "$(dirname "$0")"
root=$PWD
out=${1:-$root/build/release}; [[ $out = /* ]] || out=$root/$out
seq=${MS_SEQ:-900}
: "${RETAIL_ISO:?set RETAIL_ISO to your own Melee NTSC 1.02 image}"
[[ -f third_party/slippi-ssbm-asm/Online/Online.asm || -d third_party/slippi-ssbm-asm/Online ]] || { echo "third_party/slippi-ssbm-asm is empty: run ./prepare.sh"; exit 2; }
dol=vendor/melee/orig/GALE01/sys/main.dol       # doldecomp's input location (ignored by git): the engine links against YOUR retail main.dol
if [[ ! -f $dol ]]; then
  t=$(mktemp -d); python3.14 offline/iso/gcm.py extract "$RETAIL_ISO" "$t" --sys-only >/dev/null
  mkdir -p "$(dirname "$dol")"; cp "$t/sys/main.dol" "$dol"; rm -rf "$t"
fi
[[ $(shasum "$dol" | cut -c1-40) == $(awk '/main.dol/{print $1}' vendor/melee/config/GALE01/build.sha1) ]] || { echo "$dol is not the NTSC 1.02 main.dol (vendor/melee/config/GALE01/build.sha1)"; exit 2; }
bash offline/codeset/build_codeset.sh --profile default
bash offline/codeset/build_codeset.sh --profile net-menu-ms-ws-card || echo "note: the net-menu-ms-ws-card audit reports FAIL by design (its .bin is still written)"
export RETAIL_ISO MS_RELEASE_TARGET=lab CL_LAYOUT=${CL_LAYOUT:-generic}
export SLIPPI_GAMEFILES=${SLIPPI_GAMEFILES:-$root/no-slippi-gamefiles}
mkdir -p "$out/update"
bash tools/release.sh "$seq" --update-dir "$out/update"
built=$(ls -t build/offline/cardpkg/rc1card_*/card.raw 2>/dev/null | head -1)
card="$out/card/card.raw"                 # the built card with our banner and icon on the loader save; the build stops unless the disc scan finds 0 windows
MULTISHINE_RETAIL_ISO="$RETAIL_ISO" python3.14 tools/card/make_generic_base.py --art-card "$built" --out "$card"
echo "card image: $card"
if python3.14 tools/release/make_card_ota.py --card "$card" --seq "$seq" --dev --out-dir "$out/crd" ; then :; else echo "note: crd-$seq.mscf needs the five-file card (loader, code, overlay, state, standard Melee save): README 'Card files'"; fi
ls -l "$out"/update/*.mscf "$out"/card/ "$out"/crd/ 2>/dev/null || true
shasum -a 256 "$out"/update/*.mscf
