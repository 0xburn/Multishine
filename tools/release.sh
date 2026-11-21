#!/bin/bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# One command for a Multishine card-route release (engine N.mscf + overlay ovl-N.mscf): build, sign, publish to the server and the lab server, verify both with a real update check.
#   tools/release.sh SEQ [--no-publish] [--update-dir DIR] [--prod | --key FILE]
#     --update-dir DIR  (or env UPD_OVERRIDE) build into DIR instead of the real release mirror: nothing is published, the lab server is not touched, ~/.multishine/dev_ota.seq is restored,
#                       the overlay work files go next to DIR.  For dry run of the release recipe (a temp dir) that must not overwrite existing release artefacts.
#     --prod            PRODUCTION build: signs with ~/.multishine/prod/prod-a.key (PROD-A), SEQ must be >= 1000 (the server offers the HIGHEST seq, so once prod 1000+ files exist the dev-signed 4xx
#                       builds are never offered again).  Requires --update-dir DIR; NEVER publishes: no lab copy, no the server.  Prints the sha256s; publishing is a separate owner step
#                       (the publish tool (outside this repository) publish, which refuses anything not signed by tools/release/prod_pubkeys.txt).  Refused together with the lab publish (no --update-dir).
#     --key FILE        (or env KEY_FILE) sign with another key file (a dry run / PROD-B test); same prod rules apply when the key is not the dev key: nothing is published.
#   Default (no --prod/--key): the DEV key, lab only: the dev-signed files are copied to the lab server dir and are NEVER sent to the server (the publish tool (outside this repository) refuses them).
# SEQ must be higher than every published release (the server always offers the highest).  Needs the dev signing key (~/.multishine/dev-signing.key), devkitPPC, the retail ISO env like tools/dev_ota.sh.
# What it does: the recipe used for releases 410-452 plus the first release candidate additions (MS_BLOB_OPTS hubovl=ovl,rbread=rt,netcfg=on,flashblank=on; the overlay container carries ms/gns.ovl; online profile with the server server lock), then, only if the private area exists, private/tools/publish_lab.sh (copy into the lab server's update directory + the real UDP update check).  Never touches a card or the cube.
set -u
SEQ="${1:?usage: tools/release.sh SEQ [--no-publish] [--update-dir DIR]}"; shift; PUBLISH=1; UPD_OVERRIDE="${UPD_OVERRIDE:-}"; PROD=0; KEY="${KEY_FILE:-}"
while [[ $# -gt 0 ]]; do case "$1" in --no-publish) PUBLISH=0 ;; --update-dir) UPD_OVERRIDE="${2:?--update-dir DIR}"; shift ;; --prod) PROD=1 ;; --key) KEY="${2:?--key FILE}"; shift ;; *) echo "unknown option $1" >&2; exit 2 ;; esac; shift; done
DEVKEY="${MS_DEV_HOME:-$HOME/.multishine}/dev-signing.key"   # MS_DEV_HOME: the same dev key home as tools/card/build_card.sh and tools/dev_ota.py
if [[ $PROD == 1 ]]; then
  [[ -n "$KEY" ]] || KEY="$HOME/.multishine/prod/prod-a.key"
  [[ -n "$UPD_OVERRIDE" ]] || { echo "REFUSED: --prod never publishes to the lab dir; give --update-dir DIR (the files are built and signed there)"; exit 2; }
  (( SEQ >= 1000 )) || { echo "REFUSED: production build_seq starts at 1000 (got $SEQ)"; exit 2; }
fi
[[ -n "$KEY" ]] || KEY="$DEVKEY"
[[ -f "$KEY" ]] || { echo "no key file $KEY"; exit 2; }
if [[ "$KEY" != "$DEVKEY" ]]; then PROD=1; (( SEQ >= 1000 )) || { echo "REFUSED: a non-dev key signs production seqs (>= 1000) only"; exit 2; }; [[ -n "$UPD_OVERRIDE" ]] || { echo "REFUSED: a non-dev key needs --update-dir (never published from here)"; exit 2; }; fi
[[ $PROD == 1 ]] && export MS_PROD_BUILD=1 MS_DEV_KEY="$KEY"
[[ -n "$UPD_OVERRIDE" ]] && PUBLISH=0                       # a dry run directory is never published
cd "$(cd "$(dirname "$0")/.." && pwd)"; REPO="$PWD"
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC PATH=/opt/devkitpro/devkitPPC/bin:$PATH
# flashblank=on: the Event Match flash fix (blanks the picture between the hub and Slippi's online character screen).  NEVER tlsprobe (lab TCP+TLS probe) in a release.
export MS_BLOB_OPTS="hubovl=ovl,menuovl=ovl,flashblank=on,qchat=on,repovl=on"   # repovl=on:, Slippi game reports; sends nothing unless the server HELLO sets bit 15. qchat=on:, Slippi-style D-Pad quick chat on the online character screens. the release needs neither netcfg (card-set server rows, 6.9 KB) nor rbread=rt (rollback readout, 4.3 KB); both stay lab-only
unset SG_LAB SG_TLSPROBE SG_LAB_PIN                         # nothing of the lab probe may leak in from the caller's shell
[[ "$MS_BLOB_OPTS" == *tlsprobe* ]] && { echo "REFUSED: tlsprobe is lab only"; exit 1; }
# RELEASE_TEST (option 1 of the in-match messages cost run): a production-signed MEASUREMENT build for the lab cube,
# replaced by the next normal release.  mm_off = the baseline with the rollback readout and the frame-period line; mm_on = the same plus MS_MATCH_MSG and its tick counters; qc = the normal release plus the engine option
# qchat=on (the D-pad quick chat on the online character screen; the overlays are built with the same option).  Nothing else is accepted.
XCF=""
case "${RELEASE_TEST:-}" in
  "") ;;   # the normal release: no extra compile flags (MS_REP_LINE, the temporary Main Menu report line, removed by)
  mm_off) export MS_BLOB_OPTS="$MS_BLOB_OPTS,rbread=on"; XCF="-DMM_FTSTATS=1"; unset MS_MATCH_MSG; echo "== TEST BUILD mm_off (rbread + frame-period line)";;
  mm_on)  export MS_BLOB_OPTS="$MS_BLOB_OPTS,rbread=on"; XCF="-DMS_MATCH_MSG=1 -DMM_PROBE=1 -DMM_FTSTATS=1"; export MS_MATCH_MSG=1; echo "== TEST BUILD mm_on (in-match messages + counters)";;
  qc)     export MS_BLOB_OPTS="$MS_BLOB_OPTS"; unset MS_MATCH_MSG; echo "== TEST BUILD qc (D-pad quick chat on the online character screen, docs/design/QUICK_CHAT_PORT.md)";;
  qc_rb)  export MS_BLOB_OPTS="$MS_BLOB_OPTS,rbread=on"; XCF="-DMM_FTSTATS=1"; unset MS_MATCH_MSG; echo "== TEST BUILD qc_rb (quick chat + the readout with the long-pass lines SW/S1/S2, docs/design/MATCH_START_STALL.md)";;
  bootloop) export MS_BLOB_OPTS="$MS_BLOB_OPTS,rbread=on"; XCF="-DMM_FTSTATS=1 -DMM_BOOTLOOP=1"; unset MS_MATCH_MSG; echo "== TEST BUILD bootloop (the readout build that reboots the game every 35 s on a Wii and counts the boots at 0xD30266A0)";;
  fc)     export MS_BLOB_OPTS="${MS_BLOB_OPTS/,qchat=on/}"; XCF="-DMM_FC=1"; unset MS_MATCH_MSG; echo "== TEST BUILD fc (the normal release + the FRAME COST readout: where a frame's time goes, per match on both consoles, uploaded via LAB_DIAG at the match end; no rollback readout and no quick chat (room in the engine image), docs/design/WII_FRAME_COST_FC.md)";;
  rb_off) export MS_BLOB_OPTS="$MS_BLOB_OPTS,rbread=on"; XCF="-DMM_FTSTATS=1 -DMM_RB_OFF=1"; unset MS_MATCH_MSG; echo "== TEST BUILD rb_off (the readout build with rollback forced off: delay only, A/B against mm_off)";;
  dlog)   export MS_BLOB_OPTS="$MS_BLOB_OPTS"; XCF="-DONS_DLOG=1"; unset MS_MATCH_MSG; echo "== TEST BUILD dlog (the normal release + the network report file MultishineDiag on Test connection or Reconnect, or by itself when still offline after 45 s in the menus, docs/design/BBA_DIAG_GCI.md; no on-screen readout)";;
  *) echo "REFUSED: RELEASE_TEST is mm_off, mm_on, qc, qc_rb, rb_off, bootloop, fc or dlog"; exit 1;;
