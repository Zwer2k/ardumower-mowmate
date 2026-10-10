export interface UbxCommand {
  id: string;
  name: string;
  category: string;
  description: string;
  hex: string;
}

export const ubxCategories = [
  "Receiver",
  "Satellites",
  "Navigation",
  "Configuration",
  "Raw",
];

// Build a UBX poll message from class/id and optional payload.
// Automatically appends the correct Fletcher checksum.
function ubxPoll(classId: number, msgId: number, payloadHex = ""): string {
  const payload = hexToBytes(payloadHex);
  const len = payload.length;
  const bytes: number[] = [
    0xb5,
    0x62,
    classId,
    msgId,
    len & 0xff,
    (len >> 8) & 0xff,
    ...payload,
  ];
  let ckA = 0,
    ckB = 0;
  for (let i = 2; i < bytes.length; i++) {
    ckA = (ckA + bytes[i]) & 0xff;
    ckB = (ckB + ckA) & 0xff;
  }
  bytes.push(ckA, ckB);
  return bytesToHexString(bytes);
}

function bytesToHexString(bytes: number[]): string {
  return bytes
    .map((b) => b.toString(16).toUpperCase().padStart(2, "0"))
    .join("");
}

// Build a CFG-VALGET poll message with multiple config keys.
// Keys are 32-bit u-blox config keys (e.g. 0x10730001), sent little-endian.
function ubxCfgValGetPoll(keys: number[]): string {
  // version=0, layer=0 (RAM = currently active configuration), position=0.
  // (CFG-VALGET layers: 0=RAM, 1=BBR, 2=Flash, 7=Default)
  let payloadHex = "00000000";
  for (const key of keys) {
    const bytes = [
      key & 0xff,
      (key >> 8) & 0xff,
      (key >> 16) & 0xff,
      (key >> 24) & 0xff,
    ];
    payloadHex += bytes.map((b) => b.toString(16).padStart(2, "0")).join("");
  }
  return ubxPoll(0x06, 0x8b, payloadHex);
}

// Config keys requested by each CFG-VALGET card. A VALGET response echoes the
// key IDs, so the UI uses these lists to route each response to its card.
export const cfgValgetKeys: Record<string, number[]> = {
  "cfg-valget-port1": [0x40520001], // CFG-UART1-BAUDRATE
  "cfg-valget-sbas": [0x10310020], // CFG-SIGNAL-SBAS_ENA
  // RTCM3 input on UART1 (corrections via the mower) and UART2 (radio)
  "cfg-valget-rtcm": [0x10730004, 0x10750004],
  "cfg-valget-uart1-proto": [
    0x10730001, 0x10730002, 0x10730004, 0x10740001, 0x10740002, 0x10740004,
  ],
  "cfg-valget-gnss": [0x1031001f, 0x10310025, 0x10310021, 0x10310022],
  "cfg-valget-rate": [0x30210001, 0x30210002],
};

export const ubxCommands: UbxCommand[] = [
  // Receiver Info
  {
    id: "mon-ver",
    name: "Version & Hardware",
    category: "Receiver",
    description: "Firmware version, hardware version, module name, extensions",
    hex: ubxPoll(0x0a, 0x04),
  },
  {
    id: "mon-hw",
    name: "Hardware Status",
    category: "Receiver",
    description: "Pin status, noise level, antenna status, jamming",
    hex: ubxPoll(0x0a, 0x09),
  },
  {
    id: "mon-rf",
    name: "RF Status",
    category: "Receiver",
    description: "Per-band jamming status, noise level, antenna status",
    hex: ubxPoll(0x0a, 0x38),
  },
  {
    id: "mon-comms",
    name: "Port Traffic",
    category: "Receiver",
    description: "Per-port communication statistics (tx/rx bytes, usage)",
    hex: ubxPoll(0x0a, 0x36),
  },
  // Satellites
  {
    id: "nav-sat",
    name: "Satellite Details",
    category: "Satellites",
    description: "All visible satellites with elevation, azimuth, C/N0, health",
    hex: ubxPoll(0x01, 0x35),
  },
  {
    id: "nav-sig",
    name: "Signal Details",
    category: "Satellites",
    description: "Per-signal details (C/N0, quality, pseudorange residual)",
    hex: ubxPoll(0x01, 0x43),
  },
  {
    id: "nav-status",
    name: "GPS Status",
    category: "Satellites",
    description: "Fix type, fix validity, differential correction status",
    hex: ubxPoll(0x01, 0x03),
  },
  // Navigation
  {
    id: "nav-pvt",
    name: "Position/Velocity/Time",
    category: "Navigation",
    description: "Full PVT solution: position, velocity, time, accuracy",
    hex: ubxPoll(0x01, 0x07),
  },
  {
    id: "nav-dop",
    name: "Dilution of Precision",
    category: "Navigation",
    description: "Geometric, position, time, vertical DOP values",
    hex: ubxPoll(0x01, 0x04),
  },
  // Configuration
  {
    id: "cfg-rate-get",
    name: "Measurement Rate",
    category: "Configuration",
    description: "Current measurement rate and navigation rate",
    hex: ubxPoll(0x06, 0x08),
  },
  {
    id: "cfg-nav5-get",
    name: "Navigation Engine",
    category: "Configuration",
    description: "Navigation engine settings (dynModel, fixMode, etc.)",
    hex: ubxPoll(0x06, 0x24),
  },
  {
    id: "cfg-valget-port1",
    name: "UART1 Config",
    category: "Configuration",
    description: "UART1 protocol and baudrate configuration",
    hex: ubxCfgValGetPoll(cfgValgetKeys["cfg-valget-port1"]),
  },
  {
    id: "cfg-valget-sbas",
    name: "SBAS Settings",
    category: "Configuration",
    description: "SBAS enable/disable and mode",
    hex: ubxCfgValGetPoll(cfgValgetKeys["cfg-valget-sbas"]),
  },
  {
    id: "cfg-valget-rtcm",
    name: "RTCM Input",
    category: "Configuration",
    description: "RTCM protocol enable on UART1",
    hex: ubxCfgValGetPoll(cfgValgetKeys["cfg-valget-rtcm"]),
  },
  {
    id: "cfg-valget-uart1-proto",
    name: "UART1 Protocols",
    category: "Configuration",
    description: "UART1 input/output protocol enables",
    hex: ubxCfgValGetPoll(cfgValgetKeys["cfg-valget-uart1-proto"]),
  },
  {
    id: "cfg-valget-gnss",
    name: "GNSS Signals",
    category: "Configuration",
    description: "Enabled GNSS systems (GPS, GLONASS, Galileo, BeiDou)",
    hex: ubxCfgValGetPoll(cfgValgetKeys["cfg-valget-gnss"]),
  },
  {
    id: "cfg-valget-rate",
    name: "Rate Settings",
    category: "Configuration",
    description: "Measurement and navigation rate",
    hex: ubxCfgValGetPoll(cfgValgetKeys["cfg-valget-rate"]),
  },
  // Raw
  {
    id: "raw-custom",
    name: "Custom Hex Command",
    category: "Raw",
    description: "Enter any UBX hex command manually",
    hex: "",
  },
];

