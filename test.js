import { test } from 'node:test'
import assert from 'node:assert'
import { RE2, RE2Set } from './index.js'

test('RE2', () => {
  const expr = new RE2('foo')
  assert(expr.test('foo'))
})


test('Set', () => {
  const expr = new RE2Set(['foo', 'o', 'bar'])
  assert.deepStrictEqual(expr.test('foo'), [1, 0])
})
