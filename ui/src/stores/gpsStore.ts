import { writable } from "svelte/store";
import { socketStore, socketService } from "./socket";
import type { GpsDetails } from "../model";

export type { GpsDetails };

// ─── Types ──────────────────────────────────────────────────────────────────

export interface NavSatInfo {
  gnssId: number;
  svId: number;
  elev: number; // degrees
  azim: number; // degrees
  cno: number; // dB-Hz
  used: boolean;
  health: number;
  quality: number;
}

export interface NavPvtInfo {
  lat: number;
  lon: number;
  height: number; // ellipsoid
  hMSL: number;
  hAcc: number;
  vAcc: number;
  gSpeed: number;
  heading: number;
  pDOP: number;
  fixType: number;
  fixOk: boolean;
  numSV: number;
  carrSoln: number; // 0=none, 1=float, 2=fixed
  timestamp: number;
}

export interface NavDopInfo {
  gDOP: number;
  pDOP: number;
  tDOP: number;
  vDOP: number;
  hDOP: number;
  nDOP: number;
  eDOP: number;
}

export interface AltitudeSample {
  time: number;
  height: number;
  hMSL: number;
}

export interface PositionSample {
  time: number;
  lat: number;
  lon: number;
  hAcc: number;
}

/** Lösungsart je NAV-PVT, für den RTK-Verlauf. */
export interface FixSample {
  time: number;
  carrSoln: number; // 0=none, 1=float, 2=fixed
  fixOk: boolean;
}

export interface GpsStoreState {
  // From S4 CSV
  gpsDetails: GpsDetails | null;
  /** Browserzeit (ms), zu der die letzten S4-Daten ankamen. */
  gpsDetailsReceivedAt: number | null;

  // From UBX responses (polled by modem backend)
  navSat: NavSatInfo[];
  navPvt: NavPvtInfo | null;
  navDop: NavDopInfo | null;

  // History ring buffers (positions only from valid fixes)
  altitudeHistory: AltitudeSample[];
  positionHistory: PositionSample[];
  fixHistory: FixSample[];

  // Reference position for deviation map (mean of first N valid fixes)
  refLat: number | null;
  refLon: number | null;
}

const HISTORY_SIZE = 1000;
const REF_FIX_COUNT = 20;
/** RTK-Verlauf: so weit reicht die Zeitleiste zurück. */
export const FIX_HISTORY_MS = 30 * 60 * 1000;

function initialState(): GpsStoreState {
  return {
    gpsDetails: null,
    gpsDetailsReceivedAt: null,
    navSat: [],
    navPvt: null,
    navDop: null,
    altitudeHistory: [],
    positionHistory: [],
    fixHistory: [],
    refLat: null,
    refLon: null,
  };
}

function pushCapped<T>(list: T[], item: T, max: number): T[] {
  const next = list.length >= max ? list.slice(list.length - max + 1) : list.slice();
  next.push(item);
  return next;
}

