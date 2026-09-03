import type { Map } from "./model";
import type { MowSettings } from "../model";

export type WpCategory = 'border' | 'exclusionBorder' | 'area' | 'other' | 'connector' | 'mixedConnector';

// Kategorie-Tags aus dem Backend (mower_map.h). Muss mit dem C++-Enum übereinstimmen.
export enum WpTag {
  NONE = 0,
  AREA = 1,
  BORDER = 3,
  EXCLUSION_BORDER = 4,
  CONNECTOR = 6,
  TRANSIT = 7,
}

function pointInPolygon(p: { x: number; y: number }, poly: { x: number; y: number }[]): boolean {
  if (poly.length < 3) return false;
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const a = poly[i], b = poly[j];
    if (((a.y > p.y) !== (b.y > p.y)) &&
        (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 1e-12) + a.x))
      inside = !inside;
  }
  return inside;
}

function distanceToSegment(p: { x: number; y: number }, a: { x: number; y: number }, b: { x: number; y: number }): number {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const len2 = dx * dx + dy * dy;
  if (len2 < 1e-12) {
    const dxp = p.x - a.x;
    const dyp = p.y - a.y;
    return Math.sqrt(dxp * dxp + dyp * dyp);
  }
  let t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
  t = Math.max(0, Math.min(1, t));
  const projX = a.x + t * dx;
  const projY = a.y + t * dy;
  const dxp = p.x - projX;
  const dyp = p.y - projY;
  return Math.sqrt(dxp * dxp + dyp * dyp);
}

function pointNearPolygon(p: { x: number; y: number }, poly: { x: number; y: number }[], threshold: number): boolean {
  if (poly.length < 2) return false;
  for (let i = 0; i < poly.length; i++) {
    const j = (i + 1) % poly.length;
    if (distanceToSegment(p, poly[i], poly[j]) <= threshold) return true;
  }
  return false;
}

export function classifyWaypoint(
  p: { x: number; y: number; conn?: boolean; tag?: number },
  map: Map,
  settings: MowSettings
): WpCategory {
  // New planner routes tag safe connectors with the category they connect
  // (AREA or BORDER). Only untagged legacy connectors stay always active.
  if (p.conn && (p.tag === undefined || p.tag === WpTag.NONE)) return 'connector';

  // Verwende Backend-Tag wenn vorhanden (neue Firmware).
  const tag = p.tag ?? WpTag.NONE;
  switch (tag) {
    case WpTag.AREA: return 'area';
    case WpTag.BORDER: return 'border';
    case WpTag.EXCLUSION_BORDER: return 'exclusionBorder';
    case WpTag.CONNECTOR: return 'mixedConnector';
    case WpTag.TRANSIT:
      return 'connector';
  }

  // Fallback für alte Firmware ohne Tags.
  const perimeter = map.perimeter.points;
  const exclusions = map.exclusions.map(ex => ex.points).filter(ex => ex.length > 0);
  const borderThreshold = 0.25;
  const hasBorder = (settings.borderLaps ?? 0) > 0;

  if (perimeter.length >= 3) {
    if (hasBorder && pointNearPolygon(p, perimeter, borderThreshold)) return 'border';
  }
  for (const ex of exclusions) {
    if (ex.length >= 3 && pointNearPolygon(p, ex, 0.05)) return 'exclusionBorder';
  }
  for (const ex of exclusions) {
    if (ex.length >= 3 && pointInPolygon(p, ex)) return 'other';
  }
  if (perimeter.length >= 3 && pointInPolygon(p, perimeter)) return 'area';
  return 'other';
}

export function isCategoryActive(cat: WpCategory, settings: MowSettings): boolean {
  if (!settings) return true;
  switch (cat) {
    case 'area': return settings.doMowArea ?? true;
    case 'border': return settings.doMowBorder ?? false;
    case 'exclusionBorder': return settings.doMowExclusionBorder ?? false;
    case 'connector': return true;
    case 'mixedConnector': return (settings.doMowArea ?? true) && (settings.doMowBorder ?? false);
    default: return false;
  }
}

export function filterWaypointsByToggles(
  points: { x: number; y: number; tag?: number; conn?: boolean }[],
  map: Map,
  settings: MowSettings
): { x: number; y: number; tag?: number; conn?: boolean; routeBreakBefore?: boolean }[] {
  const result: { x: number; y: number; tag?: number; conn?: boolean; routeBreakBefore?: boolean }[] = [];
  let skippedRoutePart = false;
  for (const point of points) {
    if (!isCategoryActive(classifyWaypoint(point, map, settings), settings)) {
      skippedRoutePart = true;
      continue;
    }
    result.push(skippedRoutePart ? { ...point, routeBreakBefore: true } : point);
    skippedRoutePart = false;
  }
  return result;
}

export function waypointActiveStates(
  points: { x: number; y: number; tag?: number; conn?: boolean }[],
  map: Map,
  settings: MowSettings
): boolean[] {
  return points.map(p => isCategoryActive(classifyWaypoint(p, map, settings), settings));
}
