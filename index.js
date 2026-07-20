import { Buffer } from 'node:buffer'
import binding from './binding.js'

const COMPILE_CACHE_MAX_SIZE = 16
const kCompiledContext = Symbol('compiledContext')
const compileCache = new Map()
const compileLocks = new Map()

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

/**
 * @param {unknown} patterns
 * @returns {Buffer[]}
 */
function snapshotPatterns(patterns) {
  if (!Array.isArray(patterns)) {
    throw new TypeError('patterns must be an array')
  }
  return Array.from(patterns, pattern => {
    if (typeof pattern === 'string') {
      return Buffer.from(pattern)
    }
    const view = asBinaryView(pattern, 'pattern')
    return Buffer.from(new Uint8Array(view.buffer, view.byteOffset, view.byteLength))
  })
}

/**
 * @param {readonly Buffer[]} patterns
 * @returns {string}
 */
function compileCacheKey(patterns) {
  return JSON.stringify(patterns.map(pattern => pattern.toString('base64')))
}

function trimCompileCache() {
  while (compileCache.size > COMPILE_CACHE_MAX_SIZE) {
    compileCache.delete(compileCache.keys().next().value)
  }
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
   * sets share one in-flight compilation and a bounded per-environment cache.
   *
   * @param {readonly (string | NodeJS.ArrayBufferView)[]} patterns
   * @returns {Promise<RE2Set>}
   */
  static compileAsync(patterns) {
    const snapshot = snapshotPatterns(patterns)
    const key = compileCacheKey(snapshot)

    const cached = compileCache.get(key)
    if (cached) {
      compileCache.delete(key)
      compileCache.set(key, cached)
      return Promise.resolve(cached)
    }

    const locked = compileLocks.get(key)
    if (locked) {
      return locked
    }

    const compilation = binding
      .set_compile_async(snapshot)
      .then(context => {
        const expressions = new RE2Set(kCompiledContext, context)
        compileCache.set(key, expressions)
        trimCompileCache()
        return expressions
      })
      .finally(() => compileLocks.delete(key))
    compileLocks.set(key, compilation)
    return compilation
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
}
