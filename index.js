import binding from './binding.js'

export class RE2 {
  #context

  /**
   * @param {string | Buffer} pattern
   */
  constructor(pattern) {
    this.#context = binding.regex_init(typeof pattern === 'string' ? Buffer.from(pattern) : pattern)
  }

  /**
   * @param {Buffer} buffer
   * @param {number} [byteOffset]
   * @param {number} [byteLength]
   * @returns {boolean} True if the pattern matches, false otherwise.
   */
  test(buffer, byteOffset, byteLength) {
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
   * @param {(string | Buffer)[]} patterns
   */
  constructor(patterns) {
    this.#context = binding.set_init(patterns.map(pattern => typeof pattern === 'string' ? Buffer.from(pattern) : pattern))
  }

  /**
   * @param {Buffer} buffer
   * @param {number} [byteOffset]
   * @param {number} [byteLength]
   * @returns {number[]} An array of the indices of the patterns that matched, or an empty array if no patterns matched.
   */
  test(buffer, byteOffset, byteLength) {
    if (byteOffset === undefined) {
      byteOffset = 0
    }
    if (byteLength === undefined) {
      byteLength = buffer.byteLength - byteOffset
    }

    return binding.set_test(this.#context, buffer, byteOffset, byteLength)
  }
}
