import { parentPort, workerData } from 'node:worker_threads'

import { RE2, RE2Set } from '@nxtedition/re2'

const input = Buffer.alloc(256 << 10, 0x61)
const inputs = Array(256).fill(input)
const operation =
  workerData.kind === 'set'
    ? new RE2Set(['^(?:[a-z]{1,8}[0-9])+$']).testManyAsync(inputs, {
        unsafe: workerData.unsafe,
      })
    : new RE2('^(?:[a-z]{1,8}[0-9])+$').testManyAsync(inputs, {
        unsafe: workerData.unsafe,
      })

parentPort.postMessage('queued')
await operation
parentPort.postMessage('done')