// ─── Generic helpers ────────────────────────────────────────────────────────

function hexToBytes(hex: string): number[] {
  const bytes: number[] = [];
  hex = hex.replace(/\s/g, "");
  for (let i = 0; i + 1 < hex.length; i += 2) {
    bytes.push(parseInt(hex.substring(i, i + 2), 16));
  }
  return bytes;
}

function bytesToAscii(bytes: number[], start: number, len: number): string {
  let s = "";
  for (let i = 0; i < len && start + i < bytes.length; i++) {
    const c = bytes[start + i];
    s += c >= 0x20 && c < 0x7f ? String.fromCharCode(c) : "\0";
  }
  return s;
}

function ubxChecksum(
  bytes: number[],
  start: number,
  len: number,
): { ckA: number; ckB: number } {
  let ckA = 0,
    ckB = 0;
  for (let i = 0; i < len; i++) {
    ckA = (ckA + bytes[start + i]) & 0xff;
    ckB = (ckB + ckA) & 0xff;
  }
  return { ckA, ckB };
}

export function findUbxFrames(hex: string): Array<{
  start: number;
  classId: number;
  msgId: number;
  length: number;
  payloadStart: number;
  payloadEnd: number;
  ckA: number;
  ckB: number;
  ckA_calc: number;
  ckB_calc: number;
  valid: boolean;
  className: string;
  msgName: string;
}> {
  const bytes = hexToBytes(hex);
  const frames = [];
  for (let i = 0; i < bytes.length - 8; i++) {
    if (bytes[i] === 0xb5 && bytes[i + 1] === 0x62) {
      const classId = bytes[i + 2];
      const msgId = bytes[i + 3];
      const length = bytes[i + 4] | (bytes[i + 5] << 8);
      const payloadStart = i + 6;
      const payloadEnd = payloadStart + length;
      const ckPos = payloadEnd;
      // Ensure the complete frame (header + payload + 2-byte checksum) fits in the buffer
      if (ckPos + 1 >= bytes.length) {
        // Not enough data for checksum - skip this potential sync byte
        continue;
      }
      const ckA = bytes[ckPos];
      const ckB = bytes[ckPos + 1];
      const calc = ubxChecksum(bytes, i + 2, length + 4);
      const valid = ckA === calc.ckA && ckB === calc.ckB;
      // Only accept if it's a known class (0x01=NAV, 0x02=RXM, 0x05=ACK, 0x06=CFG, 0x0A=MON)
      // This filters out false positives from random B5 62 sequences in data
      const knownClasses = [0x01, 0x02, 0x05, 0x06, 0x0a, 0x0d, 0x13, 0x21];
      if (!knownClasses.includes(classId)) {
        continue;
      }
      frames.push({
        start: i,
        classId,
        msgId,
        length,
        payloadStart,
        payloadEnd,
        ckA,
        ckB,
        ckA_calc: calc.ckA,
        ckB_calc: calc.ckB,
        valid,
        className: ubxClassName(classId),
        msgName: ubxMessageName(classId, msgId),
      });
      // Advance past this frame to avoid overlapping matches
      i = ckPos + 1;
    }
  }
  return frames;
}

function ubxClassName(c: number): string {
  const names: Record<number, string> = {
    0x01: "NAV",
    0x02: "RXM",
    0x04: "INF",
    0x05: "ACK",
    0x06: "CFG",
    0x0a: "MON",
    0x0d: "TIM",
    0x13: "MGA",
    0x21: "LOG",
    0x27: "SEC",
    0x28: "HNR",
  };
  return names[c] || `0x${c.toString(16).padStart(2, "0")}`;
}

function ubxMessageName(classId: number, msgId: number): string {
  const map: Record<string, string> = {
    // IDs per u-blox interface descriptions (F9P protocol 27+, plus M8 legacy)
    // NAV (0x01)
    "0x1-0x1": "NAV-POSECEF",
    "0x1-0x2": "NAV-POSLLH",
    "0x1-0x3": "NAV-STATUS",
    "0x1-0x4": "NAV-DOP",
    "0x1-0x5": "NAV-ATT",
    "0x1-0x6": "NAV-SOL",
    "0x1-0x7": "NAV-PVT",
    "0x1-0x9": "NAV-ODO",
    "0x1-0x10": "NAV-RESETODO",
    "0x1-0x11": "NAV-VELECEF",
    "0x1-0x12": "NAV-VELNED",
    "0x1-0x13": "NAV-HPPOSECEF",
    "0x1-0x14": "NAV-HPPOSLLH",
    "0x1-0x20": "NAV-TIMEGPS",
    "0x1-0x21": "NAV-TIMEUTC",
    "0x1-0x22": "NAV-CLOCK",
    "0x1-0x23": "NAV-TIMEGLO",
    "0x1-0x24": "NAV-TIMEBDS",
    "0x1-0x25": "NAV-TIMEGAL",
    "0x1-0x26": "NAV-TIMELS",
    "0x1-0x27": "NAV-TIMEQZSS",
    "0x1-0x30": "NAV-SVINFO",
    "0x1-0x31": "NAV-DGPS",
    "0x1-0x32": "NAV-SBAS",
    "0x1-0x34": "NAV-ORB",
    "0x1-0x35": "NAV-SAT",
    "0x1-0x36": "NAV-COV",
    "0x1-0x39": "NAV-GEOFENCE",
    "0x1-0x3b": "NAV-SVIN",
    "0x1-0x3c": "NAV-RELPOSNED",
    "0x1-0x3d": "NAV-EELL",
    "0x1-0x42": "NAV-SLAS",
    "0x1-0x43": "NAV-SIG",
    "0x1-0x60": "NAV-AOPSTATUS",
    "0x1-0x61": "NAV-EOE",
    // RXM (0x02)
    "0x2-0x13": "RXM-SFRBX",
    "0x2-0x14": "RXM-MEASX",
    "0x2-0x15": "RXM-RAWX",
    "0x2-0x32": "RXM-RTCM",
    "0x2-0x33": "RXM-SPARTN",
    "0x2-0x34": "RXM-COR",
    "0x2-0x72": "RXM-PMP",
    // MON (0x0a)
    "0xa-0x2": "MON-IO",
    "0xa-0x4": "MON-VER",
    "0xa-0x6": "MON-MSGPP",
    "0xa-0x7": "MON-RXBUF",
    "0xa-0x8": "MON-TXBUF",
    "0xa-0x9": "MON-HW",
    "0xa-0xb": "MON-HW2",
    "0xa-0x21": "MON-RXR",
    "0xa-0x27": "MON-PATCH",
    "0xa-0x28": "MON-GNSS",
    "0xa-0x2e": "MON-SMGR",
    "0xa-0x31": "MON-SPAN",
    "0xa-0x36": "MON-COMMS",
    "0xa-0x37": "MON-HW3",
    "0xa-0x38": "MON-RF",
    "0xa-0x39": "MON-SYS",
    // CFG (0x06)
    "0x6-0x0": "CFG-PRT",
    "0x6-0x1": "CFG-MSG",
    "0x6-0x2": "CFG-INF",
    "0x6-0x4": "CFG-RST",
    "0x6-0x6": "CFG-DAT",
    "0x6-0x8": "CFG-RATE",
    "0x6-0x9": "CFG-CFG",
    "0x6-0x13": "CFG-ANT",
    "0x6-0x16": "CFG-SBAS",
    "0x6-0x17": "CFG-NMEA",
    "0x6-0x1b": "CFG-USB",
    "0x6-0x1e": "CFG-ODO",
    "0x6-0x23": "CFG-NAVX5",
    "0x6-0x24": "CFG-NAV5",
    "0x6-0x31": "CFG-TP5",
    "0x6-0x34": "CFG-RINV",
    "0x6-0x39": "CFG-ITFM",
    "0x6-0x3b": "CFG-PM2",
    "0x6-0x3e": "CFG-GNSS",
    "0x6-0x47": "CFG-LOGFILTER",
    "0x6-0x69": "CFG-GEOFENCE",
    "0x6-0x70": "CFG-DGNSS",
    "0x6-0x71": "CFG-TMODE3",
    "0x6-0x86": "CFG-PMS",
    "0x6-0x8a": "CFG-VALSET",
    "0x6-0x8b": "CFG-VALGET",
    "0x6-0x8c": "CFG-VALDEL",
    // ACK (0x05)
    "0x5-0x0": "ACK-NAK",
    "0x5-0x1": "ACK-ACK",
  };
  const key = `0x${classId.toString(16)}-0x${msgId.toString(16)}`;
  return map[key] || `ID-0x${msgId.toString(16).padStart(2, "0")}`;
}

