import type { Map, Point } from "../model";
import { emptyMap } from "./map-utils";

export type MapFormat = "mower" | "geojson";

interface GeoJsonFeature {
  type: "Feature";
  properties?: {
    name?: string;
    type?: string;
    [key: string]: unknown;
  };
  geometry: {
    type: "Polygon" | "LineString" | "Point";
    coordinates: number[][][] | number[][] | number[];
  };
}

interface GeoJsonFeatureCollection {
  type: "FeatureCollection";
  features: GeoJsonFeature[];
}

function toPoint(arr: number[]): Point {
  return { x: arr[0], y: -arr[1] };
}

function fromPoint(p: Point): number[] {
  return [p.x, -p.y];
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
 * - Perimeter: Feature mit geometry.type Polygon, type/perimeter
 * - Exclusions: Feature mit geometry.type Polygon, type/exclusion_N
 * - Dockpoints: Feature mit geometry.type LineString, type/dockpoints
 * - Search Wire: Feature mit geometry.type LineString, type/search_wire
 */
export function importGeoJson(json: string): { map: Map; rotation: number } | null {
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
  const map = emptyMap();
  let rotation = 0;

  for (const feature of fc.features) {
    const geom = feature.geometry;
    const type = feature.properties?.type?.toLowerCase() ?? "";

    if (geom.type === "Polygon" && Array.isArray(geom.coordinates)) {
      const rings = geom.coordinates as number[][][];
      const outer = rings[0] as number[][];
      const points = outer.map(toPoint);
      if (type.startsWith("exclusion")) {
        if (points.length >= 3) {
          map.exclusions.push({ points });
        }
      } else if (type === "perimeter" || type === "" || type === "map") {
        if (points.length >= 3) {
          map.perimeter.points = points;
        }
      }
    } else if (geom.type === "LineString" && Array.isArray(geom.coordinates)) {
      const coords = geom.coordinates as number[][];
      const points = coords.map(toPoint);
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

  // Falls Perimeter nicht geschlossen, schließen.
  map.perimeter.points = ringClosed(map.perimeter.points);

  return { map, rotation };
}

export function exportGeoJson(map: Map, options: { rotation?: number; name?: string } = {}): string {
  const features: GeoJsonFeature[] = [];

  features.push({
    type: "Feature",
    properties: { name: options.name ?? "perimeter", type: "perimeter" },
    geometry: {
      type: "Polygon",
      coordinates: [ringClosed(map.perimeter.points).map(fromPoint)],
    },
  });

  map.exclusions.forEach((ex, idx) => {
    if (ex.points.length >= 3) {
      features.push({
        type: "Feature",
        properties: { name: `exclusion_${idx}`, type: `exclusion_${idx}` },
        geometry: {
          type: "Polygon",
          coordinates: [ringClosed(ex.points).map(fromPoint)],
        },
      });
    }
  });

  if (map.dockpoints.points.length > 0) {
    features.push({
      type: "Feature",
      properties: { name: "dockpoints", type: "dockpoints" },
      geometry: {
        type: "LineString",
        coordinates: map.dockpoints.points.map(fromPoint),
      },
    });
  }

  if (map.searchWire.points.length >= 2) {
    features.push({
      type: "Feature",
      properties: { name: "search_wire", type: "search_wire" },
      geometry: {
        type: "LineString",
        coordinates: map.searchWire.points.map(fromPoint),
      },
    });
  }

  const fc: GeoJsonFeatureCollection = {
    type: "FeatureCollection",
    features,
  };

  return JSON.stringify(fc, null, 2);
}

export function isValidGeoJson(json: string): boolean {
  return importGeoJson(json) !== null;
}
