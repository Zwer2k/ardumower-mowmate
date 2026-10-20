import { describe, expect, it } from "vitest";
import { isJobOnMower, isJobPreview, jobPartProgress, jobRouteIndex, mergeMowJobChunk } from "./mow-job";

const chunk = (offset: number, xy: number[], total = 4) => ({
  id: "job:a,b", crc: 42, timestamp: 7, overlapOnce: false, offset, total, xy,
  ...(offset === 0 ? { parts: [{ id: "a", name: "A", start: 0 }, { id: "b", name: "B", start: 2 }] } : {}),
});

describe("mow job route", () => {
  it("assembles chunks and mirrors y", () => {
    let job = mergeMowJobChunk(null, chunk(0, [0, 0, 1, 1]));
    expect(job?.complete).toBe(false);
    job = mergeMowJobChunk(job, chunk(2, [2, 2, 3, 3]));
    expect(job?.complete).toBe(true);
    expect(job?.points[3]).toEqual({ x: 3, y: -3 });
    expect(job?.parts.length).toBe(2);
  });

  it("drops a chunk that does not continue the route", () => {
    const job = mergeMowJobChunk(null, chunk(0, [0, 0, 1, 1]));
    expect(mergeMowJobChunk(job, chunk(3, [3, 3], 4))).toBeNull();
  });

  it("is on the mower only with matching id and crc", () => {
    const job = mergeMowJobChunk(mergeMowJobChunk(null, chunk(0, [0, 0, 1, 1])), chunk(2, [2, 2, 3, 3]));
    expect(isJobOnMower(job, { uploaded_map_id: "job:a,b", map_crc: 42 })).toBe(true);
    expect(isJobOnMower(job, { uploaded_map_id: "job:a,b", map_crc: 41 })).toBe(false);
    expect(isJobOnMower(job, { uploaded_map_id: "a", map_crc: 42 })).toBe(false);
  });

  it("is a preview until the mower has it", () => {
    const job = mergeMowJobChunk(mergeMowJobChunk(null, chunk(0, [0, 0, 1, 1])), chunk(2, [2, 2, 3, 3]));
    expect(isJobPreview(job, { uploaded_map_id: "other", map_crc: 7 })).toBe(true);
    expect(isJobPreview(job, { uploaded_map_id: "job:a,b", map_crc: 42 })).toBe(false);
    expect(isJobPreview(null, null)).toBe(false);
  });

  it("maps the index of a job part to the whole route", () => {
    const job = { ...mergeMowJobChunk(null, { ...chunk(0, [0, 0, 1, 1, 2, 2, 3, 3]), segment: 1, segmentStarts: [0, 2] })! };
    expect(jobRouteIndex(job, 1)).toBe(3);
    expect(jobRouteIndex(job, -1)).toBe(-1);
  });

  it("splits progress per area", () => {
    const pts = [0, 1, 2, 3, 4, 5].map((x) => ({ x, y: 0 }));
    const parts = [{ id: "a", name: "A", start: 0 }, { id: "b", name: "B", start: 3 }];
    // Mäher fährt Punkt 4 an: A fertig, B mit aktuellem Teilstück 3-4
    const [a, b] = jobPartProgress(pts, parts, 4, true);
    expect(a.todo).toEqual([]);
    expect(a.done[0].map((p) => p.x)).toEqual([0, 1, 2]);
    expect(b.current?.map((p) => p.x)).toEqual([3, 4]);
    expect(b.done[0].map((p) => p.x)).toEqual([2, 3]);
    expect(a.color).not.toBe(b.color);
    // unbekannter Fortschritt: alles offen
    expect(jobPartProgress(pts, parts, 4, false)[0].done).toEqual([]);
  });
});

describe("job outline", () => {
  it("mirrors y and drops short rings", async () => {
    const { parseOutline } = await import("./mow-job");
    const o = parseOutline({ perimeter: [[0, 0], [2, 0], [2, 3]], exclusions: [[[1, 1], [1, 2]]], dockpoints: [[5, 6]], rotation: 30 })!;
    expect(o.perimeter[2]).toEqual({ x: 2, y: -3 });
    expect(o.exclusions).toEqual([]);
    expect(o.dockpoints).toEqual([{ x: 5, y: -6 }]);
    expect(o.rotation).toBe(30);
    expect(parseOutline({ perimeter: [[0, 0]] })).toBeUndefined();
  });
});
