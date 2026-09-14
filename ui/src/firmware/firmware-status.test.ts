import { get } from "svelte/store";
import { beforeEach, describe, expect, it } from "vitest";
import {
  applyFirmwareStatus as applyMessage,
  firmwareStatusStore,
  resetFirmwareStatus,
} from "../stores/socket";

describe("firmware status store", () => {
  beforeEach(resetFirmwareStatus);

  it("stays unknown until the modem reported for the first time", () => {
    expect(get(firmwareStatusStore).reachable).toBeNull();

    applyMessage({ reachable: false, checking: true, updateAvailable: false, checked: false });
    expect(get(firmwareStatusStore).reachable).toBeNull();
    expect(get(firmwareStatusStore).checking).toBe(true);
  });

  it("keeps the previous verdict during a forced re-check", () => {
    applyMessage({ reachable: true, checking: false, updateAvailable: false, checked: true });
    applyMessage({ reachable: false, checking: true, updateAvailable: false, checked: false });

    expect(get(firmwareStatusStore).reachable).toBe(true);
    expect(get(firmwareStatusStore).checking).toBe(true);
  });

  it("takes over versions and the update flag", () => {
    applyMessage({
      reachable: true,
      checking: false,
      updateAvailable: true,
      checked: true,
      current: "v1.3.0",
      latest: "v1.4.0",
      target: "esp32-s3",
      versions: ["v1.4.0", "v1.3.0"],
    });

    expect(get(firmwareStatusStore)).toEqual({
      reachable: true,
      checking: false,
      updateAvailable: true,
      checked: true,
      current: "v1.3.0",
      latest: "v1.4.0",
      target: "esp32-s3",
      versions: ["v1.4.0", "v1.3.0"],
      error: null,
    });
  });

  it("keeps the last known versions when a later check fails", () => {
    applyMessage({
      reachable: true,
      checking: false,
      updateAvailable: true,
      checked: true,
      current: "v1.3.0",
      latest: "v1.4.0",
      versions: ["v1.4.0", "v1.3.0"],
    });
    applyMessage({
      reachable: false,
      checking: false,
      updateAvailable: false,
      checked: true,
      error: "dns-failed",
    });

    const status = get(firmwareStatusStore);
    expect(status.reachable).toBe(false);
    expect(status.error).toBe("dns-failed");
    expect(status.latest).toBe("v1.4.0");
    // Die zuletzt bekannte Auswahl bleibt stehen, damit das Dropdown bei einem
    // vorübergehenden Fehler nicht leer wird.
    expect(status.versions).toEqual(["v1.4.0", "v1.3.0"]);
    expect(status.updateAvailable).toBe(false);
  });

  it("does not claim unreachable before the first check completed", () => {
    // Genau der Zustand direkt nach dem Modem-Start: noch kein Ergebnis, aber
    // auch kein Grund – die UI darf hier nicht "not reachable" behaupten.
    applyMessage({ reachable: false, checking: false, updateAvailable: false, checked: false });

    const status = get(firmwareStatusStore);
    expect(status.checked).toBe(false);
    expect(status.error).toBeNull();
  });

  it("clears the reason once GitHub becomes reachable again", () => {
    applyMessage({ reachable: false, checking: false, updateAvailable: false, checked: false, error: "wifi-disconnected" });
    applyMessage({ reachable: true, checking: false, updateAvailable: false, checked: true });

    expect(get(firmwareStatusStore).error).toBeNull();
  });
});
