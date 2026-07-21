import assert from 'node:assert/strict'
import os from 'node:os'
import { performance } from 'node:perf_hooks'
import { parseArgs } from 'node:util'

import { RE2, RE2Set } from '@nxtedition/re2'
import binding from './lib/binding.js'

const { values } = parseArgs({
  options: {
    help: { type: 'boolean', short: 'h' },
    'input-count': { type: 'string', default: '4096' },
    'input-bytes': { type: 'string', default: '4096' },
    iterations: { type: 'string', default: '10' },
    'set-size': { type: 'string', default: '256' },
    warmup: { type: 'string', default: '3' },
  },
})

if (values.help) {
  console.log(`Usage: node benchmark.js [options]

  --input-count <number>  inputs per matching run (default: 4096)
  --input-bytes <number>  requested bytes per input (default: 4096)
  --iterations <number>   measured matching runs per mode (default: 10)
  --set-size <number>     patterns in RE2Set (default: 256)
  --warmup <number>       warmup matching runs (default: 3)`)
  process.exit(0)
}

function positiveInteger(name, value) {
  const number = Number(value)
  if (!Number.isSafeInteger(number) || number <= 0) {
    throw new TypeError(`${name} must be a positive integer`)
  }
  return number
}

const inputCount = positiveInteger('--input-count', values['input-count'])
const inputBytes = positiveInteger('--input-bytes', values['input-bytes'])
const iterations = positiveInteger('--iterations', values.iterations)
const setSize = positiveInteger('--set-size', values['set-size'])
const warmup = positiveInteger('--warmup', values.warmup)

const marker = (index) => `match-${String(index % setSize).padStart(6, '0')}`
const paddingLength = Math.max(inputBytes - marker(setSize - 1).length, 0)
const padding = 'x'.repeat(paddingLength)
const setInputs = Array.from({ length: inputCount }, (_, index) =>
  Buffer.from(
    index % 2 === 0
      ? `${padding}${marker(index)}`
      : `${padding}miss--${String(index % setSize).padStart(6, '0')}`,
  ),
)
const scanMatch = 'a1'.repeat(Math.ceil(inputBytes / 2)).slice(0, inputBytes)
const scanMiss = 'a'.repeat(inputBytes)
const scanInputs = Array.from({ length: inputCount }, (_, index) =>
  Buffer.from(index % 2 === 0 ? scanMatch : scanMiss),
)
const patterns = Array.from(
  { length: setSize },
  (_, index) => `match-${String(index).padStart(6, '0')}$`,
)

const literalRegex = new RE2('match-[0-9]{6}$')
const earlyFailRegex = new RE2('^z')
const scanRegex = new RE2('^(?:[a-z]{1,8}[0-9])+$')
const set = new RE2Set(patterns)
const expectedLiteralRegex = setInputs.map((input) => literalRegex.test(input))
const expectedEarlyFailRegex = scanInputs.map((input) =>
  earlyFailRegex.test(input),
)
const expectedScanRegex = scanInputs.map((input) => scanRegex.test(input))
const expectedSet = setInputs.map((input) =>
  set.test(input).toSorted((left, right) => left - right),
)
const firstBatchStart = performance.now()
const firstBatchResult = scanRegex.testManySync(scanInputs)
const firstBatchMs = performance.now() - firstBatchStart
assert.deepEqual(literalRegex.testManySync(setInputs), expectedLiteralRegex)
assert.deepEqual(
  earlyFailRegex.testManySync(scanInputs),
  expectedEarlyFailRegex,
)
assert.deepEqual(firstBatchResult, expectedScanRegex)
assert.deepEqual(
  set
    .testManySync(setInputs)
    .map((indices) => indices.toSorted((left, right) => left - right)),
  expectedSet,
)

const batchBytes = (inputs) =>
  inputs.reduce((total, input) => total + input.byteLength, 0)

let sink = 0
function consume(results) {
  for (const result of results) {
    sink += Array.isArray(result) ? result.length : Number(result)
  }
}

function median(numbers) {
  const sorted = numbers.toSorted((left, right) => left - right)
  const middle = Math.floor(sorted.length / 2)
  return sorted.length % 2 === 0
    ? (sorted[middle - 1] + sorted[middle]) / 2
    : sorted[middle]
}

