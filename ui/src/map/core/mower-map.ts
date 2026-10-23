import type { Map, Point } from "../model";
import { emptyMap } from "./map-utils";
import type { MowSettings } from "../model";
import { mowSettingsStore } from "../mow-settings";
import { get } from "svelte/store";

// Webapp-compatible JSON map format ("MAP DATA" export):
// {
//   "perimeter": [{"X": ..., "Y": ..., "delta": ..., "timestamp": ..., "sol": ...}, ...],
//   "exclusions": [[{"X": ..., "Y": ...}, ...], ...],
//   "dockpoints": [{"X": ..., "Y": ...}, ...],
//   "waypoints": [{"X": ..., "Y": ...}, ...],
//   "patternAngle": 0,
//   "mowOfs": 0.13,
//   "patternRings": false,
//   "mowBorderCcw": false,
//   "doMowArea": true,
//   "doMowPerimeter": true,
//   "doMowBorder": false,
//   "doMowExclusions": true,
//   "doExclusionsBorder": false
// }
//
// The UI uses Y-up (positive y is North). The Webapp map export uses Y-down
// (positive y is South/screen coordinates). We flip Y on import/export so that
// coordinates are visually consistent in both applications.

/**
 * Bereich (Subkarte) im Export: Name, Elternbereich als Index in der Liste
 * (-1 = die Hauptkarte, Eltern stehen immer vor ihren Kindern) und sein
 * eigener Teil im Format des Modems (Auswahl, Mäh-Einstellungen). Routen
 * gehören nicht dazu; sie werden nach dem Import neu berechnet.
 */
export interface ExportedArea {
  name: string;
  parent: number;
  map: Record<string, unknown>;
}

export interface WebappMapJson {
  dateTime?: string;
  source?: string;
  rotation?: number;
  perimeter?: Array<{ X: number; Y: number; delta?: number; timestamp?: string; sol?: number }>;
  exclusions?: Array<Array<{ X: number; Y: number; delta?: number; timestamp?: string; sol?: number }>>;
  dockpoints?: Array<{ X: number; Y: number; delta?: number; timestamp?: string; sol?: number }>;
  waypoints?: Array<{ X: number; Y: number; delta?: number; timestamp?: string; sol?: number; tag?: number }>;
  // Mäh-Einstellungen, die der Webapp-Export/Import mit sich führt
  patternAngle?: number;
  angleMode?: number;
  angleStep?: number;
  mowOfs?: number;
  patternRings?: boolean;
  distanceToBorder?: number;
  borderLaps?: number;
  distanceToExclusions?: number;
  exclusionLaps?: number;
  mowBorderCcw?: boolean;
  routeOrder?: string;
  exclusionLapsWithArea?: boolean;
  doMowArea?: boolean;
  doMowPerimeter?: boolean;
  doMowBorder?: boolean;
  doMowExclusions?: boolean;
  doExclusionsBorder?: boolean;
  areas?: ExportedArea[];
}

/** Liest die Bereiche einer Datei; ungültige (und ihre Kinder) entfallen. */
export function parseAreas(raw: unknown): ExportedArea[] {
  if (!Array.isArray(raw)) return [];
  const out: ExportedArea[] = [];
  const newIndex: number[] = [];
  raw.forEach((a, i) => {
    newIndex[i] = -2;
    if (typeof a !== "object" || a === null) return;
    const area = a as { name?: unknown; parent?: unknown; map?: unknown };
    const parent = typeof area.parent === "number" && Number.isInteger(area.parent) ? area.parent : -1;
    if (parent >= i) return;
    const mappedParent = parent < 0 ? -1 : newIndex[parent];
    if (mappedParent === -2) return;
    if (typeof area.map !== "object" || area.map === null) return;
    newIndex[i] = out.length;
    out.push({
      name: typeof area.name === "string" && area.name ? area.name : `Area ${out.length + 1}`,
      parent: mappedParent,
      map: area.map as Record<string, unknown>,
    });
  });
  return out;
}

/** Bereichsliste des Modems (requestAreas) im Exportformat. Als Baum
 * geordnet (jeder Bereich direkt nach seinem Elternbereich, Geschwister nach
 * Namen): Das Modem liefert sie in anderer Reihenfolge, und die Einrückung
 * ließ Unterbereiche dann unter dem falschen Bereich erscheinen. */
export function areasFromModem(
  list: { id: string; name: string; parentId: string; map?: Record<string, unknown> }[],
  rootId: string
): ExportedArea[] {
  const byName = (a: { name: string }, b: { name: string }) =>
    a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" });
  const out: ExportedArea[] = [];
  const add = (parentId: string, parentIndex: number, depth: number) => {
    if (depth > 8) return;
    for (const a of list.filter((x) => x.parentId === parentId).sort(byName)) {
      const index = out.length;
      out.push({ name: a.name, parent: parentIndex, map: a.map ?? {} });
      add(a.id, index, depth + 1);
    }
  };
  add(rootId, -1, 0);
  return out;
}