export function hexDump(hex: string, startOffset: number = 0): string {
  const bytes = hexToBytes(hex);
  const lines: string[] = [];
  for (let i = 0; i < bytes.length; i += 16) {
    const chunk = bytes.slice(i, i + 16);
    const hexStr = chunk.map((b) => b.toString(16).padStart(2, "0")).join(" ");
    const asciiStr = chunk
      .map((b) => (b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : "."))
      .join("");
    lines.push(
      `${(startOffset + i).toString(16).padStart(4, "0")}  ${hexStr.padEnd(48, " ")}  |${asciiStr}|`,
    );
  }
  return lines.join("\n");
}

export function gnssName(id: number): string {
  const names: Record<number, string> = {
    0: "GPS",
    1: "SBAS",
    2: "Galileo",
    3: "BeiDou",
    4: "IMES",
    5: "QZSS",
    6: "GLONASS",
    7: "NavIC",
  };
  return names[id] || `GNSS-${id}`;
}

// ─── Generic frame parser ───────────────────────────────────────────────────

export function parseUbxGeneric(hex: string): Record<string, string> | null {
  const frames = findUbxFrames(hex);
  if (frames.length === 0) return null;
  const f = frames[0];
  return {
    "UBX Class": `${f.className} (0x${f.classId.toString(16).padStart(2, "0")})`,
    "UBX Message": `${f.msgName} (0x${f.msgId.toString(16).padStart(2, "0")})`,
    "Payload Length": `${f.length} bytes`,
    Checksum: `${f.valid ? "✓ Valid" : "✗ Invalid"} (recv ${f.ckA.toString(16).toUpperCase().padStart(2, "0")} ${f.ckB.toString(16).toUpperCase().padStart(2, "0")} / calc ${f.ckA_calc.toString(16).toUpperCase().padStart(2, "0")} ${f.ckB_calc.toString(16).toUpperCase().padStart(2, "0")})`,
    "Total Frames": `${frames.length}`,
  };
}

// ─── Specific parsers ───────────────────────────────────────────────────────

export function parseUbxMonVer(hex: string): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const frames = findUbxFrames(hex);
  const f = frames.find((x) => x.classId === 0x0a && x.msgId === 0x04);
  if (!f || f.length < 40) return null;

  // swVersion CH[30] @0, hwVersion CH[10] @30, then N x extension CH[30] @40
  const p = f.payloadStart;
  const sw = bytesToAscii(bytes, p, 30).replace(/\0/g, "");
  const hw = bytesToAscii(bytes, p + 30, 10).replace(/\0/g, "");

  const extensions: string[] = [];
  for (let off = p + 40; off + 30 <= f.payloadEnd; off += 30) {
    const ext = bytesToAscii(bytes, off, 30).replace(/\0/g, "");
    if (ext) extensions.push(ext);
  }
  const firstExt = (prefix: string) =>
    extensions.find((e) => e.startsWith(prefix))?.substring(prefix.length);

  return {
    "Software Version": sw,
    "Hardware Version": hw || "—",
    Firmware: firstExt("FWVER=") ?? "—",
    Protocol: firstExt("PROTVER=") ?? "—",
    Module: firstExt("MOD=") ?? "—",
    Extensions: extensions.join(", ") || "none",
  };
}

export function parseUbxMonHw(hex: string): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x0a && x.msgId === 0x09,
  );
  if (!f || f.length < 60) return null;
  const p = f.payloadStart;

  // pinSel/pinBank/pinDir/pinVal X4 @0..@12, noisePerMS U2 @16, agcCnt U2 @18
  // (0..8191), aStatus U1 @20, aPower U1 @21, flags X1 @22, jamInd U1 @45
  const pinSel = readU4(bytes, p);
  const pinBank = readU4(bytes, p + 4);
  const pinDir = readU4(bytes, p + 8);
  const pinVal = readU4(bytes, p + 12);
  const noisePerMS = readU2(bytes, p + 16);
  const agcCnt = readU2(bytes, p + 18);
  const antStatus = bytes[p + 20];
  const antPower = bytes[p + 21];
  const jammingState = (bytes[p + 22] >> 2) & 0x03;
  const jamInd = bytes[p + 45];

  const antStatusNames = ["Init", "Unknown", "OK", "Short", "Open"];
  const antPowerNames = ["Off", "On", "Unknown"];
  const jamNames = ["Unknown", "OK", "Warning", "Critical"];

  return {
    "Noise Level": `${noisePerMS}`,
    AGC: `${agcCnt} / 8191`,
    "Antenna Status": antStatusNames[antStatus] || `Code ${antStatus}`,
    "Antenna Power": antPowerNames[antPower] || `Code ${antPower}`,
    "Jamming State": jamNames[jammingState],
    "Jamming Indicator": `${jamInd} / 255`,
    "Pin Sel": `0x${pinSel.toString(16).toUpperCase()}`,
    "Pin Bank": `0x${pinBank.toString(16).toUpperCase()}`,
    "Pin Dir": `0x${pinDir.toString(16).toUpperCase()}`,
    "Pin Val": `0x${pinVal.toString(16).toUpperCase()}`,
  };
}

