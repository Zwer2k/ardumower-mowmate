// Buffer-Logik für Map-Chunks (Waypoints) aus WebSocket

import { writable } from 'svelte/store';
import type { MapPoint } from '../model';

// Enum-Werte müssen mit Backend übereinstimmen!
export enum MapPointType {
  Perimeter = 0,
  Exclusion = 1,
  Dockpoints = 2,
  Waypoints = 3,
  SearchWire = 4,
}

export interface MapChunk {
  transferId?: number;
  startIndex?: number;
  total: number;
  pointType: MapPointType;
  exclusionIdx?: number;
  points?: MapPoint[];
  reset?: boolean;
  complete?: boolean;
}

// Buffer für alle Typen
let perimeterBuffer: MapPoint[] = [];
let perimeterTotal = 0;
let dockpointsBuffer: MapPoint[] = [];
let dockpointsTotal = 0;
let searchWireBuffer: MapPoint[] = [];
let searchWireTotal = 0;
let waypointsBuffer: MapPoint[] = [];
let waypointsTotal = 0;
let exclusionsBuffer: MapPoint[][] = [];
let exclusionsTotal: number[] = [];

export const perimeterStore = writable<MapPoint[]>([]);
export const dockpointsStore = writable<MapPoint[]>([]);
export const searchWireStore = writable<MapPoint[]>([]);
export const waypointsStore = writable<MapPoint[]>([]);
export const exclusionsStore = writable<MapPoint[][]>([]);
export const mapSnapshotStore = writable<{
  perimeter: MapPoint[];
  exclusions: MapPoint[][];
  dockpoints: MapPoint[];
  searchWire: MapPoint[];
  waypoints: MapPoint[];
}>({ perimeter: [], exclusions: [], dockpoints: [], searchWire: [], waypoints: [] });

let activeTransferId: number | null = null;
let publishedTransferId: number | null = null;
let receivedTypes = new Set<MapPointType>();

function isNewerTransferId(candidate: number, reference: number): boolean {
  const difference = (candidate - reference) >>> 0;
  return difference !== 0 && difference < 0x80000000;
}

function bufferComplete(buffer: MapPoint[], total: number): boolean {
  return buffer.length === total && buffer.filter(Boolean).length === total;
}

function transferComplete(): boolean {
  return receivedTypes.size === 5
    && bufferComplete(perimeterBuffer, perimeterTotal)
    && bufferComplete(dockpointsBuffer, dockpointsTotal)
    && bufferComplete(searchWireBuffer, searchWireTotal)
    && bufferComplete(waypointsBuffer, waypointsTotal)
    && exclusionsBuffer.every((buffer, index) => bufferComplete(buffer, exclusionsTotal[index] ?? 0));
}

function publishSnapshot() {
  const perimeter = [...perimeterBuffer];
  const exclusions = exclusionsBuffer.map((points) => [...points]);
  const dockpoints = [...dockpointsBuffer];
  const searchWire = [...searchWireBuffer];
  const waypoints = [...waypointsBuffer];
  perimeterStore.set(perimeter);
  exclusionsStore.set(exclusions);
  dockpointsStore.set(dockpoints);
  searchWireStore.set(searchWire);
  waypointsStore.set(waypoints);
  mapSnapshotStore.set({ perimeter, exclusions, dockpoints, searchWire, waypoints });
}

// Progress during chunk reception (download from backend)
export const mapChunkProgress = writable<{ received: number; total: number; label: string } | null>(null);