/** Nur die gewählten Bereiche; ein Bereich ohne gewählten Elternbereich entfällt. */
export function selectAreas(areas: ExportedArea[], keep: boolean[]): ExportedArea[] {
  const out: ExportedArea[] = [];
  const newIndex: number[] = [];
  areas.forEach((a, i) => {
    newIndex[i] = -2;
    if (!keep[i]) return;
    const parent = a.parent < 0 ? -1 : newIndex[a.parent];
    if (parent === -2) return;
    newIndex[i] = out.length;
    out.push({ ...a, parent });
  });
  return out;
}

/** An: auch alle Elternbereiche an. Aus: auch alle Unterbereiche aus. */
export function toggleArea(areas: ExportedArea[], keep: boolean[], index: number, on: boolean): boolean[] {
  const next = [...keep];
  next[index] = on;
  if (on) {
    for (let p = areas[index]?.parent ?? -1; p >= 0; p = areas[p].parent) next[p] = true;
  } else {
    for (let i = index + 1; i < areas.length; i++) {
      if (areas[i].parent >= 0 && !next[areas[i].parent]) next[i] = false;
    }
  }
  return next;
}

/** Tiefe eines Bereichs im Baum (0 = direkt unter der Hauptkarte) */
export function areaDepth(areas: ExportedArea[], index: number): number {
  let depth = 0;
  for (let p = areas[index]?.parent ?? -1; p >= 0 && depth < 9; p = areas[p].parent) depth++;
  return depth;
}

function flip(p: { X: number; Y: number }): Point {
  return {
    x: p.X,
    y: -p.Y,
    delta: p.delta,
    timestamp: p.timestamp,
    sol: p.sol,
    tag: p.tag,
  };
}

function unflip(p: Point): { X: number; Y: number; delta?: number; timestamp?: string; sol?: number; tag?: number } {
  const result: { X: number; Y: number; delta?: number; timestamp?: string; sol?: number; tag?: number } = {
    X: p.x,
    Y: -p.y,
  };
  if (p.delta !== undefined) result.delta = p.delta;
  if (p.timestamp !== undefined) result.timestamp = p.timestamp;
  if (p.sol !== undefined) result.sol = p.sol;
  if (p.tag !== undefined) result.tag = p.tag;
  return result;
}

function closedRing(points: Array<{ X: number; Y: number; delta?: number; timestamp?: string; sol?: number; tag?: number }>): Array<{ X: number; Y: number; delta?: number; timestamp?: string; sol?: number; tag?: number }> {
  if (points.length === 0) return points;
  const first = points[0];
  const last = points[points.length - 1];
  if (first.X === last.X && first.Y === last.Y) return points;
  return [...points, first];
}

function normalizePoint(p: unknown): { X: number; Y: number; delta?: number; timestamp?: string; sol?: number; tag?: number } | null {
  if (typeof p !== "object" || p === null) return null;
  const pt = p as { [key: string]: unknown };
  const X = typeof pt.X === "number" ? pt.X : typeof pt.x === "number" ? pt.x : undefined;
  const Y = typeof pt.Y === "number" ? pt.Y : typeof pt.y === "number" ? pt.y : undefined;
  if (X === undefined || Y === undefined) return null;
  return {
    X,
    Y,
    delta: typeof pt.delta === "number" ? pt.delta : undefined,
    timestamp: typeof pt.timestamp === "string" ? pt.timestamp : undefined,
    sol: typeof pt.sol === "number" ? pt.sol : undefined,
    tag: typeof pt.tag === "number" ? pt.tag : undefined,
  };
}