esac
[[ -n "$XCF" ]] && export MS_XCF="${XCF/-DMS_MATCH_MSG=1 /}" || unset MS_XCF MS_MATCH_MSG
case "${RELEASE_TEST:-}" in mm_off|qc_rb) CHK_ALLOW="rbread,MS_RB_READOUT,MM_FTSTATS"; ;; fc) CHK_ALLOW="MM_FC";; rb_off) CHK_ALLOW="rbread,MS_RB_READOUT,MM_FTSTATS,MM_RB_OFF";; bootloop) CHK_ALLOW="rbread,MS_RB_READOUT,MM_FTSTATS,MM_BOOTLOOP";; mm_on) CHK_ALLOW="rbread,MS_RB_READOUT,MM_FTSTATS,MM_PROBE,MS_MATCH_MSG";; *) CHK_ALLOW="";; esac
# L2 guard: no lab-only switch in the options, flags or environment of a release (a RELEASE_TEST build may carry only its own), before anything is built or signed
python3.14 -I "$REPO/tools/release/check_release_shape.py" --opts "$MS_BLOB_OPTS" --xcf="$XCF" --allow "$CHK_ALLOW" || exit 1
UPD="$REPO/.analysis-deps/work/ota_online/update"; LAB="$REPO/.analysis-deps/work/flippy_stage/disc_free_1/update"; LOG=/tmp/release_$SEQ.log; OBASE="$REPO/.analysis-deps/work/ota_online"
SEQFILE="${MS_DEV_HOME:-$HOME/.multishine}/dev_ota.seq"; SEQBAK=""
if [[ -n "$UPD_OVERRIDE" ]]; then
  mkdir -p "$UPD_OVERRIDE"; UPD="$(cd "$UPD_OVERRIDE" && pwd)"; OBASE="$UPD/.."; LOG="$UPD/release_$SEQ.log"; LAB=""
  [[ -f "$SEQFILE" ]] && { SEQBAK="$UPD/.dev_ota.seq.bak"; cp "$SEQFILE" "$SEQBAK"; }     # tools/dev_ota.py commit moves the dev counter: a dry run puts it back
  trap '[[ -n "$SEQBAK" && -f "$SEQBAK" ]] && cp "$SEQBAK" "$SEQFILE"' EXIT
