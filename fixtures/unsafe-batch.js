import assert from 'node:assert/strict'
import { pbkdf2 } from 'node:crypto'
import { promisify } from 'node:util'

import { RE2, RE2Set } from '@nxtedition/re2'

const blockWorkerPool = promisify(pbkdf2)('password', 'salt', 500_000, 32, 'sha256')
const regex = new RE2('^foo$')
const set = new RE2Set(['^foo$'])
const safeRegexInput = Buffer.from('foo')
const unsafeRegexInput = Buffer.from('foo')
const safeSetInput = Buffer.from('foo')
const unsafeSetInput = Buffer.from('foo')

const safeRegex = regex.testManyAsync([safeRegexInput])
const unsafeRegex = regex.testManyAsync([unsafeRegexInput], { unsafe: true })
const safeSet = set.testManyAsync([safeSetInput])
const unsafeSet = set.testManyAsync([unsafeSetInput], { unsafe: true })

safeRegexInput.fill(0x78)
unsafeRegexInput.fill(0x78)
safeSetInput.fill(0x78)
unsafeSetInput.fill(0x78)

await blockWorkerPool
assert.deepEqual(await safeRegex, [true])
assert.deepEqual(await unsafeRegex, [false])
assert.deepEqual(await safeSet, [[0]])
assert.deepEqual(await unsafeSet, [[]])
