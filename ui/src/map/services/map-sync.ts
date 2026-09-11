import { writable } from "svelte/store";
import type { State } from "../../model";

export const isMapDirty = writable<boolean>(false);

export function setMapDirty(dirty: boolean) {
  isMapDirty.set(dirty);
}

type MapCrcState = Pick<State, "map_crc" | "uploaded_map_id" | "uploaded_map_crc">;

export function expectedMowerMapCrc(
  state: MapCrcState | null,
  currentMapId: string,
  storedCrc: number,
): number {
  return state?.uploaded_map_id === currentMapId
    ? (state.uploaded_map_crc ?? storedCrc)
    : storedCrc;
}

export function isMowerMapSynced(
  state: MapCrcState | null,
  currentMapId: string,
  storedCrc: number,
): boolean {
  return state !== null && state.map_crc === expectedMowerMapCrc(state, currentMapId, storedCrc);
}

