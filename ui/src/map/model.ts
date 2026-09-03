export interface Point {
  x: number;
  y: number;
  delta?: number;
  timestamp?: string;
  sol?: number;
  /** true: Punkt ist eine automatisch berechnete Verbindung zwischen aktiven
   * Wegpunkt-Segmenten und wird bei Änderung der "Mow areas"-Schalter entfernt
   * und neu berechnet. */
  conn?: boolean;
  /** Compact category assigned by the planner (AREA, BORDER, etc.). */
  tag?: number;
  /** UI-only boundary after a route section filtered by active toggles. */
  routeBreakBefore?: boolean;
}

export interface Edge {
  begin: Point;
  end: Point;
}

export interface Area {
  points: Point[];
}

export interface Perimeter extends Area {}

export interface Exclusion extends Area {}

export interface Dockpoints extends Area {}

export interface SearchWire extends Area {}

export interface Waypoints extends Area {}

export interface Map {
  perimeter: Perimeter;
  exclusions: Exclusion[];
  dockpoints: Dockpoints;
  searchWire: SearchWire;
  waypoints: Waypoints;
}

export interface MapPresentation {
  boundary: {
    a: Point;
    b: Point;
  };
  center: Point;
  rotation: number;
  viewBox: string;
}

export type MapArea = "perimeter" | "exclusion" | "dockpoints" | "waypoints";

export interface MapSelection {
  area: MapArea;
  exclusionIndex?: number;
  index: number;
  type: "point" | "edge";
}

