#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# DEVELOPER OTA LOOP: build the card-route (rc1) engine from the current tree, give it the next dev build_seq, sign it with YOUR dev key, drop it into
# the update directory of the TARGET server. The server rescans the directory on every UPD_CHECK / UPD_FETCH: NO server restart. Then on the console: Update > A > A, restart.
#   tools/dev_ota.sh [--target lab|online] [--no-slippi] [--compress] [--cfg "k=v ..."] [--update-dir DIR] [--seq N] [--keep K] [--also-lab] [--no-vps] [--dry-run]
#     --target       lab (today's loop: our lab server on the Mac; the engine takes server and key from the card) or online (the server: build_card.sh --online patches
#                    play.example.org / 192.0.2.10 + its server key with the server lock; published to the server by the publish tool (outside this repository)).  Default $MS_TARGET, else lab.
#                    ; the values: tools/ms_target.py show online.
#     --no-slippi    the old lab default (cfg flags=0 reticket_max=1): no engine hand-off, a Slippi-route match never starts.  Since the COMPILED default is flags=0x20 (SBRA_F_ENGINE_PEER) reticket_max=0
#, so the normal build needs no cfg; --slippi is accepted and does nothing. mm_name stays empty here (fail closed); tools/release.sh passes --cfg "mm_name=mm.slippi.gg" so a release knows the matchmaker.
#     --compress     MSLZ-compressed file (MSCF v2, about 26 blocks instead of 62).  ONLY for a card whose exploit-save loader has the decoder (a --compress / combined-loader card, e.g. the
#                    59-block physical card).  The current cube card (ota_console_1) has the plain dev loader: a compressed file there is refused at Name Entry (E_HDR) and the loader falls back
#                    to MultishineCodeB (the previous build).  Safe, but pointless: use raw unless the card was built with --compress.
#     --update-dir   lab default: $MS_UPDATE_DIR, else the directory the running lab server was started with (a local work folder);
#                    online default: the local mirror a local work folder (what was sent to the server; the seq bookkeeping)
#     --also-lab     online only: ALSO publish the same file into the lab update directory ($MS_UPDATE_DIR or the lab default): the bootstrap step:
#                    the cube, still on the lab server, installs the online build from there and talks to the server after the restart
#     --no-vps       online only: build and mirror locally, do not send (later: the publish tool (outside this repository) publish FILE)
#     --seq N        force the build_seq (default: next free, state in ~/.multishine/dev_ota.seq, floor 102 or $MS_DEV_OTA_FLOOR, max 999)
#     --keep K       keep the newest K files in the local update directory (default 3; older ones are deleted; the server is never pruned)
#     --dry-run      print the plan (target, variant, seq, directories, public key, the server commands) and stop; builds nothing, sends nothing
# The dev key (~/.multishine/dev-signing.key, MS_DEV_KEY to override) is only passed to build_card.sh as a file name; it is never printed or copied.  Same key as the card's loader and
# Update overlay pin (a card built with another key refuses these files); the server pins the same DEV public key for now (ops/03_SERVER.md).  Needs the retail ISO (RETAIL_ISO) like
# build_card.sh.  Never touches the lab server process, a card or an SD; the server only through the publish tool (outside this repository) (adds files, never overwrites, never restarts).
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd); cd "$repo"
PY=${PYTHON:-/opt/homebrew/bin/python3}; [[ -x $PY ]] || PY=python3
hook="$repo/private/tools/dev_ota_publish.sh"   # server side (catalog check, the server publish); absent in the public tree
labdir=${MS_UPDATE_DIR:-"$repo/.analysis-deps/work/flippy_stage/ota_loop_1/update"}
target=${MS_TARGET:-lab}; updir=""; samenat=0; slippi=1; comp=0; cfg=""; seq=""; keep=3; dry=0; alsolab=0; novps=0
while [[ $# -gt 0 ]]; do case $1 in
  --target) target=${2:?--target lab|online}; shift ;;
  --slippi) slippi=1 ;; --no-slippi) slippi=0 ;; --compress) comp=1 ;; --cfg) cfg=${2:?--cfg "k=v ..."}; shift ;;
  --update-dir) updir=${2:?--update-dir DIR}; shift ;; --seq) seq=${2:?--seq N}; shift ;; --keep) keep=${2:?--keep K}; shift ;; --dry-run) dry=1 ;;
  --also-lab) alsolab=1 ;; --no-vps) novps=1 ;;
  -h|--help) sed -n 3,26p "$0"; exit 0 ;;
  *) echo "unknown argument $1 (see: $0 --help)" >&2; exit 2 ;;
esac; shift; done
case $target in
  lab) ((alsolab || novps)) && { echo "--also-lab / --no-vps are for --target online" >&2; exit 2; }; [[ -n $updir ]] || updir=$labdir ;;
  online) 
          [[ -n $updir ]] || updir=$("$PY" tools/ms_target.py get online update_dir) ;;
  *) echo "--target / MS_TARGET must be lab or online, not '$target'" >&2; exit 2 ;;
