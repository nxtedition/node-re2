import assert from 'node:assert/strict'
import { parentPort, workerData } from 'node:worker_threads'

import { RE2, RE2Set } from '@nxtedition/re2'

const twoThreadInputs = Array(32).fill(Buffer.alloc(8_193, 0x61))
const fullPoolInputs = Array(128).fill(Buffer.alloc(16_384, 0x61))
const regex = new RE2('z$')
const set = new RE2Set(['z$', '^b'])
const barrier = new Int32Array(workerData.barrier)

Atomics.add(barrier, 0, 1)
Atomics.notify(barrier, 0)
Atomics.wait(barrier, 1, 0)

for (let iteration = 0; iteration < 100; ++iteration) {
  assert.ok(regex.testMany(twoThreadInputs).every(result => !result))
  assert.ok(set.testMany(fullPoolInputs).every(indices => indices.length === 0))
}

parentPort.postMessage('ok')
