#!/bin/bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: build-darwin-prebuild.sh <node-version>" >&2
  exit 64
fi

cd "$(dirname "$0")/.."

PLATFORM=darwin-arm64
ADDON=@nxtedition+re2.node
TARGET_DIR="prebuilds/$PLATFORM"
OUT_DIR=
CANDIDATE_DIR=
BACKUP_ROOT=
BACKUP_DIR=
COMMITTED=0
INSTALLED=0

cleanup_resources() {
  local cleanup_status=0
  local preserve_backup=0

  if [ -n "$BACKUP_DIR" ] && { [ -e "$BACKUP_DIR" ] || [ -L "$BACKUP_DIR" ]; }; then
    if [ "$COMMITTED" -eq 1 ]; then
      :
    else
      if { [ -e "$TARGET_DIR" ] || [ -L "$TARGET_DIR" ]; } && ! rm -rf "$TARGET_DIR"; then
        preserve_backup=1
      elif ! mv "$BACKUP_DIR" "$TARGET_DIR"; then
        preserve_backup=1
      else
        INSTALLED=0
      fi
    fi
  fi

  if [ "$preserve_backup" -eq 1 ]; then
    echo "Could not restore the prior Darwin prebuild; preserved it at $BACKUP_DIR." >&2
    cleanup_status=1
  elif [ -n "$BACKUP_ROOT" ]; then
    if rm -rf "$BACKUP_ROOT"; then
      BACKUP_ROOT=
      BACKUP_DIR=
    else
      cleanup_status=1
    fi
  fi

  if [ "$COMMITTED" -eq 0 ] && [ "$INSTALLED" -eq 1 ] && \
    { [ -e "$TARGET_DIR" ] || [ -L "$TARGET_DIR" ]; }; then
    if ! rm -rf "$TARGET_DIR"; then
      echo "Could not remove the uncommitted Darwin prebuild at $TARGET_DIR." >&2
      cleanup_status=1
    fi
  fi

  if [ -n "$OUT_DIR" ]; then
    if rm -rf "$OUT_DIR"; then
      OUT_DIR=
    else
      cleanup_status=1
    fi
  fi

  return "$cleanup_status"
}

on_exit() {
  local status=$?
  trap - EXIT INT TERM
  if ! cleanup_resources && [ "$status" -eq 0 ]; then
    status=1
  fi
  exit "$status"
}

trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -p prebuilds
OUT_DIR=$(mktemp -d "prebuilds/.$PLATFORM-out.XXXXXX")
CANDIDATE_DIR="$OUT_DIR/prebuilds/$PLATFORM"

(
  unset CC CXX ARCHFLAGS MACOSX_DEPLOYMENT_TARGET SDKROOT
  unset PREBUILD_ARCH PREBUILD_PLATFORM PREBUILD_UV PREBUILD_ARMV PREBUILD_LIBC
  unset PREBUILD_STRIP PREBUILD_STRIP_BIN PREBUILD_NODE_GYP PREBUILD_SHELL
  CFLAGS= CPPFLAGS= CXXFLAGS= LDFLAGS= NODE_RE2_MARCH= GYP_DEFINES= JOBS="${JOBS:-8}" \
    npx prebuildify -t "$1" --napi --strip --arch arm64 --platform darwin --out "$OUT_DIR"
)

EXPECTED_PREBUILD="$CANDIDATE_DIR/$ADDON"
if [ ! -d "$CANDIDATE_DIR" ]; then
  echo "Expected prebuildify to create $CANDIDATE_DIR." >&2
  exit 1
fi

ENTRY_COUNT=$(find "$CANDIDATE_DIR" -mindepth 1 -maxdepth 1 -print | wc -l | tr -d ' ')
if [ ! -f "$EXPECTED_PREBUILD" ] || [ -L "$EXPECTED_PREBUILD" ] || [ "$ENTRY_COUNT" -ne 1 ]; then
  echo "Expected exactly one Darwin prebuild named $ADDON." >&2
  exit 1
fi

if [ -e "$TARGET_DIR" ] || [ -L "$TARGET_DIR" ]; then
  BACKUP_ROOT=$(mktemp -d "prebuilds/.$PLATFORM-backup.XXXXXX")
  BACKUP_DIR="$BACKUP_ROOT/$PLATFORM"
  mv "$TARGET_DIR" "$BACKUP_DIR"
fi

INSTALLED=1
if ! mv "$CANDIDATE_DIR" "$TARGET_DIR"; then
  echo "Could not install the staged Darwin prebuild." >&2
  exit 1
fi

# Keep rollback armed until the packaged prebuild has loaded and passed the
# complete runtime/type suite without falling back to build/Release.
PREBUILDS_ONLY=1 npm run test:prebuild
COMMITTED=1

if ! cleanup_resources; then
  trap - EXIT INT TERM
  exit 1
fi
trap - EXIT INT TERM
