import { get } from "svelte/store";
import type { Map } from "../model";
import type { MowSettings } from "../../model";
import { filterWaypointsByToggles } from "../core/waypoint-filter";
import { isMowerMapSynced } from "./map-sync";
import { socketStore, type SocketState } from "../../stores/socket";
import { MapStore } from "../service";
import { mowSettingsStore } from "../mow-settings";
import { openConfirm } from "../../stores/confirm-dialog";

// Wie viele Kartenpunkte passen in den Arbeitsspeicher des Mähers?
//
// Sunray (map.cpp) sammelt beim Upload erst alle Punkte in einer Liste, die
// Punkt für Punkt wächst (neues Array, umkopieren, altes freigeben), und
// kopiert sie dann in Perimeter-, Dock- und Mähpunkte. Ein Punkt belegt 4 Byte
// (zwei short), zeitweise aber doppelt: rund 8 Byte pro Punkt. Unter
// SETPOINT_MIN_FREE_MEMORY (5000 Byte) bricht Sunray mit "setPoint: OUT OF
// MEMORY" ab. Die bisher geladene Karte gibt Sunray zu Beginn des Uploads frei.
const BYTES_PER_POINT = 8;
const STORED_BYTES_PER_POINT = 4;
const SUNRAY_MIN_FREE_MEMORY = 5000;
// Sunray unter Linux meldet sehr viel freien Speicher; dort gibt es kein Limit.
const UNLIMITED_FREE_MEMORY = 1024 * 1024;

export interface MowerCapacity {
  /** Punkte, die zum Mäher übertragen werden. */
  points: number;
  /** Geschätzte Obergrenze, null wenn unbekannt oder praktisch unbegrenzt. */
  maxPoints: number | null;
  /** true, wenn die geladene Karte im Mäher unbekannt ist: dann ist maxPoints eine Untergrenze. */
  lowerBound: boolean;
}

export function uploadPointCount(map: Map | null | undefined, settings: MowSettings | null): number {
  if (!map) return 0;
  const waypoints = settings
    ? filterWaypointsByToggles(map.waypoints.points, map, settings).length
    : map.waypoints.points.length;
  return (
    map.perimeter.points.length +
    map.exclusions.reduce((sum, e) => sum + e.points.length, 0) +
    map.dockpoints.points.length +
    waypoints
  );
}

export function estimateMowerCapacity(
  freeMemory: number | undefined,
  points: number,
  loadedPoints: number | null,
): MowerCapacity {
  if (!freeMemory || freeMemory <= 0 || freeMemory >= UNLIMITED_FREE_MEMORY) {
    return { points, maxPoints: null, lowerBound: false };
  }
  const available = freeMemory + (loadedPoints ?? 0) * STORED_BYTES_PER_POINT - SUNRAY_MIN_FREE_MEMORY;
  const maxPoints = Math.max(0, Math.floor(available / BYTES_PER_POINT));
  return { points, maxPoints, lowerBound: loadedPoints == null };
}

/** Kapazität für eine Karte; Werte aus socketStore, MapStore und mowSettingsStore. */
export function mowerCapacityFor(
  socket: SocketState,
  map: Map | null | undefined,
  settings: MowSettings | null,
): MowerCapacity {
  const points = uploadPointCount(map, settings);
  const synced = isMowerMapSynced(socket.state, socket.currentMapId, socket.currentMapMeta?.crc ?? 0);
  // Hält der Mäher genau diese Karte, wird ihr Speicher beim Upload frei.
  // Hält er eine andere Karte, ist deren Größe unbekannt.
  const mowerHasMap = (socket.state?.map_crc ?? 0) !== 0;
  const loadedPoints = synced ? points : mowerHasMap ? null : 0;
  return estimateMowerCapacity(socket.stats?.free_memory, points, loadedPoints);
}

/** Kapazität für die aktuell geladene Karte. */
export function currentMowerCapacity(): MowerCapacity {
  return mowerCapacityFor(get(socketStore), get(MapStore)?.map, get(mowSettingsStore));
}

export function capacityExceeded(c: MowerCapacity): boolean {
  return c.maxPoints != null && c.points > c.maxPoints;
}

/** Vor dem Upload: bei zu großer Karte nachfragen. true = hochladen. */
export async function confirmUploadCapacity(): Promise<boolean> {
  const c = currentMowerCapacity();
  if (!capacityExceeded(c)) return true;
  return openConfirm({
    title: "Map probably too large for the mower",
    message:
      `The map has ${c.points} points, the mower has memory for ${c.lowerBound ? "at least " : "about "}` +
      `${c.maxPoints} (estimated from its free memory). Sunray will likely stop the upload with ` +
      `"OUT OF MEMORY" and switch to an error. Reduce the points first: Lines instead of Rings, ` +
      `a larger track width or stronger route simplification. Upload anyway?`,
    confirmText: "Upload anyway",
    cancelText: "Cancel",
    kind: "danger",
  });
}
