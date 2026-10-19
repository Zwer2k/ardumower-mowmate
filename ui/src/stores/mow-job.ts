// Route des zuletzt hochgeladenen Mähauftrags (mehrere Bereiche). Das Modem
// schickt sie in Stücken; die Dashboards zeigen sie statt der Route der
// geladenen Karte, solange sie auf dem Mäher ist.
import { writable } from "svelte/store";
import { routeSegments, splitRouteProgress, type RoutePoint } from "../map/core/route-progress";

export interface MowJobPart {
  id: string;
  name: string;
  /** Index des ersten Wegpunkts dieses Bereichs */
  start: number;
}

/** Perimeter, Exclusions und Dock einer Karte (UI-Koordinaten, y gespiegelt) */
export interface MapOutline {
  perimeter: RoutePoint[];
  exclusions: RoutePoint[][];
  dockpoints: RoutePoint[];
  rotation: number;
}

/** Umriss vom Modem ([[x,y],...], y nicht gespiegelt) */
export interface RawOutline {
  perimeter?: number[][];
  exclusions?: number[][][];
  dockpoints?: number[][];
  rotation?: number;
}

export function parseOutline(raw: RawOutline | null | undefined): MapOutline | undefined {
  if (!raw || !Array.isArray(raw.perimeter) || raw.perimeter.length < 3) return undefined;
  const ring = (pts: number[][] | undefined) =>
    (pts ?? []).filter((p) => Array.isArray(p) && p.length >= 2).map((p) => ({ x: p[0], y: -p[1] }));
  return {
    perimeter: ring(raw.perimeter),
    exclusions: (raw.exclusions ?? []).map(ring).filter((e) => e.length >= 3),
    dockpoints: ring(raw.dockpoints),
    rotation: raw.rotation ?? 0,
  };
}

/** Umriss in der Form einer Karte (für Komponenten, die eine Map zeichnen);
 * je Umriss nur einmal gebaut, damit Anzeigen nicht bei jedem Status neu zeichnen */
const outlineMaps = new WeakMap<MapOutline, ReturnType<typeof buildOutlineMap>>();
export function outlineAsMap(o: MapOutline) {
  let m = outlineMaps.get(o);
  if (!m) {
    m = buildOutlineMap(o);
    outlineMaps.set(o, m);
  }
  return m;
}
function buildOutlineMap(o: MapOutline) {
  return {
    perimeter: { points: o.perimeter },
    exclusions: o.exclusions.map((points) => ({ points })),
    dockpoints: { points: o.dockpoints },
    searchWire: { points: [] as RoutePoint[] },
    waypoints: { points: [] as RoutePoint[] },
  };
}

export interface MowJobRoute {
  id: string;
  crc: number;
  timestamp: number;
  overlapOnce: boolean;
  parts: MowJobPart[];
  /** UI-Koordinaten (y gespiegelt) */
  points: RoutePoint[];
  total: number;
  complete: boolean;
  /** Teilaufträge: erster Wegpunkt je Teil und der Teil auf dem Mäher */
  segmentStarts: number[];
  segment: number;
  /** Geometrie der Karte des Auftrags; er gehört nicht zwingend zur geladenen */
  geometry?: MapOutline;
}

export interface MowJobChunk {
  id: string;
  crc: number;
  timestamp: number;
  overlapOnce: boolean;
  offset: number;
  total: number;
  parts?: MowJobPart[];
  xy: number[];
  segment?: number;
  segmentStarts?: number[];
  geometry?: RawOutline;
}

export const mowJobStore = writable<MowJobRoute | null>(null);

/** Letzte Auswahl im Dialog "Choose areas"; bleibt beim Schließen und Seitenwechsel erhalten */
export const mowNowSelectionStore = writable<{ selected: string[]; overlapOnce: boolean } | null>(null);

let building: MowJobRoute | null = null;

