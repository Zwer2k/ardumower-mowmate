import { writable, derived, get } from "svelte/store";
import type { ScheduleData, ScheduleEntry, MapMeta } from "../model";
import { socketStore, socketService } from "./socket";

export interface ScheduleState extends ScheduleData {
  dirty: boolean;
}

export const defaultScheduleState: ScheduleState = {
  enabled: false,
  entries: [],
  nextRun: 0,
  nextRunEntryId: -1,
  nextRunMapName: "",
  dirty: false,
};

function createScheduleStore() {
  const { subscribe, set, update } = writable<ScheduleState>(defaultScheduleState);

  // Last schedule JSON sent to the backend via commitDraft(). Used to ignore
  // echo broadcasts: after a local change we immediately get the same data
  // back from the backend, which must not reset our dirty flag.
  let lastSentScheduleJson = "";

  return {
    subscribe,
    set,
    update,
    /**
     * Override the whole local draft from a backend push.
     * Local dirty is preserved when the push is just an echo of the last draft
     * we sent (same enabled + entries), so the Save button stays active after a
     * user change. Reloads / external syncs that differ clear dirty.
     */
    resetFromBackend(data: ScheduleData | null) {
      console.log("[schedule] resetFromBackend, enabled:", data?.enabled);
      if (!data) {
        set({ ...defaultScheduleState, dirty: false });
        lastSentScheduleJson = "";
        return;
      }
      const incoming: ScheduleData = {
        enabled: data.enabled ?? false,
        entries: data.entries ? [...data.entries] : [],
      };
      const incomingJson = JSON.stringify(incoming);
      const isEcho = incomingJson === lastSentScheduleJson;
      if (isEcho) {
        console.log("[schedule] resetFromBackend ignored echo");
        return;
      }
      lastSentScheduleJson = "";
      set({
        enabled: data.enabled ?? false,
        entries: data.entries ? [...data.entries] : [],
        nextRun: data.nextRun ?? 0,
        nextRunEntryId: data.nextRunEntryId ?? -1,
        nextRunMapName: data.nextRunMapName ?? "",
        dirty: data.dirty ?? false,
      });
    },
    /** Mark dirty and push the current draft to the backend (RAM only). */
    commitDraft() {
      console.log("[schedule] commitDraft called");
      update((s) => {
        const draft: ScheduleData = {
          enabled: s.enabled,
          entries: s.entries.map((e) => ({ ...e })),
        };
        lastSentScheduleJson = JSON.stringify(draft);
        socketService.sendSetSchedule(draft);
        return { ...s, dirty: true };
      });
    },
    /** Explicitly save to SPIFFS via backend. */
    save() {
      socketService.sendSaveSchedule();
      lastSentScheduleJson = "";
      update((s) => ({ ...s, dirty: false }));
    },
  };
}

export const scheduleStore = createScheduleStore();

export function nextEntryId(entries: ScheduleEntry[]): number {
  let max = 0;
  for (const e of entries) {
    if (e.id > max) max = e.id;
  }
  return max + 1;
}

export function newScheduleEntry(currentMapId: string, currentMapName: string): ScheduleEntry {
  return {
    id: 0,
    enabled: true,
    name: "Mähen",
    mapId: currentMapId,
    mapName: currentMapName,
    mode: 0,
    hour: 8,
    minute: 0,
    daysOfWeek: 0x7f,
    daysOfMonth: 0x1,
  };
}

export function weekDayBit(day: number): number {
  // day 0=Sunday ... 6=Saturday
  return 1 << day;
}

export function toggleWeekDay(days: number, day: number): number {
  return days ^ weekDayBit(day);
}

export function hasWeekDay(days: number, day: number): boolean {
  return (days & weekDayBit(day)) !== 0;
}

export function monthDayBit(day: number): number {
  // day 1..31
  if (day < 1 || day > 31) return 0;
  return 1 << (day - 1);
}

export function toggleMonthDay(days: number, day: number): number {
  return days ^ monthDayBit(day);
}

export function hasMonthDay(days: number, day: number): boolean {
  return (days & monthDayBit(day)) !== 0;
}

export function formatNextRun(epochSec: number | undefined): string {
  if (!epochSec) return "—";
  const d = new Date(epochSec * 1000);
  return d.toLocaleString("de-DE", {
    weekday: "short",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
  });
}

export const scheduleNextRunDisplay = derived(
  [socketStore, scheduleStore],
  ([$socket, $local]) => {
    const data = $socket.schedule;
    if (!data || !data.enabled || !data.nextRun) return null;
    const entry = data.entries.find((e) => e.id === data.nextRunEntryId);
    return {
      time: formatNextRun(data.nextRun),
      mapName: entry?.mapName || data.nextRunMapName || "—",
      entryName: entry?.name || "",
    };
  }
);

// Sync backend schedule into local store whenever it arrives.
let lastBackendScheduleJson = "";
socketStore.subscribe(($socket) => {
  const data = $socket.schedule;
  const json = data ? JSON.stringify(data) : "";
  console.log("[schedule] backend sync check, dirty before:", get(scheduleStore).dirty, "json same:", json === lastBackendScheduleJson);
  if (json === lastBackendScheduleJson) return;
  lastBackendScheduleJson = json;
  console.log("[schedule] resetFromBackend, data:", data);
  scheduleStore.resetFromBackend(data);
});

export function useCurrentMap(maps: MapMeta[], currentMapId: string): { id: string; name: string } | null {
  const map = maps.find((m) => m.id === currentMapId);
  if (!map) return null;
  return { id: map.id, name: map.name };
}