function createGpsStore() {
  const { subscribe, update } = writable<GpsStoreState>(initialState());

  let refCount = 0;
  let unsubscribeSocket: (() => void) | null = null;
  let processedUbxTimestamp = 0;
  let lastGpsDetails: GpsDetails | null = null;
  // Abo auf dem Modem (requestGpsDetails). Das Modem vergisst es, wenn die
  // WebSocket-Verbindung abreißt; nach dem Reconnect wird es erneuert.
  let modemSubscribed = false;

  function syncModemSubscription(connected: boolean) {
    const wanted = refCount > 0 && connected;
    if (wanted && !modemSubscribed) {
      modemSubscribed = true;
      queueMicrotask(() => socketService.requestGpsDetails());
    } else if (!wanted && modemSubscribed) {
      modemSubscribed = false;
      if (connected) queueMicrotask(() => socketService.stopGpsDetails());
    }
  }

  function handleSocketState(state: {
    connected: boolean;
    gpsDetails: GpsDetails | null;
    ubxResponse: { timestamp: number; hex: string } | null;
  }) {
    syncModemSubscription(state.connected);

    const newDetails = state.gpsDetails && state.gpsDetails !== lastGpsDetails;
    const newUbx =
      !!state.ubxResponse &&
      !!state.ubxResponse.timestamp &&
      state.ubxResponse.timestamp !== processedUbxTimestamp;
    if (!newDetails && !newUbx) return;

    // Immer ein neues Objekt liefern: Svelte erkennt Änderungen per Identität.
    update((prev) => {
      let s: GpsStoreState = { ...prev };
      if (newDetails) {
        lastGpsDetails = state.gpsDetails;
        s.gpsDetails = state.gpsDetails;
        s.gpsDetailsReceivedAt = Date.now();
      }
      if (newUbx) {
        processedUbxTimestamp = state.ubxResponse!.timestamp;
        const cleanHex = (state.ubxResponse!.hex || "").replace(/[^0-9a-fA-F]/g, "");
        if (cleanHex.length > 0) s = applyUbx(s, hexToBytes(cleanHex));
      }
      return s;
    });
  }

  function applyUbx(s: GpsStoreState, bytes: number[]): GpsStoreState {
    for (const f of findUbxFrames(bytes)) {
      if (f.classId === 0x01 && f.msgId === 0x35) {
        const sats = parseNavSat(bytes, f);
        if (sats) s.navSat = sats;
      } else if (f.classId === 0x01 && f.msgId === 0x07) {
        const pvt = parseNavPvt(bytes, f);
        if (pvt) s = applyNavPvt(s, pvt);
      } else if (f.classId === 0x01 && f.msgId === 0x04) {
        const dop = parseNavDop(bytes, f);
        if (dop) s.navDop = dop;
      }
    }
    return s;
  }

  function applyNavPvt(s: GpsStoreState, pvt: NavPvtInfo): GpsStoreState {
    s.navPvt = pvt;
    s.fixHistory = pushCapped(
      s.fixHistory.filter((f) => pvt.timestamp - f.time <= FIX_HISTORY_MS),
      { time: pvt.timestamp, carrSoln: pvt.fixOk ? pvt.carrSoln : 0, fixOk: pvt.fixOk },
      HISTORY_SIZE,
    );
    // Ohne gültige Lösung sind Position und Höhe Unsinn (oft 0/0): nicht in
    // die Verläufe und nicht in den Bezugspunkt der Abweichungskarte.
    const valid = pvt.fixOk && pvt.fixType >= 2 && (pvt.lat !== 0 || pvt.lon !== 0);
    if (!valid) return s;
    s.altitudeHistory = pushCapped(
      s.altitudeHistory,
      { time: pvt.timestamp, height: pvt.height, hMSL: pvt.hMSL },
      HISTORY_SIZE,
    );
    s.positionHistory = pushCapped(
      s.positionHistory,
      { time: pvt.timestamp, lat: pvt.lat, lon: pvt.lon, hAcc: pvt.hAcc },
      HISTORY_SIZE,
    );
    if ((s.refLat === null || s.refLon === null) && s.positionHistory.length >= REF_FIX_COUNT) {
      const first = s.positionHistory.slice(0, REF_FIX_COUNT);
      s.refLat = first.reduce((sum, p) => sum + p.lat, 0) / first.length;
      s.refLon = first.reduce((sum, p) => sum + p.lon, 0) / first.length;
    }
    return s;
  }

  function parseNavSat(bytes: number[], f: UbxFrame): NavSatInfo[] | null {
    if (f.length < 8) return null;
    const p = f.payloadStart;
    // NAV-SAT: +0 iTOW, +4 version, +5 numSvs, then 12 bytes per satellite
    const numSats = bytes[p + 5];
    const sats: NavSatInfo[] = [];
    for (let i = 0; i < numSats && 8 + i * 12 + 12 <= f.length; i++) {
      const off = p + 8 + i * 12;
      const flags = readU4(bytes, off + 8);
      sats.push({
        gnssId: bytes[off],
        svId: bytes[off + 1],
        cno: bytes[off + 2],
        elev: bytesToSigned(bytes, off + 3),
        azim: readU2(bytes, off + 4),
        // flags: qualityInd 0-2, svUsed 3, health 4-5, diffCorr 6
        quality: flags & 0x07,
        used: (flags & 0x08) !== 0,
        health: (flags >> 4) & 0x03,
      });
    }
    return sats;
  }

  function parseNavPvt(bytes: number[], f: UbxFrame): NavPvtInfo | null {
    if (f.length < 92) return null;
    const p = f.payloadStart;
    return {
      fixType: bytes[p + 20],
      fixOk: (bytes[p + 21] & 0x01) !== 0,
      carrSoln: (bytes[p + 21] >> 6) & 0x03,
      numSV: bytes[p + 23],
      lon: readI4(bytes, p + 24) * 1e-7,
      lat: readI4(bytes, p + 28) * 1e-7,
      height: readI4(bytes, p + 32) * 1e-3,
      hMSL: readI4(bytes, p + 36) * 1e-3,
      hAcc: readU4(bytes, p + 40) * 1e-3,
      vAcc: readU4(bytes, p + 44) * 1e-3,
      gSpeed: readI4(bytes, p + 60) * 1e-3,
      heading: readI4(bytes, p + 64) * 1e-5,
      pDOP: readU2(bytes, p + 76) * 0.01,
      timestamp: Date.now(),
    };
  }

  function parseNavDop(bytes: number[], f: UbxFrame): NavDopInfo | null {
    if (f.length < 18) return null;
    const p = f.payloadStart + 4; // after iTOW
    return {
      gDOP: readU2(bytes, p + 0) * 0.01,
      pDOP: readU2(bytes, p + 2) * 0.01,
      tDOP: readU2(bytes, p + 4) * 0.01,
      vDOP: readU2(bytes, p + 6) * 0.01,
      hDOP: readU2(bytes, p + 8) * 0.01,
      nDOP: readU2(bytes, p + 10) * 0.01,
      eDOP: readU2(bytes, p + 12) * 0.01,
    };
  }

  function connect() {
    refCount++;
    if (refCount === 1) {
      unsubscribeSocket = socketStore.subscribe(handleSocketState);
    }
  }

  function disconnect() {
    if (refCount === 0) return;
    refCount--;
    if (refCount === 0) {
      let connected = false;
      socketStore.subscribe((s) => (connected = s.connected))();
      syncModemSubscription(connected);
      if (unsubscribeSocket) {
        unsubscribeSocket();
        unsubscribeSocket = null;
      }
      // Reset so next connect re-processes the cached ubxResponse
      processedUbxTimestamp = 0;
      lastGpsDetails = null;
    }
  }

  return {
    subscribe,
    connect,
    disconnect,
  };
}

