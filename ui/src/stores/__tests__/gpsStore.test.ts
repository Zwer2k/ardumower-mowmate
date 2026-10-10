import { describe, it, expect, beforeEach, afterEach } from "vitest";
import { get } from "svelte/store";
import { socketStore } from "../socket";
import { gpsStore } from "../gpsStore";

// UBX-Frame mit Prüfsumme als Hex, wie ihn das Modem weiterreicht
function frame(cls: number, id: number, payload: number[]): string {
  const body = [cls, id, payload.length & 0xff, payload.length >> 8, ...payload];
  let a = 0, b = 0;
  for (const x of body) { a = (a + x) & 0xff; b = (b + a) & 0xff; }
  return [0xb5, 0x62, ...body, a, b].map((x) => x.toString(16).padStart(2, "0")).join("");
}

function u4(v: number): number[] {
  return [v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, (v >>> 24) & 0xff];
}

function navPvt(opts: { fixOk: boolean; fixType: number; carrSoln: number; lat: number; lon: number; hAcc?: number }): string {
  const p = new Array(92).fill(0);
  p[20] = opts.fixType;
  p[21] = (opts.fixOk ? 1 : 0) | (opts.carrSoln << 6);
  p[23] = 17;
  p.splice(24, 4, ...u4(Math.round(opts.lon * 1e7)));
  p.splice(28, 4, ...u4(Math.round(opts.lat * 1e7)));
  p.splice(40, 4, ...u4(opts.hAcc ?? 14));
  return frame(0x01, 0x07, p);
}

let ubxTimestamp = 1;
function receive(hex: string) {
  socketStore.update((s) => ({ ...s, ubxResponse: { timestamp: ubxTimestamp++, hex } } as any));
}

describe("gpsStore", () => {
  beforeEach(() => {
    socketStore.update((s) => ({ ...s, connected: true }));
    gpsStore.connect();
  });
  afterEach(() => gpsStore.disconnect());

  it("parses NAV-PVT and delivers a new state object per update", () => {
    const before = get(gpsStore);
    receive(navPvt({ fixOk: true, fixType: 3, carrSoln: 2, lat: 51.5, lon: 10.25, hAcc: 0xffffffff }));
    const after = get(gpsStore);
    expect(after).not.toBe(before);
    expect(after.navPvt?.lat).toBeCloseTo(51.5, 6);
    expect(after.navPvt?.lon).toBeCloseTo(10.25, 6);
    expect(after.navPvt?.carrSoln).toBe(2);
    // unsigned: 0xFFFFFFFF mm is no negative accuracy
    expect(after.navPvt!.hAcc).toBeGreaterThan(4e6);
    expect(after.positionHistory.length).toBe(before.positionHistory.length + 1);
  });

  it("keeps samples without a fix out of the position history but in the RTK history", () => {
    const before = get(gpsStore);
    receive(navPvt({ fixOk: false, fixType: 0, carrSoln: 0, lat: 0, lon: 0 }));
    const after = get(gpsStore);
    expect(after.positionHistory.length).toBe(before.positionHistory.length);
    expect(after.fixHistory.length).toBe(before.fixHistory.length + 1);
    expect(after.fixHistory[after.fixHistory.length - 1].carrSoln).toBe(0);
  });

  it("reads NAV-SAT count and flags (svUsed bit 3, health bits 4-5)", () => {
    const p = [...u4(1000), 1, 2, 0, 0];
    // GPS 5: used, healthy (1), quality 7; Galileo 11: not used, quality 4, diffCorr only
    p.push(0, 5, 45, 60, 90, 0, 0, 0, ...u4(0x07 | 0x08 | (1 << 4)));
    p.push(2, 11, 30, 10, 0x0e, 0x01, 0, 0, ...u4(0x04 | 0x40));
    receive(frame(0x01, 0x35, p));
    const sats = get(gpsStore).navSat;
    expect(sats.length).toBe(2);
    expect(sats[0]).toMatchObject({ gnssId: 0, svId: 5, cno: 45, elev: 60, azim: 90, used: true, health: 1, quality: 7 });
    expect(sats[1]).toMatchObject({ gnssId: 2, svId: 11, azim: 270, used: false, health: 0, quality: 4 });
  });

  it("ignores frames with a wrong checksum", () => {
    const good = navPvt({ fixOk: true, fixType: 3, carrSoln: 1, lat: 1, lon: 2 });
    const bad = good.slice(0, -2) + "00";
    const before = get(gpsStore).navPvt;
    receive(bad);
    expect(get(gpsStore).navPvt).toBe(before);
  });
});