/** Fügt ein Stück an; liefert die vollständige Route, sobald alle Punkte da sind. */
export function mergeMowJobChunk(current: MowJobRoute | null, chunk: MowJobChunk): MowJobRoute | null {
  let job = current;
  if (chunk.offset === 0 || !job || job.timestamp !== chunk.timestamp || job.points.length !== chunk.offset) {
    if (chunk.offset !== 0) return null; // Stück verpasst: auf den nächsten Durchlauf warten
    job = {
      id: chunk.id,
      crc: chunk.crc,
      timestamp: chunk.timestamp,
      overlapOnce: chunk.overlapOnce,
      parts: chunk.parts ?? [],
      points: [],
      total: chunk.total,
      complete: false,
      segmentStarts: chunk.segmentStarts?.length ? chunk.segmentStarts : [0],
      segment: chunk.segment ?? 0,
      geometry: parseOutline(chunk.geometry),
    };
  }
  const points = job.points.slice();
  for (let i = 0; i + 1 < chunk.xy.length; i += 2) points.push({ x: chunk.xy[i], y: -chunk.xy[i + 1] });
  return { ...job, points, complete: points.length >= job.total };
}

export function applyMowJobChunk(chunk: MowJobChunk) {
  if (!chunk.id) {
    // Das Modem hat den Auftrag verworfen
    building = null;
    mowJobStore.set(null);
    return;
  }
  building = mergeMowJobChunk(building, chunk);
  if (building?.complete) mowJobStore.set(building);
}

/** Die Auftragsroute ist das, was der Mäher gerade hat. */
export function isJobOnMower(
  job: MowJobRoute | null,
  state: { map_crc?: number; uploaded_map_id?: string } | null | undefined
): boolean {
  return !!job && job.complete && !!state && state.uploaded_map_id === job.id && state.map_crc === job.crc;
}

/** mow_point_index des Mähers (im Teil auf dem Mäher) als Index der ganzen Route */
export function jobRouteIndex(job: MowJobRoute | null, mowPointIndex: number): number {
  if (!job || mowPointIndex < 0) return mowPointIndex;
  return (job.segmentStarts[job.segment] ?? 0) + mowPointIndex;
}

/** Zusammengesetzt, aber (noch) nicht auf dem Mäher: Vorschau */
export function isJobPreview(
  job: MowJobRoute | null,
  state: { map_crc?: number; uploaded_map_id?: string } | null | undefined
): boolean {
  return !!job && job.complete && !isJobOnMower(job, state);
}

// Farben der Bereiche eines Auftrags, offene Bahnen
export const JOB_COLORS = ["#7fb249", "#1192e8", "#fa4d56", "#a56eff", "#d2a106", "#009d9a", "#ee538b", "#8a3800"];

export interface JobPartProgress<P extends RoutePoint> {
  name: string;
  color: string;
  done: P[][];
  current: P[] | null;
  todo: P[][];
}

/** Route je Bereich, aufgeteilt nach dem Fortschritt (globaler mow_point_index). */
export function jobPartProgress<P extends RoutePoint>(
  points: P[],
  parts: MowJobPart[],
  mowPointIndex: number,
  known: boolean
): JobPartProgress<P>[] {
  const result: JobPartProgress<P>[] = [];
  const list = parts.length > 0 ? parts : [{ id: "", name: "", start: 0 }];
  for (let i = 0; i < list.length; i++) {
    // Mit dem letzten Punkt davor, damit die Anfahrt zum Bereich gehört
    const from = Math.max(0, list[i].start - 1);
    const to = i + 1 < list.length ? list[i + 1].start : points.length;
    const slice = points.slice(from, to);
    const color = JOB_COLORS[i % JOB_COLORS.length];
    const base = { name: list[i].name, color };
    if (!known || mowPointIndex < 0 || mowPointIndex <= from) {
      result.push({ ...base, done: [], current: null, todo: routeSegments(slice, 0, slice.length) });
    } else if (mowPointIndex >= to) {
      result.push({ ...base, done: routeSegments(slice, 0, slice.length), current: null, todo: [] });
    } else {
      result.push({ ...base, ...splitRouteProgress(slice, mowPointIndex - from, true) });
    }
  }
  return result;
}
