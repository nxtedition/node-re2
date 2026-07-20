import { Buffer } from 'node:buffer'
import { RE2, RE2Set, type BinaryView } from '@nxtedition/re2'

const buffer = Buffer.from('foo')
const typedArray = new Uint8Array(buffer)
const dataView = new DataView(typedArray.buffer)
const sharedView = new Uint8Array(new SharedArrayBuffer(3))
const views: readonly BinaryView[] = [buffer, typedArray, dataView, sharedView]

const expression = new RE2(typedArray)
const expressions = new RE2Set(['foo', buffer, dataView] as const)
const asyncExpressions: RE2Set = await RE2Set.compileAsync([
  'foo',
  buffer,
  dataView,
] as const)
const batchMatches: boolean[] = expression.testMany(views)
const batchIndices: number[][] = expressions.testMany(views)
void [batchMatches, batchIndices]

for (const view of views) {
  const matches: boolean = expression.test(view, 0, view.byteLength)
  const indices: number[] = expressions.test(view)
  const asyncIndices: number[] = asyncExpressions.test(view)
  void [matches, indices, asyncIndices]
}

// @ts-expect-error patterns must be strings or binary views
new RE2(42)
// @ts-expect-error raw ArrayBuffers are not views
new RE2(new ArrayBuffer(3))
// @ts-expect-error input must be a binary view
expression.test('foo')
// @ts-expect-error testMany inputs must be binary views
expression.testMany(['foo'])
// @ts-expect-error RE2Set requires an array
new RE2Set('foo')
// @ts-expect-error testMany requires an array
expressions.testMany(buffer)
// @ts-expect-error RE2Set.compileAsync requires an array
RE2Set.compileAsync('foo')