function measureInterleaved(runs) {
  for (let index = 0; index < warmup; ++index) {
    for (let offset = 0; offset < runs.length; ++offset) {
      consume(runs[(index + offset) % runs.length].run())
    }
  }

  const samples = new Map(runs.map(({ name }) => [name, []]))
  for (let index = 0; index < iterations; ++index) {
    for (let offset = 0; offset < runs.length; ++offset) {
      const { name, run } = runs[(index + offset) % runs.length]
      const start = performance.now()
      const result = run()
      samples.get(name).push(performance.now() - start)
      consume(result)
    }
  }
  return new Map([...samples].map(([name, values]) => [name, median(values)]))
}

async function measureAsyncInterleaved(runs) {
  for (let index = 0; index < warmup; ++index) {
    for (let offset = 0; offset < runs.length; ++offset) {
      consume(await runs[(index + offset) % runs.length].run())
    }
  }

  const samples = new Map(
    runs.map(({ name }) => [name, { admission: [], settlement: [] }]),
  )
  for (let index = 0; index < iterations; ++index) {
    for (let offset = 0; offset < runs.length; ++offset) {
      const { name, run } = runs[(index + offset) % runs.length]
      const start = performance.now()
      const promise = run()
      const admissionMs = performance.now() - start
      const result = await promise
      const settlementMs = performance.now() - start
      samples.get(name).admission.push(admissionMs)
      samples.get(name).settlement.push(settlementMs)
      consume(result)
    }
  }
  return new Map(
    [...samples].map(([name, values]) => [
      name,
      {
        admissionMs: median(values.admission),
        settlementMs: median(values.settlement),
      },
    ]),
  )
}

function schedulingBatchSizes(inputs) {
  const totalBytes = batchBytes(inputs)
  const automaticThreads = binding.batch_parallelism(inputs.length, totalBytes)
  if (automaticThreads < 2) {
    return []
  }

  const candidates = new Set()
  for (let size = 1; size < inputs.length; size *= 2) {
    candidates.add(size)
  }
  candidates.add(
    Math.max(Math.ceil(inputs.length / (automaticThreads * 16)), 1),
  )
  return [...candidates]
    .filter((size) => size < inputs.length)
    .sort((left, right) => left - right)
}

function batchOptions(batchSize) {
  return batchSize === undefined ? undefined : { batchSize }
}

function parallelism(inputs, batchSize) {
  const totalBytes = batchBytes(inputs)
  return {
    requested: binding.batch_parallelism(inputs.length, totalBytes, batchSize),
    observed: binding.batch_observed_parallelism(
      inputs.length,
      totalBytes,
      batchSize,
    ),
  }
}

function automaticBatchSize(inputs) {
  const { requested } = parallelism(inputs, undefined)
  return Math.ceil(inputs.length / Math.min(inputs.length, requested * 16))
}

function benchmarkMatch(
  operation,
  inputs,
  expected,
  scalar,
  batch,
  asyncBatch,
) {
  const candidates = schedulingBatchSizes(inputs)
  for (const batchSize of candidates) {
    assert.deepEqual(batch(batchSize), expected)
  }
  const sweep = measureInterleaved(
    candidates.map((batchSize, index) => ({
      name: String(index),
      run: () => batch(batchSize),
    })),
  )
  let candidateBatchSize = candidates[0] ?? Infinity
  let candidateSweepMs = sweep.get('0') ?? Infinity
  for (let index = 1; index < candidates.length; ++index) {
    const candidateMs = sweep.get(String(index))
    if (candidateMs < candidateSweepMs) {
      candidateBatchSize = candidates[index]
      candidateSweepMs = candidateMs
    }
  }

  const finalRuns = [
    { name: 'scalar', run: scalar },
    { name: 'single', run: () => batch(Infinity) },
    { name: 'auto', run: () => batch(undefined) },
  ]
  if (candidateBatchSize !== Infinity) {
    finalRuns.push({ name: 'candidate', run: () => batch(candidateBatchSize) })
  }
  assert.deepEqual(batch(Infinity), expected)
  assert.deepEqual(batch(undefined), expected)
  assert.deepEqual(batch(candidateBatchSize), expected)
  const final = measureInterleaved(finalRuns)
  const finalCandidates = [['Infinity', Infinity, final.get('single')]]
  if (parallelism(inputs, undefined).requested > 1) {
    finalCandidates.push(['auto', undefined, final.get('auto')])
  }
  if (candidateBatchSize !== Infinity) {
    finalCandidates.push([
      'explicit',
      candidateBatchSize,
      final.get('candidate'),
    ])
  }
  const [optimalScheduling, optimalBatchSize, optimalMs] =
    finalCandidates.reduce((best, candidate) =>
      candidate[2] < best[2] ? candidate : best,
    )
  return {
    operation,
    inputs,
    expected,
    batch,
    asyncBatch,
    batchSize: optimalBatchSize,
    optimalScheduling,
    automaticBatchSize: automaticBatchSize(inputs),
    singleParallelism: parallelism(inputs, Infinity),
    autoParallelism: parallelism(inputs, undefined),
    tunedParallelism: parallelism(inputs, optimalBatchSize),
    scalarMs: final.get('scalar'),
    singleThreadMs: final.get('single'),
    autoMs: final.get('auto'),
    tunedMs: optimalMs,
  }
}

