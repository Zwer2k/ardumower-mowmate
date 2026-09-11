import { describe, expect, it } from "vitest";
import { expectedMowerMapCrc, isMowerMapSynced } from "./map-sync";

describe("mower map synchronization", () => {
  it("uses the uploaded snapshot CRC for the current map", () => {
    const state = {
      map_crc: -20719,
      uploaded_map_id: "lines-map",
      uploaded_map_crc: -20719,
    };

    expect(expectedMowerMapCrc(state, "lines-map", 12345)).toBe(-20719);
    expect(isMowerMapSynced(state, "lines-map", 12345)).toBe(true);
  });

  it("ignores an uploaded snapshot CRC from another map", () => {
    const state = {
      map_crc: -20719,
      uploaded_map_id: "rings-map",
      uploaded_map_crc: -20719,
    };

    expect(expectedMowerMapCrc(state, "lines-map", 12345)).toBe(12345);
    expect(isMowerMapSynced(state, "lines-map", 12345)).toBe(false);
  });
});
