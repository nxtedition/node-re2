#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")"

PLATFORM=linux/amd64
IMAGE_ID_FILE=$(mktemp "${TMPDIR:-/tmp}/node-re2-prebuild.XXXXXX")
CONTAINER_ID=

cleanup() {
  if [ -n "$CONTAINER_ID" ]; then
    docker rm "$CONTAINER_ID" > /dev/null 2>&1 || true
  fi
  rm -f "$IMAGE_ID_FILE"
}
trap cleanup EXIT

if [ ! -e vendor/abseil-cpp/.git ] || [ ! -e vendor/re2/.git ]; then
  git submodule update --init --recursive
fi

DOCKER_ARGS=(--platform "$PLATFORM")
if [ -n "${JOBS:-}" ]; then
  DOCKER_ARGS+=(--build-arg "JOBS=$JOBS")
fi

docker build \
  "${DOCKER_ARGS[@]}" \
  --iidfile "$IMAGE_ID_FILE" \
  .

IMAGE_ID=$(cat "$IMAGE_ID_FILE")
CONTAINER_ID=$(docker create --platform "$PLATFORM" "$IMAGE_ID")

rm -rf prebuilds/linux-x64
mkdir -p prebuilds
docker cp "$CONTAINER_ID:/node-re2/prebuilds/linux-x64" prebuilds/

echo "Built prebuilds/linux-x64."
