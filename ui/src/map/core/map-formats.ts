import type { Map, Point } from "../model";
import { emptyMap } from "./map-utils";

export type MapFormat = "mower" | "geojson";

interface GeoJsonFeature {
  type: "Feature";
  idx?: number;
  properties?: {
    name?: string;
    type?: string;
    [key: string]: unknown;
  };
  geometry?: {
    type: "Polygon" | "LineString" | "Point";
    coordinates: number[][][] | number[][] | number[];
  };
}

interface GeoJsonFeatureCollection {
  type: "FeatureCollection";
  features: GeoJsonFeature[];
}

const MAX_REFERENCE_OFFSET_METERS = 10000;
const METERS_PER_DEGREE = 111111;
const ZERO_REFERENCE: GeoJsonReference = { lon: 0, lat: 0 };

export interface GeoJsonReference {
  lon: number;
  lat: number;
}

function isCassandraFeature(feature: GeoJsonFeature): boolean {
  const declaredType = feature.properties?.type?.trim().toLowerCase() ?? "";
  const name = feature.properties?.name?.trim().toLowerCase() ?? "";
  return declaredType === "" && name !== "";
}

/**
 * CaSSAndRA exportiert mit rovercfg.lat/lon = 0, solange dort keine Referenz
 * konfiguriert ist. Die "WGS84"-Werte sind dann nur Grad-Offsets um (0,0) und
 * muessen mit Nullreferenz umgerechnet werden statt mit der eigenen Position.
 */
function usesZeroReference(features: GeoJsonFeature[]): boolean {
  const maxDegrees = MAX_REFERENCE_OFFSET_METERS / METERS_PER_DEGREE;
  let found = false;
  for (const feature of features) {
    if (!isCassandraFeature(feature) || !Array.isArray(feature.geometry?.coordinates)) continue;
    const coords = (feature.geometry!.coordinates as unknown[]).flat(2) as unknown[];
    for (const value of coords) {
      if (typeof value !== "number" || !Number.isFinite(value)) continue;
      if (Math.abs(value) > maxDegrees) return false;
      found = true;
    }
  }
  return found;
}

function toPoint(arr: number[]): Point {
  return { x: arr[0], y: -arr[1] };
}

function absoluteToRelative(arr: number[], reference: GeoJsonReference): Point {
  const metersPerDegree = METERS_PER_DEGREE;
  const x = (arr[0] - reference.lon) * metersPerDegree * Math.cos(reference.lat * Math.PI / 180);
  const y = (arr[1] - reference.lat) * metersPerDegree;
  return { x: x === 0 ? 0 : x, y: y === 0 ? 0 : -y };
}

/** Kartenpunkt (Meter, y wie im Editor nach unten) in [lon, lat] umrechnen. */
export function relativeToAbsolute(point: Point, reference: GeoJsonReference): number[] {
  const metersPerDegree = METERS_PER_DEGREE;
  const lon = point.x / (metersPerDegree * Math.cos(reference.lat * Math.PI / 180)) + reference.lon;
  const lat = -point.y / metersPerDegree + reference.lat;
  return [lon, lat];
}

function ringClosed(pts: Point[]): Point[] {
  if (pts.length === 0) return pts;
  const first = pts[0];
  const last = pts[pts.length - 1];
  if (first.x !== last.x || first.y !== last.y) {
    return [...pts, first];
  }
  return pts;
}

/**
 * CaSSAndRA-spezifisches GeoJSON:
 * - Original: Klassifizierung über properties.name, Koordinaten als WGS84
 * - MowMate-Export: Klassifizierung über properties.type, lokale Koordinaten
 */
