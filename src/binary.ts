export type BinaryView = ArrayBufferView<ArrayBufferLike>
export type Pattern = string | BinaryView

const MAX_BATCH_INPUT_COUNT = 2 ** 20
const MAX_PATTERN_COUNT = 100_000

export function asBinaryView(value: unknown, name: string): BinaryView {
  if (!ArrayBuffer.isView(value)) {
    throw new TypeError(`${name} must be a Buffer, TypedArray, or DataView`)
  }
  return value
}

function validatePattern(pattern: unknown): Pattern {
  return typeof pattern === 'string' ? pattern : asBinaryView(pattern, 'pattern')
}

export function normalizePatterns(patterns: unknown): Pattern[] {
  if (!Array.isArray(patterns)) {
    throw new TypeError('patterns must be an array')
  }
  const length = patterns.length
  if (length > MAX_PATTERN_COUNT) {
    throw new RangeError('Too many patterns')
  }
  const normalized = new Array<Pattern>(length)
  for (let index = 0; index < length; ++index) {
    normalized[index] = validatePattern(patterns[index])
  }
  return normalized
}

export function normalizeInputs(inputs: unknown): BinaryView[] {
  if (!Array.isArray(inputs)) {
    throw new TypeError('inputs must be an array')
  }
  const length = inputs.length
  if (length > MAX_BATCH_INPUT_COUNT) {
    throw new RangeError('Too many inputs')
  }
  const normalized = new Array<BinaryView>(length)
  for (let index = 0; index < length; ++index) {
    normalized[index] = inputs[index] as BinaryView
  }
  return normalized
}
