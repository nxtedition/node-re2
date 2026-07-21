import assert from 'node:assert/strict'
import {
  cpSync,
  existsSync,
  mkdtempSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  rmSync,
  writeFileSync,
} from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { test } from 'node:test'

import { expectedPrebuilds, validatePrebuildPaths } from '../scripts/verify-prebuilds.mjs'

test('release builds and validates prebuilds before versioning or publishing', () => {
  const release = readFileSync(new URL('../release.sh', import.meta.url), 'utf8')
  const linux = release.indexOf('./build.sh')
  const darwin = release.indexOf('./scripts/build-darwin-prebuild.sh', linux)
  const typescript = release.indexOf('npm run build:ts --silent', darwin)
  const verify = release.indexOf('npm run verify:prebuilds', typescript)
  const version = release.indexOf(
    'npm version "$BUMP" --git-tag-version=true --tag-version-prefix=v',
    verify
  )
  const publish = release.indexOf('npm publish', version)
  const push = release.indexOf('git push --atomic origin', publish)

  assert.match(release, /\[ "\$BRANCH" != main \]/)
  assert.ok(linux >= 0)
  assert.ok(darwin > linux)
  assert.ok(typescript > darwin)
  assert.ok(verify > typescript)
  assert.ok(version > verify)
  assert.ok(publish > version)
  assert.ok(push > publish)
  assert.match(release, /\[ "\$TAG_COMMIT" != "\$HEAD_COMMIT" \]/)
  assert.match(release, /--access public/)
  assert.match(release, /--dry-run=false/)
  assert.match(release, /--tag latest/)
  assert.match(release, /export NODE_RE2_OPENMP=0/)
  assert.doesNotMatch(release, /git push --tags/)
})

test('only the Linux prebuild enables dynamic GNU OpenMP', () => {
  const dockerfile = readFileSync(new URL('../Dockerfile', import.meta.url), 'utf8')
  const binding = readFileSync(new URL('../binding.gyp', import.meta.url), 'utf8')
  const darwin = readFileSync(
    new URL('../scripts/build-darwin-prebuild.sh', import.meta.url),
    'utf8'
  )

  assert.match(dockerfile, /NODE_RE2_OPENMP=1 NODE_RE2_MARCH=znver3/)
  assert.match(dockerfile, /ldd .* \| grep -E 'libgomp\[\.\]so\[\.\]1 => \/'/)
  assert.match(binding, /node_re2_openmp%.*NODE_RE2_OPENMP === '1'/)
  assert.match(binding, /node_re2_openmp==1.*-fopenmp/s)
  assert.match(darwin, /NODE_RE2_OPENMP=0/)
})

test('Darwin release prebuild is staged, tested, and installed transactionally', () => {
  const helper = readFileSync(
    new URL('../scripts/build-darwin-prebuild.sh', import.meta.url),
    'utf8'
  )
  const stagedBuild = helper.indexOf('--out "$OUT_DIR"')
  const install = helper.indexOf('mv "$CANDIDATE_DIR" "$TARGET_DIR"', stagedBuild)
  const testPrebuild = helper.indexOf('npm run test:prebuild', install)
  const commit = helper.indexOf('COMMITTED=1', testPrebuild)

  assert.ok(stagedBuild >= 0)
  assert.ok(install > stagedBuild)
  assert.ok(testPrebuild > install)
  assert.ok(commit > testPrebuild)
})

function withDarwinHelper(run) {
  const root = mkdtempSync(path.join(tmpdir(), 'node-re2-release-'))
  try {
    mkdirSync(path.join(root, 'scripts'))
    cpSync(
      new URL('../scripts/build-darwin-prebuild.sh', import.meta.url),
      path.join(root, 'scripts/build-darwin-prebuild.sh')
    )
    mkdirSync(path.join(root, 'prebuilds/darwin-arm64'), { recursive: true })
    writeFileSync(path.join(root, 'prebuilds/darwin-arm64/known-good'), 'old')
    mkdirSync(path.join(root, 'bin'))
    run(root)
  } finally {
    rmSync(root, { recursive: true, force: true })
  }
}

function runDarwinHelper(root) {
  return spawnSync(process.env.BASH ?? 'bash', ['scripts/build-darwin-prebuild.sh', '26.5.0'], {
    cwd: root,
    env: { ...process.env, PATH: `${path.join(root, 'bin')}:${process.env.PATH}` },
    encoding: 'utf8',
  })
}

function assertKnownGoodDarwinPrebuild(root) {
  assert.equal(readFileSync(path.join(root, 'prebuilds/darwin-arm64/known-good'), 'utf8'), 'old')
  assert.deepEqual(
    readdirSync(path.join(root, 'prebuilds')).filter(entry => entry.startsWith('.darwin-arm64')),
    []
  )
}

test('Darwin staging failure leaves the prior prebuild untouched', () => {
  withDarwinHelper(root => {
    writeFileSync(path.join(root, 'bin/npx'), '#!/bin/sh\nexit 7\n', { mode: 0o755 })
    const result = runDarwinHelper(root)
    assert.equal(result.status, 7, result.stderr)
    assertKnownGoodDarwinPrebuild(root)
  })
})

test('Darwin test failure restores the prior prebuild', () => {
  withDarwinHelper(root => {
    writeFileSync(
      path.join(root, 'bin/npx'),
      `#!/bin/sh
while [ "$#" -gt 0 ]; do
  if [ "$1" = --out ]; then
    shift
    output=$1
  fi
  shift
done
mkdir -p "$output/prebuilds/darwin-arm64"
: > "$output/prebuilds/darwin-arm64/@nxtedition+re2.node"
`,
      { mode: 0o755 }
    )
    writeFileSync(path.join(root, 'bin/npm'), '#!/bin/sh\nexit 9\n', { mode: 0o755 })
    const result = runDarwinHelper(root)
    assert.equal(result.status, 9, result.stderr)
    assertKnownGoodDarwinPrebuild(root)
    assert.equal(
      existsSync(path.join(root, 'prebuilds/darwin-arm64/@nxtedition+re2.node')),
      false
    )
  })
})

test('release manifest accepts only the two supported prebuilds', () => {
  assert.doesNotThrow(() => validatePrebuildPaths([...expectedPrebuilds], 'test'))
  assert.throws(() => validatePrebuildPaths(expectedPrebuilds.slice(1), 'test'), /missing=/)
  assert.throws(
    () => validatePrebuildPaths([...expectedPrebuilds, 'prebuilds/linux-x64/stale.node'], 'test'),
    /unexpected=/
  )
  assert.throws(
    () => validatePrebuildPaths([...expectedPrebuilds, expectedPrebuilds[0]], 'test'),
    /duplicates=/
  )
})
