#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Build the OFFLINE Slippi codeset (flat Gecko list, 04/C2 only) from third_party/slippi-ssbm-asm.
#
#   offline/codeset/build_codeset.sh                         # profile 'default'
#   offline/codeset/build_codeset.sh --profile delay2
#   offline/codeset/build_codeset.sh --set boot=css --flag widescreen --name mine
#   offline/codeset/build_codeset.sh --all                   # every profile in configs/profiles.json
#   offline/codeset/build_codeset.sh --self-check            # only: rebuild stock netplay ini, assert SHA-1 f0ece3c9...
#   offline/codeset/build_codeset.sh --list                  # profiles / options / flags
#   (--update-configs / --update-docs rewrite configs/gecko/<profile>.json / )
#
# Outputs (build/offline/codeset/, gitignored):
#   <name>.bin            raw code words (Nintendont g_*.bin style)
#   <name>.gct            00D0C0DE header + codes + F0 footer
#   <name>.txt            gecko text (Dolphin ini [Gecko] syntax, one $block per origin group)
#   <name>.gecko.json     the exact gecko config built
#   <name>.manifest.json  per module: source file + sha256, origin section, decision, hooks, op types,
#                         sizes, per-op DOL section / retail word / .bin offset
#   <name>.audit.json     static dependency audit (build fails on FAIL)
# Everything is assembled in build/offline/codeset/work/asm, a copy of the submodule (gecko writes
# temp files next to the sources, so it never runs inside third_party/).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PYTHONDONTWRITEBYTECODE=1   # keep lib/ free of __pycache__

if [ "${1:-}" = "--list" ]; then
  exec python3 "$HERE/lib/codeset.py" --list
fi

eval "$("$HERE/tools.sh" --env)"
export GECKO_BIN DKP_BIN TOOLS_GO_VERSION TOOLS_GECKO_COMMIT TOOLS_GECKO_SHA256 TOOLS_AS_VERSION TOOLS_AS_SHA256 TOOLS_OBJCOPY_SHA256

if [ "${1:-}" = "--all" ]; then
  shift
  for p in $(python3 -c "import json,sys;print(' '.join(json.load(open(sys.argv[1]))['profiles']))" "$HERE/configs/profiles.json"); do
    python3 "$HERE/lib/codeset.py" --profile "$p" "$@"
  done
  exit 0
fi
exec python3 "$HERE/lib/codeset.py" "$@"