export function handleMapChunk(chunk: MapChunk) {
  const atomicTransfer = chunk.transferId !== undefined;
  if (atomicTransfer && chunk.transferId !== activeTransferId) {
    const transferId = chunk.transferId!;
    const newestKnownId = activeTransferId ?? publishedTransferId;
    if (newestKnownId !== null && !isNewerTransferId(transferId, newestKnownId)) return;
    activeTransferId = transferId;
    perimeterBuffer = [];
    perimeterTotal = 0;
    dockpointsBuffer = [];
    dockpointsTotal = 0;
    searchWireBuffer = [];
    searchWireTotal = 0;
    waypointsBuffer = [];
    waypointsTotal = 0;
    exclusionsBuffer = [];
    exclusionsTotal = [];
    receivedTypes = new Set<MapPointType>();
  }

  if (atomicTransfer && chunk.complete) {
    if (chunk.transferId === activeTransferId && transferComplete()) {
      publishSnapshot();
      publishedTransferId = activeTransferId;
      activeTransferId = null;
    }
    return;
  }

  function resetType(type: MapPointType) {
    switch (type) {
      case MapPointType.Perimeter:
        perimeterBuffer = [];
        perimeterTotal = 0;
        if (!atomicTransfer) perimeterStore.set([]);
        break;
      case MapPointType.Dockpoints:
        dockpointsBuffer = [];
        dockpointsTotal = 0;
        if (!atomicTransfer) dockpointsStore.set([]);
        break;
      case MapPointType.SearchWire:
        searchWireBuffer = [];
        searchWireTotal = 0;
        if (!atomicTransfer) searchWireStore.set([]);
        break;
      case MapPointType.Waypoints:
        waypointsBuffer = [];
        waypointsTotal = 0;
        if (!atomicTransfer) waypointsStore.set([]);
        break;
      case MapPointType.Exclusion:
        exclusionsBuffer = [];
        exclusionsTotal = [];
        if (!atomicTransfer) exclusionsStore.set([]);
        break;
    }
  }

  // Reset-Chunks löschen nur den jeweiligen Typ, nicht die ganze Karte
  if (chunk.reset) {
    if (atomicTransfer) receivedTypes.add(chunk.pointType);
    resetType(chunk.pointType ?? MapPointType.Perimeter);
    if (!atomicTransfer) publishSnapshot();
    return;
  }
  const startIndex = chunk.startIndex ?? 0;
  const points = chunk.points ?? [];
  if (atomicTransfer) receivedTypes.add(chunk.pointType);

  // Helper for shared logic, chunk is in closure
  function handleGeneric(buffer: MapPoint[], total: number, store: typeof perimeterStore): [MapPoint[], number] {
    if (startIndex === 0 || chunk.total !== total) {
      buffer = new Array(chunk.total);
      total = chunk.total;
    }
    for (let i = 0; i < points.length; i++) {
      buffer[startIndex + i] = points[i];
    }
    const filled = buffer.filter(Boolean).length;
    if (filled === total) {
      if (!atomicTransfer) store.set([...buffer]);
    }
    return [buffer, total];
  }

  if (chunk.pointType === MapPointType.Exclusion) {
    const idx = chunk.exclusionIdx ?? 0;
    if (!exclusionsBuffer[idx] || startIndex === 0 || chunk.total !== exclusionsTotal[idx]) {
      exclusionsBuffer[idx] = new Array(chunk.total);
      exclusionsTotal[idx] = chunk.total;
    }
    for (let i = 0; i < points.length; i++) {
      exclusionsBuffer[idx][startIndex + i] = points[i];
    }
    const filled = exclusionsBuffer[idx].filter(Boolean).length;
    if (filled === exclusionsTotal[idx]) {
      if (!atomicTransfer) {
        exclusionsStore.set([...exclusionsBuffer]);
        publishSnapshot();
      }
    }
    return;
  }

  let progressLabel = '';
  switch (chunk.pointType) {
    case MapPointType.Perimeter:
      [perimeterBuffer, perimeterTotal] = handleGeneric(perimeterBuffer, perimeterTotal, perimeterStore);
      progressLabel = 'Perimeter';
      break;
    case MapPointType.Dockpoints:
      [dockpointsBuffer, dockpointsTotal] = handleGeneric(dockpointsBuffer, dockpointsTotal, dockpointsStore);
      progressLabel = 'Dockpoints';
      break;
    case MapPointType.SearchWire:
      [searchWireBuffer, searchWireTotal] = handleGeneric(searchWireBuffer, searchWireTotal, searchWireStore);
      progressLabel = 'Search Wire';
      break;
    case MapPointType.Waypoints:
      [waypointsBuffer, waypointsTotal] = handleGeneric(waypointsBuffer, waypointsTotal, waypointsStore);
      progressLabel = 'Waypoints';
      break;
    default:
      console.warn('[MapChunkBuffer] Unbekannter pointType:', chunk.pointType);
  }
  if (!atomicTransfer) {
    publishSnapshot();
  }
  if (progressLabel && chunk.total > 0) {
    const received = Math.min(startIndex + points.length, chunk.total);
    mapChunkProgress.set({ received, total: chunk.total, label: progressLabel });
    if (received >= chunk.total) {
      mapChunkProgress.set(null);
    }
  }
}


export function resetMapChunkBuffer() {
  activeTransferId = null;
  publishedTransferId = null;
  receivedTypes = new Set<MapPointType>();
  perimeterBuffer = [];
  perimeterTotal = 0;
  dockpointsBuffer = [];
  dockpointsTotal = 0;
  searchWireBuffer = [];
  searchWireTotal = 0;
  waypointsBuffer = [];
  waypointsTotal = 0;
  exclusionsBuffer = [];
  exclusionsTotal = [];
  perimeterStore.set([]);
  dockpointsStore.set([]);
  searchWireStore.set([]);
  waypointsStore.set([]);
  exclusionsStore.set([]);
  mapSnapshotStore.set({ perimeter: [], exclusions: [], dockpoints: [], searchWire: [], waypoints: [] });
}

export function clearWaypointsBuffer() {
  waypointsBuffer = [];
  waypointsTotal = 0;
  publishSnapshot();
}
