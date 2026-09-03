import { describe, expect, it } from "vitest";
import { exportMowerMap } from "./mower-map";
import { emptyMap } from "./map-utils";

describe("exportMowerMap", () => {
  it("does not close dockpoint or waypoint routes", () => {
    const map = emptyMap();
    map.perimeter.points = [
      { x: 0, y: 0 },
      { x: 2, y: 0 },
      { x: 0, y: 2 },
    ];
    map.dockpoints.points = [{ x: 0, y: 0 }, { x: 1, y: 0 }];
    map.waypoints.points = [
      { x: 0, y: 1, tag: 1 },
      { x: 1, y: 1, tag: 3 },
    ];

    const exported = JSON.parse(exportMowerMap(map));

    expect(exported.perimeter).toHaveLength(4);
    expect(exported.dockpoints).toHaveLength(2);
    expect(exported.waypoints).toHaveLength(2);
    expect(exported.waypoints[1]).toEqual({ X: 1, Y: -1, tag: 3 });
  });
});