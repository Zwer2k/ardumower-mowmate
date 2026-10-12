import { writable } from "svelte/store";
import { socketStore } from "./socket";

// Ersatzanzeige für Sunray ohne AT+S3 (z. B. upstream): Dort gibt es keine
// Sonar-Abstände, aber einen Sonar-Ereigniszähler in der Statistik
// (AT+T, stats.sonar_triggered). Der Grund eines Stopps (state.sensor, z. B.
// Regen) ist ein Mäher-Status und steht in der Status-Kachel.

/** So lange gilt ein gezähltes Sonar-Ereignis als "ausgelöst". */
export const SONAR_RECENT_MS = 30000;

export interface SensorFallback {
  /** Sonar-Ereignisse laut Statistik, null solange keine Statistik da ist */
  sonarCount: number | null;
  /** Browserzeit des letzten Zählerzuwachses */
  sonarTriggeredAt: number | null;
}

export const sensorFallbackStore = writable<SensorFallback>({ sonarCount: null, sonarTriggeredAt: null });

let lastCount: number | null = null;
socketStore.subscribe((s) => {
  const count = s.stats?.sonar_triggered;
  if (count === undefined || count === null || count === lastCount) return;
  // Der erste Wert ist nur die Ausgangslage; ein kleinerer Wert heißt, dass die
  // Statistik zurückgesetzt wurde.
  const increased = lastCount !== null && count > lastCount;
  lastCount = count;
  sensorFallbackStore.update((f) => ({
    sonarCount: count,
    sonarTriggeredAt: increased ? Date.now() : f.sonarTriggeredAt,
  }));
});

export function sonarRecentlyTriggered(f: SensorFallback, now: number): boolean {
  return f.sonarTriggeredAt !== null && now - f.sonarTriggeredAt < SONAR_RECENT_MS;
}
