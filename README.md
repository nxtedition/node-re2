# @nxtedition/re2

Native [RE2](https://github.com/google/re2) partial matching for Node.js 26 and later. It exposes a small API for matching one expression or many expressions at once without backtracking.

```js
import { RE2, RE2Set } from '@nxtedition/re2'

const expression = new RE2('foo')
expression.test(Buffer.from('before foo after')) // true
expression.testMany([Buffer.from('foo'), Buffer.from('bar')]) // [true, false]
expression.testMany([Buffer.from('foo'), Buffer.from('bar')], { batchSize: Infinity }) // caller thread only

const expressions = new RE2Set(['foo', 'bar'])
expressions.test(Buffer.from('bar')) // [1]
expressions.testMany([Buffer.from('foo'), Buffer.from('bar')]) // [[0], [1]]

const asyncExpressions = await RE2Set.compileAsync(['foo', 'bar'])
asyncExpressions.test(Buffer.from('foo')) // [0]
```

Patterns may be strings, Buffers, TypedArrays, or DataViews. Input must be a Buffer, TypedArray, or DataView. SharedArrayBuffer-backed views are supported, but their bytes must not be mutated concurrently while a native call reads them. Both APIs operate on bytes; optional `byteOffset` and `byteLength` values select the input range. Negative values clamp to zero, values past the view clamp to its bounds, and fractional values are truncated. `testMany()` accepts up to 1,048,576 binary inputs. Its optional `batchSize` is the number of inputs per native scheduling chunk; `Infinity` or a value greater than or equal to the input count executes on the caller thread without waking the pool. Omitting it keeps automatic scheduling at about 16 chunks per participating thread.

The published Linux x64 prebuild uses a process-wide pool when a batch contains enough input bytes to amortize dispatch. It selects at most half of the container-affinity CPUs, including the caller, so matching leaves capacity for other work. Helpers block when idle and the pool shuts down after the last Node.js environment; source builds and other platforms match the batch sequentially regardless of `batchSize`.

Invalid RE2 syntax throws during synchronous construction and rejects `RE2Set.compileAsync()`. A pattern is limited to 16 MiB; a set accepts at most 100,000 patterns and 16 MiB of aggregate pattern bytes. The asynchronous API snapshots pattern bytes before returning, compiles on the Node.js worker pool, and resolves to a normal `RE2Set`.

Async compilations are cached by the complete ordered pattern bytes. Concurrent calls for the same pattern set share one in-flight compilation, and the native addon retains recent compiled sets in a bounded process-wide cache shared across Node.js Workers. Compiled sets use shared ownership so cache eviction and Worker teardown cannot invalidate another Worker's live set. Failed compilations are not cached. `RE2Set#test()` returns every matching pattern index, or `[]` when nothing matches; index order is unspecified.

## Benchmark

Run `npm run benchmark` to compare scalar and single-threaded matching with the fastest scheduling batch size from a bounded sweep, report the selected batch/thread count and cold-pool latency, and measure admission plus total settlement time for cold, cached, and deduplicated `compileAsync()` calls. Use `npm run benchmark:prebuild` to force the packaged prebuild. Input count, input bytes, iterations, set size, and warmups can be configured with command-line options; run `node benchmark.js --help` for details.

## Prebuilds

Release tarballs include Node-API prebuilds for `darwin-arm64` and glibc `linux-x64`. The Linux binary is built in the Node 26 Bookworm image with `-march=znver3 -mtune=znver3`, enabling AVX2 and targeting Zen 3-compatible deployment CPUs. It is compatible with Bookworm and newer glibc systems, and is libc-tagged so it is never selected on musl. Other platforms fall back to a portable source build when install scripts are enabled; Linux x64 source builds can opt into the same tuning with `NODE_RE2_MARCH=znver3`.

`./build.sh` builds and tests the Linux prebuild in Docker, validates its exact artifact manifest, and installs it transactionally. From a clean, up-to-date `main` checkout on an arm64 Mac, `yarn release [patch|minor|major|x.y.z]` builds and tests the Linux and Darwin prebuilds before changing the version, verifies the npm tarball's exact native-artifact manifest, and only then versions, publishes, and pushes the release. Both platform builds stage their candidate and restore the prior artifact if generation, validation, or installation fails.

## Development

```sh
npm install
npm test
```

`npm test` always rebuilds the native addon before running the JavaScript and TypeScript contract tests.
