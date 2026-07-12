export type BinaryView = ArrayBufferView;

export class RE2 {
  constructor(pattern: string | BinaryView);
  /**
   * @returns True if the pattern matches, false otherwise.
   */
  test(buffer: BinaryView, byteOffset?: number, byteLength?: number): boolean;
}

export class RE2Set {
  constructor(patterns: readonly (string | BinaryView)[]);
  /**
   * @returns The indices of the matching patterns in unspecified order, or an empty array if no patterns matched.
   */
  test(buffer: BinaryView, byteOffset?: number, byteLength?: number): number[];
}
