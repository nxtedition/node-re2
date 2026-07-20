import binding from './binding.js'
import type { NativeContext } from './binding.js'
import {
  asBinaryView,
  normalizeInputs,
  type BinaryView,
  type Pattern,
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

  /** Matches a batch of binary inputs. */
  testMany(inputs: readonly BinaryView[]): boolean[] {
    return binding.regex_test_many(this.#context, normalizeInputs(inputs))
  }
}
