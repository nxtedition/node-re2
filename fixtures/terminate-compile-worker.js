import { parentPort, workerData } from 'node:worker_threads'

import { RE2Set } from '@nxtedition/re2'

const patterns = Array.from(
  { length: 30_000 },
  (_, index) => `^terminate-worker-${workerData}-${index}$`
)
void RE2Set.compileAsync(patterns).catch(() => {})
parentPort.postMessage('queued')
setInterval(() => {}, 1_000)
