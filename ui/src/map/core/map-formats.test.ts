import { describe, expect, it } from "vitest";
import { exportGeoJson, importGeoJson, isGeoJsonFeatureCollection } from "./map-formats";
import { calculatePresentation } from "./presentation";

describe("CaSSAndRA GeoJSON import", () => {
  it("imports original name-based WGS84 features as local coordinates", () => {
    const json = JSON.stringify({
      type: "FeatureCollection",
      features: [
        { type: "Feature", properties: { name: "current map", id: "map-1" } },
        {
          type: "Feature",
          properties: { name: "perimeter" },
          geometry: {
            type: "Polygon",
            coordinates: [[
              [9.22530, 48.87038],
              [9.22531, 48.87038],
              [9.22531, 48.87039],
              [9.22530, 48.87038],
            ]],
          },
        },
        {
          type: "Feature",
          properties: { name: "dockpoints" },
          geometry: {
            type: "LineString",
            coordinates: [[9.22530, 48.87038], [9.22529, 48.87038]],
          },
        },
        {
          type: "Feature",
          properties: { name: "search wire" },
          geometry: { type: "LineString", coordinates: [] },
        },
      ],
    });

    const result = importGeoJson(json, { lon: 9.22530, lat: 48.87038 });

    expect(result).not.toBeNull();
    expect(result!.map.perimeter.points).toHaveLength(4);
    expect(result!.map.perimeter.points[0]).toEqual({ x: 0, y: 0 });
    expect(result!.map.perimeter.points[1].x).toBeCloseTo(0.731, 2);
    expect(result!.map.perimeter.points[2].y).toBeCloseTo(-1.111, 2);
    expect(result!.map.dockpoints.points).toHaveLength(2);
    expect(result!.map.searchWire.points).toEqual([]);
    expect(calculatePresentation(result!.map).viewBox.split(" ").map(Number).every(Number.isFinite)).toBe(true);
    expect(importGeoJson(json)).toBeNull();
    expect(importGeoJson(json, { lon: 7, lat: 50 })).toBeNull();
  });

  it("keeps typed MowMate GeoJSON in local coordinates without a reference", () => {
    const json = JSON.stringify({
      type: "FeatureCollection",
      features: [{
        type: "Feature",
        properties: { name: "perimeter", type: "perimeter" },
        geometry: {
          type: "Polygon",
          coordinates: [[[1, 2], [3, 2], [3, 4], [1, 2]]],
        },
      }],
    });

    const result = importGeoJson(json);

    expect(result?.map.perimeter.points[0]).toEqual({ x: 1, y: -2 });
  });

  it("exports the original CaSSAndRA WGS84 feature structure", () => {
    const reference = { lon: 9.22530, lat: 48.87038 };
    const imported = importGeoJson(JSON.stringify({
      type: "FeatureCollection",
      features: [{
        type: "Feature",
        properties: { name: "perimeter" },
        geometry: {
          type: "Polygon",
          coordinates: [[[9.22530, 48.87038], [9.22531, 48.87038], [9.22531, 48.87039], [9.22530, 48.87038]]],
        },
      }],
    }), reference)!;

    const exported = JSON.parse(exportGeoJson(imported.map, reference));

    expect(exported.features.map((feature: { properties: { name: string } }) => feature.properties.name))
      .toEqual(["perimeter", "dockpoints", "search wire"]);
    expect(exported.features[0].properties.type).toBeUndefined();
    expect(exported.features[0].geometry.coordinates[0][1][0]).toBeCloseTo(9.22531, 8);
    expect(exported.features[0].geometry.coordinates[0][2][1]).toBeCloseTo(48.87039, 8);
    expect(exported.features[1].geometry.coordinates).toEqual([]);
    expect(exported.features[2].geometry.coordinates).toEqual([]);
    expect(importGeoJson(JSON.stringify(exported), reference)?.map.perimeter.points)
      .toEqual(imported.map.perimeter.points);
    expect(() => exportGeoJson(imported.map, { lon: 0, lat: 0 })).toThrow();
  });

  it("detects a FeatureCollection independently of the selected import tab", () => {
    expect(isGeoJsonFeatureCollection('{"type":"FeatureCollection","features":[]}')).toBe(true);
    expect(isGeoJsonFeatureCollection('{"perimeter":[]}')).toBe(false);
  });
});
