import assert from 'node:assert/strict'
import { test } from 'node:test'

import { RE2Set } from '@nxtedition/re2'
import binding from '../lib/binding.js'

const stats = () => binding.set_compile_cache_stats()
const sorted = indices => indices.toSorted((left, right) => left - right)

test('cache keys preserve pattern boundaries, bytes, and order', async () => {
  const before = stats()
  const splitLeft = await RE2Set.compileAsync(['a', 'bc'])
  const splitRight = await RE2Set.compileAsync(['ab', 'c'])
  const withNull = await RE2Set.compileAsync([Buffer.from([0x61, 0x00, 0x62]), 'plain'])
  const forward = await RE2Set.compileAsync(['left', 'right'])
  const reversed = await RE2Set.compileAsync(['right', 'left'])
  const after = stats()

  assert.equal(after.compilations, before.compilations + 5n)
  assert.deepEqual(sorted(splitLeft.test(Buffer.from('bc'))), [1])
  assert.deepEqual(sorted(splitRight.test(Buffer.from('ab'))), [0])
  assert.deepEqual(withNull.test(Buffer.from([0x61, 0x00, 0x62])), [0])
  assert.deepEqual(forward.test(Buffer.from('left')), [0])
  assert.deepEqual(reversed.test(Buffer.from('left')), [1])
})

test('deduplicates concurrent failures but retries after settlement', async () => {
  const patterns = ['^failed-dedupe-unique$', '(']
  const before = stats()
  const compilations = Array.from({ length: 8 }, () => RE2Set.compileAsync(patterns))

  const results = await Promise.allSettled(compilations)
  assert.ok(results.every(result => result.status === 'rejected'))
  const afterConcurrent = stats()
  assert.equal(afterConcurrent.compilations, before.compilations + 1n)
  assert.equal(afterConcurrent.deduplications, before.deduplications + 7n)
  assert.equal(afterConcurrent.inFlight, 0)

  await assert.rejects(RE2Set.compileAsync(patterns), Error)
  assert.equal(stats().compilations, afterConcurrent.compilations + 1n)
})
