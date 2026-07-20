import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { createRequire } from 'node:module'
import { describe, test } from 'node:test'
import { Worker } from 'node:worker_threads'
import { RE2, RE2Set } from '@nxtedition/re2'
import nodeGypBuild from 'node-gyp-build'
import binding from './binding.js'

const toSortedIndices = indices => indices.toSorted((left, right) => left - right)
const compileStats = () => binding.set_compile_cache_stats()

function waitForCompileWorker(worker) {
  return new Promise((resolve, reject) => {
    let result
    worker.once('message', value => {
      result = value
    })
    worker.once('error', reject)
    worker.once('exit', code => {
      if (code === 0) {
        resolve(result)
      } else {
        reject(new Error(`RE2Set worker exited with code ${code}`))
      }
    })
  })
}

describe('RE2', () => {
  test('performs partial matches', () => {
    const expression = new RE2('foo')

    assert.equal(expression.test(Buffer.from('before foo after')), true)
    assert.equal(expression.test(Buffer.from('bar')), false)
  })

  test('supports empty and binary patterns', () => {
    assert.equal(new RE2('').test(Buffer.alloc(0)), true)
    assert.equal(new RE2(new Uint8Array()).test(new DataView(new ArrayBuffer(0))), true)
    assert.equal(
      new RE2(Buffer.from([0x00, 0x62])).test(Buffer.from([0x61, 0x00, 0x62, 0x63])),
      true
    )
  })

  test('copies pattern data at construction', () => {
    const pattern = Buffer.from('foo')
    const expression = new RE2(pattern)
    pattern.fill(0x78)

    assert.equal(expression.test(Buffer.from('foo')), true)
    assert.equal(expression.test(Buffer.from('xxx')), false)
  })

  test('rejects invalid patterns without native log noise', () => {
    for (const pattern of ['(', '(a)\\1', Buffer.from([0xff])]) {
      assert.throws(() => new RE2(pattern), Error)
    }

    const moduleUrl = new URL('./index.js', import.meta.url).href
    const child = spawnSync(
      process.execPath,
      ['--input-type=module', '--eval', `import { RE2 } from ${JSON.stringify(moduleUrl)}; try { new RE2('(') } catch {}`],
      { encoding: 'utf8' }
    )
    assert.equal(child.status, 0, child.stderr)
    assert.equal(child.stderr, '')
  })

  test('supports Buffer, TypedArray, DataView, and shared views', () => {
    const bytes = new Uint8Array([0x78, 0x66, 0x6f, 0x6f, 0x79])
    const typedArray = bytes.subarray(1, 4)
    const dataView = new DataView(bytes.buffer, 1, 3)
    const sharedBuffer = new SharedArrayBuffer(3)
    const sharedView = new Uint8Array(sharedBuffer)
    sharedView.set(typedArray)

    assert.equal(new RE2(typedArray).test(Buffer.from('foo')), true)
    assert.equal(new RE2(dataView).test(typedArray), true)
    assert.equal(new RE2('foo').test(sharedView), true)
    assert.equal(
      new RE2('cd').test(new Uint16Array(new Uint8Array([0x61, 0x62, 0x63, 0x64]).buffer)),
      true
    )
    assert.throws(() => new RE2(new ArrayBuffer(3)), TypeError)
    assert.throws(() => new RE2('foo').test('foo'), TypeError)
  })

  test('matches batches', () => {
    const expression = new RE2('^match-[0-9]+$')
    const inputs = Array.from({ length: 512 }, (_, index) =>
      Buffer.from(index % 3 === 0 ? `match-${index}` : `miss-${index}`)
    )

    assert.deepEqual(
      expression.testMany(inputs),
      inputs.map(input => expression.test(input))
    )
    assert.deepEqual(expression.testMany([]), [])
    assert.throws(() => expression.testMany('match-1'), TypeError)
    assert.throws(() => expression.testMany([Buffer.from('match-1'), 'match-2']), TypeError)
  })

  test('matches byte ranges and clamps them without 32-bit wrapping', () => {
    const buffer = Buffer.from('xfooy')
    const exact = new RE2('^foo$')

    assert.equal(exact.test(buffer, 1, 3), true)
    assert.equal(exact.test(buffer, 0, 4), false)
    assert.equal(new RE2('^xfoo$').test(buffer, -10, 4), true)
    assert.equal(exact.test(buffer, 1.9, 3.9), true)
    assert.equal(new RE2('x').test(buffer, 2 ** 31, 1), false)
    assert.equal(new RE2('x').test(buffer, 2 ** 32, 1), false)
    assert.equal(new RE2('x').test(buffer, Number.MAX_SAFE_INTEGER, 1), false)
    assert.equal(new RE2('^xfooy$').test(buffer, 0, 2 ** 32), true)
    assert.equal(new RE2('^x$').test(buffer, Number.NaN, 1), true)
    assert.equal(new RE2('^x$').test(buffer, Number.POSITIVE_INFINITY, 1), false)
    assert.equal(new RE2('^xfooy$').test(buffer, 0, Number.POSITIVE_INFINITY), true)
    assert.throws(() => exact.test(buffer, '1', 3), TypeError)
  })

  test('uses byte offsets for Unicode input', () => {
    const buffer = Buffer.from('a💩b')
    const expression = new RE2('^💩$')

    assert.equal(expression.test(buffer, 1, 4), true)
    assert.equal(expression.test(buffer, 2, 3), false)
  })
})