const matchResults = [
  benchmarkMatch(
    'RE2 (literal suffix)',
    setInputs,
    expectedLiteralRegex,
    () => setInputs.map((input) => literalRegex.test(input)),
    (batchSize) =>
      literalRegex.testManySync(setInputs, batchOptions(batchSize)),
    (batchSize, unsafe) =>
      literalRegex.testManyAsync(setInputs, {
        ...batchOptions(batchSize),
        unsafe,
      }),
  ),
  benchmarkMatch(
    'RE2 (anchored early fail)',
    scanInputs,
    expectedEarlyFailRegex,
    () => scanInputs.map((input) => earlyFailRegex.test(input)),
    (batchSize) =>
      earlyFailRegex.testManySync(scanInputs, batchOptions(batchSize)),
    (batchSize, unsafe) =>
      earlyFailRegex.testManyAsync(scanInputs, {
        ...batchOptions(batchSize),
        unsafe,
      }),
  ),
  benchmarkMatch(
    'RE2 (scan-heavy)',
    scanInputs,
    expectedScanRegex,
    () => scanInputs.map((input) => scanRegex.test(input)),
    (batchSize) => scanRegex.testManySync(scanInputs, batchOptions(batchSize)),
    (batchSize, unsafe) =>
      scanRegex.testManyAsync(scanInputs, {
        ...batchOptions(batchSize),
        unsafe,
      }),
  ),
  benchmarkMatch(
    'RE2Set',
    setInputs,
    expectedSet,
    () => setInputs.map((input) => set.test(input)),
    (batchSize) =>
      set
        .testManySync(setInputs, batchOptions(batchSize))
        .map((indices) => indices.toSorted((left, right) => left - right)),
    (batchSize, unsafe) =>
      set
        .testManyAsync(setInputs, { ...batchOptions(batchSize), unsafe })
        .then((results) =>
          results.map((indices) =>
            indices.toSorted((left, right) => left - right),
          ),
        ),
  ),
]

const asyncResults = []
for (const result of matchResults) {
  const batchSizes = [
    ['single', Infinity],
    ['auto', undefined],
    ['optimal', result.batchSize],
  ]
  const runs = []
  for (const [scheduling, batchSize] of batchSizes) {
    for (const unsafe of [false, true]) {
      const name = `${scheduling}-${unsafe ? 'unsafe' : 'safe'}`
      const run = () =>
        result.asyncBatch(batchSize, unsafe).then((values) => {
          assert.deepEqual(values, result.expected)
          return values
        })
      assert.deepEqual(
        await result.asyncBatch(batchSize, unsafe),
        result.expected,
      )
      runs.push({ name, run })
    }
  }
  const measured = await measureAsyncInterleaved(runs)
  for (const [scheduling, batchSize] of batchSizes) {
    for (const unsafe of [false, true]) {
      const timing = measured.get(`${scheduling}-${unsafe ? 'unsafe' : 'safe'}`)
      asyncResults.push({
        operation: result.operation,
        scheduling,
        batchSize,
        unsafe,
        ...parallelism(result.inputs, batchSize),
        ...timing,
      })
    }
  }
}

async function measureAsync(run) {
  const start = performance.now()
  const promise = run()
  const admissionMs = performance.now() - start
  await promise
  return { admissionMs, totalMs: performance.now() - start }
}