export function importGeoJson(
  json: string,
  reference?: GeoJsonReference,
): { map: Map; rotation: number } | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(json);
  } catch (e) {
    console.error("Failed to parse GeoJSON", e);
    return null;
  }

  if (typeof parsed !== "object" || parsed === null || (parsed as GeoJsonFeatureCollection).type !== "FeatureCollection") {
    return null;
  }

  const fc = parsed as GeoJsonFeatureCollection;
  if (!Array.isArray(fc.features)) return null;
  const map = emptyMap();
  let rotation = 0;
  const effectiveReference = usesZeroReference(fc.features) ? ZERO_REFERENCE : reference;

  for (const feature of fc.features) {
    const geom = feature.geometry;
    if (!geom || !Array.isArray(geom.coordinates)) continue;
    const declaredType = feature.properties?.type?.trim().toLowerCase() ?? "";
    const name = feature.properties?.name?.trim().toLowerCase() ?? "";
    const type = (declaredType || name).replace(/[\s-]+/g, "_");
    const cassandraCoordinates = isCassandraFeature(feature);
    if (cassandraCoordinates && !effectiveReference) return null;
    const convertPoint = cassandraCoordinates
      ? (point: number[]) => absoluteToRelative(point, effectiveReference!)
      : toPoint;

    if (geom.type === "Polygon") {
      const rings = geom.coordinates as number[][][];
      const outer = rings[0];
      if (!Array.isArray(outer)) continue;
      const points = outer.filter((point) => Array.isArray(point) && point.length >= 2).map(convertPoint);
      if (type.startsWith("exclusion")) {
        if (points.length >= 3) {
          map.exclusions.push({ points });
        }
      } else if (type === "perimeter" || type === "" || type === "map" || type === "current_map") {
        if (points.length >= 3) {
          map.perimeter.points = points;
        }
      }
    } else if (geom.type === "LineString") {
      const coords = geom.coordinates as number[][];
      const points = coords.filter((point) => Array.isArray(point) && point.length >= 2).map(convertPoint);
      if (type === "dockpoints" || type === "docking") {
        map.dockpoints.points = points;
      } else if (type === "search_wire" || type === "searchwire") {
        map.searchWire.points = points;
      } else if (points.length >= 3 && type === "perimeter") {
        map.perimeter.points = points;
      }
    }
  }

  if (map.perimeter.points.length < 3) {
    return null;
  }

  if (map.perimeter.points.some((point) =>
    !Number.isFinite(point.x) || !Number.isFinite(point.y) ||
    Math.abs(point.x) > MAX_REFERENCE_OFFSET_METERS || Math.abs(point.y) > MAX_REFERENCE_OFFSET_METERS
  )) {
    return null;
  }

  // Falls Perimeter nicht geschlossen, schließen.
  map.perimeter.points = ringClosed(map.perimeter.points);

  return { map, rotation };
}

export function exportGeoJson(map: Map, reference: GeoJsonReference): string {
  if (!Number.isFinite(reference.lon) || !Number.isFinite(reference.lat)) {
    throw new Error("A valid Position reference is required for CaSSAndRA GeoJSON export.");
  }

  const features: GeoJsonFeature[] = [];
  const convertPoint = (point: Point) => relativeToAbsolute(point, reference);

  features.push({
    type: "Feature",
    properties: { name: "perimeter" },
    geometry: {
      type: "Polygon",
      coordinates: [ringClosed(map.perimeter.points).map(convertPoint)],
    },
  });

  map.exclusions.forEach((ex, idx) => {
    if (ex.points.length >= 3) {
      features.push({
        type: "Feature",
        properties: { name: "exclusion" },
        idx,
        geometry: {
          type: "Polygon",
          coordinates: [ringClosed(ex.points).map(convertPoint)],
        },
      });
    }
  });

  features.splice(1, 0, {
    type: "Feature",
    properties: { name: "dockpoints" },
    geometry: {
      type: "LineString",
      coordinates: map.dockpoints.points.map(convertPoint),
    },
  });

  features.splice(2, 0, {
    type: "Feature",
    properties: { name: "search wire" },
    geometry: {
      type: "LineString",
      coordinates: map.searchWire.points.map(convertPoint),
    },
  });

  const fc: GeoJsonFeatureCollection = {
    type: "FeatureCollection",
    features,
  };

  return JSON.stringify(fc, null, 2);
}

export function isValidGeoJson(json: string, reference?: GeoJsonReference): boolean {
  return importGeoJson(json, reference) !== null;
}

export function isGeoJsonFeatureCollection(json: string): boolean {
  try {
    const parsed = JSON.parse(json) as { type?: unknown };
    return parsed?.type === "FeatureCollection";
  } catch {
    return false;
  }
}