export function parseUbxMonRf(hex: string): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x0a && x.msgId === 0x38,
  );
  if (!f || f.length < 4) return null;
  const p = f.payloadStart;
  // version U1 @0, nBlocks U1 @1, reserved U1[2], then 24-byte blocks @4:
  // blockId U1 +0, flags X1 +1 (jammingState b0-1), antStatus U1 +2,
  // antPower U1 +3, postStatus U4 +4, noisePerMS U2 +12, agcCnt U2 +14,
  // jamInd U1 +16, ofsI I1 +17, magI U1 +18, ofsQ I1 +19, magQ U1 +20
  const nBlocks = bytes[p + 1];
  const result: Record<string, string> = {};
  const jamNames = ["Unknown", "OK", "Warning", "Critical"];
  const antStatusNames = ["Init", "Unknown", "OK", "Short", "Open"];
  const antPowerNames = ["Off", "On", "Unknown"];
  for (let i = 0; i < nBlocks; i++) {
    const off = p + 4 + i * 24;
    if (off + 24 > f.payloadEnd) break;
    const blockId = bytes[off];
    const jamState = bytes[off + 1] & 0x03;
    const antStatus = bytes[off + 2];
    const antPower = bytes[off + 3];
    const postStatus = readU4(bytes, off + 4);
    const noisePerMS = readU2(bytes, off + 12);
    const agcCnt = readU2(bytes, off + 14);
    const jamInd = bytes[off + 16];
    const ofsI = bytesToSigned(bytes, off + 17);
    const magI = bytes[off + 18];
    const ofsQ = bytesToSigned(bytes, off + 19);
    const magQ = bytes[off + 20];
    const band = blockId === 0 ? "L1" : blockId === 1 ? "L2/L5" : `${blockId}`;

    result[`Block ${band}`] =
      `Noise ${noisePerMS}, AGC ${agcCnt}/8191, Jam ${jamNames[jamState]} (${jamInd}/255), Ant ${antStatusNames[antStatus] ?? antStatus}, Pwr ${antPowerNames[antPower] ?? antPower}`;
    result[`  ${band} ofsI/magI`] = `${ofsI} / ${magI}`;
    result[`  ${band} ofsQ/magQ`] = `${ofsQ} / ${magQ}`;
    result[`  ${band} postStatus`] = `${postStatus}`;
  }
  result["Block Count"] = `${nBlocks}`;
  return result;
}

export function parseUbxMonComms(hex: string): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x0a && x.msgId === 0x36,
  );
  if (!f || f.length < 4) return null;
  const p = f.payloadStart;

  // version U1 @0, nPorts U1 @1, txErrors X1 @2, reserved U1, protIds U1[4],
  // then 40-byte port blocks @8: portId U2 +0, txBytes U4 +4, txUsage U1 +8,
  // txPeakUsage U1 +9, rxBytes U4 +12, rxUsage U1 +16, rxPeakUsage U1 +17,
  // overrunErrs U2 +18
  const version = bytes[p];
  const nPorts = bytes[p + 1];
  const portSize = 40;

  const portNames: Record<number, string> = {
    0x0000: "I2C",
    0x0100: "UART1",
    0x0201: "UART2",
    0x0300: "USB",
    0x0400: "SPI",
  };

  const result: Record<string, string> = {};
  result["Version"] = `${version}`;
  result["Ports"] = `${nPorts}`;

  for (let i = 0; i < nPorts; i++) {
    const off = p + 8 + i * portSize;
    if (off + portSize > f.payloadEnd) break;
    const portId = readU2(bytes, off);
    const txBytes = readU4(bytes, off + 4);
    const txUsage = bytes[off + 8];
    const txPeak = bytes[off + 9];
    const rxBytes = readU4(bytes, off + 12);
    const rxUsage = bytes[off + 16];
    const rxPeak = bytes[off + 17];
    const overrun = readU2(bytes, off + 18);

    const name =
      portNames[portId] || `Port 0x${portId.toString(16).padStart(4, "0")}`;
    result[`${name} TX`] = `${txBytes} B (${txUsage}% peak ${txPeak}%)`;
    result[`${name} RX`] = `${rxBytes} B (${rxUsage}% peak ${rxPeak}%)`;
    if (overrun > 0) {
      result[`${name} Overrun`] = `${overrun}`;
    }
  }

  return result;
}

export function parseUbxNavSat(
  hex: string,
): Array<Record<string, string | number>> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x01 && x.msgId === 0x35,
  );
  if (!f || f.length < 8) return null;
  const p = f.payloadStart;
  // iTOW U4 @0, version U1 @4, numSvs U1 @5, reserved U1[2], blocks @8
  const numSats = bytes[p + 5];
  const sats: Array<Record<string, string | number>> = [];
  for (let i = 0; i < numSats && p + 8 + i * 12 + 12 <= f.payloadEnd; i++) {
    const off = p + 8 + i * 12;
    const gnssId = bytes[off];
    const svId = bytes[off + 1];
    const cno = bytes[off + 2];
    const elev = bytesToSigned(bytes, off + 3);
    const azim = readI2(bytes, off + 4);
    // prRes: I2, scale 0.1 m
    const prRes = readI2(bytes, off + 6) * 0.1;
    // flags: X4 — qualityInd b0-2, svUsed b3, health b4-5, diffCorr b6,
    // smoothed b7, orbitSource b8-10, ephAvail b11, almAvail b12
    const flags = readU4(bytes, off + 8);
    const qual = flags & 0x07;
    const used = (flags >> 3) & 0x01;
    const health = (flags >> 4) & 0x03;
    const diffCorr = (flags >> 6) & 0x01;
    const smoothed = (flags >> 7) & 0x01;

    sats.push({
      GNSS: gnssName(gnssId),
      SV: svId,
      Elev: `${elev}°`,
      Azim: `${azim}°`,
      "C/N0": `${cno} dB-Hz`,
      Quality: qual,
      Health: health,
      Used: used ? "Yes" : "No",
      "Diff Corr": diffCorr ? "Yes" : "No",
      Smoothed: smoothed ? "Yes" : "No",
      "PR Res": `${prRes.toFixed(1)} m`,
    });
  }
  return sats;
}

