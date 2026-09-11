import { describe, it, expect, beforeEach } from "vitest";
import { get } from "svelte/store";
import { cloneMap, emptyMap, MapStore, buildMapFromChunkStores, currentMapRotationStore } from "../service";
import { handleMapChunk, mapChunkProgress, MapPointType, resetMapChunkBuffer } from "../map-chunk-buffer";

describe("cloneMap", () => {
  it("should create a deep copy of all point arrays", () => {
    const map = {
      perimeter: { points: [{ x: 1, y: 2 }] },
      exclusions: [{ points: [{ x: 3, y: 4 }] }],
      dockpoints: { points: [{ x: 5, y: 6 }] },
      searchWire: { points: [{ x: 6, y: 7 }] },
      waypoints: { points: [{ x: 7, y: 8 }] },
    };
    const cloned = cloneMap(map);

    expect(cloned).toEqual(map);
    expect(cloned.perimeter.points[0]).not.toBe(map.perimeter.points[0]);
    expect(cloned.exclusions[0].points[0]).not.toBe(map.exclusions[0].points[0]);
    expect(cloned.dockpoints.points[0]).not.toBe(map.dockpoints.points[0]);
    expect(cloned.searchWire.points[0]).not.toBe(map.searchWire.points[0]);
    expect(cloned.waypoints.points[0]).not.toBe(map.waypoints.points[0]);
  });
});

describe("buildMapFromChunkStores", () => {
  beforeEach(() => {
    resetMapChunkBuffer();
    currentMapRotationStore.set(0);
  });

  it("should build a map from chunk stores with y-flipped coordinates", () => {
    handleMapChunk({ pointType: MapPointType.Perimeter, total: 1, startIndex: 0, points: [{ X: 10, Y: 20 }] });
    handleMapChunk({ pointType: MapPointType.Waypoints, total: 1, startIndex: 0, points: [{ X: 30, Y: 40 }] });
    handleMapChunk({ pointType: MapPointType.Dockpoints, total: 1, startIndex: 0, points: [{ X: 50, Y: 60 }] });
    handleMapChunk({ pointType: MapPointType.Exclusion, total: 1, startIndex: 0, exclusionIdx: 0, points: [{ X: 70, Y: 80 }] });

    const map = buildMapFromChunkStores();
    expect(map.perimeter.points).toEqual([{ x: 10, y: -20 }]);
    expect(map.waypoints.points).toEqual([{ x: 30, y: -40 }]);
    expect(map.dockpoints.points).toEqual([{ x: 50, y: -60 }]);
    expect(map.exclusions).toEqual([{ points: [{ x: 70, y: -80 }] }]);
  });
});

