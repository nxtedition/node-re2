#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")"

BRANCH=$(git branch --show-current)
if [ "$BRANCH" != main ]; then
  echo "Releases must be run from main (current branch: ${BRANCH:-detached HEAD})." >&2
  exit 1
fi

if ! npm whoami --registry https://registry.npmjs.org > /dev/null 2>&1; then
  echo "Not logged in to npm, run 'npm login' first." >&2
  exit 1
fi

if [ -n "$(git status --porcelain)" ]; then
  echo "Working tree is not clean, commit or stash changes first." >&2
  exit 1
fi

echo "Fetching origin..."
git fetch origin "$BRANCH"

LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse "origin/$BRANCH")
BASE=$(git merge-base HEAD "origin/$BRANCH")

if [ "$LOCAL" = "$REMOTE" ]; then
  : # up to date
elif [ "$LOCAL" = "$BASE" ]; then
  echo "Branch '$BRANCH' is behind origin, pull the latest changes first." >&2
  exit 1
elif [ "$REMOTE" = "$BASE" ]; then
  : # local is ahead, fine to push
else
  echo "Branch '$BRANCH' has diverged from origin, reconcile before releasing." >&2
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

# Public artifacts must not inherit local compiler flags or gyp overrides.
# With no override, build.sh uses the Dockerfile's audited Zen 3 default.
unset RE2_LEVEL_MARCH
export NODE_RE2_OPENMP=0
unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS GYP_DEFINES

echo "Building linux-x64 prebuild (Docker)..."
./build.sh

echo "Building darwin-arm64 prebuild (Node $NODE_TARGET)..."
export RE2_LEVEL_MARCH=
JOBS=${JOBS:-8} ./scripts/build-darwin-prebuild.sh "$NODE_TARGET"

echo "Building TypeScript package outputs..."
npm run build:ts --silent

echo "Checking release prebuild manifest..."
npm run verify:prebuilds

BUMP=${1:-}
if [ -z "$BUMP" ]; then
  read -r -p "Version bump (patch/minor/major or explicit version): " BUMP
fi
if [[ "$BUMP" != patch && "$BUMP" != minor && "$BUMP" != major && \
  ! "$BUMP" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Invalid version: '$BUMP' (expected patch, minor, major or an explicit version like 1.2.3)." >&2
  exit 1
fi

npm version "$BUMP" --git-tag-version=true --tag-version-prefix=v
VERSION=$(node -p "require('./package.json').version")
TAG="v$VERSION"
TAG_COMMIT=$(git rev-parse --verify "$TAG^{}" 2> /dev/null || true)
HEAD_COMMIT=$(git rev-parse HEAD)
if [ "$TAG_COMMIT" != "$HEAD_COMMIT" ]; then
  echo "npm version did not create the expected tag '$TAG' at HEAD." >&2
  exit 1
fi

npm publish \
  --access public \
  --dry-run=false \
  --tag latest \
  --registry https://registry.npmjs.org
git push --atomic origin "HEAD:refs/heads/$BRANCH" "refs/tags/$TAG:refs/tags/$TAG"

echo "Published $VERSION."
