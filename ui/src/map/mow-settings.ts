import { writable } from "svelte/store";
import type { MowSettings } from "../model";

const defaultSettings: MowSettings = {
  timestamp: 0,
  pattern: 0,
  width: 0.3,
  angle: 0,
  angleMode: 0,
  angleStep: 30,
  distanceToBorder: 0,
  borderLaps: 0,
  distanceToExclusions: 0,
  exclusionLaps: 0,
  mowBorderCcw: false,
  routeOrder: "",
  exclusionLapsWithArea: false,
  selectionEdgeLaps: false,
  doMowArea: true,
  doMowPerimeter: true,
  doMowBorder: false,
  doMowExclusions: true,
  doMowExclusionBorder: false,
  simplifyEpsilon: 0.02,
  checkTurnRadius: 0.3,
};

/** Schalter "Mow areas" im Main-Dashboard warten auf das Modem; die Mini-Karte
 * zeigt dann "Updating…" neben dem Kompass (siehe MowAreaToggles hintOutside) */
export const mowAreasUpdatingStore = writable(false);

export const mowSettingsStore = writable<MowSettings>(defaultSettings);
