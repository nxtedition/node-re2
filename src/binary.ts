export type BinaryView = ArrayBufferView
export type Pattern = string | BinaryView

export interface TestManyOptions {
  /**
   * Inputs per native scheduling chunk. A finite value must be a positive safe
   * integer. Omit it for automatic scheduling. Infinity or a value at least as
   * large as the input count disables intra-call parallelism.
   */
  readonly batchSize?: number
}

export interface TestManyAsyncOptions extends TestManyOptions {
  /**
   * Avoid snapshotting input bytes before dispatch. This reduces admission
   * overhead, but every backing store must remain attached, the same size, and
   * unmodified until the returned Promise settles.
   */
  readonly unsafe?: boolean
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

function normalizeBatchSizeValue(batchSize: unknown): number {
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

function validateOptions(options: object | undefined): void {
  if (options === undefined) {
    return
  }
  if (typeof options !== 'object' || options === null) {
    throw new TypeError('options must be an object')
  }
}

export function normalizeBatchSize(options: TestManyOptions | undefined): number {
  validateOptions(options)
  return normalizeBatchSizeValue(options?.batchSize)
}

export function normalizeAsyncOptions(
  options: TestManyAsyncOptions | undefined
): readonly [batchSize: number, unsafe: boolean] {
  validateOptions(options)
  const unsafe = options?.unsafe
  if (unsafe !== undefined && typeof unsafe !== 'boolean') {
    throw new TypeError('unsafe must be a boolean')
  }
  return [normalizeBatchSizeValue(options?.batchSize), unsafe ?? false]
}
