# @nxtedition/re2

Native [RE2](https://github.com/google/re2) partial matching for Node.js 26 and later. It exposes a small API for matching one expression or many expressions at once without backtracking.

```js
import { RE2, RE2Set } from '@nxtedition/re2'

const expression = new RE2('foo')
expression.test(Buffer.from('before foo after')) // true
expression.testManySync([Buffer.from('foo'), Buffer.from('bar')]) // [true, false]
await expression.testManyAsync([Buffer.from('foo'), Buffer.from('bar')]) // [true, false]

const expressions = new RE2Set(['foo', 'bar'])
expressions.test(Buffer.from('bar')) // [1]
expressions.testManySync([Buffer.from('foo'), Buffer.from('bar')]) // [[0], [1]]
await expressions.testManyAsync([Buffer.from('foo'), Buffer.from('bar')]) // [[0], [1]]

const asyncExpressions = await RE2Set.compileAsync(['foo', 'bar'])
asyncExpressions.test(Buffer.from('foo')) // [0]
```

Patterns may be strings, Buffers, TypedArrays, or DataViews. Input must be a Buffer, TypedArray, or DataView. Both APIs operate on bytes; optional `byteOffset` and `byteLength` values select the input range. Negative values clamp to zero, values past the view clamp to its bounds, and fractional values are truncated.

`testManySync()` blocks until matching completes. Without intra-call parallelism it runs inline; with parallelism the caller joins the OpenMP team and blocks until every chunk completes. It borrows the supplied views for the duration of the call, so their backing stores must not be mutated, resized, transferred, or detached concurrently. `testManyAsync()` returns a Promise and performs matching from the Node.js worker pool, optionally entering OpenMP from that worker. It snapshots all input bytes before returning by default, so later mutation, resizing, transfer, or detachment cannot affect the result. `{ unsafe: true }` skips that copy; every backing store must then remain attached, the same size, and unmodified until the Promise settles. SharedArrayBuffer-backed views follow the same rule. `testMany()` remains a synchronous compatibility alias for `testManySync()`.

All batch methods accept up to 1,048,576 inputs and preserve their outer result order. Their optional `batchSize` is the number of inputs per native scheduling chunk. `Infinity` or a value greater than or equal to the input count disables intra-call OpenMP: synchronous matching stays on the caller thread, while asynchronous matching stays on one Node.js worker. Omitting `batchSize` keeps automatic scheduling at about 16 chunks per requested OpenMP thread.

The published Linux x64 prebuild uses GNU OpenMP when a batch contains enough input bytes to amortize dispatch. It requests at most half of the container-affinity CPUs, including the initiating thread, and respects the OpenMP runtime thread limit. Concurrent Node.js environments using the same addon instance share one submission lock to avoid oversubscribing that limit; libgomp owns the process-wide worker lifecycle. The prebuild dynamically links `libgomp.so.1`; Debian and Ubuntu provide it in the `libgomp1` package. Source builds, macOS, and other platforms match each batch sequentially regardless of `batchSize`; `testManyAsync()` still moves that sequential work to a Node.js worker.

Invalid RE2 syntax throws during synchronous construction and rejects `RE2Set.compileAsync()`. A pattern is limited to 16 MiB; a set accepts at most 100,000 patterns and 16 MiB of aggregate pattern bytes. The asynchronous API snapshots pattern bytes before returning, compiles on the Node.js worker pool, and resolves to a normal `RE2Set`.

Async compilations are cached by the complete ordered pattern bytes. Concurrent calls for the same pattern set share one in-flight compilation, and the native addon retains recent compiled sets in a bounded process-wide cache shared across Node.js Workers. Compiled sets use shared ownership so cache eviction and Worker teardown cannot invalidate another Worker's live set. Failed compilations are not cached. `RE2Set#test()` returns every matching pattern index, or `[]` when nothing matches; index order is unspecified.

## Benchmark

Run `npm run benchmark` to compare scalar, single-threaded, automatic, and explicitly batched synchronous matching. The harness verifies every candidate, independently confirms the fastest scheduling mode, reports requested and observed OpenMP threads, and measures safe/unsafe asynchronous admission plus settlement time. It also measures cold, cached, and deduplicated `compileAsync()` calls. Use `npm run benchmark:prebuild` to force the packaged prebuild. Input count, input bytes, iterations, set size, and warmups can be configured with command-line options; run `node benchmark.js --help` for details.

## Prebuilds

Release tarballs include Node-API prebuilds for `darwin-arm64` and glibc `linux-x64`. The Linux binary is built in the Node 26 Bookworm image with `-march=znver3 -mtune=znver3` by default, enabling AVX2 and targeting Zen 3-compatible deployment CPUs. Set `RE2_LEVEL_MARCH` when running `./build.sh` to select another CPU, or set `RE2_LEVEL_MARCH=` explicitly for a portable x86-64 artifact. The prebuild is compatible with Bookworm and newer glibc systems, and is libc-tagged so it is never selected on musl. Other platforms fall back to a portable source build when install scripts are enabled; Linux x64 source builds can opt into the same tuning with `RE2_LEVEL_MARCH=znver3`.

`./build.sh` builds and tests the Linux prebuild in Docker, validates its exact artifact manifest, and installs it transactionally. From a clean, up-to-date `main` checkout on an arm64 Mac, `yarn release [patch|minor|major|x.y.z]` builds and tests the Linux and Darwin prebuilds before changing the version, verifies the npm tarball's exact native-artifact manifest, and only then versions, publishes, and pushes the release. Both platform builds stage their candidate and restore the prior artifact if generation, validation, or installation fails.

## Development

```sh
npm install
npm test
```

`npm test` always rebuilds the native addon before running the JavaScript and TypeScript contract tests.
