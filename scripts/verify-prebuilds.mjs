import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { existsSync, readdirSync } from 'node:fs'

const required = [
  'prebuilds/linux-x64/@nxtedition+re2.glibc.node',
  'prebuilds/darwin-arm64/@nxtedition+re2.node',
]

for (const path of required) {
  assert.ok(existsSync(path), `missing ${path}`)
}

assert.deepEqual(
  readdirSync('prebuilds/linux-x64').filter((path) => path.endsWith('.node')),
  ['@nxtedition+re2.glibc.node'],
  'linux-x64 must contain only the libc-tagged glibc prebuild',
)

const [pack] = JSON.parse(
  execFileSync('npm', ['pack', '--dry-run', '--json'], {
    encoding: 'utf8',
    env: { ...process.env, npm_config_ignore_scripts: 'true' },
  }),
)
const packedFiles = new Set(pack.files.map(({ path }) => path))

for (const path of required) {
  assert.ok(packedFiles.has(path), `${path} is missing from the npm tarball`)
}

console.log('Verified packaged linux-x64 and darwin-arm64 N-API prebuilds.')
