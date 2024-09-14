import binding from './binding.js'

export class RE2 {
  #context

  constructor(pattern) {
    this.#context = binding.regex_init(typeof pattern === 'string' ? Buffer.from(pattern) : pattern)
  }

  test(value) {
    return binding.regex_test(this.#context, typeof value === 'string' ? Buffer.from(value) : value)
  }
}

export class RE2Set {

}