const compilePatterns = Array.from(
  { length: Math.max(setSize * 8, 10_000) },
  (_, index) => `^compile-benchmark-${process.pid}-${index}$`,
)
const coldCompile = await measureAsync(() =>
  RE2Set.compileAsync(compilePatterns),
)
const cacheHit = await measureAsync(() => RE2Set.compileAsync(compilePatterns))
const dedupePatterns = compilePatterns.map(
  (_, index) => `^dedupe-benchmark-${process.pid}-${index}$`,
)
const dedupeCalls = Math.min(os.availableParallelism(), 8)
const beforeDedupe = binding.set_compile_cache_stats()
const dedupe = await measureAsync(() =>
  Promise.all(
    Array.from({ length: dedupeCalls }, () =>
      RE2Set.compileAsync(dedupePatterns),
    ),
  ),
)
const afterDedupe = binding.set_compile_cache_stats()

console.log('# @nxtedition/re2 benchmark')
console.log()
console.log(
  `Node ${process.version}, ${process.platform} ${process.arch}, ${os.cpus()[0]?.model ?? 'unknown CPU'}, ${os.availableParallelism()} available CPUs`,
)
console.log(
  `${inputCount.toLocaleString('en-US')} inputs x ${inputBytes.toLocaleString('en-US')} requested bytes, ${setSize.toLocaleString('en-US')} set patterns, median of ${iterations} runs after ${warmup} warmups`,
)
console.log(
  `First scan-heavy testManySync call: ${firstBatchMs.toFixed(2)} ms (${binding.batch_parallelism(scanInputs.length, batchBytes(scanInputs))} requested threads; first OpenMP dispatch when greater than one)`,
)
console.log(
  `Explicit batch-size sweep: ${schedulingBatchSizes(scanInputs).join(', ') || 'none (sequential build or workload)'}`,
)
console.log()
console.log(
  '| operation | optimal scheduling | scalar loop | sync Infinity (requested/observed) | sync auto (requested/observed) | sync optimal (requested/observed) | optimal vs scalar | optimal vs Infinity | optimal throughput |',
)
console.log('| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |')
for (const result of matchResults) {
  const throughput = (inputCount / result.tunedMs) * 1_000
  console.log(
    `| ${result.operation} | ${result.optimalScheduling === 'auto' ? `auto (batch ${result.automaticBatchSize})` : result.batchSize === Infinity ? 'Infinity' : `batch ${result.batchSize}`} | ${result.scalarMs.toFixed(2)} ms | ${result.singleThreadMs.toFixed(2)} ms (${result.singleParallelism.requested}/${result.singleParallelism.observed}) | ${result.autoMs.toFixed(2)} ms (${result.autoParallelism.requested}/${result.autoParallelism.observed}) | ${result.tunedMs.toFixed(2)} ms (${result.tunedParallelism.requested}/${result.tunedParallelism.observed}) | ${(result.scalarMs / result.tunedMs).toFixed(2)}x | ${(result.singleThreadMs / result.tunedMs).toFixed(2)}x | ${Math.round(throughput).toLocaleString('en-US')} inputs/s |`,
  )
}
console.log()
console.log(
  'Requested/observed is the planner thread count followed by the OpenMP team size observed in a probe region.',
)
console.log()
console.log(
  '| async operation | scheduling | input ownership | requested/observed threads | admission | total to settle |',
)
console.log('| --- | --- | --- | ---: | ---: | ---: |')
for (const result of asyncResults) {
  console.log(
    `| ${result.operation} | ${result.scheduling} (${result.batchSize === undefined ? 'omitted' : result.batchSize === Infinity ? 'Infinity' : result.batchSize}) | ${result.unsafe ? 'unsafe borrowed' : 'safe snapshot'} | ${result.requested}/${result.observed} | ${result.admissionMs.toFixed(2)} ms | ${result.settlementMs.toFixed(2)} ms |`,
  )
}
console.log()
console.log('| compileAsync operation | admission | total to settle |')
console.log('| --- | ---: | ---: |')
console.log(
  `| cold compile (${compilePatterns.length.toLocaleString('en-US')} patterns) | ${coldCompile.admissionMs.toFixed(2)} ms | ${coldCompile.totalMs.toFixed(2)} ms |`,
)
console.log(
  `| native cache hit | ${cacheHit.admissionMs.toFixed(2)} ms | ${cacheHit.totalMs.toFixed(2)} ms |`,
)
console.log(
  `| ${dedupeCalls} concurrent calls, ${afterDedupe.compilations - beforeDedupe.compilations} compilation, ${afterDedupe.deduplications - beforeDedupe.deduplications} in-flight deduplications | ${dedupe.admissionMs.toFixed(2)} ms | ${dedupe.totalMs.toFixed(2)} ms |`,
)

// Keep V8 from treating the benchmark results as unused.
if (sink === -1) {
  console.log(sink)
}
