#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# One command for the card route: build the engine payload for the card hole, export it as the MultishineCode file,
# assemble the card (loader save + code file), stage it in the Swiss path/name, verify it, and print the upload command.
#   tools/card/build_card.sh [textonly|textonly-hp|rc1] [--compress]   (rc1: the RC1 feature set,: engine + menus + hub + Slippi bridge on a stock disc, contract multishine-card-rc1.json, no disc overlay files delivered; textonly: the engine of canonical image 1, no disc overlay files;
#               textonly-hp: LAB ONLY, the same engine plus the heap probe readout; staged under..._textonly_hp..., hp.json next to the card)
#   --compress: MSLZ-compressed code file (MSCF v2) + a loader with the in-place decoder (CL_COMP=1): 15 blocks instead of 44, fits a 59-block card
#               ; staged under cardpkg_textonly_z so the plain card is never overwritten
#   --verify release|dev   the OTA stage-0 loader: checks the Ed25519 signature (of the STORED stream for a compressed file, before any
#               decode), ABI and the build_seq floor, falls back to MultishineCodeB.  Staged under ..._rel / ..._dev.
#   --sign KEYFILE [--pubkey2 HEX] [--seq N]   release: sign the code file with this key (made by `mscf_sign.py keygen`, which refuses a path inside a git tree) and pin
#               its public key in the release loader; --verify release is implied.  Refuses the published test key.
#   --name N    build under build/offline/cardpkg/prod_N and stage under flippy_stage/ladder_prod_N (never touches the default staged cards)
#   --no-scratch30   OLD entry stub (r30 = slot number when the one-shot body runs): that card HANGS the console; only to reproduce it.
#               The default stub points r30 at a scratch word of its own frame (console-proven, bs_fix_all).
#   --claim-kb N   LAB ONLY, with textonly-hp: claim N KB (rounded up to 8 KiB) of AllM instead of the profile's 352 KB, to make a console see the memory pressure of
#               the RC1 image (805,568 B = 787 KB); the hole base moves down accordingly (the same engine, only the claim differs).  Staged under ..._claim<N>.
#   --dev       one command for smoke tests: dev loader (display name DEV BUILD, accepts a per-user dev key AND unsigned files), the code file signed with YOUR dev key
#               (${MS_DEV_KEY:-~/.multishine/dev-signing.key}, generated on first use OUTSIDE the repo, never committed) with an automatic dev build_seq
#               (1..999, ~/.multishine/dev.seq; release builds start at 1000 so a dev file can never satisfy a release floor), staged under ..._dev, plus
#               <stage>/update/<seq>.mscf for `a lab update server (not in this repository) --update-dir ... --update-pubkey <dev pub>`.  Never put a dev card on the website.
#   --cfg "k=v ..."   rc1 only: patch the bridge cfg block of the payload before export (offline/bridge/build_sbr.py patch keys, e.g. mm_name=mm.slippi.gg flags=10 hist0=BOT#1: the LAB/Dolphin mock setup)
#   --pack DIR   rc1 only: add the disc-file pack DIR/MultishinePack.gci (tools/card/build_pack.py: changed/added disc files served from the card); the card then gets a "+pack" staged name only through --name
#   --online   rc1 only: the 'online' profile (values in tools/ms_target.py): the patched hub block names the server (play.example.org /
#               192.0.2.10, udp 43150, its pinned server key) with OD_CFGF_SRVLOCK, so the card record's server and key are ignored and its identity is used only when
#               it was issued by the server (else the console is a guest there; the Update row works either way).  $MS_ONLINE_ADDR=ip|name.
#               --target lab|online: the same choice by name (lab = today's build, nothing patched); default $MS_TARGET, else lab.  Staged with an _online suffix.
#   --rb-readout   rc1 only, LAB ONLY: engine built with rbread=on: the post-game CSS readout of the last match (store, heap, capacity peaks, both heaps' free / minimum)
#   --identity FILE   put this user.json (a FAKE test file, never our) on the card as MultishineIdentity (vmc_builder)
#   --coins N | --no-coins   Trophy Lottery coins in Melee's own save: the card gets a ready MultishineMelee with N coins (0..999, as the Lottery shows them)
#               instead of the empty one Melee makes at its first boot.  DEFAULT: 999 for dev cards (--dev), OFF for everything else (a release card does not pre-fill a
#               user's save).  Needs a fresh Melee-made save as template (tools/card/melee_coins.py capture; $MS_MELEE_TEMPLATE or
#               a local work folder); a defaulted run without the template warns and builds without coins, an explicit --coins N stops.
#   --records [on|off] | --no-records   Melee single-player records (Target Test times, Home-Run distances, 10-Man times, Event Match scores; tools/card/melee_records.py) written into the same
#               MultishineMelee, from docs/data/melee_records.json (real time, NTSC, resolved rows only).  DEFAULT ON for every card incl. release ('coins and records on');
#               a defaulted run without the template or the dataset warns and builds without records, an explicit --records stops.  --records-sets target,hrc,tenman,events picks the sets
#               (default all four).
#   --standard-melee | --no-standard-melee | --replace-melee   THE standard MultishineMelee (tools/card/standard_save.py: everything unlocked, 999 coins, world records, Vs items off, OUR banner/icon + real digest,
#               deterministic): DEFAULT for every new card when the template exists, and it REPLACES the --coins/--records steps.  An explicit --coins/--no-coins/--records/--no-records/--no-standard-melee keeps the old
#               step-by-step route.  A MultishineMelee already on the card is only replaced with --replace-melee.  Same template as --coins ($MS_MELEE_TEMPLATE or a local work folder); a defaulted run without it warns and builds without.
#   --vs-test   STANDING TEST: after the build run the card on the STOCK disc in headless Dolphin with the MMU ON: Name Entry,
#               Vs Melee, two players, START x2, stage select, Pokemon Stadium, the match must start and run 900 frames with no OS exception; the build fails otherwise.  Needs Docker and about 4 minutes.
# Needs the retail ISO on this host (guard CRC; the card carries no game words) and Python >= 3.10 (Homebrew python3.14 is used).
# Nothing is copied to any SD: the last lines print the exact commands to run while the drive is in remote mode.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd); cd "$repo"
online=""; tflag=""; target=${MS_TARGET:-lab}; profile=textonly; labcfg=""; identity=""; packdir=""; labnat=""; rbread=""; vstest=0; comp=""; verify=""; signkey=""; pub2=""; seq=""; dev=0; s30=(); name=""; claimkb=""; coins=""; coinsx=0; stdmelee=""; replmelee=""; records=""; recx=0; recsets="target,hrc,tenman,events"
while [[ $# -gt 0 ]]; do case $1 in
  --compress) comp=--compress ;;
  --verify) verify=${2:?--verify release|dev}; shift ;;
  --sign) signkey=${2:?--sign KEYFILE}; shift ;;
  --pubkey2) pub2=${2:?--pubkey2 HEX}; shift ;;
  --seq) seq=${2:?--seq N}; shift ;;
  --claim-kb) claimkb=${2:?--claim-kb N}; shift ;;
  --dev) dev=1 ;;
  --coins) coins=${2:?--coins N (0..999)}; coinsx=1; shift ;;
  --no-coins) coins=off; coinsx=1 ;;
  --records) records=on; recx=1; if [[ ${2:-} == on || ${2:-} == off ]]; then records=$2; shift; fi ;;   # --records [on|off]
  --records-sets) recsets=${2:?--records-sets target,hrc,tenman,events}; shift ;;
  --no-records) records=off; recx=1 ;;
  --standard-melee) stdmelee=on ;;
  --no-standard-melee) stdmelee=off ;;
  --replace-melee) replmelee=1 ;;
  --vs-test) vstest=1 ;;
  --cfg) labcfg=${2:?--cfg "k=v ..."}; shift ;;
  --identity) identity=${2:?--identity FILE}; shift ;;
  --pack) packdir=${2:?--pack DIR}; shift ;;
  --online) target=online; tflag=1 ;;
  --target) target=${2:?--target lab|online}; tflag=1; shift ;;
  --rb-readout) rbread=1 ;;
  --no-scratch30) s30=(--no-scratch30) ;;
  --name) name=${2:?--name N}; shift ;;
  textonly) profile=textonly ;;
  textonly-hp|heapprobe) profile=textonly_hp ;;
  rc1) profile=rc1 ;;
  *) echo "unknown argument $1 (only: textonly rc1 --online --target lab|online --compress --coins N --no-coins --records [on|off] --no-records --records-sets LIST --standard-melee --no-standard-melee --replace-melee --vs-test --name N --no-scratch30 --verify release|dev --sign KEYFILE [--pubkey2 HEX] [--seq N] --dev)" >&2; exit 2 ;;
