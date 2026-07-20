import assert from 'node:assert/strict'
import { execFileSync, spawnSync } from 'node:child_process'
import { availableParallelism } from 'node:os'
import { test } from 'node:test'

import { RE2, RE2Set } from '@nxtedition/re2'
import binding from '../lib/binding.js'

test('reports scheduler-selected batch threads', () => {
  assert.throws(() => binding.batch_parallelism('128', 1), TypeError)
  assert.throws(() => binding.batch_parallelism(128, '1'), TypeError)
  assert.throws(() => binding.batch_parallelism(128, 1, '1'), TypeError)
  assert.throws(() => binding.batch_parallelism(-1, 1), RangeError)
  assert.equal(binding.batch_parallelism(0, 0), 0)
  assert.equal(binding.batch_parallelism(128, 128 * 64), 1)
  const expectedMaximum =
    process.platform === 'linux' && process.env.PREBUILDS_ONLY
      ? Math.max(Math.floor(availableParallelism() / 2), 1)
      : 1
  assert.equal(binding.batch_parallelism(1_000_000, 1_000_000_000), expectedMaximum)
  assert.equal(binding.batch_parallelism(128, 128 * 16_384, Infinity), 1)
  assert.equal(binding.batch_parallelism(128, 128 * 16_384, 128), 1)
  assert.equal(binding.batch_parallelism(128, 128 * 16_384, 256), 1)
  assert.equal(
    binding.batch_parallelism(128, 128 * 16_384, 64),
    Math.min(expectedMaximum, 2)
  )
})

const parallelInputCount = 257
const parallelInputBytes = 16 * 1024
const explicitParallelism = binding.batch_parallelism(
  parallelInputCount,
  parallelInputCount * parallelInputBytes,
  7
)

test(
  'explicit batch size preserves results across parallel chunk boundaries',
  {
    skip:
      explicitParallelism < 2
        ? 'parallel matching is only enabled in a Linux prebuild with at least two CPUs'
        : false,
  },
  () => {
    const suffixes = ['alpha', 'beta', 'gamma', 'miss']
    const inputs = Array.from({ length: parallelInputCount }, (_, index) => {
      const suffix = suffixes[index % suffixes.length]
      const input = Buffer.alloc(parallelInputBytes, 'x')
      input.write(suffix, input.length - suffix.length)
      return input
    })
    const expression = new RE2('(alpha|gamma)$')
    const expressions = new RE2Set(['alpha$', 'beta$', 'gamma$'])

    assert.deepEqual(
      expression.testMany(inputs, { batchSize: 7 }),
      inputs.map(input => expression.test(input))
    )
    assert.deepEqual(
      expressions.testMany(inputs, { batchSize: 7 }),
      inputs.map(input => expressions.test(input))
    )
  }
)

test('rejects mismatched native contexts without terminating Node', () => {
  const bindingUrl = new URL('../lib/binding.js', import.meta.url).href
  const child = spawnSync(
    process.execPath,
    [
      '--input-type=module',
      '--eval',
      `
        import assert from 'node:assert/strict'
        import binding from ${JSON.stringify(bindingUrl)}
        const regex = binding.regex_init('foo')
        const set = binding.set_init(['foo'])
        assert.throws(
          () => binding.set_test(regex, Buffer.from('foo'), 0, 3),
          /Invalid RE2Set context/
        )
        assert.throws(
          () => binding.regex_test(set, Buffer.from('foo'), 0, 3),
          /Invalid RE2 context/
        )
      `,
    ],
    { encoding: 'utf8' }
  )

  assert.equal(child.signal, null)
  assert.equal(child.status, 0, child.stderr)
  assert.equal(child.stderr, '')
})

test('collects batch values before reading their backing stores', () => {
  const regex = binding.regex_init('^$')
  const first = new Uint8Array(Buffer.from('foo'))
  let transferred
  const inputs = [first]
  Object.defineProperty(inputs, 1, {
    enumerable: true,
    get() {
      transferred = structuredClone(first.buffer, { transfer: [first.buffer] })
      return new Uint8Array(transferred)
    },
  })
  inputs.length = 2

  assert.deepEqual(binding.regex_test_many(regex, inputs), [true, false])
  assert.equal(first.byteLength, 0)
})

test('bounds native pattern count before compiling', async () => {
  const patterns = Array(100_001).fill('')

  assert.throws(() => binding.set_init(patterns), /Too many patterns/)
  await assert.rejects(binding.set_compile_async(patterns), /Too many patterns/)
})

test('packs generated JavaScript, declarations, and split native sources', () => {
  const [pack] = JSON.parse(
    execFileSync('npm', ['pack', '--dry-run', '--json'], {
      cwd: new URL('..', import.meta.url),
      encoding: 'utf8',
      env: { ...process.env, npm_config_ignore_scripts: 'true' },
    })
  )
  const paths = new Set(pack.files.map(({ path }) => path))

  for (const path of [
    'lib/index.js',
    'lib/index.d.ts',
    'lib/binding.js',
    'binding.cc',
    'native/addon-lifecycle.cc',
    'native/addon-lifecycle.h',
    'native/batch-binding.cc',
    'native/batch-binding.h',
    'native/napi-utils.cc',
    'native/napi-utils.h',
    'native/batch-plan.h',
    'native/parallel-for.h',
    'native/regex-binding.cc',
    'native/regex-binding.h',
    'native/set-binding.cc',
    'native/set-binding.h',
    'native/set-cache.cc',
    'native/set-cache.h',
    'native/thread-pool.cc',
    'native/thread-pool.h',
  ]) {
    assert.ok(paths.has(path), `${path} is missing from the npm tarball`)
  }
  assert.equal(paths.has('index.js'), false)
  assert.equal(paths.has('index.d.ts'), false)
})
