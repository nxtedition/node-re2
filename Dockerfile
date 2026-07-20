FROM node:26.5.0-bookworm AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
  build-essential \
  ca-certificates \
  python3 \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /node-re2

COPY package*.json ./
RUN npm ci --ignore-scripts

COPY . .

ARG JOBS=8
# Match the deployment RocksDB build: the shipped x64 binary targets Zen 3,
# enabling AVX2 while local source builds remain portable by leaving this unset.
RUN npm run build:ts && \
  CXXFLAGS=-DNODE_RE2_PARALLEL=1 NODE_RE2_MARCH=znver3 JOBS=$JOBS npx prebuildify \
  -t "$(node -p process.versions.node)" \
  --napi \
  --strip \
  --tag-libc \
  --arch x64

# PREBUILDS_ONLY makes node-gyp-build ignore build/Release, proving that the
# binary which will be embedded in the npm tarball is independently loadable.
RUN npm run test:prebuild

FROM scratch AS artifact

COPY --from=build /node-re2/prebuilds/linux-x64/@nxtedition+re2.glibc.node /
