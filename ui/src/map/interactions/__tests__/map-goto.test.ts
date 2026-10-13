import { describe, it, expect, vi } from "vitest";
import { get } from "svelte/store";
import { createMapSelection, setGotoTarget, updateGotoFromMower, type GotoState } from "../map-goto";

// Kartenkoordinaten der UI: y ist gegenüber der Mäherposition gespiegelt.
function driving(x: number, y: number): GotoState {
  return {
    targetDist: 0,
    targetBearing: 0,
    routeStarted: false,
    ...setGotoTarget(x, -y),
    isDriving: true,
  } as GotoState;
}

describe("goto target", () => {
  it("tracks distance to the target", () => {
    const s = updateGotoFromMower(driving(3, 4), { x: 0, y: 0 }, 0);
    expect(s.targetDist).toBeCloseTo(5);
  });

  it("disappears when the mower stops at the target after driving the route", () => {
    let s = driving(5, 0);
    s = updateGotoFromMower(s, { x: 0, y: 0 }, 1);   // route started
    s = updateGotoFromMower(s, { x: 4.9, y: 0 }, 1);
    expect(s.targetSet).toBe(true);
    s = updateGotoFromMower(s, { x: 4.95, y: 0 }, 0); // IDLE at the target
    expect(s.targetSet).toBe(false);
    expect(s.isDriving).toBe(false);
  });

  it("stays while the mower has not started the route yet", () => {
    const s = updateGotoFromMower(driving(0.2, 0), { x: 0, y: 0 }, 0);
    expect(s.targetSet).toBe(true);
    expect(s.isDriving).toBe(true);
  });

  it("stays for a new try when the drive ends away from the target", () => {
    let s = driving(5, 0);
    s = updateGotoFromMower(s, { x: 0, y: 0 }, 1);
    s = updateGotoFromMower(s, { x: 2, y: 0 }, 0);   // cancelled or error
    expect(s.targetSet).toBe(true);
    expect(s.isDriving).toBe(false);
  });

  it("does not react to the mower reaching the spot without a drive", () => {
    const s = updateGotoFromMower({ ...driving(1, 0), isDriving: false }, { x: 1, y: 0 }, 0);
    expect(s.targetSet).toBe(true);
  });
});

describe("map selection before a goto target", () => {
  it("needs a second click within the hint time", () => {
    vi.useFakeTimers();
    const sel = createMapSelection(2500);
    expect(sel.click()).toBe(false);          // selects the map
    expect(get(sel.hint)).toBe(true);
    expect(sel.click()).toBe(true);           // sets the target
    expect(get(sel.hint)).toBe(false);
    vi.advanceTimersByTime(2000);
    expect(sel.click()).toBe(true);           // moves the target directly
    vi.advanceTimersByTime(2600);
    expect(sel.click()).toBe(false);          // expired: two clicks again
    vi.useRealTimers();
  });

  it("starts over after another action", () => {
    const sel = createMapSelection(2500);
    sel.click();
    sel.interact();
    expect(get(sel.hint)).toBe(false);
    expect(sel.click()).toBe(false);
    sel.destroy();
  });
});
