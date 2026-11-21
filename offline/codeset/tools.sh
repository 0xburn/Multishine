#!/usr/bin/env bash
# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
# Provision the pinned toolchain for the offline Slippi codeset build.
#
#   Go 1.27.1 (official tarball, sha256-pinned)          -> $DEPS/go
#   JLaferri/gecko @ e3183ba (v5.0.0 + README-only),
#     built with `go build -trimpath`                      -> $DEPS/bin/gecko (sha256-pinned per host)
#   devkitPPC binutils (powerpc-eabi-as / -objcopy)      -> located, version-checked (not downloaded)
#
# $DEPS defaults to <repo>/.analysis-deps/offline-codeset (gitignored).
#
# Network use is optional. Seeds let you provision from local copies; every seed is
# verified the same way as a download:
#   GO_TARBALL=/path/go1.27.1.<os>-<arch>.tar.gz   verified against the pinned sha256
#   GO_SEED_DIR=/path/to/extracted/go              accepted only if `go version` is exactly go1.27.1
#                                                  (the gecko binary hash below then proves the toolchain)
#   GECKO_SEED_DIR=/path/to/gecko/git/clone        must be a git repo containing an earlier revision
#   OFFLINE_FETCH=0                                never touch the network (fail instead)
#
# After building, the gecko binary's sha256 must equal the pinned value for this host
# (GECKO_SHA256_<os>_<arch> below). Set ALLOW_UNPINNED_GECKO=1 to accept another hash on a
# host without a pin (the build's netplay-ini parity self-check still guards correctness).
#
# Prints `export ...` lines on success when called with --env (consumed by build_codeset.sh).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
DEPS="${OFFLINE_CODESET_DEPS:-$REPO/.analysis-deps/offline-codeset}"

GO_VERSION=go1.27.1
# sha256 of the official https://go.dev/dl/ tarballs
GO_SHA256_darwin_arm64=ee215d57e0ec269c60cc9ceca68e6bda321ba9ee5afe24f4b0988703c2d87d12

GECKO_REPO=https://github.com/JLaferri/gecko.git
GECKO_COMMIT=e3183ba88332ba7f15a1c36664d9e5fc472d4794
# sha256 of `go build -trimpath` output of $GECKO_COMMIT with $GO_VERSION (GOFLAGS empty, CGO_ENABLED=0)
GECKO_SHA256_darwin_arm64=16f6917a89190111b3b625447e84fd70533b172b06abd9b1562cff1c21ec9524

# devkitPPC binutils used to reproduce the shipped Slippi ini byte-exactly
DKP_AS_VERSION_RE='GNU assembler \(GNU Binutils\) 2\.46'

log() { echo "[tools] $*" >&2; }
die() { echo "[tools] ERROR: $*" >&2; exit 1; }
sha256() { shasum -a 256 "$1" | awk '{print $1}'; }

os="$(uname -s | tr '[:upper:]' '[:lower:]')"
arch="$(uname -m)"; case "$arch" in x86_64) arch=amd64;; aarch64) arch=arm64;; esac
plat="${os}_${arch}"

mkdir -p "$DEPS/bin"

# ---------------------------------------------------------------- Go
GOROOT_DIR="$DEPS/go"
go_ok() { [ -x "$1/bin/go" ] && [ "$("$1/bin/go" version | awk '{print $3}')" = "$GO_VERSION" ]; }
if ! go_ok "$GOROOT_DIR"; then
  rm -rf "$GOROOT_DIR"
  want_var="GO_SHA256_${plat}"; want="${!want_var:-${GO_SHA256:-}}"
  if [ -n "${GO_SEED_DIR:-}" ]; then
    go_ok "$GO_SEED_DIR" || die "GO_SEED_DIR=$GO_SEED_DIR is not $GO_VERSION"
    log "seeding Go from $GO_SEED_DIR"
    rsync -a "$GO_SEED_DIR/" "$GOROOT_DIR/"
  else
    tb="${GO_TARBALL:-}"
    if [ -z "$tb" ]; then
      [ "${OFFLINE_FETCH:-1}" = 0 ] && die "Go missing and OFFLINE_FETCH=0 (set GO_TARBALL or GO_SEED_DIR)"
      tb="$DEPS/$GO_VERSION.$os-$arch.tar.gz"
      log "downloading https://go.dev/dl/$GO_VERSION.$os-$arch.tar.gz"
      curl -fsSL -o "$tb" "https://go.dev/dl/$GO_VERSION.$os-$arch.tar.gz"
    fi
    [ -n "$want" ] || die "no pinned sha256 for Go on $plat (set GO_SHA256)"
    got="$(sha256 "$tb")"; [ "$got" = "$want" ] || die "Go tarball sha256 $got != pinned $want"
    tar -C "$DEPS" -xzf "$tb"
    [ -z "${GO_TARBALL:-}" ] && rm -f "$tb"
  fi
  go_ok "$GOROOT_DIR" || die "Go install failed"
