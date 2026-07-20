import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import {
  chmodSync,
  copyFileSync,
  existsSync,
  mkdtempSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  rmSync,
  statSync,
  writeFileSync,
} from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { test } from 'node:test'

const addon = '@nxtedition+re2.glibc.node'

function executable(file, source) {
  writeFileSync(file, source, { mode: 0o755 })
}

function fixture() {
  const root = mkdtempSync(path.join(tmpdir(), 'node-re2-build-script-'))
  const bin = path.join(root, 'bin')
  const linux = path.join(root, 'prebuilds/linux-x64')
  const darwin = path.join(root, 'prebuilds/darwin-arm64')

  mkdirSync(bin, { recursive: true })
  mkdirSync(linux, { recursive: true })
  mkdirSync(darwin, { recursive: true })
  mkdirSync(path.join(root, 'vendor/abseil-cpp/.git'), { recursive: true })
  mkdirSync(path.join(root, 'vendor/re2/.git'), { recursive: true })
  copyFileSync(new URL('../build.sh', import.meta.url), path.join(root, 'build.sh'))
  chmodSync(path.join(root, 'build.sh'), 0o755)
  writeFileSync(path.join(linux, addon), 'old')
  writeFileSync(path.join(linux, 'stale.node'), 'stale')
  writeFileSync(path.join(darwin, '@nxtedition+re2.node'), 'darwin')

  executable(path.join(bin, 'mv'), `#!/usr/bin/env bash
set -euo pipefail
phase=
case "$1:$2" in
  prebuilds/linux-x64:prebuilds/.linux-x64-backup.*/linux-x64)
    phase=backup
    ;;
  prebuilds/.linux-x64-backup.*/linux-x64:prebuilds/linux-x64)
    phase=restore
    ;;
  prebuilds/.linux-x64.*:prebuilds/linux-x64)
    phase=candidate
    ;;
esac
case "\${FAKE_MV_MODE:-success}:$phase" in
  backup-fail-before:backup)
    exit 42
    ;;
  backup-term-before:backup)
    kill -TERM "$PPID"
    exit 143
    ;;
  backup-fail-after:backup|candidate-fail-after:candidate)
    /bin/mv "$@"
    exit 42
    ;;
  backup-term-after:backup|candidate-term-after:candidate)
    /bin/mv "$@"
    kill -TERM "$PPID"
    exit 143
    ;;
  restore-fail:candidate)
    /bin/mv "$@"
    exit 42
    ;;
  restore-fail:restore)
    exit 43
    ;;
esac
exec /bin/mv "$@"
`)
  executable(path.join(bin, 'rm'), `#!/usr/bin/env bash
set -euo pipefail
target=
for argument in "$@"; do target=$argument; done
if [ "\${FAKE_RM_TARGET_FAIL:-}" = 1 ] && [ "$target" = prebuilds/linux-x64 ]; then
  exit 42
fi
exec /bin/rm "$@"
`)
  executable(path.join(bin, 'docker'), `#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$FAKE_DOCKER_LOG"
test "$1" = build
shift
target=
output=
while [ "$#" -gt 0 ]; do
  case "$1" in
    --target)
      shift
      target=$1
      ;;
    --output)
      shift
      output=$1
      ;;
  esac
  shift
done
test "$target" = artifact
case "$output" in
  type=local,dest=*) destination=\${output#type=local,dest=} ;;
  *) exit 64 ;;
esac
test -d "$destination"

case "\${FAKE_DOCKER_BUILD_MODE:-success}" in
  fail-before-output)
    exit 42
    ;;
  missing)
    ;;
  symlink)
    ln -s "$FAKE_DOCKER_LOG" "$destination/${addon}"
    ;;
  extra)
    printf 'candidate\n' > "$destination/${addon}"
    printf 'extra\n' > "$destination/extra.node"
    ;;
  *)
    printf 'candidate\n' > "$destination/${addon}"
    ;;
esac

case "\${FAKE_DOCKER_BUILD_MODE:-success}" in
  fail-after-output)
    exit 42
    ;;
  term-after-output)
    kill -TERM "$PPID"
    exit 143
    ;;
esac
`)

  return { bin, darwin, linux, root }
}

function runBuild(context, extraEnv = {}) {
  const log = path.join(context.root, 'docker.log')
  const result = spawnSync(process.env.BASH ?? 'bash', ['./build.sh'], {
    cwd: context.root,
    encoding: 'utf8',
    env: {
      ...process.env,
      ...extraEnv,
      FAKE_DOCKER_LOG: log,
      PATH: `${context.bin}${path.delimiter}${process.env.PATH}`,
    },
  })

  return { log: readFileSync(log, 'utf8'), result }
}

function assertOldPlatforms(context) {
  assert.deepEqual(readdirSync(context.linux).toSorted(), [addon, 'stale.node'].toSorted())
  assert.equal(readFileSync(path.join(context.linux, addon), 'utf8'), 'old')
  assert.equal(
    readFileSync(path.join(context.darwin, '@nxtedition+re2.node'), 'utf8'),
    'darwin'
  )
}