export function parseUbxNavSig(
  hex: string,
): Array<Record<string, string | number>> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x01 && x.msgId === 0x43,
  );
  if (!f || f.length < 8) return null;
  const p = f.payloadStart;
  // iTOW U4 @0, version U1 @4, numSigs U1 @5, reserved U1[2], blocks @8
  const numSigs = bytes[p + 5];
  const sigs: Array<Record<string, string | number>> = [];
  for (let i = 0; i < numSigs && p + 8 + i * 16 + 16 <= f.payloadEnd; i++) {
    const off = p + 8 + i * 16;
    const gnssId = bytes[off];
    const svId = bytes[off + 1];
    const sigId = bytes[off + 2];
    const freqId = bytes[off + 3];
    // prRes: I2, scale 0.1 m
    const prRes = readI2(bytes, off + 4) * 0.1;
    const cno = bytes[off + 6];
    const qual = bytes[off + 7];
    const corrSrc = bytes[off + 8];
    const iono = bytes[off + 9];
    // sigFlags: X2 — health b0-1, prSmoothed b2, prUsed b3, crUsed b4,
    // doUsed b5, prCorrUsed b6, crCorrUsed b7, doCorrUsed b8
    const sigFlags = readU2(bytes, off + 10);
    const health = sigFlags & 0x03;
    const prUsed = (sigFlags >> 3) & 0x01;
    const prCorrUsed = (sigFlags >> 6) & 0x01;
    const crCorrUsed = (sigFlags >> 7) & 0x01;

    sigs.push({
      GNSS: gnssName(gnssId),
      SV: svId,
      Signal: sigId,
      Freq: freqId,
      "C/N0": `${cno} dB-Hz`,
      Quality: qual,
      "PR Res": `${prRes.toFixed(1)} m`,
      Used: prUsed ? "Yes" : "No",
      "Diff Corr": prCorrUsed || crCorrUsed ? "Yes" : "No",
      "Corr Src": corrSrc,
      Iono: iono,
      Health: health,
    });
  }
  return sigs;
}

export function parseUbxNavStatus(hex: string): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x01 && x.msgId === 0x03,
  );
  if (!f || f.length < 16) return null;
  const p = f.payloadStart;

  // iTOW U4 @0, gpsFix U1 @4, flags X1 @5, fixStat X1 @6, flags2 X1 @7,
  // ttff U4 @8, msss U4 @12
  const fix = bytes[p + 4];
  const flags = bytes[p + 5];
  const fixStat = bytes[p + 6];
  const flags2 = bytes[p + 7];
  const ttff = readU4(bytes, p + 8);
  const msss = readU4(bytes, p + 12);

  const fixNames: Record<number, string> = {
    0x00: "No fix",
    0x01: "Dead reckoning only",
    0x02: "2D fix",
    0x03: "3D fix",
    0x04: "GNSS + dead reckoning",
    0x05: "Time only fix",
  };
  const carrNames: Record<number, string> = { 0: "None", 1: "Float", 2: "Fixed" };
  const psmNames: Record<number, string> = {
    0: "Acquisition / PSM off",
    1: "Tracking",
    2: "Power optimized tracking",
    3: "Inactive",
  };
  const spoofNames: Record<number, string> = {
    0: "Unknown / deactivated",
    1: "No spoofing indicated",
    2: "Spoofing indicated",
    3: "Multiple spoofing indications",
  };

  return {
    "Fix Type": fixNames[fix] || `Unknown (${fix})`,
    "GPS Fix Valid": flags & 0x01 ? "Yes" : "No",
    "Diff Soln": flags & 0x02 ? "Yes" : "No",
    "Diff Corr Available": fixStat & 0x01 ? "Yes" : "No",
    "Carrier Soln Valid": fixStat & 0x02 ? "Yes" : "No",
    "Week Valid": flags & 0x04 ? "Yes" : "No",
    "TOW Valid": flags & 0x08 ? "Yes" : "No",
    "Carrier Soln": carrNames[(flags2 >> 6) & 0x03] ?? `Code ${(flags2 >> 6) & 0x03}`,
    TTFF: `${ttff} ms`,
    "Time Since Startup": `${msss} ms`,
    "PSM State": psmNames[flags2 & 0x03] ?? `Code ${flags2 & 0x03}`,
    Spoofing: spoofNames[(flags2 >> 3) & 0x03] ?? `Code ${(flags2 >> 3) & 0x03}`,
  };
}

export function parseUbxNavPvt(hex: string): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x01 && x.msgId === 0x07,
  );
  if (!f || f.length < 92) return null;
  const p = f.payloadStart;

  const year = bytes[p + 4] | (bytes[p + 5] << 8);
  const month = bytes[p + 6];
  const day = bytes[p + 7];
  const hour = bytes[p + 8];
  const minute = bytes[p + 9];
  const second = bytes[p + 10];
  const validDate = bytes[p + 11] & 0x01;
  const validTime = bytes[p + 11] & 0x02;
  const fullyResolved = bytes[p + 11] & 0x04;
  const validMag = bytes[p + 11] & 0x08;

  const fixType = bytes[p + 20];
  const fixOk = bytes[p + 21] & 0x01;
  const diffSoln = bytes[p + 21] & 0x02;
  const carrSoln = (bytes[p + 21] >> 6) & 0x03;

  const numSV = bytes[p + 23];
  const lon = readI4(bytes, p + 24) * 1e-7;
  const lat = readI4(bytes, p + 28) * 1e-7;
  const height = readI4(bytes, p + 32) * 1e-3;
  const hMSL = readI4(bytes, p + 36) * 1e-3;
  const hAcc = readU4(bytes, p + 40) * 1e-3;
  const vAcc = readU4(bytes, p + 44) * 1e-3;
  const velN = readI4(bytes, p + 48) * 1e-3;
  const velE = readI4(bytes, p + 52) * 1e-3;
  const velD = readI4(bytes, p + 56) * 1e-3;
  const gSpeed = readI4(bytes, p + 60) * 1e-3;
  const heading = readI4(bytes, p + 64) * 1e-5;
  const sAcc = readU4(bytes, p + 68) * 1e-3;
  const headingAcc = readU4(bytes, p + 72) * 1e-5;
  const pDOP = readU2(bytes, p + 76) * 0.01;

  const fixNames: Record<number, string> = {
    0: "No fix",
    1: "Dead reckoning",
    2: "2D fix",
    3: "3D fix",
    4: "GNSS + DR",
    5: "Time only",
  };

  const carrNames: Record<number, string> = {
    0: "None",
    1: "Float",
    2: "Fixed",
  };

  return {
    "Date/Time": `${year}-${String(month).padStart(2, "0")}-${String(day).padStart(2, "0")} ${String(hour).padStart(2, "0")}:${String(minute).padStart(2, "0")}:${String(second).padStart(2, "0")}`,
    "Date Valid": validDate ? "Yes" : "No",
    "Time Valid": validTime ? "Yes" : "No",
    "Fix Type": fixNames[fixType] || `Unknown (${fixType})`,
    "Fix OK": fixOk ? "Yes" : "No",
    "RTK Status": carrNames[carrSoln] || `Code ${carrSoln}`,
    "Diff Solution": diffSoln ? "Yes" : "No",
    Satellites: `${numSV}`,
    Latitude: `${lat.toFixed(7)}°`,
    Longitude: `${lon.toFixed(7)}°`,
    "Height (MSL)": `${hMSL.toFixed(3)} m`,
    "Height (Ellipsoid)": `${height.toFixed(3)} m`,
    "H-Accuracy": `${hAcc.toFixed(3)} m`,
    "V-Accuracy": `${vAcc.toFixed(3)} m`,
    "Speed (Ground)": `${gSpeed.toFixed(3)} m/s`,
    "Speed (3D)": `${Math.sqrt(velN * velN + velE * velE + velD * velD).toFixed(3)} m/s`,
    Heading: `${heading.toFixed(2)}°`,
    "Speed Accuracy": `${sAcc.toFixed(3)} m/s`,
    "Heading Accuracy": `${headingAcc.toFixed(2)}°`,
    PDOP: `${pDOP.toFixed(2)}`,
  };
}