describe('RE2Set', () => {
  test('returns all matching indices and an empty array for misses', () => {
    const expressions = new RE2Set(['foo', 'o', 'bar'])

    assert.deepEqual(toSortedIndices(expressions.test(Buffer.from('foo'))), [0, 1])
    assert.deepEqual(expressions.test(Buffer.from('baz')), [])
  })

  test('supports empty sets, duplicates, and empty patterns', () => {
    assert.deepEqual(new RE2Set([]).test(Buffer.alloc(0)), [])
    assert.deepEqual(
      toSortedIndices(new RE2Set(['foo', 'foo']).test(Buffer.from('foo'))),
      [0, 1]
    )
    assert.deepEqual(new RE2Set(['']).test(Buffer.alloc(0)), [0])
  })

  test('rejects invalid patterns', () => {
    for (const pattern of ['(', '(a)\\1', Buffer.from([0xff])]) {
      assert.throws(() => new RE2Set(['valid', pattern]), Error)
    }
  })

  test('normalizes Array subclasses and validates constructor input', () => {
    class PatternArray extends Array {
      static get [Symbol.species]() {
        return Object
      }
    }

    const patterns = new PatternArray('foo', 'bar')
    const expressions = new RE2Set(patterns)
    assert.deepEqual(expressions.test(Buffer.from('foo')), [0])
    assert.throws(() => new RE2Set('foo'), TypeError)
  })

  test('supports binary views and the same byte-range semantics as RE2', () => {
    const bytes = new Uint8Array([0x78, 0x66, 0x6f, 0x6f, 0x79])
    const patterns = [new DataView(bytes.buffer, 1, 3), 'bar']
    const expressions = new RE2Set(patterns)

    assert.deepEqual(expressions.test(bytes, 1, 3), [0])
    assert.deepEqual(expressions.test(bytes, 2 ** 32, 1), [])
  })

  test('matches batches with independent RE2Set result storage', () => {
    const expressions = new RE2Set(['^foo-[0-9]+$', '[02468]$', '^bar'])
    const inputs = Array.from({ length: 512 }, (_, index) =>
      Buffer.from(index % 2 === 0 ? `foo-${index}` : `bar-${index}`)
    )

    assert.deepEqual(
      expressions.testMany(inputs).map(toSortedIndices),
      inputs.map(input => toSortedIndices(expressions.test(input)))
    )
    assert.deepEqual(expressions.testMany([]), [])
    assert.throws(() => expressions.testMany('foo-1'), TypeError)
    assert.throws(() => expressions.testMany([Buffer.from('foo-1'), 'bar-2']), TypeError)
  })

  test('compiles asynchronously on the worker pool', async () => {
    const expressions = await RE2Set.compileAsync(['foo', 'o', 'bar'])

    assert.deepEqual(toSortedIndices(expressions.test(Buffer.from('foo'))), [0, 1])
    assert.deepEqual(expressions.test(Buffer.from('baz')), [])
    assert.deepEqual((await RE2Set.compileAsync([])).test(Buffer.alloc(0)), [])
  })

  test('snapshots binary pattern data before compiling', async () => {
    const pattern = Buffer.from('async-snapshot')
    const compilation = RE2Set.compileAsync([pattern])
    pattern.fill(0x78)
    const expressions = await compilation

    assert.deepEqual(expressions.test(Buffer.from('async-snapshot')), [0])
    assert.deepEqual(expressions.test(pattern), [])
  })

  test('deduplicates in-flight compilations and caches completed sets', async () => {
    const patterns = ['^async-dedupe$', Buffer.from('cache')]
    const before = compileStats()
    const first = RE2Set.compileAsync(patterns)
    const second = RE2Set.compileAsync(['^async-dedupe$', Buffer.from('cache')])

    const [firstExpressions, secondExpressions] = await Promise.all([first, second])
    assert.deepEqual(firstExpressions.test(Buffer.from('async-dedupe')), [0])
    assert.deepEqual(secondExpressions.test(Buffer.from('cache')), [1])

    const afterConcurrent = compileStats()
    assert.equal(afterConcurrent.compilations, before.compilations + 1n)
    assert.ok(
      afterConcurrent.cacheHits + afterConcurrent.deduplications >=
        before.cacheHits + before.deduplications + 1n
    )

    await RE2Set.compileAsync(patterns)
    const afterCached = compileStats()
    assert.equal(afterCached.compilations, afterConcurrent.compilations)
    assert.equal(afterCached.cacheHits, afterConcurrent.cacheHits + 1n)
  })

  test('bounds the completed compile cache', async () => {
    const retained = await RE2Set.compileAsync(['^async-eviction-0$'])
    for (let index = 1; index <= 16; index += 1) {
      await RE2Set.compileAsync([`^async-eviction-${index}$`])
    }

    const beforeRetry = compileStats()
    await RE2Set.compileAsync(['^async-eviction-0$'])
    assert.equal(compileStats().compilations, beforeRetry.compilations + 1n)
    assert.deepEqual(retained.test(Buffer.from('async-eviction-0')), [0])
  })

  test('does not cache failed compilations', async () => {
    const before = compileStats()
    const first = RE2Set.compileAsync(['async-invalid', '('])
    await assert.rejects(first, Error)

    const second = RE2Set.compileAsync(['async-invalid', '('])
    await assert.rejects(second, Error)
    assert.equal(compileStats().compilations, before.compilations + 2n)
  })

  test('validates compileAsync input synchronously', () => {
    assert.throws(() => RE2Set.compileAsync('foo'), TypeError)
    assert.throws(() => RE2Set.compileAsync([new ArrayBuffer(3)]), TypeError)
  })

  test('rejects native setup failures through the returned promise', async () => {
    const compilation = binding.set_compile_async([new ArrayBuffer(3)])
    assert.ok(compilation instanceof Promise)
    await assert.rejects(compilation, TypeError)
  })

  test('compiles and reuses sets inside a Worker environment', async () => {
    const worker = new Worker(new URL('./fixtures/compile-worker.js', import.meta.url))
    assert.equal(await waitForCompileWorker(worker), 'ok')
  })

  test('shares cache and in-flight dedupe across Worker environments', async () => {
    const workerCount = 4
    const barrier = new Int32Array(new SharedArrayBuffer(2 * Int32Array.BYTES_PER_ELEMENT))
    const patterns = Array.from({ length: 2_000 }, (_, index) => `^cross-thread-dedupe-${index}$`)
    const before = compileStats()
    const workers = Array.from(
      { length: workerCount },
      () =>
        new Worker(new URL('./fixtures/compile-worker.js', import.meta.url), {
          workerData: {
            batchSize: 128,
            barrier: barrier.buffer,
            patterns,
            input: 'cross-thread-dedupe-0',
            expected: [0],
          },
        })
    )
    const results = workers.map(waitForCompileWorker)

    while (Atomics.load(barrier, 0) < workerCount) {
      const ready = Atomics.load(barrier, 0)
      await Atomics.waitAsync(barrier, 0, ready).value
    }
    Atomics.store(barrier, 1, 1)
    Atomics.notify(barrier, 1)

    assert.deepEqual(await Promise.all(results), Array(workerCount).fill('ok'))
    const after = compileStats()
    assert.equal(after.compilations, before.compilations + 1n)
    assert.ok(
      after.cacheHits + after.deduplications >=
        before.cacheHits + before.deduplications + BigInt(workerCount * 2 - 1)
    )
  })

  test('survives Worker termination with compilation in flight', async () => {
    for (let iteration = 0; iteration < 8; iteration += 1) {
      const worker = new Worker(
        new URL('./fixtures/terminate-compile-worker.js', import.meta.url),
        { workerData: iteration }
      )
      await new Promise((resolve, reject) => {
        worker.once('message', resolve)
        worker.once('error', reject)
      })
      assert.equal(await worker.terminate(), 1)
    }
  })
})

