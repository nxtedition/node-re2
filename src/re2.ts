import binding from './binding.js'
import type { NativeContext } from './binding.js'
import {
  asBinaryView,
  normalizeAsyncOptions,
  normalizeBatchSize,
  normalizeInputs,
  type BinaryView,
  type Pattern,
  type TestManyAsyncOptions,
  type TestManyOptions,
} from './binary.js'

export class RE2 {
  readonly #context: NativeContext

  constructor(pattern: Pattern) {
    this.#context = binding.regex_init(
      typeof pattern === 'string' ? pattern : asBinaryView(pattern, 'pattern')
    )
  }

  /** @returns True if the pattern matches, false otherwise. */
  test(buffer: BinaryView, byteOffset?: number, byteLength?: number): boolean {
    buffer = asBinaryView(buffer, 'buffer')
    if (byteOffset === undefined) {
      byteOffset = 0
    }
    if (byteLength === undefined) {
      byteLength = buffer.byteLength - byteOffset
    }
    return binding.regex_test(this.#context, buffer, byteOffset, byteLength)
  }

  /**
   * Matches binary inputs, using automatic scheduling unless `batchSize` is
   * provided. Infinity or a value at least as large as the input count runs on
   * the caller thread.
   */
  testManySync(inputs: readonly BinaryView[], options?: TestManyOptions): boolean[] {
    return binding.regex_test_many(
      this.#context,
      normalizeInputs(inputs),
      normalizeBatchSize(options)
    )
  }

  /** Compatibility alias for `testManySync()`. */
  testMany(inputs: readonly BinaryView[], options?: TestManyOptions): boolean[] {
    return this.testManySync(inputs, options)
  }

  /**
   * Matches binary inputs off the event loop. Inputs are snapshotted before
   * dispatch unless `unsafe: true` is provided.
   */
  testManyAsync(
    inputs: readonly BinaryView[],
    options?: TestManyAsyncOptions
  ): Promise<boolean[]> {
    const normalizedInputs = normalizeInputs(inputs)
    const [batchSize, unsafe] = normalizeAsyncOptions(options)
    return binding.regex_test_many_async(this.#context, normalizedInputs, batchSize, unsafe)
  }
}