export function importMowerMap(
  json: string
): { map: Map; rotation: number; dateTime?: string; source?: string; settings?: Partial<MowSettings>; areas: ExportedArea[] } | null {
  let parsed: WebappMapJson;
  try {
    parsed = JSON.parse(json) as WebappMapJson;
  } catch (e) {
    console.error("Failed to parse Webapp map JSON", e);
    return null;
  }

  const perimeter = (parsed.perimeter || [])
    .map(normalizePoint)
    .filter((p): p is { X: number; Y: number; delta?: number; timestamp?: string; sol?: number; tag?: number } => p !== null)
    .map(flip);
  if (perimeter.length < 3) {
    console.error("Webapp map has too few perimeter points", perimeter.length);
    return null;
  }

  const map = emptyMap();
  map.perimeter.points = perimeter;
  map.exclusions = (parsed.exclusions || [])
    .map((ex) =>
      ex
        .map(normalizePoint)
        .filter((p): p is { X: number; Y: number; delta?: number; timestamp?: string; sol?: number; tag?: number } => p !== null)
        .map(flip)
    )
    .filter((ex) => ex.length >= 3)
    .map((points) => ({ points }));
  map.dockpoints.points = (parsed.dockpoints || [])
    .map(normalizePoint)
    .filter((p): p is { X: number; Y: number; delta?: number; timestamp?: string; sol?: number; tag?: number } => p !== null)
    .map(flip);
  map.waypoints.points = (parsed.waypoints || [])
    .map(normalizePoint)
    .filter((p): p is { X: number; Y: number; delta?: number; timestamp?: string; sol?: number; tag?: number } => p !== null)
    .map(flip);

  const hasLegacyBorder =
    parsed.doPerimeterBorder === true ||
    parsed.doMowBorder === true;
  const needsBorderDefaults =
    hasLegacyBorder &&
    parsed.distanceToBorder === undefined &&
    parsed.borderLaps === undefined;

  return {
    map,
    rotation: parsed.rotation ?? 0,
    dateTime: parsed.dateTime,
    source: parsed.source,
    areas: parseAreas(parsed.areas),
    settings: {
      pattern: parsed.patternRings ? 2 : 0,
      width: parsed.mowOfs ?? 0.3,
      angle: parsed.patternAngle ?? 0,
      angleMode: parsed.angleMode ?? 0,
      angleStep: parsed.angleStep ?? 30,
      distanceToBorder: needsBorderDefaults ? 1 : (parsed.distanceToBorder ?? 0),
      borderLaps: needsBorderDefaults ? 1 : (parsed.borderLaps ?? 0),
      distanceToExclusions: parsed.distanceToExclusions ?? 0,
      // Ältere Exporte kennen keine eigenen Exclusion-Runden: dort liefen so
      // viele Runden um jede Exclusion wie um den Perimeter.
      exclusionLaps: parsed.exclusionLaps ?? (needsBorderDefaults ? 1 : (parsed.borderLaps ?? 0)),
      mowBorderCcw: parsed.mowBorderCcw ?? false,
      routeOrder: parsed.routeOrder ?? "",
      exclusionLapsWithArea: parsed.exclusionLapsWithArea ?? false,
      doMowArea: parsed.doMowArea ?? true,
      doMowPerimeter: parsed.doMowPerimeter ?? true,
      doMowBorder: parsed.doMowBorder ?? (parsed.doPerimeterBorder ?? false),
      doMowExclusions: parsed.doMowExclusions ?? true,
      doMowExclusionBorder: parsed.doExclusionsBorder ?? false,
    },
  };
}

export function exportMowerMap(
  map: Map,
  options: { rotation?: number; dateTime?: string; source?: string; settings?: MowSettings; areas?: ExportedArea[] } = {}
): string {
  const s = options.settings ?? get(mowSettingsStore);
  const payload: WebappMapJson = {
    dateTime: options.dateTime ?? new Date().toISOString(),
    source: options.source ?? "MowMate",
    rotation: options.rotation ?? 0,
    ...settingsFields(s),
    perimeter: closedRing(map.perimeter.points.map(unflip)),
    exclusions: map.exclusions.map((ex) => closedRing(ex.points.map(unflip))),
    // Dockpoints and calculated waypoints are ordered routes, not polygon
    // rings. Closing them would create a false segment from the final point
    // (often a Border lap) back to the first Area point.
    dockpoints: map.dockpoints.points.map(unflip),
    // Zufälliger/dynamischer Winkel: Die Route gilt nur für einen Auftrag
    waypoints: s.angleMode ? [] : map.waypoints.points.map(unflip),
    ...(options.areas && options.areas.length > 0 ? { areas: options.areas } : {}),
  };
  return JSON.stringify(payload, null, 2);
}

/** Mäh-Einstellungen in den Feldnamen der Datei (wie MowerMap::fromJson im Modem) */
export function settingsFields(s: Partial<MowSettings>): Partial<WebappMapJson> {
  return {
    patternAngle: s.angle,
    ...(s.angleMode ? { angleMode: s.angleMode, angleStep: s.angleStep ?? 30 } : {}),
    mowOfs: s.width,
    patternRings: s.pattern === 2,
    distanceToBorder: s.distanceToBorder,
    borderLaps: s.borderLaps,
    distanceToExclusions: s.distanceToExclusions ?? 0,
    exclusionLaps: s.exclusionLaps ?? 0,
    mowBorderCcw: s.mowBorderCcw,
    ...(s.routeOrder ? { routeOrder: s.routeOrder } : {}),
    ...(s.exclusionLapsWithArea ? { exclusionLapsWithArea: true } : {}),
    doMowArea: s.doMowArea,
    doMowPerimeter: s.doMowPerimeter,
    doMowBorder: s.doMowBorder,
    doMowExclusions: s.doMowExclusions,
    doExclusionsBorder: s.doMowExclusionBorder,
  };
}

export function isValidMowerMap(json: string): boolean {
  return importMowerMap(json) !== null;
}
