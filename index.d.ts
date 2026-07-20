export type BinaryView = ArrayBufferView;

export class RE2 {
  constructor(pattern: string | BinaryView);
  /**
   * @returns True if the pattern matches, false otherwise.
   */
  test(buffer: BinaryView, byteOffset?: number, byteLength?: number): boolean;
  /**
   * Matches a batch of binary inputs in parallel.
   */
  testMany(inputs: readonly BinaryView[]): boolean[];
}

export class RE2Set {
  constructor(patterns: readonly (string | BinaryView)[]);
  /**
   * Compiles patterns on the Node.js worker pool. Byte-identical ordered
   * pattern sets share one process-wide in-flight compilation and bounded
   * native cache.
   */
  static compileAsync(patterns: readonly (string | BinaryView)[]): Promise<RE2Set>;
  /**
   * @returns The indices of the matching patterns in unspecified order, or an empty array if no patterns matched.
   */
  test(buffer: BinaryView, byteOffset?: number, byteLength?: number): number[];
  /**
   * Matches a batch of binary inputs in parallel.
   */
  testMany(inputs: readonly BinaryView[]): number[][];
}