fi

# ---------------------------------------------------------------- gecko
GECKO_SRC="$DEPS/gecko-src"
GECKO_BIN="$DEPS/bin/gecko"
want_var="GECKO_SHA256_${plat}"; GECKO_WANT="${!want_var:-}"
gecko_ok() { [ -x "$GECKO_BIN" ] && [ -n "$GECKO_WANT" ] && [ "$GECKO_WANT" != REPLACE_ME ] && [ "$(sha256 "$GECKO_BIN")" = "$GECKO_WANT" ]; }
if ! gecko_ok; then
  if [ ! -d "$GECKO_SRC/.git" ]; then
    if [ -n "${GECKO_SEED_DIR:-}" ]; then
      log "cloning gecko from seed $GECKO_SEED_DIR"
      git clone -q --no-checkout "$GECKO_SEED_DIR" "$GECKO_SRC"
    else
      [ "${OFFLINE_FETCH:-1}" = 0 ] && die "gecko source missing and OFFLINE_FETCH=0 (set GECKO_SEED_DIR)"
      log "cloning $GECKO_REPO"
      git clone -q --no-checkout "$GECKO_REPO" "$GECKO_SRC"
    fi
  fi
  git -C "$GECKO_SRC" -c advice.detachedHead=false checkout -q --force "$GECKO_COMMIT"
  [ "$(git -C "$GECKO_SRC" rev-parse HEAD)" = "$GECKO_COMMIT" ] || die "gecko checkout is not $GECKO_COMMIT"
  [ -z "$(git -C "$GECKO_SRC" status --porcelain)" ] || die "gecko source tree is dirty"
  log "building gecko $GECKO_COMMIT with $GO_VERSION (-trimpath)"
  ( cd "$GECKO_SRC" && env -i PATH="$GOROOT_DIR/bin:/usr/bin:/bin" HOME="$DEPS" \
      GOROOT="$GOROOT_DIR" GOPATH="$DEPS/gopath" GOCACHE="$DEPS/gocache" GOMODCACHE="$DEPS/gomodcache" \
      GOTOOLCHAIN=local GOFLAGS= CGO_ENABLED=0 GOPROXY=off \
      "$GOROOT_DIR/bin/go" build -trimpath -o "$GECKO_BIN" . )
  got="$(sha256 "$GECKO_BIN")"
  if [ -z "$GECKO_WANT" ] || [ "$GECKO_WANT" = REPLACE_ME ]; then
    [ "${ALLOW_UNPINNED_GECKO:-0}" = 1 ] || die "no pinned gecko sha256 for $plat (built: $got). Pin it or set ALLOW_UNPINNED_GECKO=1"
    log "WARNING: gecko sha256 $got is unpinned for $plat"
  elif [ "$got" != "$GECKO_WANT" ]; then
    die "gecko sha256 $got != pinned $GECKO_WANT"
  fi
fi
log "gecko OK: $GECKO_BIN sha256=$(sha256 "$GECKO_BIN")"

# ---------------------------------------------------------------- devkitPPC
DKP_BIN=""
for c in "${DEVKITPPC:+$DEVKITPPC/bin}" /opt/devkitpro/devkitPPC/bin; do
  [ -n "$c" ] && [ -x "$c/powerpc-eabi-as" ] && [ -x "$c/powerpc-eabi-objcopy" ] && { DKP_BIN="$c"; break; }
done
[ -n "$DKP_BIN" ] || die "devkitPPC binutils not found (install devkitPPC or set DEVKITPPC)"
asv="$("$DKP_BIN/powerpc-eabi-as" --version | head -1)"
if ! echo "$asv" | grep -Eq "$DKP_AS_VERSION_RE"; then
  [ "${ALLOW_TOOL_MISMATCH:-0}" = 1 ] || die "unexpected assembler: '$asv' (want binutils 2.46; ALLOW_TOOL_MISMATCH=1 to override)"
  log "WARNING: assembler '$asv' differs from the pinned binutils 2.46"
fi
log "devkitPPC OK: $DKP_BIN ($asv)"

if [ "${1:-}" = "--env" ]; then
  echo "export GECKO_BIN='$GECKO_BIN'"
  echo "export DKP_BIN='$DKP_BIN'"
  echo "export TOOLS_GO_VERSION='$GO_VERSION'"
  echo "export TOOLS_GECKO_COMMIT='$GECKO_COMMIT'"
  echo "export TOOLS_GECKO_SHA256='$(sha256 "$GECKO_BIN")'"
  echo "export TOOLS_AS_VERSION='$asv'"
  echo "export TOOLS_AS_SHA256='$(sha256 "$DKP_BIN/powerpc-eabi-as")'"
  echo "export TOOLS_OBJCOPY_SHA256='$(sha256 "$DKP_BIN/powerpc-eabi-objcopy")'"
fi
