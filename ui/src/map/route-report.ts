import { writable } from "svelte/store";
import type { RouteReportData } from "../model";

/**
 * Result of the route check that the modem runs after every waypoint
 * calculation. Null means no report for the current route, either because
 * nothing has been calculated in this session or because a new calculation
 * is running.
 */
export const routeReportStore = writable<RouteReportData | null>(null);

export function clearRouteReport() {
  routeReportStore.set(null);
}

/** "1 h 52 min" / "4 min 20 s" — never a bare pile of seconds. */
export function formatDuration(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds <= 0) return "0 s";
  const total = Math.round(seconds);
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  if (h > 0) return `${h} h ${m} min`;
  if (m > 0) return `${m} min ${s} s`;
  return `${s} s`;
}

/** Metres below 1 km, kilometres above. */
export function formatLength(metres: number): string {
  if (!Number.isFinite(metres)) return "-";
  if (metres >= 1000) return `${(metres / 1000).toFixed(2)} km`;
  return `${metres.toFixed(1)} m`;
}