fi
# Card layout: CL_LAYOUT is stock (default, byte-identical to every release so far), generic (list at 0x817E0000, for Swiss with cheats) or low.
# The engine is built for it by build_card.sh through dev_ota.sh (same variable, inherited); the overlay must know the same card work area.  A generic build is NEVER mixed with stock cards:
# the exploit save and the engine have to match, so a generic release is published only with the migration for existing cards.
case "${CL_LAYOUT:-stock}" in stock) WA_ADDR=0x817E4000;; generic) WA_ADDR=0x817D4000;; low) WA_ADDR=0x817B4000;; *) echo "REFUSED: CL_LAYOUT is stock, generic or low"; exit 2;; esac
[[ "${CL_LAYOUT:-stock}" != stock ]] && echo "== CARD LAYOUT ${CL_LAYOUT} (work area $WA_ADDR): a layout build, not for the stock cards"
PY=python3.14
rm -f "$UPD/$SEQ.mscf" "$UPD/ovl-$SEQ.mscf"
# Slippi bridge: the compiled cfg has mm_name EMPTY (fail closed, lab shape); a release must know the real matchmaker or Unranked/Direct stop at "Matchmaker name invalid".  Exactly this one patch
# (the same shape as live-shaped diag515/516/520: mm_name matchmaking.slippi.gg, compiled flags 0x20 and the compiled reticket_max 10 unchanged).  build_sbr.py refuses mm2.slippi.gg and non-private addresses; nothing else may be added here.
BRIDGE_CFG="mm_name=matchmaking.slippi.gg"
[[ "$BRIDGE_CFG" =~ ^mm_name=matchmaking\.slippi\.gg$ ]] || { echo "REFUSED: a release bridge cfg may only set mm_name=matchmaking.slippi.gg (no flags, no addresses, no lab options)"; exit 1; }
$PY tools/check_match_rules.py > "$LOG.mr" 2>&1 || { echo "REFUSED: match-time code reads the search mode or writes the match rules record outside its writers (docs/design/MATCH_RULES.md):"; tail -6 "$LOG.mr"; exit 1; }   # guard rail, 1 s
# MS_RELEASE_TARGET=lab (dev-signed, dry run dir only): the engine takes its server and key from the card record (no the server lock), for a cube card that talks to the STAGING server
[[ ${MS_RELEASE_TARGET:-online} == lab && $PROD == 0 && -n "$UPD_OVERRIDE" ]] && RELTARGET="--target lab" || RELTARGET="--target online --no-vps"
echo "== engine $SEQ"; tools/dev_ota.sh $RELTARGET --seq "$SEQ" --cfg "$BRIDGE_CFG" --update-dir "$UPD" > "$LOG" 2>&1 || { echo "engine build FAILED, see $LOG"; tail -5 "$LOG"; exit 1; }
O="$OBASE/ovl$SEQ"; mkdir -p "$O"; pub=$($PY tools/card/mscf_sign.py pubkey --key "$KEY"); ovpub=$pub
[[ $PROD == 1 ]] && ovpub=$(awk '$1=="prod-a"||$1=="prod-b"{printf "%s%s",s,$2; s=","}' tools/release/prod_pubkeys.txt)   # the overlay pre-check list must equal the release loader's pinned list (A,B)
echo "== overlay $SEQ"
( SG_LAB=0 SG_UPDHOST=1 SG_UPD_PUBKEY=$ovpub SG_EXTRA_CFLAGS="-DMS_CARD_WA_ADDR=${WA_ADDR}u" MS_EXTRA_CFLAGS="$XCF" $PY offline/multishine/menus/sign/sg_ovl_pack.py build "$O/sign.ovl" --stats "$O/sign.json" \
  && MS_EXTRA_CFLAGS="$XCF" $PY offline/multishine/menus/hub/hub_ovl_pack.py build "$O/hub.ovl" --stats "$O/hub.json" \
  && MS_EXTRA_CFLAGS="$XCF" $PY offline/multishine/menus/gns/gns_pack.py build "$O/gns.ovl" --stats "$O/gns.json" \
  && MS_EXTRA_CFLAGS="-DSD_CARD_HOLE -DMS_CARD_WA_ADDR=${WA_ADDR}u $XCF" $PY offline/multishine/menus/ml/ml_ovl_pack.py build "$O/menu.ovl" --profile s17-compat-onbc --stats "$O/menu.json" \
  && MS_EXTRA_CFLAGS="$XCF" $PY offline/multishine/menus/rep/rep_ovl_pack.py build "$O/rep.ovl" --stats "$O/rep.json" --workdir "$O/rep_w" \
  && { ! strings "$O/rep.ovl" | grep -q "REP-TEST-ROOTS" || { echo "REFUSED: rep.ovl carries the test roots"; false; }; } \
  && $PY offline/multishine/menus/hub/hub_pack.py --sign-ovl "$O/sign.ovl" --hub-ovl "$O/hub.ovl" --gns-ovl "$O/gns.ovl" --menu-ovl "$O/menu.ovl" --rep-ovl "$O/rep.ovl" --out "$O/pack" --ovl-seq "$SEQ" \
  && $PY tools/card/mscf_sign.py sign --key "$KEY" --out "$O/MultishineOvl_signed.gci" --seq "$SEQ" "$O/pack/MultishineOvl.gci" \
  && $PY tools/dev_ota.py publish --kind ovl --gci "$O/MultishineOvl_signed.gci" --seq "$SEQ" --update-dir "$UPD" --pub "$pub" --like "$REPO/.analysis-deps/work/flippy_cli_1.4.6/data/live.raw" ) >> "$LOG" 2>&1 || { echo "overlay build FAILED, see $LOG"; tail -5 "$LOG"; exit 1; }
