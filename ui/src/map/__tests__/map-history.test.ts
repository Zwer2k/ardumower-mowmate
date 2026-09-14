import { describe, it, expect, beforeEach } from "vitest";
import { get } from "svelte/store";
import { MapStore, emptyMap, emptyPresentation } from "../service";
import { isMapDirty } from "../services/map-sync";
import {
  recordMapSnapshot,
  undoMapEdit,
  redoMapEdit,
  resetMapHistory,
  canUndoMapEdit,
  canRedoMapEdit,
} from "../interactions/map-history";

function setPerimeter(xs: number[]) {
  MapStore.update((s) => ({
    ...s,
    map: { ...s.map, perimeter: { points: xs.map((x) => ({ x, y: 0 })) } },
  }));
}

function perimeterXs(): number[] {
  return get(MapStore).map.perimeter.points.map((p) => p.x);
}

describe("map edit history", () => {
  beforeEach(() => {
    MapStore.set({ map: emptyMap(), presentation: emptyPresentation() });
    resetMapHistory();
    isMapDirty.set(false);
  });

  it("starts with nothing to undo or redo", () => {
    expect(get(canUndoMapEdit)).toBe(false);
    expect(get(canRedoMapEdit)).toBe(false);
    expect(undoMapEdit()).toBe(false);
    expect(redoMapEdit()).toBe(false);
  });

  it("restores the map recorded before an edit", () => {
    setPerimeter([1, 2]);
    recordMapSnapshot();
    setPerimeter([1, 2, 3]);

    expect(get(canUndoMapEdit)).toBe(true);
    expect(undoMapEdit()).toBe(true);
    expect(perimeterXs()).toEqual([1, 2]);
  });

  it("redoes an undone edit", () => {
    setPerimeter([1]);
    recordMapSnapshot();
    setPerimeter([1, 2]);

    undoMapEdit();
    expect(perimeterXs()).toEqual([1]);
    expect(get(canRedoMapEdit)).toBe(true);

    expect(redoMapEdit()).toBe(true);
    expect(perimeterXs()).toEqual([1, 2]);
  });

  it("walks back through several edits in order", () => {
    setPerimeter([1]);
    recordMapSnapshot();
    setPerimeter([1, 2]);
    recordMapSnapshot();
    setPerimeter([1, 2, 3]);

    undoMapEdit();
    expect(perimeterXs()).toEqual([1, 2]);
    undoMapEdit();
    expect(perimeterXs()).toEqual([1]);
    expect(get(canUndoMapEdit)).toBe(false);
  });

  it("drops the redo stack once a new edit is recorded", () => {
    setPerimeter([1]);
    recordMapSnapshot();
    setPerimeter([1, 2]);
    undoMapEdit();
    expect(get(canRedoMapEdit)).toBe(true);

    recordMapSnapshot();
    setPerimeter([9]);

    expect(get(canRedoMapEdit)).toBe(false);
    expect(redoMapEdit()).toBe(false);
  });

  it("marks the map dirty again after undoing", () => {
    setPerimeter([1]);
    recordMapSnapshot();
    setPerimeter([1, 2]);
    isMapDirty.set(false);

    undoMapEdit();

    expect(get(isMapDirty)).toBe(true);
  });

  it("snapshots are independent copies of the map", () => {
    setPerimeter([1, 2]);
    recordMapSnapshot();
    // Mutate the live map in place, as a drag handler would.
    get(MapStore).map.perimeter.points[0].x = 99;
    setPerimeter([5]);

    undoMapEdit();

    expect(perimeterXs()).toEqual([1, 2]);
  });

  it("resetMapHistory clears both stacks", () => {
    setPerimeter([1]);
    recordMapSnapshot();
    setPerimeter([1, 2]);
    undoMapEdit();

    resetMapHistory();

    expect(get(canUndoMapEdit)).toBe(false);
    expect(get(canRedoMapEdit)).toBe(false);
  });
});
