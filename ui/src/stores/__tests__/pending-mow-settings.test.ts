import { describe, it, expect } from "vitest";
import { overlayPendingMowSettings, rememberPendingMowSettings } from "../socket";

describe("pending mow settings", () => {
  it("keeps a newer change when the answer to an older one arrives", () => {
    rememberPendingMowSettings({ doMowArea: false }, "m1", 1000);
    rememberPendingMowSettings({ doMowBorder: true }, "m1", 1100);
    // Antwort auf die erste Änderung: doMowBorder noch alt
    const first = overlayPendingMowSettings({ doMowArea: false, doMowBorder: false }, "m1", 1200);
    expect(first).toEqual({ doMowArea: false, doMowBorder: true });
    // Antwort auf die zweite Änderung bestätigt beide
    const second = overlayPendingMowSettings({ doMowArea: false, doMowBorder: true }, "m1", 1300);
    expect(second).toEqual({ doMowArea: false, doMowBorder: true });
    // nichts mehr offen: das Modem gilt wieder
    expect(overlayPendingMowSettings({ doMowArea: true, doMowBorder: true }, "m1", 1400))
      .toEqual({ doMowArea: true, doMowBorder: true });
  });

  it("gives up after the timeout and ignores other maps", () => {
    rememberPendingMowSettings({ doMowArea: false }, "m1", 0);
    expect(overlayPendingMowSettings({ doMowArea: true }, "m2", 100)).toEqual({ doMowArea: true });
    rememberPendingMowSettings({ doMowArea: false }, "m1", 0);
    expect(overlayPendingMowSettings({ doMowArea: true }, "m1", 6000)).toEqual({ doMowArea: true });
  });
});
