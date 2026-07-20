export type BinaryView = ArrayBufferView
export type Pattern = string | BinaryView

export interface TestManyOptions {
  /**
   * Inputs per native scheduling chunk. A finite value must be a positive safe
   * integer. Omit it for automatic scheduling. Infinity or a value at least as
   * large as the input count executes sequentially on the caller thread.
   */
  readonly batchSize?: number
}

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
    normalized[index] = asBinaryView(inputs[index], 'input')
  }
  return normalized
}

export function normalizeBatchSize(options: TestManyOptions | undefined): number {
  if (options === undefined) {
    return 0
  }
  if (typeof options !== 'object' || options === null) {
    throw new TypeError('options must be an object')
  }

  const { batchSize } = options
  if (batchSize === undefined) {
    return 0
  }
  if (batchSize === Number.POSITIVE_INFINITY) {
    return batchSize
  }
  if (typeof batchSize !== 'number') {
    throw new TypeError('batchSize must be a number')
  }
  if (!Number.isSafeInteger(batchSize) || batchSize <= 0) {
    throw new RangeError('batchSize must be a positive safe integer or Infinity')
  }
  return batchSize
}
