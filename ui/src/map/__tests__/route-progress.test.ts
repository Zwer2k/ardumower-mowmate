import { describe, it, expect } from "vitest";
import { splitRouteProgress, routeTransits, routeTransitPaths } from "../core/route-progress";

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

describe("route transit paths", () => {
  it("uses the modem's path when start and end match, a straight line otherwise", () => {
    const pts = [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 5, y: 5, routeBreakBefore: true }, { x: 6, y: 5 }];
    const modem = [[{ x: 1, y: 0.01 }, { x: 3, y: 2 }, { x: 5, y: 5 }]];
    expect(routeTransitPaths(pts, modem)).toEqual([{ path: modem[0], exact: true }]);
    expect(routeTransitPaths(pts, null)).toEqual([{ path: [pts[1], pts[2]], exact: false }]);
  });
});
