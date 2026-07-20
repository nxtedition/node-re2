import { Buffer } from 'node:buffer'
import binding from './binding.js'

const kCompiledContext = Symbol('compiledContext')

/**
 * @param {unknown} value
 * @param {string} name
 * @returns {NodeJS.ArrayBufferView}
 */
function asBinaryView(value, name) {
  if (!ArrayBuffer.isView(value)) {
    throw new TypeError(`${name} must be a Buffer, TypedArray, or DataView`)
  }
  return value
}

/**
 * @param {unknown} pattern
 * @returns {NodeJS.ArrayBufferView}
 */
function encodePattern(pattern) {
  return typeof pattern === 'string'
    ? Buffer.from(pattern)
    : asBinaryView(pattern, 'pattern')
}

export class RE2 {
  #context

  /**
   * @param {string | NodeJS.ArrayBufferView} pattern
   */
  constructor(pattern) {
    this.#context = binding.regex_init(encodePattern(pattern))
  }

  /**
   * @param {NodeJS.ArrayBufferView} buffer
   * @param {number} [byteOffset]
   * @param {number} [byteLength]
   * @returns {boolean} True if the pattern matches, false otherwise.
   */
  test(buffer, byteOffset, byteLength) {
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
   * Matches a batch of binary inputs in parallel.
   *
   * @param {readonly NodeJS.ArrayBufferView[]} inputs
   * @returns {boolean[]}
   */
  testMany(inputs) {
    if (!Array.isArray(inputs)) {
      throw new TypeError('inputs must be an array')
    }
    return binding.regex_test_many(
      this.#context,
      Array.from(inputs, input => asBinaryView(input, 'input'))
    )
  }
}

export class RE2Set {
  #context

  /**
   * @param {readonly (string | NodeJS.ArrayBufferView)[]} patterns
   * @param {unknown} [context]
   */
  constructor(patterns, context) {
    if (patterns === kCompiledContext) {
      this.#context = context
      return
    }
    if (!Array.isArray(patterns)) {
      throw new TypeError('patterns must be an array')
    }
    this.#context = binding.set_init(Array.from(patterns, encodePattern))
  }

  /**
   * Compiles patterns on the Node.js worker pool. Byte-identical ordered pattern
   * sets share one process-wide in-flight compilation and bounded native cache.
   *
   * @param {readonly (string | NodeJS.ArrayBufferView)[]} patterns
   * @returns {Promise<RE2Set>}
   */
  static compileAsync(patterns) {
    if (!Array.isArray(patterns)) {
      throw new TypeError('patterns must be an array')
    }
    return binding
      .set_compile_async(Array.from(patterns, encodePattern))
      .then(context => new RE2Set(kCompiledContext, context))
  }

  /**
   * @param {NodeJS.ArrayBufferView} buffer
   * @param {number} [byteOffset]
   * @param {number} [byteLength]
   * @returns {number[]} The indices of the matching patterns in unspecified order, or an empty array if no patterns matched.
   */
  test(buffer, byteOffset, byteLength) {
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
   * Matches a batch of binary inputs in parallel.
   *
   * @param {readonly NodeJS.ArrayBufferView[]} inputs
   * @returns {number[][]}
   */
  testMany(inputs) {
    if (!Array.isArray(inputs)) {
      throw new TypeError('inputs must be an array')
    }
    return binding.set_test_many(
      this.#context,
      Array.from(inputs, input => asBinaryView(input, 'input'))
    )
  }
}
