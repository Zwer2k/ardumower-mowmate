import { writable } from "svelte/store";
import type { MowSettings } from "../model";

const defaultSettings: MowSettings = {
  timestamp: 0,
  pattern: 0,
  width: 0.3,
  angle: 0,
  distanceToBorder: 0,
  borderLaps: 0,
  mowBorderCcw: false,
  doMowArea: true,
  doMowPerimeter: true,
  doMowBorder: false,
  doMowExclusions: true,
  doMowExclusionBorder: false,
};

export const mowSettingsStore = writable<MowSettings>(defaultSettings);
