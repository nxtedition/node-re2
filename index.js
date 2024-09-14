import binding from './binding.js'

export class RE2 {
  #context

  constructor(pattern) {
    this.#context = binding.regex_init(typeof pattern === 'string' ? Buffer.from(pattern) : pattern)
  }

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

  constructor(patterns) {
    this.#context = binding.set_init(patterns.map(pattern => typeof pattern === 'string' ? Buffer.from(pattern) : pattern))
  }

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
