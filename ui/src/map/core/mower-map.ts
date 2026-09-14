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
  mowOfs?: number;
  patternRings?: boolean;
  distanceToBorder?: number;
  borderLaps?: number;
  mowBorderCcw?: boolean;
  doMowArea?: boolean;
  doMowPerimeter?: boolean;
  doMowBorder?: boolean;
  doMowExclusions?: boolean;
  doExclusionsBorder?: boolean;
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
): { map: Map; rotation: number; dateTime?: string; source?: string; settings?: Partial<MowSettings> } | null {
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
    settings: {
      pattern: parsed.patternRings ? 2 : 0,
      width: parsed.mowOfs ?? 0.3,
      angle: parsed.patternAngle ?? 0,
      distanceToBorder: needsBorderDefaults ? 1 : (parsed.distanceToBorder ?? 0),
      borderLaps: needsBorderDefaults ? 1 : (parsed.borderLaps ?? 0),
      mowBorderCcw: parsed.mowBorderCcw ?? false,
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
  options: { rotation?: number; dateTime?: string; source?: string; settings?: MowSettings } = {}
): string {
  const s = options.settings ?? get(mowSettingsStore);
  const payload: WebappMapJson = {
    dateTime: options.dateTime ?? new Date().toISOString(),
    source: options.source ?? "MowMate",
    rotation: options.rotation ?? 0,
    patternAngle: s.angle,
    mowOfs: s.width,
    patternRings: s.pattern === 2,
    distanceToBorder: s.distanceToBorder,
    borderLaps: s.borderLaps,
    mowBorderCcw: s.mowBorderCcw,
    doMowArea: s.doMowArea,
    doMowPerimeter: s.doMowPerimeter,
    doMowBorder: s.doMowBorder,
    doMowExclusions: s.doMowExclusions,
    doExclusionsBorder: s.doMowExclusionBorder,
    perimeter: closedRing(map.perimeter.points.map(unflip)),
    exclusions: map.exclusions.map((ex) => closedRing(ex.points.map(unflip))),
    // Dockpoints and calculated waypoints are ordered routes, not polygon
    // rings. Closing them would create a false segment from the final point
    // (often a Border lap) back to the first Area point.
    dockpoints: map.dockpoints.points.map(unflip),
    waypoints: map.waypoints.points.map(unflip),
  };
  return JSON.stringify(payload, null, 2);
}

export function isValidMowerMap(json: string): boolean {
  return importMowerMap(json) !== null;
}
