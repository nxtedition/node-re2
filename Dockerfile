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

# Linux prebuilds target AMD Zen 3 by default. build.sh forwards an explicitly
# set RE2_LEVEL_MARCH so private builds can select another CPU or use an empty
# value for a portable x86-64 artifact.
ARG RE2_LEVEL_MARCH=znver3

RUN npm run build:ts && \
  RE2_LEVEL_MARCH="$RE2_LEVEL_MARCH" NODE_RE2_OPENMP=1 JOBS=$JOBS npx prebuildify \
  -t "$(node -p process.versions.node)" \
  --napi \
  --strip \
  --tag-libc \
  --arch x64

# The published Linux binary intentionally relies on Debian's GNU OpenMP
# runtime instead of embedding another thread-pool implementation.
RUN ldd prebuilds/linux-x64/@nxtedition+re2.glibc.node | grep -E 'libgomp[.]so[.]1 => /'

# PREBUILDS_ONLY makes node-gyp-build ignore build/Release, proving that the
# binary which will be embedded in the npm tarball is independently loadable.
RUN npm run test:prebuild

FROM scratch AS artifact

COPY --from=build /node-re2/prebuilds/linux-x64/@nxtedition+re2.glibc.node /

# Keep an unqualified Docker build runnable for development and benchmarks.
FROM build AS tested