describe("MapStore", () => {
  beforeEach(() => {
    resetMapChunkBuffer();
    currentMapRotationStore.set(0);
  });

  it("should update when chunk stores are filled", () => {
    const before = get(MapStore);
    expect(before.map.perimeter.points).toEqual([]);

    handleMapChunk({ pointType: MapPointType.Perimeter, total: 1, startIndex: 0, points: [{ X: 10, Y: 20 }] });

    const after = get(MapStore);
    expect(after.map.perimeter.points).toEqual([{ x: 10, y: -20 }]);
    expect(after.presentation).toBeDefined();
  });

  it("preserves waypoint tags received through map chunks", () => {
    handleMapChunk({
      pointType: MapPointType.Waypoints,
      total: 1,
      startIndex: 0,
      points: [{ X: 30, Y: 40, tag: 3 }],
    });

    expect(get(MapStore).map.waypoints.points).toEqual([{ x: 30, y: -40, conn: false, tag: 3 }]);
  });

  it("publishes a transfer-ID map atomically after the final waypoint chunk", () => {
    handleMapChunk({ transferId: 7, pointType: MapPointType.Perimeter, total: 0, reset: true });
    handleMapChunk({ transferId: 7, pointType: MapPointType.Exclusion, total: 0, reset: true });
    handleMapChunk({ transferId: 7, pointType: MapPointType.Dockpoints, total: 0, reset: true });
    handleMapChunk({ transferId: 7, pointType: MapPointType.SearchWire, total: 0, reset: true });
    handleMapChunk({ transferId: 7, pointType: MapPointType.Waypoints, total: 0, reset: true });
    handleMapChunk({ transferId: 7, pointType: MapPointType.Perimeter, total: 1, startIndex: 0, points: [{ X: 10, Y: 20 }] });
    handleMapChunk({ transferId: 7, pointType: MapPointType.Dockpoints, total: 1, startIndex: 0, points: [{ X: 30, Y: 40 }] });

    expect(get(MapStore).map.perimeter.points).toEqual([]);
    expect(get(MapStore).map.dockpoints.points).toEqual([]);

    handleMapChunk({ transferId: 7, pointType: MapPointType.Waypoints, total: 0, startIndex: 0, points: [] });

    expect(get(MapStore).map.perimeter.points).toEqual([]);

    handleMapChunk({ transferId: 7, pointType: MapPointType.Waypoints, total: 0, complete: true });

    expect(get(MapStore).map.perimeter.points).toEqual([{ x: 10, y: -20 }]);
    expect(get(MapStore).map.dockpoints.points).toEqual([{ x: 30, y: -40 }]);
  });

  it("ignores chunks from an older transfer after a newer map was published", () => {
    handleMapChunk({ transferId: 10, pointType: MapPointType.Perimeter, total: 0, reset: true });
    handleMapChunk({ transferId: 10, pointType: MapPointType.Exclusion, total: 0, reset: true });
    handleMapChunk({ transferId: 10, pointType: MapPointType.Dockpoints, total: 0, reset: true });
    handleMapChunk({ transferId: 10, pointType: MapPointType.SearchWire, total: 0, reset: true });
    handleMapChunk({ transferId: 10, pointType: MapPointType.Waypoints, total: 0, reset: true });
    handleMapChunk({ transferId: 10, pointType: MapPointType.Perimeter, total: 1, startIndex: 0, points: [{ X: 10, Y: 20 }] });
    handleMapChunk({ transferId: 10, pointType: MapPointType.Waypoints, total: 0, startIndex: 0, points: [] });
    handleMapChunk({ transferId: 10, pointType: MapPointType.Waypoints, total: 0, complete: true });
    handleMapChunk({ transferId: 9, pointType: MapPointType.Perimeter, total: 1, startIndex: 0, points: [{ X: 99, Y: 99 }] });
    handleMapChunk({ transferId: 9, pointType: MapPointType.Waypoints, total: 0, complete: true });

    expect(get(MapStore).map.perimeter.points).toEqual([{ x: 10, y: -20 }]);
  });

  it("reports cumulative loading progress until the transfer completes", () => {
    handleMapChunk({ transferId: 12, transferTotal: 2, pointType: MapPointType.Perimeter, total: 0, reset: true });
    handleMapChunk({ transferId: 12, transferTotal: 2, pointType: MapPointType.Exclusion, total: 0, reset: true });
    handleMapChunk({ transferId: 12, transferTotal: 2, pointType: MapPointType.Dockpoints, total: 0, reset: true });
    handleMapChunk({ transferId: 12, transferTotal: 2, pointType: MapPointType.SearchWire, total: 0, reset: true });
    handleMapChunk({ transferId: 12, transferTotal: 2, pointType: MapPointType.Waypoints, total: 0, reset: true });
    handleMapChunk({ transferId: 12, transferTotal: 2, pointType: MapPointType.Perimeter, total: 1, startIndex: 0, points: [{ X: 1, Y: 2 }] });

    expect(get(mapChunkProgress)).toEqual({ received: 1, total: 2, label: "Loading perimeter" });

    handleMapChunk({ transferId: 12, transferTotal: 2, pointType: MapPointType.Waypoints, total: 1, startIndex: 0, points: [{ X: 3, Y: 4 }] });
    expect(get(mapChunkProgress)?.received).toBe(2);

    handleMapChunk({ transferId: 12, pointType: MapPointType.Waypoints, total: 0, complete: true });
    expect(get(mapChunkProgress)).toBeNull();
  });
});
