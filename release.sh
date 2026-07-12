#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")"

if ! npm whoami --registry https://registry.npmjs.org > /dev/null 2>&1; then
  echo "Not logged in to npm; run 'npm login' first." >&2
  exit 1
fi

if [ -n "$(git status --porcelain)" ]; then
  echo "Working tree is not clean; commit or stash changes first." >&2
  exit 1
fi

BRANCH=$(git rev-parse --abbrev-ref HEAD)
git fetch origin "$BRANCH"

LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse "origin/$BRANCH")
BASE=$(git merge-base HEAD "origin/$BRANCH")

if [ "$LOCAL" = "$REMOTE" ] || [ "$REMOTE" = "$BASE" ]; then
  :
elif [ "$LOCAL" = "$BASE" ]; then
  echo "Branch '$BRANCH' is behind origin; pull first." >&2
  exit 1
else
  echo "Branch '$BRANCH' has diverged from origin; reconcile first." >&2
  exit 1
fi

NODE_TARGET=$(sed -n 's/^FROM node:\([0-9.]*\).*/\1/p' Dockerfile)
if [ -z "$NODE_TARGET" ]; then
  echo "Could not determine the Node target from Dockerfile." >&2
  exit 1
fi

if [ "$(uname -s)" != "Darwin" ] || [ "$(uname -m)" != "arm64" ]; then
  echo "The darwin-arm64 prebuild must be produced on an arm64 Mac." >&2
  exit 1
fi

rm -rf prebuilds

echo "Building linux-x64 prebuild (Docker)..."
./build.sh

echo "Building darwin-arm64 prebuild (Node $NODE_TARGET)..."
JOBS=${JOBS:-8} npx prebuildify \
  -t "$NODE_TARGET" \
  --napi \
  --strip \
  --arch arm64

npm run test:prebuild
npm run verify:prebuilds

read -r -p "Version bump (patch/minor/major): " BUMP
case "$BUMP" in
  patch | minor | major) ;;
  *)
    echo "Invalid bump: '$BUMP' (expected patch, minor, or major)." >&2
    exit 1
    ;;
esac

npm version "$BUMP"
npm publish --registry https://registry.npmjs.org
git push
git push --tags

echo "Published $(node -p "require('./package.json').version")."
