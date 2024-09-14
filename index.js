import binding from './binding.js'

export class RE2 {
  #context

  constructor(pattern) {
    this.#context = binding.regex_init()
  }
}

export class RE2Set {

}
