import assert from 'node:assert/strict'
import { parentPort, workerData } from 'node:worker_threads'

import { RE2Set } from '@nxtedition/re2'

const patterns = workerData?.patterns ?? ['^worker$', 'work']
const input = Buffer.from(workerData?.input ?? 'worker')
const expected = workerData?.expected ?? [0, 1]
const batchSize = workerData?.batchSize ?? 1
if (workerData?.barrier) {
  const barrier = new Int32Array(workerData.barrier)
  Atomics.add(barrier, 0, 1)
  Atomics.notify(barrier, 0)
  Atomics.wait(barrier, 1, 0)
}

const first = RE2Set.compileAsync(patterns)
const second = RE2Set.compileAsync(patterns)
const expressions = await Promise.all([first, second])
for (const expression of expressions) {
  assert.deepEqual(
    expression.test(input).toSorted((left, right) => left - right),
    expected
  )
  for (const match of expression.testMany(Array(batchSize).fill(input))) {
    assert.deepEqual(match.toSorted((left, right) => left - right), expected)
  }
}
parentPort.postMessage('ok')
