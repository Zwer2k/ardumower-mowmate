import { writable, get } from "svelte/store";
import type { Writable } from "svelte/store";
import type { Map, MapPresentation } from "./model";
import type { DrivenTrackData, MapMeta, MapSetData } from "../model";
import {
  perimeterStore,
  dockpointsStore,
  searchWireStore,
  exclusionsStore,
  waypointsStore,
} from "./map-chunk-buffer";
import { calculatePresentation } from "./core/presentation";
import { cloneMap, emptyMap, emptyPresentation } from "./core/map-utils";
import { loadCachedMap, saveCachedMap, isCachedMapCurrent } from "./map-cache";

export { cloneMap, emptyMap, emptyPresentation };
export { calculatePresentation } from "./core/presentation";
export { rotatePointsAroundOrigin, pointsToEdges, pointsForPolygon, edgeArrowPath, polygonArrowPath } from "./core/geometry";

export const drivenTrackStore = writable<DrivenTrackData | null>(null);
export const currentMapRotationStore = writable<number>(0);

export interface StoredMap {
  map: Map;
  presentation: MapPresentation;
  meta?: MapMeta | null;
  synced?: boolean;
}

let MapStore: Writable<StoredMap> = writable<StoredMap>({
  map: emptyMap(),
  presentation: emptyPresentation(),
});

export { MapStore };

export function resetMapStore(): Writable<StoredMap> {
  MapStore = writable<StoredMap>({
    map: emptyMap(),
    presentation: emptyPresentation(),
  });
  return MapStore;
}

const p2p = (a: { X: number; Y: number; delta?: number; timestamp?: string; sol?: number; conn?: number; tag?: number }): { x: number; y: number; delta?: number; timestamp?: string; sol?: number; conn?: boolean; tag?: number } => {
  const pt: { x: number; y: number; delta?: number; timestamp?: string; sol?: number; conn?: boolean; tag?: number } = {
    x: a.X,
    y: -a.Y,
  };
  if (a.delta !== undefined) pt.delta = a.delta;
  if (a.timestamp !== undefined) pt.timestamp = a.timestamp;
  if (a.sol !== undefined) pt.sol = a.sol;
  if (a.conn !== undefined) pt.conn = a.conn !== 0;
  if (a.tag !== undefined) pt.tag = a.tag;
  return pt;
};

export function mapRawToMap(mapRaw: any): Map {
  let perimeterPoints = (mapRaw.perimeter || []).map(p2p);
  if (
    perimeterPoints.length === 0 &&
    Array.isArray(mapRaw.waypoints) &&
    mapRaw.waypoints.length > 0
  ) {
    perimeterPoints = mapRaw.waypoints.map(p2p);
  }
  return {
    perimeter: { points: perimeterPoints },
    exclusions: (mapRaw.exclusions || []).map((e: any) => ({
      points: e.map(p2p),
    })),
    dockpoints: { points: (mapRaw.dockpoints || []).map(p2p) },
    searchWire: { points: (mapRaw.searchWire || []).map(p2p) },
    waypoints: { points: (mapRaw.waypoints || []).map(p2p) },
  };
}

export function buildMapFromChunkStores(): Map {
  const toPoint = ({ X, Y, delta, timestamp, sol, conn, tag }: { X: number; Y: number; delta?: number; timestamp?: string; sol?: number; conn?: number; tag?: number }) => {
    const pt: { x: number; y: number; delta?: number; timestamp?: string; sol?: number; conn?: boolean; tag?: number } = { x: X, y: -Y };
    if (delta !== undefined) pt.delta = delta;
    if (timestamp !== undefined) pt.timestamp = timestamp;
    if (sol !== undefined) pt.sol = sol;
    if (conn !== undefined) pt.conn = conn !== 0;
    if (tag !== undefined) pt.tag = tag;
    return pt;
  };
  return {
    perimeter: { points: get(perimeterStore).map(toPoint) },
    exclusions: get(exclusionsStore).map((arr) => ({ points: arr.map(toPoint) })),
    dockpoints: { points: get(dockpointsStore).map(toPoint) },
    searchWire: { points: get(searchWireStore).map(toPoint) },
    waypoints: { points: get(waypointsStore).map(toPoint) },
  };
}

