import assert from 'node:assert/strict'
import os from 'node:os'
import { performance } from 'node:perf_hooks'
import { parseArgs } from 'node:util'

import { RE2, RE2Set } from '@nxtedition/re2'

const { values } = parseArgs({
  options: {
    'batch-size': { type: 'string', default: '4096' },
    help: { type: 'boolean', short: 'h' },
    'input-bytes': { type: 'string', default: '4096' },
    iterations: { type: 'string', default: '10' },
    'set-size': { type: 'string', default: '256' },
    warmup: { type: 'string', default: '3' },
  },
})

if (values.help) {
  console.log(`Usage: node benchmark.js [options]

  --batch-size <number>   inputs per matching run (default: 4096)
  --input-bytes <number>  requested bytes per input (default: 4096)
  --iterations <number>   measured matching runs (default: 10)
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

const batchSize = positiveInteger('--batch-size', values['batch-size'])
const inputBytes = positiveInteger('--input-bytes', values['input-bytes'])
const iterations = positiveInteger('--iterations', values.iterations)
const setSize = positiveInteger('--set-size', values['set-size'])
const warmup = positiveInteger('--warmup', values.warmup)

const marker = index => `match-${String(index % setSize).padStart(6, '0')}`
const paddingLength = Math.max(inputBytes - marker(setSize - 1).length, 0)
const padding = 'x'.repeat(paddingLength)
const setInputs = Array.from({ length: batchSize }, (_, index) =>
  Buffer.from(
    index % 2 === 0
      ? `${padding}${marker(index)}`
      : `${padding}miss--${String(index % setSize).padStart(6, '0')}`
  )
)
const scanMatch = 'a1'.repeat(Math.ceil(inputBytes / 2)).slice(0, inputBytes)
const scanMiss = 'a'.repeat(inputBytes)
const scanInputs = Array.from({ length: batchSize }, (_, index) =>
  Buffer.from(index % 2 === 0 ? scanMatch : scanMiss)
)
const patterns = Array.from(
  { length: setSize },
  (_, index) => `match-${String(index).padStart(6, '0')}$`
)

const literalRegex = new RE2('match-[0-9]{6}$')
const scanRegex = new RE2('^(?:[a-z]{1,8}[0-9])+$')
const set = new RE2Set(patterns)
const expectedLiteralRegex = setInputs.map(input => literalRegex.test(input))
const expectedScanRegex = scanInputs.map(input => scanRegex.test(input))
const expectedSet = setInputs.map(input => set.test(input).toSorted((left, right) => left - right))
assert.deepEqual(literalRegex.testMany(setInputs), expectedLiteralRegex)
assert.deepEqual(scanRegex.testMany(scanInputs), expectedScanRegex)
assert.deepEqual(
  set.testMany(setInputs).map(indices => indices.toSorted((left, right) => left - right)),
  expectedSet
)

let sink = 0
function consume(results) {
  for (const result of results) {
    sink += Array.isArray(result) ? result.length : Number(result)
  }
}

function median(numbers) {
  const sorted = numbers.toSorted((left, right) => left - right)
  const middle = Math.floor(sorted.length / 2)
  return sorted.length % 2 === 0 ? (sorted[middle - 1] + sorted[middle]) / 2 : sorted[middle]
}

function measure(run) {
  for (let index = 0; index < warmup; ++index) {
    consume(run())
  }

  const samples = []
  for (let index = 0; index < iterations; ++index) {
    const start = performance.now()
    const result = run()
    samples.push(performance.now() - start)
    consume(result)
  }
  return median(samples)
}

const matchResults = [
  {
    operation: 'RE2 (literal suffix)',
    scalarMs: measure(() => setInputs.map(input => literalRegex.test(input))),
    batchMs: measure(() => literalRegex.testMany(setInputs)),
  },
  {
    operation: 'RE2 (scan-heavy)',
    scalarMs: measure(() => scanInputs.map(input => scanRegex.test(input))),
    batchMs: measure(() => scanRegex.testMany(scanInputs)),
  },
  {
    operation: 'RE2Set',
    scalarMs: measure(() => setInputs.map(input => set.test(input))),
    batchMs: measure(() => set.testMany(setInputs)),
  },
]

async function elapsed(run) {
  const start = performance.now()
  await run()
  return performance.now() - start
}

const compilePatterns = Array.from(
  { length: Math.max(setSize * 8, 2_000) },
  (_, index) => `^compile-benchmark-${process.pid}-${index}$`
)
const coldCompileMs = await elapsed(() => RE2Set.compileAsync(compilePatterns))
const cacheHitMs = await elapsed(() => RE2Set.compileAsync(compilePatterns))
const dedupePatterns = compilePatterns.map(pattern => `${pattern}-dedupe`)
const dedupeCalls = Math.min(os.availableParallelism(), 8)
const dedupeMs = await elapsed(() =>
  Promise.all(Array.from({ length: dedupeCalls }, () => RE2Set.compileAsync(dedupePatterns)))
)

console.log('# @nxtedition/re2 benchmark')
console.log()
console.log(
  `Node ${process.version}, ${process.platform} ${process.arch}, ${os.cpus()[0]?.model ?? 'unknown CPU'}, ${os.availableParallelism()} available CPUs`
)
console.log(
  `${batchSize.toLocaleString('en-US')} inputs x ${inputBytes.toLocaleString('en-US')} requested bytes, ${setSize.toLocaleString('en-US')} set patterns, median of ${iterations} runs after ${warmup} warmups`
)
console.log()
console.log('| operation | scalar loop | testMany | speedup | testMany throughput |')
console.log('| --- | ---: | ---: | ---: | ---: |')
for (const result of matchResults) {
  const throughput = (batchSize / result.batchMs) * 1_000
  console.log(
    `| ${result.operation} | ${result.scalarMs.toFixed(2)} ms | ${result.batchMs.toFixed(2)} ms | ${(result.scalarMs / result.batchMs).toFixed(2)}x | ${Math.round(throughput).toLocaleString('en-US')} inputs/s |`
  )
}
console.log()
console.log('| compileAsync operation | wall time |')
console.log('| --- | ---: |')
console.log(
  `| cold compile (${compilePatterns.length.toLocaleString('en-US')} patterns) | ${coldCompileMs.toFixed(2)} ms |`
)
console.log(`| native cache hit | ${cacheHitMs.toFixed(2)} ms |`)
console.log(`| ${dedupeCalls} concurrent calls, one compilation | ${dedupeMs.toFixed(2)} ms |`)

// Keep V8 from treating the benchmark results as unused.
if (sink === -1) {
  console.log(sink)
}