export function parseUbxNavDop(hex: string): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x01 && x.msgId === 0x04,
  );
  if (!f || f.length < 18) return null;
  const p = f.payloadStart;

  const gDOP = readU2(bytes, p + 0) * 0.01;
  const pDOP = readU2(bytes, p + 2) * 0.01;
  const tDOP = readU2(bytes, p + 4) * 0.01;
  const vDOP = readU2(bytes, p + 6) * 0.01;
  const hDOP = readU2(bytes, p + 8) * 0.01;
  const nDOP = readU2(bytes, p + 10) * 0.01;
  const eDOP = readU2(bytes, p + 12) * 0.01;

  return {
    GDOP: `${gDOP.toFixed(2)}`,
    PDOP: `${pDOP.toFixed(2)}`,
    TDOP: `${tDOP.toFixed(2)}`,
    VDOP: `${vDOP.toFixed(2)}`,
    HDOP: `${hDOP.toFixed(2)}`,
    NDOP: `${nDOP.toFixed(2)}`,
    EDOP: `${eDOP.toFixed(2)}`,
  };
}

export function parseUbxCfgRate(hex: string): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x06 && x.msgId === 0x08,
  );
  if (!f || f.length < 6) return null;
  const p = f.payloadStart;

  const measRate = readU2(bytes, p + 0);
  const navRate = readU2(bytes, p + 2);
  const timeRef = readU2(bytes, p + 4);

  return {
    "Measurement Rate": `${measRate} ms (${(1000 / measRate).toFixed(1)} Hz)`,
    "Navigation Rate": `${navRate} cycles`,
    "Time Reference":
      timeRef === 0 ? "UTC" : timeRef === 1 ? "GPS" : `Code ${timeRef}`,
  };
}

export function parseUbxCfgNav5(hex: string): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x06 && x.msgId === 0x24,
  );
  if (!f || f.length < 36) return null;
  const p = f.payloadStart;

  const dynModel = bytes[p + 2];
  const fixMode = bytes[p + 3];
  const fixedAlt = readI4(bytes, p + 4) * 0.01;
  const fixedAltVar = readU4(bytes, p + 8) * 0.0001;
  const minElev = bytesToSigned(bytes, p + 12);
  const drLimit = bytes[p + 13];
  const pDop = readU2(bytes, p + 14) * 0.1;
  const tDop = readU2(bytes, p + 16) * 0.1;
  const pAcc = readU2(bytes, p + 18);
  const tAcc = readU2(bytes, p + 20);
  const staticHoldThresh = bytes[p + 22];
  const dgnssTimeout = bytes[p + 23];
  const cnoThreshNumSats = bytes[p + 24];
  const cnoThresh = bytes[p + 25];
  const staticHoldMaxDist = readU2(bytes, p + 28);

  const dynNames: Record<number, string> = {
    0: "Portable",
    2: "Stationary",
    3: "Pedestrian",
    4: "Automotive",
    5: "Sea",
    6: "Airborne <1g",
    7: "Airborne <2g",
    8: "Airborne <4g",
    9: "Wrist",
    10: "Bike",
  };

  const fixNames: Record<number, string> = {
    1: "2D only",
    2: "3D only",
    3: "Auto 2D/3D",
  };

  return {
    "Dynamic Model": dynNames[dynModel] || `Code ${dynModel}`,
    "Fix Mode": fixNames[fixMode] || `Code ${fixMode}`,
    "Fixed Altitude": `${fixedAlt.toFixed(2)} m`,
    "Fixed Alt Var": `${fixedAltVar.toFixed(4)} m²`,
    "Min Elevation": `${minElev}°`,
    "DR Limit": `${drLimit} s`,
    "Max PDOP": `${pDop.toFixed(1)}`,
    "Max TDOP": `${tDop.toFixed(1)}`,
    "Pos Accuracy": `${pAcc} m`,
    "Time Accuracy": `${tAcc} m`,
    "Static Hold": `${staticHoldThresh} cm/s`,
    "Static Max Dist": `${staticHoldMaxDist} m`,
    "DGNSS Timeout": `${dgnssTimeout} s`,
    "C/N0 Threshold": `${cnoThreshNumSats} sats @ ${cnoThresh} dB-Hz`,
  };
}

// Byte size of a configuration value, encoded in key bits 28-30.
function cfgValueSize(key: number): number {
  switch ((key >>> 28) & 0x07) {
    case 1: // 1 bit, stored in one byte
    case 2:
      return 1;
    case 3:
      return 2;
    case 4:
      return 4;
    case 5:
      return 8;
    default:
      return 0;
  }
}

// Decode the key/value pairs of the first CFG-VALGET response in `hex`.
export function parseCfgValgetEntries(
  hex: string,
): { version: number; layer: number; entries: Array<{ key: number; value: number | null }> } | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x06 && x.msgId === 0x8b,
  );
  if (!f || f.length < 4) return null;
  const p = f.payloadStart;
  const entries: Array<{ key: number; value: number | null }> = [];
  let off = p + 4;
  while (off + 4 <= f.payloadEnd) {
    const key = readU4(bytes, off);
    off += 4;
    const size = cfgValueSize(key);
    if (size === 0 || off + size > f.payloadEnd) {
      entries.push({ key, value: null });
      break;
    }
    let value: number;
    if (size === 1) value = bytes[off];
    else if (size === 2) value = readU2(bytes, off);
    else if (size === 4) value = readU4(bytes, off);
    else value = readU4(bytes, off) + readU4(bytes, off + 4) * 0x100000000;
    off += size;
    entries.push({ key, value });
  }
  return { version: bytes[p], layer: bytes[p + 1], entries };
}

