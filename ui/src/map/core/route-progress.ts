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