$PY -I "$REPO/tools/release/check_release_shape.py" --images "$UPD/$SEQ.mscf" "$UPD/ovl-$SEQ.mscf" --allow "$CHK_ALLOW" || exit 1   # the built images carry no lab marker (nothing is published before this)
# The system-files data container: pack-$SEQ.mscf, Slippi's public GPL files unmodified + our pack header, signed with the same key; the in-game Update sets up MultishinePack from it and the player's own disc.
# Built when Slippi's GameFiles/GALE01 is on this machine (SLIPPI_GAMEFILES, else the vendored Ishiiruka copy); a release without it simply has no pack-N.mscf (a build that already has the data published keeps serving it).
SLPGF="${SLIPPI_GAMEFILES:-$REPO/vendor/ishiiruka/Data/Sys/GameFiles/GALE01}"
rm -f "$UPD/pack-$SEQ.mscf"
if [[ -f "$SLPGF/slpCSS.dat" ]]; then
  PD="$OBASE/pack$SEQ"; mkdir -p "$PD"
  ( $PY -I tools/card/pack_data.py build --slippi-files "$SLPGF" --seq "$SEQ" --out "$PD/pack.gci" \
    && $PY tools/card/mscf_sign.py sign "$PD/pack.gci" --out "$PD/pack_signed.gci" --key "$KEY" --seq "$SEQ" \
    && $PY tools/dev_ota.py publish --kind pack --gci "$PD/pack_signed.gci" --seq "$SEQ" --update-dir "$UPD" --pub "$pub" ) >> "$LOG" 2>&1 || { echo "system-files data build FAILED, see $LOG"; tail -5 "$LOG"; exit 1; }
  echo "built: system-files data pack-$SEQ.mscf $(( $(stat -f %z "$UPD/pack-$SEQ.mscf") / 8192 )) blocks"
