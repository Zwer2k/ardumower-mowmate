import { describe, expect, it } from "vitest";
import { estimateMowerCapacity, capacityExceeded } from "./mower-capacity";

describe("mower capacity estimate", () => {
  it("derives the point limit from the free memory reported by Sunray", () => {
    // freem=19623 from a MOW800 log: (19623 - 5000) / 8 = 1827 points
    const c = estimateMowerCapacity(19623, 4500, 0);
    expect(c.maxPoints).toBe(1827);
    expect(capacityExceeded(c)).toBe(true);
    expect(c.lowerBound).toBe(false);
  });

  it("counts the memory of the map the upload replaces", () => {
    // The mower holds this map (1000 points * 4 bytes), freed at upload start.
    const c = estimateMowerCapacity(15000, 1500, 1000);
    expect(c.maxPoints).toBe(Math.floor((15000 + 4000 - 5000) / 8));
    expect(capacityExceeded(c)).toBe(false);
  });

  it("marks the limit as a lower bound when the loaded map is unknown", () => {
    const c = estimateMowerCapacity(15000, 100, null);
    expect(c.lowerBound).toBe(true);
  });

  it("does not limit when the free memory is unknown or very large (Linux)", () => {
    expect(estimateMowerCapacity(undefined, 9999, 0).maxPoints).toBeNull();
    expect(estimateMowerCapacity(0, 9999, 0).maxPoints).toBeNull();
    expect(estimateMowerCapacity(50 * 1024 * 1024, 9999, 0).maxPoints).toBeNull();
  });
});
