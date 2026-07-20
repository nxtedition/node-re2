import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { existsSync, lstatSync, readdirSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

export const expectedPrebuilds = [
  'prebuilds/darwin-arm64/@nxtedition+re2.node',
  'prebuilds/linux-x64/@nxtedition+re2.glibc.node',
]

const required = ['lib/index.js', 'lib/index.d.ts', 'lib/binding.js', ...expectedPrebuilds]

export function validatePrebuildPaths(paths, source) {
  const actual = paths.toSorted()
  const expected = expectedPrebuilds.toSorted()
  const missing = expected.filter(entry => !actual.includes(entry))
  const unexpected = actual.filter(entry => !expected.includes(entry))
  const duplicates = actual.filter((entry, index) => entry === actual[index - 1])

  if (missing.length !== 0 || unexpected.length !== 0 || duplicates.length !== 0) {
    throw new Error(
      `${source} prebuild manifest is invalid; missing=${JSON.stringify(missing)}, unexpected=${JSON.stringify(unexpected)}, duplicates=${JSON.stringify([...new Set(duplicates)])}`
    )
  }
}

function listFiles(directory, relative = directory) {
  if (!existsSync(directory)) {
    return []
  }
  return readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
    const absolute = path.join(directory, entry.name)
    const child = path.join(relative, entry.name)
    return entry.isDirectory() ? listFiles(absolute, child) : [child.split(path.sep).join('/')]
  })
}

export function verifyPrebuilds() {
  for (const file of required) {
    assert.ok(existsSync(file), `missing ${file}`)
  }

  validatePrebuildPaths(listFiles('prebuilds'), 'working tree')
  for (const file of expectedPrebuilds) {
    const stats = lstatSync(file)
    assert.ok(stats.isFile() && !stats.isSymbolicLink(), `${file} must be a regular file`)
  }

  const [pack] = JSON.parse(
    execFileSync('npm', ['pack', '--dry-run', '--json', '--ignore-scripts'], {
      encoding: 'utf8',
    })
  )
  const packedFileList = pack.files.map(({ path: file }) => file)
  const packedFiles = new Set(packedFileList)

  for (const file of required) {
    assert.ok(packedFiles.has(file), `${file} is missing from the npm tarball`)
  }
  validatePrebuildPaths(
    packedFileList.filter(file => file.startsWith('prebuilds/') || file.endsWith('.node')),
    'npm pack'
  )

  console.log(`Verified release prebuild manifest: ${expectedPrebuilds.join(', ')}`)
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  verifyPrebuilds()
}