esac
((slippi)) || cfg="flags=0 reticket_max=1${cfg:+ $cfg}"   # --no-slippi: the pre- payload default
key=${MS_DEV_KEY:-${MS_DEV_HOME:-$HOME/.multishine}/dev-signing.key}
[[ -f $key ]] || { echo "no dev key at $key: run tools/card/build_card.sh rc1 --dev once (it makes the key outside the repo), then the card you flash must be built with the same key" >&2; exit 2; }
pub=$("$PY" tools/card/mscf_sign.py pubkey --key "$key")
if [[ $target == online && ${MS_PROD_BUILD:-0} != 1 ]]; then      # MS_PROD_BUILD=1 (tools/release.sh --prod): signed with a production key, the server pin is moved at cutover
  vpub=$("$PY" tools/ms_target.py get online update_pub)
  [[ $pub == "$vpub" ]] || { echo "the VPS pins update key ${vpub:0:16}... but your dev key is ${pub:0:16}...: the VPS would reject these files" >&2; exit 2; }
fi
if [[ -z $seq ]]; then
  seq=$("$PY" tools/dev_ota.py next-seq --update-dir "$updir")
  if ((alsolab)); then s2=$("$PY" tools/dev_ota.py next-seq --update-dir "$labdir"); if ((s2 > seq)); then seq=$s2; fi; fi
fi
variant=plain; [[ $target == online ]] && variant=online; ((slippi)) || variant=$variant-noslippi; ((comp)) && variant=$variant-z
name=devota_${variant//-/_}
echo "target $target   variant $variant   build_seq $seq   update dir $updir   dev public key ${pub:0:16}..."
if [[ $target == online ]]; then
  echo "server baked: $("$PY" tools/ms_target.py odcfg-args online | sed -E 's/--pin ([0-9a-f]{16})[0-9a-f]+/--pin \1.../')"
  ((alsolab)) && echo "also into the lab update dir: $labdir   (bootstrap: the cube installs it from the lab server, then talks to the VPS)"
  [[ -x $hook ]] && "$hook" plan "$target" "$updir" "$seq" "$novps" || echo "no private publish hook (private/tools/dev_ota_publish.sh): builds and signs only"
fi
((comp)) && echo "NOTE: compressed file: the card's loader must be the combined (--compress) one, see --help"
((dry)) && exit 0
if [[ ${MS_PROD_BUILD:-0} == 1 ]]; then                        # tools/release.sh --prod: a release-verified code file signed with the production key ($key = MS_DEV_KEY), PROD-B pinned as the second key
  pub2=$(awk '$1=="prod-b"{print $2}' tools/release/prod_pubkeys.txt)
  bargs=(rc1 --sign "$key" --pubkey2 "$pub2" --seq "$seq" --name "$name" --target "$target")
else
  bargs=(rc1 --dev --seq "$seq" --name "$name" --target "$target")
fi; ((comp)) && bargs+=(--compress); [[ -n $cfg ]] && bargs+=(--cfg "$cfg")
t0=$(date +%s); log=$(mktemp -t dev_ota_build.XXXXXX)
echo "== building (build_card.sh ${bargs[*]}); log $log"
nice -n 10 tools/card/build_card.sh "${bargs[@]}" >"$log" 2>&1 || { tail -25 "$log" >&2; echo "build failed" >&2; exit 1; }
t1=$(date +%s)
gci="build/offline/cardpkg/rc1card_$name/code.gci"; [[ -f $gci ]] || { echo "no $gci" >&2; exit 1; }
[[ $target == online ]] && { grep -q "server lock" "$log" || { echo "the build log has no 'server lock' line: the online block was not baked, nothing published" >&2; exit 1; }; }
echo "== signed with the dev key, publishing"
"$PY" tools/dev_ota.py publish --gci "$gci" --seq "$seq" --update-dir "$updir" --pub "$pub" --keep "$keep"
((alsolab)) && "$PY" tools/dev_ota.py publish --gci "$gci" --seq "$seq" --update-dir "$labdir" --pub "$pub" --keep "$keep"
"$PY" tools/dev_ota.py commit "$seq"
if [[ -x $hook ]]; then "$hook" check "$updir" "$pub"; fi       # catalog check: private (it imports the server's ms_update)
echo "built in $((t1 - t0)) s"
if [[ $target == online ]] && ! ((novps)) && [[ -x $hook ]]; then "$hook" vps "$updir" "$seq"; fi   # the server publish is private
where="the lab server is already running with this directory; it sees the new file on the next check, no restart"
[[ $target == online ]] && where="the cube must ALREADY run an online build to see this on the VPS (else: --also-lab, ONLINE_SWITCH.md section 5); the VPS sees the file on the next check, no restart"
cat <<MSG

ON THE CONSOLE ($where):
  1. Main Menu > More > Settings > Online > Update                  (shows "Installed: build N")
  2. A (check)     -> "Update available: build $seq"
  3. A (download)  -> progress, check, write to the card; then A on "Build $seq is installed" = restart (the loader re-injects at Name Entry like a power-on)
  4. Vs Mode > Name Entry as usual; Update again shows "Installed: build $seq".
If the check says "Up to date" the server does not serve this directory (the lab server prints its --update-dir at start) or the card pins a different dev key.
MSG
