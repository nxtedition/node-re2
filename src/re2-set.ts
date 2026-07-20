import binding from './binding.js'
import type { NativeContext } from './binding.js'
import {
  asBinaryView,
  normalizeBatchSize,
  normalizeInputs,
  normalizePatterns,
  type BinaryView,
  type Pattern,
  type TestManyOptions,
} from './binary.js'

const kCompiledContext = Symbol('compiledContext')

export class RE2Set {
  readonly #context: NativeContext

  constructor(patterns: readonly Pattern[])
  constructor(patterns: readonly Pattern[] | typeof kCompiledContext, context?: NativeContext) {
    if (patterns === kCompiledContext) {
      if (context === undefined) {
        throw new TypeError('Missing compiled RE2Set context')
      }
      this.#context = context
      return
    }
    this.#context = binding.set_init(normalizePatterns(patterns))
  }

  /**
   * Compiles patterns on the Node.js worker pool. Byte-identical ordered
   * pattern sets share one process-wide in-flight compilation and bounded
   * native cache.
   */
  static compileAsync(patterns: readonly Pattern[]): Promise<RE2Set> {
    const normalizedPatterns = normalizePatterns(patterns)
    return binding
      .set_compile_async(normalizedPatterns)
      .then((context) => Reflect.construct(RE2Set, [kCompiledContext, context]) as RE2Set)
  }

  /**
   * @returns The indices of the matching patterns in unspecified order, or an
   * empty array if no patterns matched.
   */
  test(buffer: BinaryView, byteOffset?: number, byteLength?: number): number[] {
    buffer = asBinaryView(buffer, 'buffer')
    if (byteOffset === undefined) {
      byteOffset = 0
    }
    if (byteLength === undefined) {
      byteLength = buffer.byteLength - byteOffset
    }
    return binding.set_test(this.#context, buffer, byteOffset, byteLength)
  }

  /**
   * Matches binary inputs, using automatic scheduling unless `batchSize` is
   * provided. Infinity or a value at least as large as the input count runs on
   * the caller thread.
   */
  testMany(inputs: readonly BinaryView[], options?: TestManyOptions): number[][] {
    return binding.set_test_many(
      this.#context,
      normalizeInputs(inputs),
      normalizeBatchSize(options)
    )
  }
}
