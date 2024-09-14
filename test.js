import { test } from 'node:test'
import assert from 'node:assert'
import { RE2 } from './index.js'

test('RE2', () => {
  const expr = new RE2('foo')
  assert(expr.test('foo'))
})