export function parseUbxCfgValget(hex: string): Record<string, string> | null {
  const parsed = parseCfgValgetEntries(hex);
  if (!parsed) return null;
  const layerNames: Record<number, string> = {
    0: "RAM",
    1: "BBR",
    2: "Flash",
    7: "Default",
  };
  const result: Record<string, string> = {
    Version: `${parsed.version}`,
    Layer: layerNames[parsed.layer] || `Code ${parsed.layer}`,
  };
  for (const { key, value } of parsed.entries) {
    const name =
      ubxConfigKeyName(key) || `0x${key.toString(16).padStart(8, "0")}`;
    result[name] = value === null ? "?" : `${value}`;
  }
  if (parsed.entries.length === 0) result["Config Entries"] = "none";
  return result;
}

// ─── Low-level helpers ────────────────────────────────────────────────────────

function readU2(bytes: number[], off: number): number {
  return bytes[off] | (bytes[off + 1] << 8);
}

function readI2(bytes: number[], off: number): number {
  const v = readU2(bytes, off);
  return v >= 0x8000 ? v - 0x10000 : v;
}

// Unsigned 32-bit little-endian (">>> 0" keeps values >= 2^31 positive).
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

function ubxConfigKeyName(key: number): string | null {
  const map: Record<number, string> = {
    // Key IDs per u-blox ZED-F9P interface description (protocol 27+)
    0x40520001: "CFG-UART1-BAUDRATE",
    0x20520002: "CFG-UART1-STOPBITS",
    0x20520003: "CFG-UART1-DATABITS",
    0x20520004: "CFG-UART1-PARITY",
    0x10520005: "CFG-UART1-ENABLED",
    0x10730001: "CFG-UART1INPROT-UBX",
    0x10730002: "CFG-UART1INPROT-NMEA",
    0x10730004: "CFG-UART1INPROT-RTCM3X",
    0x10740001: "CFG-UART1OUTPROT-UBX",
    0x10740002: "CFG-UART1OUTPROT-NMEA",
    0x10740004: "CFG-UART1OUTPROT-RTCM3X",
    0x10750001: "CFG-UART2INPROT-UBX",
    0x10750002: "CFG-UART2INPROT-NMEA",
    0x10750004: "CFG-UART2INPROT-RTCM3X",
    0x10760001: "CFG-UART2OUTPROT-UBX",
    0x10760002: "CFG-UART2OUTPROT-NMEA",
    0x10760004: "CFG-UART2OUTPROT-RTCM3X",
    0x1031001f: "CFG-SIGNAL-GPS_ENA",
    0x10310020: "CFG-SIGNAL-SBAS_ENA",
    0x10310021: "CFG-SIGNAL-GAL_ENA",
    0x10310022: "CFG-SIGNAL-BDS_ENA",
    0x10310024: "CFG-SIGNAL-QZSS_ENA",
    0x10310025: "CFG-SIGNAL-GLO_ENA",
    0x30210001: "CFG-RATE-MEAS",
    0x30210002: "CFG-RATE-NAV",
    0x20210003: "CFG-RATE-TIMEREF",
  };
  return map[key] || null;
}

export interface UbxFrameResult {
  classId: number;
  msgId: number;
  className: string;
  msgName: string;
  length: number;
  payloadStart: number;
  payloadEnd: number;
  valid: boolean;
  specific:
    | Record<string, string>
    | Array<Record<string, string | number>>
    | null;
}

export function parseUbxFrames(hex: string): UbxFrameResult[] {
  const frames = findUbxFrames(hex);
  return frames.map((f) => {
    const parser = getParserForFrame(f.classId, f.msgId);
    let specific = null;
    if (parser) {
      try {
        // Parse only this frame's bytes, so that several frames of the same
        // type in one buffer each get their own result.
        specific = parser(
          hex.replace(/\s/g, "").substring(f.start * 2, (f.payloadEnd + 2) * 2),
        );
      } catch (e) {
        specific = { "Parse Error": String(e) };
      }
    }
    return {
      classId: f.classId,
      msgId: f.msgId,
      className: f.className,
      msgName: f.msgName,
      length: f.length,
      payloadStart: f.payloadStart,
      payloadEnd: f.payloadEnd,
      valid: f.valid,
      specific,
    };
  });
}

// ─── Parser dispatcher ────────────────────────────────────────────────────────

export function parseUbxNavOdo(hex: string): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x01 && x.msgId === 0x09,
  );
  if (!f || f.length < 20) return null;
  const p = f.payloadStart;
  // version U1 @0, reserved U1[3], iTOW U4 @4, distance U4 @8 (m),
  // totalDistance U4 @12 (m), distanceStd U4 @16 (m)
  const version = bytes[p];
  const iTOW = readU4(bytes, p + 4);
  const distance = readU4(bytes, p + 8);
  const totalDistance = readU4(bytes, p + 12);
  const distanceStd = readU4(bytes, p + 16);
  return {
    "Time of Week": `${iTOW} ms`,
    Version: `${version}`,
    Distance: `${distance} m`,
    "Total Distance": `${totalDistance} m`,
    "Distance StdDev": `${distanceStd} m`,
  };
}

export function parseUbxNavHpposllh(
  hex: string,
): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x01 && x.msgId === 0x14,
  );
  if (!f || f.length < 36) return null;
  const p = f.payloadStart;
  // version U1 @0, reserved U1[2], flags X1 @3, iTOW U4 @4,
  // lon/lat I4 @8/@12 (1e-7 deg), height/hMSL I4 @16/@20 (mm),
  // lonHp/latHp I1 @24/@25 (1e-9 deg), heightHp/hMSLHp I1 @26/@27 (0.1 mm),
  // hAcc/vAcc U4 @28/@32 (0.1 mm)
  const version = bytes[p];
  const invalidLlh = bytes[p + 3] & 0x01;
  const iTOW = readU4(bytes, p + 4);
  const lon = readI4(bytes, p + 8) * 1e-7 + bytesToSigned(bytes, p + 24) * 1e-9;
  const lat = readI4(bytes, p + 12) * 1e-7 + bytesToSigned(bytes, p + 25) * 1e-9;
  const height =
    readI4(bytes, p + 16) * 1e-3 + bytesToSigned(bytes, p + 26) * 1e-4;
  const hMSL =
    readI4(bytes, p + 20) * 1e-3 + bytesToSigned(bytes, p + 27) * 1e-4;
  const hAcc = readU4(bytes, p + 28) * 1e-4;
  const vAcc = readU4(bytes, p + 32) * 1e-4;
  return {
    Version: `${version}`,
    "Time of Week": `${iTOW} ms`,
    "LLH Valid": invalidLlh ? "No" : "Yes",
    Latitude: `${lat.toFixed(9)}°`,
    Longitude: `${lon.toFixed(9)}°`,
    "Height (Ellipsoid)": `${height.toFixed(4)} m`,
    "Height (MSL)": `${hMSL.toFixed(4)} m`,
    "H-Accuracy": `${hAcc.toFixed(4)} m`,
    "V-Accuracy": `${vAcc.toFixed(4)} m`,
  };
}

