import { get, writable } from "svelte/store";
import { getModemInfo, type ApiModemInfoResponse } from "./service";
import {
  fetchFirmwareReleases,
  hasFirmwareUpdate,
  type FirmwareRelease,
} from "./github-releases";

export interface FirmwareUpdateState {
  loading: boolean;
  loaded: boolean;
  error: string | null;
  modemInfo: ApiModemInfoResponse | null;
  releases: FirmwareRelease[];
  updateAvailable: boolean;
}

const initialState: FirmwareUpdateState = {
  loading: false,
  loaded: false,
  error: null,
  modemInfo: null,
  releases: [],
  updateAvailable: false,
};

export const firmwareUpdateStore = writable<FirmwareUpdateState>(initialState);

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
        updateAvailable: hasFirmwareUpdate(modemInfo.git_tag, releases),
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