else echo "note: no Slippi GameFiles ($SLPGF): no pack-$SEQ.mscf in this build"; fi
E=$(stat -f %z "$UPD/$SEQ.mscf"); V=$(stat -f %z "$UPD/ovl-$SEQ.mscf"); echo "built: engine $((E/8192)) blocks, overlay $((V/8192)) blocks, total $(((E+V)/8192))"
echo "sha256:"; shasum -a 256 "$UPD/$SEQ.mscf" "$UPD/ovl-$SEQ.mscf"
if [[ $PROD == 1 && ${CARD_OTA:-1} != 0 ]]; then                   #: a new virtual card must start on the SAME build as the release (the Nintendont card download crd-N): built and signed with every production release (CARD_OTA=0 skips)
  echo "== card download crd-$SEQ"; bash tools/release/make_fresh_card_ota.sh "$SEQ" "$UPD" > "$UPD/../crd_$SEQ.log" 2>&1 || { echo "card download build FAILED, see $UPD/../crd_$SEQ.log"; tail -4 "$UPD/../crd_$SEQ.log"; exit 1; }
  echo "built: card download $PWD/dist/.crd_$SEQ/crd-$SEQ.mscf"
fi
[[ $PROD == 1 ]] && { echo "PRODUCTION build signed by $($PY tools/card/mscf_sign.py pubkey --key "$KEY" | cut -c1-16)... (seq $SEQ): NOT published anywhere.  Next: the publish tool (outside this repository) publish $UPD/$SEQ.mscf $UPD/ovl-$SEQ.mscf $UPD/pack-$SEQ.mscf $PWD/dist/.crd_$SEQ/crd-$SEQ.mscf  (add --dry-run first if unsure)"; exit 0; }
[[ $PUBLISH == 0 ]] && { echo "not published (--no-publish / --update-dir)"; exit 0; }
# the lab publish + verify tail is server side: private/tools/publish_lab.sh (copy into the lab update dir, real update check); public builds end at "built and signed"
PRIV="$REPO/private/tools/publish_lab.sh"
[[ -x $PRIV ]] || { echo "built and signed: $UPD/$SEQ.mscf $UPD/ovl-$SEQ.mscf (no private publish script here: nothing copied, nothing verified)"; exit 0; }
PY="$PY" exec "$PRIV" "$SEQ" "$UPD" "$LAB"
