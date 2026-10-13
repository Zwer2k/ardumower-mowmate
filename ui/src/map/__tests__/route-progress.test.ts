import { describe, it, expect } from "vitest";
import { splitRouteProgress } from "../core/route-progress";

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
