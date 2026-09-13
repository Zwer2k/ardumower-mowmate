import { get, writable } from "svelte/store";
import { getModemInfo, type ApiModemInfoResponse } from "./service";
import { fetchFirmwareReleases, type FirmwareRelease } from "./github-releases";

export interface FirmwareUpdateState {
  loading: boolean;
  loaded: boolean;
  error: string | null;
  modemInfo: ApiModemInfoResponse | null;
  releases: FirmwareRelease[];
}

const initialState: FirmwareUpdateState = {
  loading: false,
  loaded: false,
  error: null,
  modemInfo: null,
  releases: [],
};

export const firmwareUpdateStore = writable<FirmwareUpdateState>(initialState);

/** Offen-Zustand des Firmware-Dialogs. Er wird aus dem Overflow-Menü und aus
 *  dem Update-Symbol im Header geöffnet, gerendert wird er nur einmal. */
export const firmwareDialogOpen = writable(false);

let pendingCheck: Promise<void> | null = null;

export const checkFirmwareUpdates = async (force = false): Promise<void> => {
  const current = get(firmwareUpdateStore);
  if (!force && (current.loaded || current.loading)) return pendingCheck ?? Promise.resolve();
  if (pendingCheck) return pendingCheck;

  firmwareUpdateStore.update((state) => ({ ...state, loading: true, error: null }));
  pendingCheck = (async () => {
    try {
      const modemInfo = await getModemInfo();
      const releases = await fetchFirmwareReleases(modemInfo.firmware_target);
      firmwareUpdateStore.set({
        loading: false,
        loaded: true,
        error: null,
        modemInfo,
        releases,
      });
    } catch (error) {
      firmwareUpdateStore.update((state) => ({
        ...state,
        loading: false,
        loaded: true,
        error: error instanceof Error ? error.message : String(error),
      }));
    } finally {
      pendingCheck = null;
    }
  })();

  return pendingCheck;
};
