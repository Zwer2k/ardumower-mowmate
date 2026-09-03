// Map-Persistenz im localStorage: Verhindert erneute Übertragung der Karte
// bei WebSocket-Reconnects, wenn die Karte bereits aktuell ist.
//
// Strategie:
// 1. Nach erfolgreicher Map-Übertragung wird die Map + CRC im localStorage gespeichert
// 2. Bei Reconnect wird die gespeicherte Map sofort geladen (keine Wartezeit)
// 3. Wenn das Backend die Metadaten sendet und die CRC übereinstimmt, wird die
//    Map nicht neu übertragen (das Backend prüft das via "mapSync" oder wir
//    verhindern es client-seitig durch Vorab-Laden)

import { browser } from "$app/environment";
import type { Map } from "./model";
import { emptyMap } from "./core/map-utils";

const STORAGE_KEY_MAP = "ardumower_map_cache";
const STORAGE_KEY_CRC = "ardumower_map_crc";

export interface CachedMap {
  map: Map;
  crc: number;
  timestamp: number;
}

/** Lädt die gespeicherte Map aus dem localStorage, falls vorhanden und nicht zu alt. */
export function loadCachedMap(): CachedMap | null {
  if (!browser) return null;
  try {
    const mapJson = localStorage.getItem(STORAGE_KEY_MAP);
    const crcStr = localStorage.getItem(STORAGE_KEY_CRC);
    if (!mapJson || !crcStr) return null;

    const crc = parseInt(crcStr, 10);
    if (isNaN(crc)) return null;

    const map = JSON.parse(mapJson) as Map;
    // Einfache Validierung: Map muss Perimeter haben
    if (!map.perimeter || !Array.isArray(map.perimeter.points)) return null;

    return {
      map,
      crc,
      timestamp: Date.now(),
    };
  } catch (e) {
    console.warn("[MapCache] Failed to load cached map:", e);
    return null;
  }
}

/** Speichert die Map und ihre CRC im localStorage. */
export function saveCachedMap(map: Map, crc: number): void {
  if (!browser) return;
  try {
    // Nur speichern, wenn die Map valide ist (hat Perimeter-Punkte)
    if (!map.perimeter || map.perimeter.points.length === 0) return;

    localStorage.setItem(STORAGE_KEY_MAP, JSON.stringify(map));
    localStorage.setItem(STORAGE_KEY_CRC, String(crc));
  } catch (e) {
    console.warn("[MapCache] Failed to save map:", e);
    // Bei Quota-Exceeded oder anderem Fehler: Cache löschen
    try {
      localStorage.removeItem(STORAGE_KEY_MAP);
      localStorage.removeItem(STORAGE_KEY_CRC);
    } catch (_) {}
  }
}

/** Löscht den Map-Cache (z.B. wenn die Karte explizit neu geladen wird). */
export function clearCachedMap(): void {
  if (!browser) return;
  try {
    localStorage.removeItem(STORAGE_KEY_MAP);
    localStorage.removeItem(STORAGE_KEY_CRC);
  } catch (e) {
    console.warn("[MapCache] Failed to clear cache:", e);
  }
}

/** Prüft, ob die gespeicherte CRC mit der Backend-CRC übereinstimmt. */
export function isCachedMapCurrent(backendCrc: number): boolean {
  if (!browser) return false;
  try {
    const crcStr = localStorage.getItem(STORAGE_KEY_CRC);
    if (!crcStr) return false;
    return parseInt(crcStr, 10) === backendCrc;
  } catch (e) {
    return false;
  }
}