test('supports CommonJS loading and async compilation', async () => {
  const require = createRequire(import.meta.url)
  const commonjs = require('@nxtedition/re2')

  assert.equal(new commonjs.RE2('foo').test(Buffer.from('foo')), true)
  assert.deepEqual(new commonjs.RE2('foo').testMany([Buffer.from('foo'), Buffer.from('bar')]), [
    true,
    false,
  ])

  const set = await commonjs.RE2Set.compileAsync(['foo'])
  assert.deepEqual(set.test(Buffer.from('foo')), [0])
  assert.deepEqual(set.testMany([Buffer.from('foo'), Buffer.from('bar')]), [[0], []])
})

test('native addon has no unresolved vendored symbols', {
  skip: process.platform === 'win32' ? 'nm is not available on Windows' : false
}, context => {
  const bindingPath = nodeGypBuild.path(import.meta.dirname)
  const symbols = spawnSync('nm', ['-u', bindingPath], { encoding: 'utf8' })
  if (symbols.error?.code === 'ENOENT') {
    context.skip('nm is not available')
    return
  }
  assert.ifError(symbols.error)
  assert.equal(symbols.status, 0, symbols.stderr)

  const demangled = spawnSync('c++filt', { input: symbols.stdout, encoding: 'utf8' })
  if (demangled.error?.code === 'ENOENT') {
    context.skip('c++filt is not available')
    return
  }
  assert.ifError(demangled.error)
  assert.equal(demangled.status, 0, demangled.stderr)
  const unresolvedVendoredSymbols = demangled.stdout
    .split('\n')
    .filter(line =>
      /(?:absl|re2)::/.test(line) &&
      !/^\s*w TLS init function for re2::hooks::context$/.test(line)
    )
  assert.deepEqual(unresolvedVendoredSymbols, [])
})
