#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")"

PLATFORM=linux/amd64
TARGET_DIR=prebuilds/linux-x64
ADDON=@nxtedition+re2.glibc.node
STAGE_DIR=
BACKUP_ROOT=
BACKUP_DIR=
COMMITTED=0
HAD_TARGET=0

if [ -e "$TARGET_DIR" ] || [ -L "$TARGET_DIR" ]; then
  HAD_TARGET=1
fi

cleanup_resources() {
  local cleanup_status=0
  local preserve_backup=0

  if [ -n "$STAGE_DIR" ]; then
    if rm -rf "$STAGE_DIR"; then
      STAGE_DIR=
    else
      cleanup_status=1
    fi
  fi

  if [ -n "$BACKUP_DIR" ] && { [ -e "$BACKUP_DIR" ] || [ -L "$BACKUP_DIR" ]; }; then
    if [ "$COMMITTED" -eq 1 ]; then
      :
    else
      if { [ -e "$TARGET_DIR" ] || [ -L "$TARGET_DIR" ]; } && ! rm -rf "$TARGET_DIR"; then
        preserve_backup=1
      elif ! mv "$BACKUP_DIR" "$TARGET_DIR"; then
        preserve_backup=1
      fi
    fi
  fi

  if [ "$preserve_backup" -eq 1 ]; then
    echo "Could not restore the prior Linux prebuild; preserved it at $BACKUP_DIR." >&2
    cleanup_status=1
  elif [ -n "$BACKUP_ROOT" ]; then
    if rm -rf "$BACKUP_ROOT"; then
      BACKUP_ROOT=
      BACKUP_DIR=
    else
      cleanup_status=1
    fi
  fi

  if [ "$COMMITTED" -eq 0 ] && [ "$HAD_TARGET" -eq 0 ] && \
    { [ -e "$TARGET_DIR" ] || [ -L "$TARGET_DIR" ]; }; then
    if ! rm -rf "$TARGET_DIR"; then
      echo "Could not remove the uncommitted Linux prebuild at $TARGET_DIR; remove it manually." >&2
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

if [ ! -e vendor/abseil-cpp/.git ] || [ ! -e vendor/re2/.git ]; then
  git submodule update --init --recursive
fi

mkdir -p prebuilds
STAGE_DIR=$(mktemp -d "prebuilds/.linux-x64.XXXXXX")

DOCKER_ARGS=(--platform "$PLATFORM")
if [ -n "${JOBS:-}" ]; then
  DOCKER_ARGS+=(--build-arg "JOBS=$JOBS")
fi

DOCKER_BUILDKIT=1 docker build \
  "${DOCKER_ARGS[@]}" \
  --target artifact \
  --output "type=local,dest=$STAGE_DIR" \
  .

EXPECTED_PREBUILD="$STAGE_DIR/$ADDON"
ENTRY_COUNT=$(find "$STAGE_DIR" -mindepth 1 -maxdepth 1 -print | wc -l | tr -d ' ')
if [ ! -f "$EXPECTED_PREBUILD" ] || [ -L "$EXPECTED_PREBUILD" ] || [ "$ENTRY_COUNT" -ne 1 ]; then
  echo "Expected exactly one Linux prebuild named $ADDON." >&2
  exit 1
fi
chmod 0755 "$STAGE_DIR"

if [ -e "$TARGET_DIR" ] || [ -L "$TARGET_DIR" ]; then
  BACKUP_ROOT=$(mktemp -d "prebuilds/.linux-x64-backup.XXXXXX")
  BACKUP_DIR="$BACKUP_ROOT/linux-x64"
  mv "$TARGET_DIR" "$BACKUP_DIR"
fi

if ! mv "$STAGE_DIR" "$TARGET_DIR"; then
  echo "Could not install the staged Linux prebuild." >&2
  exit 1
fi
COMMITTED=1
STAGE_DIR=

if ! cleanup_resources; then
  trap - EXIT INT TERM
  exit 1
fi
trap - EXIT INT TERM

echo "Built prebuilds/linux-x64."
