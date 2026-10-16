import { describe, it, expect } from "vitest";
import { splitRouteProgress, routeTransits, mergeTransits, routeLength } from "../core/route-progress";

const pts = [0, 1, 2, 3, 4].map((i) => ({ x: i, y: 0 }));

describe("route progress", () => {
  it("is all open without a known progress", () => {
    const r = splitRouteProgress(pts, 2, false);
    expect(r.done).toEqual([]);
    expect(r.current).toBeNull();
    expect(r.todo).toEqual([pts]);
  });

  it("splits at the waypoint the mower is heading to", () => {
    const r = splitRouteProgress(pts, 3, true);
    expect(r.done).toEqual([pts.slice(0, 3)]);
    expect(r.current).toEqual([pts[2], pts[3]]);
    expect(r.todo).toEqual([pts.slice(2)]);
  });

  it("breaks lines at routeBreakBefore", () => {
    const broken = pts.map((p, i) => (i === 2 ? { ...p, routeBreakBefore: true } : p));
    const r = splitRouteProgress(broken, -1, true);
    expect(r.todo).toEqual([broken.slice(0, 2), broken.slice(2)]);
  });
});

describe("route transits", () => {
  it("connects the end of one part with the start of the next", () => {
    const pts = [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 5, y: 5, routeBreakBefore: true }, { x: 6, y: 5 }];
    expect(routeTransits(pts)).toEqual([[pts[1], pts[2]]]);
  });
});

describe("merge transits", () => {
  it("inserts the modem's path as waypoints and closes the gap", () => {
    const pts = [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 5, y: 5, routeBreakBefore: true }, { x: 6, y: 5 }];
    const modem = [[{ x: 1, y: 0.01 }, { x: 3, y: 2 }, { x: 5, y: 5 }]];
    const merged = mergeTransits(pts, modem);
    expect(merged.map((p) => [p.x, p.y])).toEqual([[0, 0], [1, 0], [3, 2], [5, 5], [6, 5]]);
    expect(routeTransits(merged)).toEqual([]);
    // ohne Weg bleibt die Lücke
    expect(routeTransits(mergeTransits(pts, null))).toEqual([[pts[1], pts[2]]]);
  });
});

describe("route length", () => {
  it("sums the segment lengths", () => {
    expect(routeLength([{ x: 0, y: 0 }, { x: 3, y: 4 }, { x: 3, y: 10 }])).toBeCloseTo(11);
  });
});
