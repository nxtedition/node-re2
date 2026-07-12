# @nxtedition/re2

Native [RE2](https://github.com/google/re2) partial matching for Node.js 26 and later. It exposes a small API for matching one expression or many expressions at once without backtracking.

```js
import { RE2, RE2Set } from '@nxtedition/re2'

const expression = new RE2('foo')
expression.test(Buffer.from('before foo after')) // true

const expressions = new RE2Set(['foo', 'bar'])
expressions.test(Buffer.from('bar')) // [1]
```

Patterns may be strings, Buffers, TypedArrays, or DataViews. Input must be a Buffer, TypedArray, or DataView. SharedArrayBuffer-backed views are supported. Both APIs operate on bytes; optional `byteOffset` and `byteLength` values select the input range. Negative values clamp to zero, values past the view clamp to its bounds, and fractional values are truncated.

Invalid RE2 syntax throws during construction. `RE2Set#test()` returns every matching pattern index, or `[]` when nothing matches; index order is unspecified.

## Prebuilds

Release tarballs include Node-API prebuilds for `darwin-arm64` and glibc `linux-x64`. The Linux binary is built in the Node 26 Bookworm image for compatibility with both Bookworm and newer glibc systems, and is libc-tagged so it is never selected on musl. Other platforms fall back to a source build when install scripts are enabled.

`./build.sh` builds and tests the Linux prebuild in Docker. On an arm64 Mac, `./release.sh` builds both supported platforms, tests the current platform with source-build fallback disabled, verifies that both binaries are present in the npm tarball, and then prompts for the version bump before publishing.

## Development

```sh
npm install
npm test
```

`npm test` always rebuilds the native addon before running the JavaScript and TypeScript contract tests.
