FROM node:26.5.0-bookworm

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
RUN JOBS=$JOBS npx prebuildify \
  -t "$(node -p process.versions.node)" \
  --napi \
  --strip \
  --tag-libc \
  --arch x64

# PREBUILDS_ONLY makes node-gyp-build ignore build/Release, proving that the
# binary which will be embedded in the npm tarball is independently loadable.
RUN npm run test:prebuild
