import assert from 'node:assert/strict'
import { parentPort } from 'node:worker_threads'

import { RE2Set } from '@nxtedition/re2'

const patterns = ['^worker$', 'work']
const first = RE2Set.compileAsync(patterns)
const second = RE2Set.compileAsync(patterns)
assert.strictEqual(second, first)

const expressions = await first
assert.strictEqual(await second, expressions)
assert.deepEqual(
  expressions.test(Buffer.from('worker')).toSorted((left, right) => left - right),
  [0, 1]
)
parentPort.postMessage('ok')
