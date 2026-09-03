import { describe, expect, it } from "vitest";
import { filterWaypointsByToggles, WpTag } from "./waypoint-filter";
import { emptyMap } from "./map-utils";

const map = emptyMap();

const settings = {
  pattern: 0,
  width: 0.2,
  angle: 0,
  distanceToBorder: 0,
  borderLaps: 1,
  mowBorderCcw: false,
  doMowArea: true,
  doMowPerimeter: true,
  doMowBorder: true,
  doMowExclusions: true,
  doMowExclusionBorder: false,
};

describe("filterWaypointsByToggles", () => {
  it("keeps Area connectors when only Area is enabled", () => {
    const points = [
      { x: 0, y: 0, tag: WpTag.AREA },
      { x: 1, y: 0, tag: WpTag.AREA, conn: true },
      { x: 2, y: 0, tag: WpTag.AREA },
      { x: 3, y: 0, tag: WpTag.BORDER },
    ];

    const filtered = filterWaypointsByToggles(points, map, { ...settings, doMowBorder: false });

    expect(filtered).toHaveLength(3);
    expect(filtered.map((point) => point.tag)).toEqual([WpTag.AREA, WpTag.AREA, WpTag.AREA]);
  });

  it("removes an Area connector when Area is disabled", () => {
    const points = [
      { x: 0, y: 0, tag: WpTag.AREA },
      { x: 1, y: 0, tag: WpTag.AREA, conn: true },
      { x: 2, y: 0, tag: WpTag.BORDER },
    ];

    const filtered = filterWaypointsByToggles(points, map, { ...settings, doMowArea: false });

    expect(filtered).toEqual([{ x: 2, y: 0, tag: WpTag.BORDER, routeBreakBefore: true }]);
  });

  it("removes mixed connectors when Area or Border is disabled", () => {
    const points = [
      { x: 0, y: 0, tag: WpTag.AREA },
      { x: 1, y: 0, tag: WpTag.CONNECTOR, conn: true },
      { x: 2, y: 0, tag: WpTag.BORDER },
    ];

    const areaOnly = filterWaypointsByToggles(points, map, { ...settings, doMowBorder: false });
    const borderOnly = filterWaypointsByToggles(points, map, { ...settings, doMowArea: false });

    expect(areaOnly).toEqual([{ x: 0, y: 0, tag: WpTag.AREA }]);
    expect(borderOnly).toEqual([{ x: 2, y: 0, tag: WpTag.BORDER, routeBreakBefore: true }]);
  });
});