esac; shift; done
[[ $profile == textonly || $profile == textonly_hp || $profile == rc1 ]] || { echo "unknown profile $profile (only: textonly, textonly-hp, rc1)" >&2; exit 2; }
PY=${PYTHON:-/opt/homebrew/bin/python3}; [[ -x $PY ]] || PY=python3
case $target in lab) ;; online) [[ $profile == rc1 || -n $tflag ]] && online=1 ;; *) echo "--target / MS_TARGET must be lab or online, not '$target'" >&2; exit 2 ;; esac
if [[ -n $online ]]; then
  [[ $profile == rc1 ]] || { echo "--online (target online) is for the rc1 profile (the card route engine)" >&2; exit 2; }
  odargs=$("$PY" "$repo/tools/ms_target.py" odcfg-args online) || exit 2
fi
SIGN() { "$PY" "$repo/tools/card/mscf_sign.py" "$@"; }
vargs=(); suffix=""
if (( dev )); then
  [[ -z $signkey && ( -z $verify || $verify == dev ) ]] || { echo "--dev cannot be combined with --sign / --verify release" >&2; exit 2; }
  verify=dev
  keydir=${MS_DEV_HOME:-$HOME/.multishine}; mkdir -p "$keydir"; chmod 700 "$keydir"
  devkey=${MS_DEV_KEY:-$keydir/dev-signing.key}
  case $(cd "$(dirname "$devkey")" && pwd)/ in "$repo"/*) echo "the dev key must live outside the repo ($devkey)" >&2; exit 2 ;; esac
  [[ -f $devkey ]] || { SIGN keygen --out "$devkey" >/dev/null; echo "generated your dev signing key: $devkey (outside the repo; keep it private, never commit it)"; }
  if [[ -z $seq ]]; then
    seqf=$keydir/dev.seq; seq=$(( $(cat "$seqf" 2>/dev/null || echo 0) + 1 )); (( seq < 1000 )) || { echo "dev build_seq reached 999: reset $seqf" >&2; exit 2; }
    echo "$seq" > "$seqf"
  fi
  (( seq >= 1 && seq <= 999 )) || { echo "a dev build_seq must be 1..999 (release builds start at 1000)" >&2; exit 2; }
  signkey=$devkey; pub=$(SIGN pubkey --key "$devkey")
  vargs=(--verify dev --pubkey "$pub" --sign-key "$devkey" --seq "$seq"); suffix=_dev
elif [[ -n $signkey || $verify == release ]]; then
  [[ -n $signkey && -f $signkey ]] || { echo "--verify release needs --sign KEYFILE (a key made by mscf_sign.py keygen)" >&2; exit 2; }
  [[ -z $verify || $verify == release ]] || { echo "--sign with --verify dev: use --dev" >&2; exit 2; }
  pub=$(SIGN pubkey --key "$signkey")
  [[ $pub != d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a ]] || { echo "the published test key is not a release key" >&2; exit 2; }
  [[ -n $seq ]] || seq=1000
  (( seq >= 1000 )) || { echo "a release build_seq must be >= 1000 (OTA_UPDATE.md decision 3)" >&2; exit 2; }
  vargs=(--verify release --pubkey "$pub" --sign-key "$signkey" --seq "$seq" --min-seq 1000); [[ -n $pub2 ]] && vargs+=(--pubkey2 "$pub2"); suffix=_rel
elif [[ $verify == dev ]]; then
  vargs=(--verify dev); suffix=_dev                     # dev loader, unsigned code file: the quickest smoke test (accepts unsigned)
elif [[ -n $verify ]]; then echo "--verify must be release or dev" >&2; exit 2
fi
iso=${RETAIL_ISO:-"/Applications/Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso"}
[[ -f $iso ]] || { echo "retail ISO not found: $iso (set RETAIL_ISO)" >&2; exit 2; }
hole=0x8178E000; size=0x56000
contract=offline/multishine/contracts/multishine-card-textonly.json; payload=build/offline/cardpkg/payload_tc; out=build/offline/cardpkg/$profile; stage=.analysis-deps/work/flippy_stage/cardpkg_$profile
if [[ $profile == rc1 ]]; then contract=offline/multishine/contracts/multishine-card-rc1.json; payload=build/offline/cardpkg/payload_rc1c; hole=0x8176A000; size=0x7A000; fi   # RC1 image 447,552 B
if [[ -n ${RC1_HOLE_SIZE:-} ]]; then   # rc1 only, explicit and unproven on a console: a larger claim ending at the same address (e.g. 0x7C000 when the compressed engine no longer fits 0x7A000); default unchanged
  [[ $profile == rc1 && -z ${CL_LAYOUT:-} ]] || { echo "RC1_HOLE_SIZE is for the rc1 profile with the stock layout" >&2; exit 2; }
  size=$(printf '0x%X' $(( RC1_HOLE_SIZE ))); hole=$(printf '0x%X' $(( 0x817E4000 - size )))
fi
if [[ ${CL_LAYOUT:-} == generic ]]; then   # Cubeboot generic-VMC-safe layout: list/loader at 0x817E0000, work window below it, so the hole ends at 0x817D4000
  [[ $profile == rc1 ]] && hole=0x8175A000      # RC1 claim 0x7A000
  [[ $profile == textonly ]] && hole=0x8177E000 # 0x56000
fi
if [[ $profile == textonly_hp ]]; then contract=offline/multishine/contracts/multishine-card-textonly-hp.json; payload=build/offline/cardpkg/payload_tc_hp; hole=0x8178C000; size=0x58000; fi   # the probe adds 4.6 KB: the compressed file needs a hole of 0x58000 (8 KiB more claim than textonly)
if [[ -n $claimkb ]]; then
  [[ $profile == textonly_hp ]] || { echo "--claim-kb is for textonly-hp only" >&2; exit 2; }
  size=$(printf '0x%X' $(( (claimkb * 1024 + 8191) / 8192 * 8192 ))); hole=$(printf '0x%X' $(( 0x817E4000 - size )))
  payload=${payload}_claim$claimkb; out=${out}_claim$claimkb; stage=${stage}_claim$claimkb
fi
[[ -n $comp ]] && { out=${out}_z; stage=${stage}_z; }
out=$out$suffix; stage=$stage$suffix
[[ -n $online && -z $name ]] && { out=${out}_online; stage=${stage}_online; }
[[ -n $name ]] && { out=build/offline/cardpkg/prod_$name; stage=.analysis-deps/work/flippy_stage/ladder_prod_$name; [[ $profile == rc1 ]] && { out=build/offline/cardpkg/rc1card_$name; stage=.analysis-deps/work/flippy_stage/ladder_rc1card_$name; }; }

if [[ ${MS_BLOB_OPTS:-} == *rbtest=hang* ]]; then   # a build that hangs on purpose: never a release, never under a regular name
  (( dev )) && [[ -n $name ]] || { echo "MS_BLOB_OPTS rbtest=hang makes an engine that hangs in its first frames: only with --dev and a throw-away --name (e.g. zz_rbhang_tmp)" >&2; exit 2; }
fi
if [[ -n $rbread ]]; then [[ $profile == rc1 ]] || { echo "--rb-readout is for the rc1 profile" >&2; exit 2; }; export MS_BLOB_OPTS="${MS_BLOB_OPTS:+$MS_BLOB_OPTS,}rbread=on"; fi
[[ -n ${pub:-} ]] && export MS_DF_PUBKEYS="$pub${pub2:+,$pub2}"            # MultishineOvl (code) is verified by the engine at load with the build's pinned keys (df_glue.c)
[[ -z $verify || $verify == dev ]] && export MS_DF_UNSIGNED=1                # plain and dev cards also accept an unsigned container
stdtpl=${MS_MELEE_TEMPLATE:-$repo/.analysis-deps/work/coins/MultishineMelee_template.gci}
if [[ -z $stdmelee ]]; then   # default: the standard save, unless the old explicit flags were given or the template is missing
  if (( coinsx || recx )); then stdmelee=off; elif [[ -f $stdtpl && -f $repo/docs/data/melee_records.json ]]; then stdmelee=on; else stdmelee=off; fi
fi
if [[ $stdmelee == on ]]; then
  (( coinsx || recx )) && { echo "--standard-melee replaces --coins/--no-coins/--records: use one or the other" >&2; exit 2; }
  [[ -f $stdtpl ]] || { echo "--standard-melee needs the Melee save template ($stdtpl; tools/card/melee_coins.py capture)" >&2; exit 2; }
  coins=off; records=off; coinsx=1; recx=1
fi
if (( ! coinsx )); then   # Trophy Lottery coins: ON by default for every card incl. release ('coins and records on'); --no-coins turns it off
  if true; then
    coins=999
    [[ -f ${MS_MELEE_TEMPLATE:-$repo/.analysis-deps/work/coins/MultishineMelee_template.gci} ]] || { echo "WARNING: no Melee save template for the default 999 coins (tools/card/melee_coins.py capture --from <card Melee has made> --out ...): building WITHOUT coins" >&2; coins=off; }
  else coins=off; fi
fi
[[ $coins == off || ( $coins =~ ^[0-9]+$ && $coins -le 999 ) ]] || { echo "--coins takes 0..999 (or use --no-coins)" >&2; exit 2; }
if (( ! recx )); then   # records: ON by default like the coins; no template / no data = warn and build without
  records=on
  if [[ ! -f ${MS_MELEE_TEMPLATE:-$repo/.analysis-deps/work/coins/MultishineMelee_template.gci} || ! -f $repo/docs/data/melee_records.json ]]; then
    echo "WARNING: no Melee save template or no docs/data/melee_records.json for the default records (tools/card/melee_records.py): building WITHOUT records" >&2; records=off
  fi
fi
echo "== 1/3 engine payload for the card hole ($hole)"
"$PY" offline/cardloader/card_payload.py --contract "$contract" --out "$payload" --hole-base $hole
echo "== 2/3 loader save + MultishineCode -> card"
codes="$payload/codes.bin"
if [[ -n $labcfg ]]; then
  [[ $profile == rc1 ]] || { echo "--cfg is for the rc1 profile" >&2; exit 2; }
  # shellcheck disable=SC2086
  "$PY" offline/bridge/build_sbr.py patch "$payload/codes.bin" "$payload/codes_cfg.bin" $labcfg > "$payload/cfg.json"; codes="$payload/codes_cfg.bin"
fi
# shellcheck disable=SC2086
if [[ -n $online ]]; then "$PY" tools/card/patch_odcfg.py "$codes" "$payload/codes_online.bin" $odargs && codes="$payload/codes_online.bin"; fi
idargs=(); [[ -n $identity ]] && idargs=(--identity "$identity")
[[ -n $packdir ]] && { [[ $profile == rc1 && -f $packdir/MultishinePack.gci ]] || { echo "--pack DIR needs the rc1 profile and DIR/MultishinePack.gci" >&2; exit 2; }; idargs+=(--pack-gci "$packdir/MultishinePack.gci"); }
[[ $stdmelee == on ]] && { idargs+=(--standard-melee --melee-template "$stdtpl"); [[ -n $replmelee ]] && idargs+=(--replace-melee); echo "   Standard MultishineMelee on the card (unlocked, 999 coins, records, items off, our art)"; }
[[ $coins != off ]] && { idargs+=(--coins "$coins"); [[ -n ${MS_MELEE_TEMPLATE:-} ]] && idargs+=(--melee-template "$MS_MELEE_TEMPLATE"); echo "   Trophy Lottery coins on the card: $coins (MultishineMelee pre-made)"; }
[[ $records == on ]] && { idargs+=(--records --records-sets "$recsets"); [[ $coins == off && -n ${MS_MELEE_TEMPLATE:-} ]] && idargs+=(--melee-template "$MS_MELEE_TEMPLATE"); echo "   Melee records on the card ($recsets; melee_records.py)"; }
[[ $profile == rc1 ]] && idargs+=(--restore-manifest "$payload/manifest.json")     # the widescreen group is patched default OFF: no patch for its sites
# rc1: the exploit ends on the TITLE screen, not on its own Vs CSS: the entry stub sets the GM_MENU exit data and redirects the exploit's
# gm_SetPendingGameMode(2) once (card_export.py --exit-mode).  The earlier --goto-scene 1 had no effect (overwritten by both paths).
gotoarg=(); [[ $profile == rc1 ]] && gotoarg=(--exit-mode 0)
"$PY" offline/cardloader/build_cardpkg.py s2 --codes "$codes" ${idargs[@]+"${idargs[@]}"} --hole-base $hole --hole-size $size --oneshot 0x801A45B8 ${gotoarg[@]+"${gotoarg[@]}"} \
  --retail-iso "$iso" --out "$out" --stage-sd "$stage" $comp ${s30[@]+"${s30[@]}"} ${vargs[@]+"${vargs[@]}"}
echo "== 3/3 verify"
"$PY" tools/vmc_builder/vmc_builder.py verify "$out/card.raw" 2>&1 | grep -v note
card="$stage/swiss/saves/MemoryCardA.USA.raw"
if (( vstest )); then
  echo "== 4/4 standing test: Vs match from the card on the stock disc, Dolphin MMU on"
  bash offline/cardloader/run_vs_start.sh "$card" "build/offline/cardpkg/vstest_$(basename "$out")" || { echo "STANDING TEST FAILED: the card does not start a Vs match with the MMU on (see build/offline/cardpkg/vstest_$(basename "$out")/dolphin_userlog.log: look for UNHANDLED EXCEPTION)" >&2; exit 3; }
fi
echo; echo "card:   $card"
echo "SHA-1:  $(shasum -a 1 "$card" | cut -c1-40)    ($(stat -f %z "$card") bytes)"
echo
if [[ $profile == textonly_hp ]]; then      # LAB ONLY heap probe: where its RAM blocks are, next to the card (not inside swiss/)
  "$PY" - "$payload/image.json" "$out/package.json" "$stage/hp.json" "$out/hp.json" <<'PYEOF'
import json, sys
im = json.load(open(sys.argv[1]))
o = {"hp_cfg_addr": im["hp_cfg_addr"], "hp_status_addr": im["hp_status_addr"], "image_base": im["image_base"], "build_id": im["build_id"],
     "note": "hp_cfg word 1 = flags (1 text, 2 online, 4 pad toggle), hp_st word 0 = 'HPST'; layout offline/heapprobe/hp_ram.h"}
for p in sys.argv[3:]:
    json.dump(o, open(p, "w"), indent=1)
print("HEAP PROBE: hp_cfg", o["hp_cfg_addr"], " hp_st", o["hp_status_addr"], " (hp.json next to the card)")
PYEOF
  echo
fi
if (( dev )); then
  echo
  echo "*** DEV BUILD: loader display name 'DEV BUILD', pins YOUR dev key (${pub:0:16}...), accepts unsigned files; build_seq $seq. Smoke tests only, never a release. ***"
  mkdir -p "$stage/update"; "$PY" - "$out/code.gci" "$stage/update/$seq.mscf" <<'PYEOF'
import sys
d = open(sys.argv[1], "rb").read()
open(sys.argv[2], "wb").write(d[0x40:])
PYEOF
  echo "update file for the lab server: $stage/update/$seq.mscf"
  echo "  a lab update server (not in this repository) --update-dir $stage/update --update-pubkey $pub"
elif [[ -n $signkey ]]; then
  echo; echo "release build: build_seq $seq, signed; loader pins $(SIGN pubkey --key "$signkey" | cut -c1-16)..."
fi
echo
echo "To use it (drive in remote mode; the identity/Slippi card in slot A is replaced, so back it up first):"
echo "  tools/flippy/flippy_server.sh get /cubeboot/saves/MemoryCardA.USA.raw card_a_backup.raw"
echo "  tools/flippy/flippy_server.sh rm  /cubeboot/saves/MemoryCardA.USA.raw.wal"
echo "  cp \"$card\" /tmp/MemoryCardA.USA.raw && tools/flippy/flippy_server.sh put /tmp/MemoryCardA.USA.raw /cubeboot/saves/MemoryCardA.USA.raw"
