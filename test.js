import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { createRequire } from 'node:module'
import { describe, test } from 'node:test'
import { RE2, RE2Set } from '@nxtedition/re2'
import nodeGypBuild from 'node-gyp-build'

const toSortedIndices = indices => indices.toSorted((left, right) => left - right)

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
})

test('supports synchronous CommonJS loading', () => {
  const require = createRequire(import.meta.url)
  const commonjs = require('@nxtedition/re2')

  assert.equal(new commonjs.RE2('foo').test(Buffer.from('foo')), true)
})

test('native addon has no unresolved vendored symbols', {
  skip: process.platform === 'win32' ? 'nm is not available on Windows' : false
}, () => {
  const bindingPath = nodeGypBuild.path(import.meta.dirname)
  const symbols = spawnSync('nm', ['-u', bindingPath], { encoding: 'utf8' })
  assert.equal(symbols.status, 0, symbols.stderr)

  const demangled = spawnSync('c++filt', { input: symbols.stdout, encoding: 'utf8' })
  assert.equal(demangled.status, 0, demangled.stderr)
  const unresolvedVendoredSymbols = demangled.stdout
    .split('\n')
    .filter(line =>
      /(?:absl|re2)::/.test(line) &&
      !/^\s*w TLS init function for re2::hooks::context$/.test(line)
    )
  assert.deepEqual(unresolvedVendoredSymbols, [])
})
