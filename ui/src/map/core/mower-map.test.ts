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
import { areasFromModem, parseAreas, selectAreas, toggleArea, areaDepth, importMowerMap } from "./mower-map";

describe("areas in the map file", () => {
  const modem = [
    { id: "a", name: "Alpha", parentId: "m", map: { selection: [{ X: 0, Y: 0 }] } },
    { id: "b", name: "Beta", parentId: "m", map: {} },
    { id: "c", name: "Beta left", parentId: "b", map: { mowOfs: 0.2 } },
  ];

  it("orders the modem list as a tree", () => {
    const shuffled = [modem[2], modem[0], modem[1]];
    const areas = areasFromModem(shuffled, "m");
    expect(areas.map((a) => [a.name, a.parent])).toEqual([["Alpha", -1], ["Beta", -1], ["Beta left", 1]]);
  });

  it("turns the modem list into parent indices", () => {
    const areas = areasFromModem(modem, "m");
    expect(areas.map((a) => a.parent)).toEqual([-1, -1, 1]);
    expect(areaDepth(areas, 2)).toBe(1);
  });

  it("drops children of an unselected area and keeps indices consistent", () => {
    const areas = areasFromModem(modem, "m");
    expect(selectAreas(areas, [true, false, true]).map((a) => a.name)).toEqual(["Alpha"]);
    expect(selectAreas(areas, [false, true, true]).map((a) => a.parent)).toEqual([-1, 0]);
  });

  it("selects parents with a child and clears children with a parent", () => {
    const areas = areasFromModem(modem, "m");
    expect(toggleArea(areas, [false, false, false], 2, true)).toEqual([false, true, true]);
    expect(toggleArea(areas, [true, true, true], 1, false)).toEqual([true, false, false]);
  });

  it("parses areas from a file and skips invalid ones", () => {
    const parsed = parseAreas([
      { name: "A", parent: -1, map: {} },
      { name: "bad", parent: 5, map: {} },
      { name: "child of bad", parent: 1, map: {} },
      { name: "B", parent: 0, map: {} },
    ]);
    expect(parsed.map((a) => [a.name, a.parent])).toEqual([["A", -1], ["B", 0]]);
    expect(parseAreas("nope")).toEqual([]);
  });

  it("round-trips areas through export and import", () => {
    const imported = importMowerMap(JSON.stringify({ perimeter: [{ X: 0, Y: 0 }, { X: 1, Y: 0 }, { X: 1, Y: 1 }] }))!;
    const json = exportMowerMap(imported.map, { areas: areasFromModem(modem, "m") });
    const again = importMowerMap(json)!;
    expect(again.areas.map((a) => a.name)).toEqual(["Alpha", "Beta", "Beta left"]);
    expect(again.areas[2].map).toEqual({ mowOfs: 0.2 });
  });
});
