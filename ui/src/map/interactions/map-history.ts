import { get, writable } from "svelte/store";
import type { Map } from "../model";
import { MapStore } from "../service";
import { cloneMap } from "../core/map-utils";
import { setMapDirty } from "../services/map-sync";

/** Undo/redo for the map editor.
 *
 *  A snapshot is taken *before* each discrete edit — once per gesture, not per
 *  mouse-move event, so dragging a point across the canvas is a single undo
 *  step. Only geometry is covered; the map rotation (compass) is separate
 *  state and is not restored.
 *
 *  The stack lives as long as one map stays loaded; switching, loading or
 *  discarding a map resets it (resetMapHistory), because the snapshots would
 *  otherwise belong to a different map.
 */
const MAX_HISTORY = 50;

let undoStack: Map[] = [];
let redoStack: Map[] = [];

export const canUndoMapEdit = writable(false);
export const canRedoMapEdit = writable(false);

function publish() {
  canUndoMapEdit.set(undoStack.length > 0);
  canRedoMapEdit.set(redoStack.length > 0);
}

/** Remember the current map so the edit that follows can be undone. */
export function recordMapSnapshot() {
  undoStack.push(cloneMap(get(MapStore).map));
  if (undoStack.length > MAX_HISTORY) undoStack.shift();
  redoStack = [];
  publish();
}

function applyMap(map: Map) {
  MapStore.update((store) => ({ ...store, map }));
  // The map now differs from the backend copy again; the editor's debounced
  // sync picks it up like any other change.
  setMapDirty(true);
}

export function undoMapEdit(): boolean {
  const previous = undoStack.pop();
  if (!previous) return false;
  redoStack.push(cloneMap(get(MapStore).map));
  applyMap(previous);
  publish();
  return true;
}

export function redoMapEdit(): boolean {
  const next = redoStack.pop();
  if (!next) return false;
  undoStack.push(cloneMap(get(MapStore).map));
  applyMap(next);
  publish();
  return true;
}

export function resetMapHistory() {
  undoStack = [];
  redoStack = [];
  publish();
}
