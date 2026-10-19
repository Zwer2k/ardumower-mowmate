import { describe, expect, it } from "vitest";
import type { MapMeta, ScheduleEntry } from "../model";
import { jobName, jobTree, rootMapOf } from "./area-job";
import { canonicalScheduleJson, entryAreaIds } from "../stores/schedule";

const meta = (id: string, name: string, parentId?: string): MapMeta =>
  ({ id, name, area: 1, hash: "", crc: 0, rotation: 0, timestamp: 0, unsaved: false, ...(parentId ? { parentId } : {}) });

const maps = [meta("m", "Garden"), meta("b", "Back", "m"), meta("a", "Area 10", "m"), meta("a2", "Area 2", "m"), meta("s", "Sub", "b")];

describe("area jobs", () => {
  it("finds the main map of a sub-sub area", () => {
    expect(rootMapOf("s", maps)?.id).toBe("m");
  });

  it("lists the main map and its areas as a tree", () => {
    const tree = jobTree(maps[0], maps);
    expect(tree.map((e) => `${e.depth}:${e.map.id}`)).toEqual(["0:m", "1:a2", "1:a", "1:b", "2:s"]);
    expect(tree[0].label).toBe("Garden (whole map)");
  });

  it("names a job in its order", () => {
    expect(jobName(["b", "m"], maps)).toBe("Back, Garden");
  });

  it("turns an old schedule entry into a list with its map", () => {
    const old = { id: 1, enabled: true, name: "Mow", mapId: "m", mapName: "Garden", mode: 0, hour: 8, minute: 0, daysOfWeek: 127, daysOfMonth: 1 } as ScheduleEntry;
    expect(entryAreaIds(old)).toEqual(["m"]);
    // Echo des Backends mit ergänzten areaIds gilt als dasselbe
    const echo = { ...old, areaIds: ["m"] };
    expect(canonicalScheduleJson({ enabled: true, entries: [old] })).toBe(canonicalScheduleJson({ enabled: true, entries: [echo] }));
  });
});
