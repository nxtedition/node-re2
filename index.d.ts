export class RE2 {
  constructor(pattern: string | Buffer);
  /**
   * @returns True if the pattern matches, false otherwise.
   */
  test(buffer: Buffer, byteOffset?: number, byteLength?: number): boolean;
}

export class RE2Set {
  constructor(patterns: (string | Buffer)[]);
  /**
   * @returns An array of the indices of the patterns that matched, or an empty array if no patterns matched.
   */
  test(buffer: Buffer, byteOffset?: number, byteLength?: number): number[];
}
