import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'

import type { BinaryView, Pattern } from './binary.js'

export type NativeContext = object

export interface SetCompileCacheStats {
  readonly compilations: bigint
  readonly cacheHits: bigint
  readonly deduplications: bigint
  readonly size: number
  readonly inFlight: number
}

interface NativeBinding {
  batch_parallelism(inputCount: number, totalBytes: number): number
  regex_init(pattern: Pattern): NativeContext
  regex_test(
    context: NativeContext,
    input: BinaryView,
    byteOffset: number,
    byteLength: number
  ): boolean
  regex_test_many(
    context: NativeContext,
    inputs: readonly BinaryView[]
  ): boolean[]
  set_init(patterns: readonly Pattern[]): NativeContext
  set_compile_async(patterns: readonly Pattern[]): Promise<NativeContext>
  set_compile_cache_stats(): SetCompileCacheStats
  set_test(
    context: NativeContext,
    input: BinaryView,
    byteOffset: number,
    byteLength: number
  ): number[]
  set_test_many(
    context: NativeContext,
    inputs: readonly BinaryView[]
  ): number[][]
}

const require = createRequire(import.meta.url)
const nodeGypBuild = require('node-gyp-build') as (directory: string) => NativeBinding

export default nodeGypBuild(fileURLToPath(new URL('..', import.meta.url)))