function assertNoTemporaryPlatforms(context) {
  assert.equal(
    readdirSync(path.join(context.root, 'prebuilds')).some(entry =>
      entry.startsWith('.linux-x64')
    ),
    false
  )
}

function withFixture(run) {
  const context = fixture()
  try {
    run(context)
  } finally {
    rmSync(context.root, { recursive: true, force: true })
  }
}

test('build script exports and atomically installs only the Linux artifact', () => {
  withFixture(context => {
    const { log, result } = runBuild(context)

    assert.equal(result.status, 0, result.stderr)
    assert.match(
      log,
      /build --platform linux\/amd64 --target artifact --output type=local,dest=prebuilds\/\.linux-x64\./
    )
    assert.deepEqual(readdirSync(context.linux), [addon])
    assert.equal(readFileSync(path.join(context.linux, addon), 'utf8'), 'candidate\n')
    assert.equal(statSync(context.linux).mode & 0o777, 0o755)
    assert.equal(
      readFileSync(path.join(context.darwin, '@nxtedition+re2.node'), 'utf8'),
      'darwin'
    )
    assertNoTemporaryPlatforms(context)
  })
})

test('build script preserves the old platform on build failure or interruption', () => {
  for (const mode of ['fail-before-output', 'fail-after-output', 'term-after-output']) {
    withFixture(context => {
      const { result } = runBuild(context, { FAKE_DOCKER_BUILD_MODE: mode })
      assert.notEqual(result.status, 0, mode)
      assertOldPlatforms(context)
      assertNoTemporaryPlatforms(context)
    })
  }
})

test('build script rejects invalid exported artifact manifests', () => {
  for (const mode of ['missing', 'symlink', 'extra']) {
    withFixture(context => {
      const { result } = runBuild(context, { FAKE_DOCKER_BUILD_MODE: mode })
      assert.notEqual(result.status, 0, mode)
      assertOldPlatforms(context)
      assertNoTemporaryPlatforms(context)
    })
  }
})

test('build script preserves the old platform around every backup rename window', () => {
  for (const mode of [
    'backup-fail-before',
    'backup-term-before',
    'backup-fail-after',
    'backup-term-after',
  ]) {
    withFixture(context => {
      const { result } = runBuild(context, { FAKE_MV_MODE: mode })
      assert.notEqual(result.status, 0, mode)
      assertOldPlatforms(context)
      assertNoTemporaryPlatforms(context)
    })
  }
})

test('build script rolls back post-candidate-rename errors and signals', () => {
  for (const mode of ['candidate-fail-after', 'candidate-term-after']) {
    withFixture(context => {
      const { result } = runBuild(context, { FAKE_MV_MODE: mode })
      assert.notEqual(result.status, 0, mode)
      assertOldPlatforms(context)
      assertNoTemporaryPlatforms(context)
    })
  }
})

test('failed first Linux install removes a post-rename candidate', () => {
  for (const mode of ['candidate-fail-after', 'candidate-term-after']) {
    withFixture(context => {
      rmSync(context.linux, { recursive: true })
      const { result } = runBuild(context, { FAKE_MV_MODE: mode })
      assert.notEqual(result.status, 0, mode)
      assert.equal(existsSync(context.linux), false)
      assert.equal(
        readFileSync(path.join(context.darwin, '@nxtedition+re2.node'), 'utf8'),
        'darwin'
      )
      assertNoTemporaryPlatforms(context)
    })
  }
})

test('failed first Linux rollback reports an unremovable candidate', () => {
  withFixture(context => {
    rmSync(context.linux, { recursive: true })
    const { result } = runBuild(context, {
      FAKE_MV_MODE: 'candidate-fail-after',
      FAKE_RM_TARGET_FAIL: '1',
    })

    assert.notEqual(result.status, 0)
    assert.equal(readFileSync(path.join(context.linux, addon), 'utf8'), 'candidate\n')
    assert.match(
      result.stderr,
      /uncommitted Linux prebuild at prebuilds\/linux-x64; remove it manually/
    )
    assertNoTemporaryPlatforms(context)
  })
})

test('build script preserves the recovery backup when restoration fails', () => {
  withFixture(context => {
    const { result } = runBuild(context, { FAKE_MV_MODE: 'restore-fail' })
    const backupRoots = readdirSync(path.join(context.root, 'prebuilds')).filter(entry =>
      entry.startsWith('.linux-x64-backup.')
    )

    assert.notEqual(result.status, 0)
    assert.equal(existsSync(context.linux), false)
    assert.equal(backupRoots.length, 1)
    assert.equal(
      readFileSync(path.join(context.root, 'prebuilds', backupRoots[0], 'linux-x64', addon), 'utf8'),
      'old'
    )
    assert.match(result.stderr, /preserved it at .*\.linux-x64-backup\..*\/linux-x64/)
  })
})