// ─── Low-level helpers ───────────────────────────────────────────────────────

interface UbxFrame {
  classId: number;
  msgId: number;
  length: number;
  payloadStart: number;
}

function hexToBytes(hex: string): number[] {
  const bytes: number[] = [];
  for (let i = 0; i + 1 < hex.length; i += 2) {
    bytes.push(parseInt(hex.substring(i, i + 2), 16));
  }
  return bytes;
}

function readU2(bytes: number[], off: number): number {
  return bytes[off] | (bytes[off + 1] << 8);
}

function readU4(bytes: number[], off: number): number {
  return (
    (bytes[off] |
      (bytes[off + 1] << 8) |
      (bytes[off + 2] << 16) |
      (bytes[off + 3] << 24)) >>>
    0
  );
}

function readI4(bytes: number[], off: number): number {
  return readU4(bytes, off) | 0;
}

function bytesToSigned(bytes: number[], off: number): number {
  const v = bytes[off];
  return v >= 0x80 ? v - 0x100 : v;
}

function ubxChecksumOk(bytes: number[], start: number, len: number): boolean {
  let ckA = 0,
    ckB = 0;
  for (let i = 0; i < len; i++) {
    ckA = (ckA + bytes[start + i]) & 0xff;
    ckB = (ckB + ckA) & 0xff;
  }
  return bytes[start + len] === ckA && bytes[start + len + 1] === ckB;
}

/** Nur Frames mit gültiger Prüfsumme. Eine falsche Sync-Folge (B5 62 in den
 *  Nutzdaten) wird nur um ein Byte übersprungen, damit kein echter Frame fehlt. */
function findUbxFrames(bytes: number[]): UbxFrame[] {
  const frames: UbxFrame[] = [];
  for (let i = 0; i + 8 <= bytes.length; i++) {
    if (bytes[i] !== 0xb5 || bytes[i + 1] !== 0x62) continue;
    const length = readU2(bytes, i + 4);
    if (i + 8 + length > bytes.length) continue;
    if (!ubxChecksumOk(bytes, i + 2, length + 4)) continue;
    frames.push({ classId: bytes[i + 2], msgId: bytes[i + 3], length, payloadStart: i + 6 });
    i += 7 + length;
  }
  return frames;
}

export const gpsStore = createGpsStore();
