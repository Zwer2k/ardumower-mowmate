// Teilt die Mähroute nach dem Fortschritt des Mähers (mow_point_index = Index
// des Wegpunkts, den er gerade anfährt) wie CaSSAndRA auf: gemähte Bahnen bis
// zum zuletzt erreichten Punkt, das aktuelle Teilstück und die offenen Bahnen
// ab dem zuletzt erreichten Punkt.

export interface RoutePoint {
  x: number;
  y: number;
  routeBreakBefore?: boolean;
}

export interface RouteProgress<P extends RoutePoint> {
  done: P[][];
  current: P[] | null;
  todo: P[][];
}

// Linienzüge zwischen start und end; ein Punkt mit routeBreakBefore beginnt einen neuen.
export function routeSegments<P extends RoutePoint>(points: P[], start: number, end: number): P[][] {
  const segments: P[][] = [];
  let current: P[] | null = null;
  for (let i = start; i < end; i++) {
    const p = points[i];
    if (!p) continue;
    if (current === null || p.routeBreakBefore) {
      if (current !== null) segments.push(current);
      current = [p];
    } else {
      current.push(p);
    }
  }
  if (current !== null) segments.push(current);
  return segments.filter((s) => s.length > 1);
}

/** Ohne bekannten Fortschritt (nicht synchron, Index < 0) ist alles offen. */
export function splitRouteProgress<P extends RoutePoint>(
  points: P[],
  mowPointIndex: number,
  known: boolean
): RouteProgress<P> {
  const n = points.length;
  if (!known || mowPointIndex < 0 || n < 2) {
    return { done: [], current: null, todo: routeSegments(points, 0, n) };
  }
  const k = Math.min(mowPointIndex, n - 1);
  const current = k > 0 && !points[k].routeBreakBefore ? [points[k - 1], points[k]] : null;
  return {
    done: routeSegments(points, 0, k),
    current,
    todo: routeSegments(points, Math.max(0, k - 1), n),
  };
}

/**
 * Verbindungsfahrten über ausgeblendete Abschnitte ("Mow areas"): Das Modem
 * verbindet beim Upload jede zwei aufeinanderfolgenden Abschnitte mit einer
 * Fahrt um Exclusions herum. Hier nur als gerade Linie zwischen Ende und
 * Anfang, damit Reihenfolge und Zusammenhang sichtbar bleiben.
 */
export function routeTransits<P extends RoutePoint>(points: P[]): [P, P][] {
  const transits: [P, P][] = [];
  for (let i = 1; i < points.length; i++) {
    if (points[i].routeBreakBefore) transits.push([points[i - 1], points[i]]);
  }
  return transits;
}

export interface RouteTransit {
  path: { x: number; y: number }[];
  /** true: der Weg des Modems; false: gerade Linie, bis er vorliegt */
  exact: boolean;
}

const TRANSIT_MATCH_M = 0.05;

/**
 * Wie routeTransits(), aber mit dem Weg, den das Modem für die Lücke
 * berechnet hat (um Exclusions herum), sofern Anfang und Ende passen.
 */
export function routeTransitPaths<P extends RoutePoint>(
  points: P[],
  modemTransits: { x: number; y: number }[][] | null | undefined
): RouteTransit[] {
  const near = (a: { x: number; y: number }, b: { x: number; y: number }) =>
    Math.hypot(a.x - b.x, a.y - b.y) < TRANSIT_MATCH_M;
  return routeTransits(points).map(([a, b]) => {
    const t = modemTransits?.find((line) => line.length >= 2 && near(line[0], a) && near(line[line.length - 1], b));
    return t ? { path: t, exact: true } : { path: [a, b], exact: false };
  });
}
