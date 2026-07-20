import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { test } from 'node:test'

const cwd = new URL('..', import.meta.url)

for (const [name, args] of [
  [
    'ES modules',
    [
      '--input-type=module',
      '--eval',
      `
        import { RE2, RE2Set } from '@nxtedition/re2'
        if (!new RE2('foo').test(Buffer.from('foo'))) process.exit(1)
        const set = await RE2Set.compileAsync(['foo'])
        if (set.test(Buffer.from('foo'))[0] !== 0) process.exit(1)
      `,
    ],
  ],
  [
    'CommonJS',
    [
      '--eval',
      `
        const { RE2, RE2Set } = require('@nxtedition/re2')
        if (!new RE2('foo').test(Buffer.from('foo'))) process.exit(1)
        RE2Set.compileAsync(['foo']).then(set => {
          if (set.test(Buffer.from('foo'))[0] !== 0) process.exit(1)
        })
      `,
    ],
  ],
]) {
  test(`loads in a fresh ${name} process`, () => {
    const child = spawnSync(process.execPath, args, { cwd, encoding: 'utf8' })
    assert.equal(child.signal, null)
    assert.equal(child.status, 0, child.stderr)
    assert.equal(child.stderr, '')
  })
}