export function buildMapSetData(map: Map, rotation: number = 0): MapSetData {
  const toMapPoint = (p: { x: number; y: number; delta?: number; timestamp?: string; sol?: number; tag?: number }) => {
    const pt: { x: number; y: number; delta?: number; timestamp?: string; sol?: number; tag?: number } = { x: p.x, y: p.y };
    if (p.delta !== undefined) pt.delta = p.delta;
    if (p.timestamp !== undefined) pt.timestamp = p.timestamp;
    if (p.sol !== undefined) pt.sol = p.sol;
    if (p.tag !== undefined) pt.tag = p.tag;
    return pt;
  };
  return {
    perimeter: map.perimeter.points.map(toMapPoint),
    exclusions: map.exclusions.map((e) => e.points.map(toMapPoint)),
    dockpoints: map.dockpoints.points.map(toMapPoint),
    searchWire: map.searchWire.points.map(toMapPoint),
    waypoints: map.waypoints.points.map(toMapPoint),
    rotation,
  };
}

// Map aus Chunks zusammensetzen (alle Typen)
function updateMapStore() {
  let map: Map = emptyMap();
  let lastSerialized: string | null = null;

  function setMap() {
    // Die Präsentation wird immer ungedreht berechnet. Die Rotation wird
    // ausschließlich über die SVG-Transform in Canvas/MapStatusOverlay
    // angewendet (gleiches Verhalten wie in MiniMap).
    const presentation = calculatePresentation(map, 0);
    const serialized = JSON.stringify({ map, presentation });
    if (serialized === lastSerialized) return;
    lastSerialized = serialized;
    MapStore.set({ map, presentation });
  }

  // Beim Start: Gespeicherte Map aus localStorage laden, falls vorhanden
  // Dies verhindert, dass die Karte bei einem Reconnect neu übertragen werden muss.
  const cached = loadCachedMap();
  if (cached) {
    map = cached.map;
    setMap();
  }

  perimeterStore.subscribe((arr) => {
    map.perimeter = { points: arr.map(({ X, Y, delta, timestamp, sol }) => ({ x: X, y: -Y, delta, timestamp, sol })) };
    setMap();
  });
  dockpointsStore.subscribe((arr) => {
    map.dockpoints = { points: arr.map(({ X, Y, delta, timestamp, sol }) => ({ x: X, y: -Y, delta, timestamp, sol })) };
    setMap();
  });
  searchWireStore.subscribe((arr) => {
    map.searchWire = { points: arr.map(({ X, Y, delta, timestamp, sol }) => ({ x: X, y: -Y, delta, timestamp, sol })) };
    setMap();
  });
  exclusionsStore.subscribe((arrs) => {
    map.exclusions = arrs.map((arr) => ({
      points: arr.map(({ X, Y, delta, timestamp, sol }) => ({ x: X, y: -Y, delta, timestamp, sol })),
    }));
    setMap();
  });
  waypointsStore.subscribe((arr) => {
    map.waypoints = {
      points: arr.map(({ X, Y, delta, timestamp, sol, conn, tag }) => ({
        x: X,
        y: -Y,
        delta,
        timestamp,
        sol,
        conn: conn !== undefined && conn !== 0,
        tag,
      })),
    };
    setMap();
  });
  // Rotation ändert die Präsentation nicht mehr; sie fließt über den Store
  // direkt in die SVG-Transform.
  currentMapRotationStore.subscribe(() => {});
}

if (import.meta.hot) {
  if (!import.meta.hot.data?.updateMapStoreCalled) {
    updateMapStore();
    if (import.meta.hot.data) {
      import.meta.hot.data.updateMapStoreCalled = true;
    }
  }
} else {
  updateMapStore();
}