export function parseUbxNavRelposned(
  hex: string,
): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x01 && x.msgId === 0x3c,
  );
  if (!f || f.length < 64) return null;
  const p = f.payloadStart;
  // NAV-RELPOSNED version 1 (64 bytes, F9P): version U1 @0, refStationId U2 @2,
  // iTOW U4 @4, relPosN/E/D/Length I4 @8..@20 (cm), relPosHeading I4 @24
  // (1e-5 deg), relPosHPN/E/D/Length I1 @32..@35 (0.1 mm),
  // accN/E/D/Length U4 @36..@48 (0.1 mm), accHeading U4 @52 (1e-5 deg),
  // flags X4 @60
  const version = bytes[p];
  const refStationId = readU2(bytes, p + 2);
  const iTOW = readU4(bytes, p + 4);
  const relPosN = readI4(bytes, p + 8) + bytesToSigned(bytes, p + 32) * 0.01;
  const relPosE = readI4(bytes, p + 12) + bytesToSigned(bytes, p + 33) * 0.01;
  const relPosD = readI4(bytes, p + 16) + bytesToSigned(bytes, p + 34) * 0.01;
  const relPosLength =
    readI4(bytes, p + 20) + bytesToSigned(bytes, p + 35) * 0.01;
  const relPosHeading = readI4(bytes, p + 24) * 1e-5;
  const accN = readU4(bytes, p + 36) * 0.01;
  const accE = readU4(bytes, p + 40) * 0.01;
  const accD = readU4(bytes, p + 44) * 0.01;
  const accLength = readU4(bytes, p + 48) * 0.01;
  const accHeading = readU4(bytes, p + 52) * 1e-5;
  const flags = readU4(bytes, p + 60);
  const gnssFixOk = flags & 0x01;
  const diffSoln = (flags >> 1) & 0x01;
  const relPosValid = (flags >> 2) & 0x01;
  const carrSoln = (flags >> 3) & 0x03;
  const isMoving = (flags >> 5) & 0x01;
  const headingValid = (flags >> 8) & 0x01;

  const carrNames: Record<number, string> = {
    0: "None",
    1: "Float",
    2: "Fixed",
  };

  return {
    "Time of Week": `${iTOW} ms`,
    Version: `${version}`,
    "Ref Station ID": `${refStationId}`,
    "Rel Pos N": `${relPosN.toFixed(2)} cm`,
    "Rel Pos E": `${relPosE.toFixed(2)} cm`,
    "Rel Pos D": `${relPosD.toFixed(2)} cm`,
    "Baseline Length": `${relPosLength.toFixed(2)} cm`,
    Heading: headingValid ? `${relPosHeading.toFixed(4)}°` : "invalid",
    "Accuracy N": `${accN.toFixed(2)} cm`,
    "Accuracy E": `${accE.toFixed(2)} cm`,
    "Accuracy D": `${accD.toFixed(2)} cm`,
    "Accuracy Length": `${accLength.toFixed(2)} cm`,
    "Accuracy Heading": `${accHeading.toFixed(4)}°`,
    "Fix OK": gnssFixOk ? "Yes" : "No",
    "Diff Soln": diffSoln ? "Yes" : "No",
    "Rel Pos Valid": relPosValid ? "Yes" : "No",
    "Carr Soln": carrNames[carrSoln] || `Code ${carrSoln}`,
    Moving: isMoving ? "Yes" : "No",
  };
}

export function parseUbxNavDgps(hex: string): Record<string, string> | null {
  const bytes = hexToBytes(hex);
  const f = findUbxFrames(hex).find(
    (x) => x.classId === 0x01 && x.msgId === 0x31,
  );
  if (!f || f.length < 16) return null;
  const p = f.payloadStart;
  const iTOW = readU4(bytes, p);
  const age = readI4(bytes, p + 4);
  const baseId = readI2(bytes, p + 8);
  const baseHealth = readI2(bytes, p + 10);
  const numCh = bytes[p + 12];
  const status = bytes[p + 13];
  const statusNames: Record<number, string> = {
    0: "None",
    1: "PR+PRRC correction",
    2: "PR+PRRC+CP correction",
  };
  return {
    "Time of Week": `${iTOW} ms`,
    Age: `${age} ms`,
    "Base Station ID": `${baseId}`,
    "Base Health": `${baseHealth}`,
    Channels: `${numCh}`,
    Status: statusNames[status] || `Code ${status}`,
  };
}

export function getParserForFrame(
  classId: number,
  msgId: number,
):
  | ((
      hex: string,
    ) => Record<string, string> | Array<Record<string, string | number>> | null)
  | null {
  const key = `${classId}-${msgId}`;
  switch (key) {
    case "10-4":
      return parseUbxMonVer;
    case "10-9":
      return parseUbxMonHw;
    case "10-56":
      return parseUbxMonRf;
    case "10-54":
      return parseUbxMonComms;
    case "1-9": // NAV-ODO (0x09)
      return parseUbxNavOdo;
    case "1-20": // NAV-HPPOSLLH (0x14)
      return parseUbxNavHpposllh;
    case "1-49": // NAV-DGPS (0x31, u-blox 8 only; not on F9P)
      return parseUbxNavDgps;
    case "1-60":
      return parseUbxNavRelposned;
    case "1-53":
      return parseUbxNavSat;
    case "1-67":
      return parseUbxNavSig;
    case "1-3":
      return parseUbxNavStatus;
    case "1-7":
      return parseUbxNavPvt;
    case "1-4":
      return parseUbxNavDop;
    case "6-8":
      return parseUbxCfgRate;
    case "6-36":
      return parseUbxCfgNav5;
    case "6-139":
      return parseUbxCfgValget;
    default:
      return null;
  }
}

export function getParser(commandId: string) {
  switch (commandId) {
    case "mon-ver":
      return parseUbxMonVer;
    case "mon-hw":
      return parseUbxMonHw;
    case "mon-rf":
      return parseUbxMonRf;
    case "nav-sat":
      return parseUbxNavSat;
    case "nav-sig":
      return parseUbxNavSig;
    case "nav-status":
      return parseUbxNavStatus;
    case "nav-pvt":
      return parseUbxNavPvt;
    case "nav-dop":
      return parseUbxNavDop;
    case "cfg-rate-get":
      return parseUbxCfgRate;
    case "cfg-nav5-get":
      return parseUbxCfgNav5;
    case "cfg-valget-port1":
    case "cfg-valget-uart1-proto":
    case "cfg-valget-gnss":
    case "cfg-valget-sbas":
    case "cfg-valget-rtcm":
    case "cfg-valget-rate":
      return parseUbxCfgValget;
    default:
      return null;
  }
}
