// Gamepad-Service: Globaler Zugriff auf Gamepad-Status und -Eingaben
// Wird von GamepadControl (Status-Popover) befüllt und von Map.svelte konsumiert.
import { writable, type Writable } from 'svelte/store';

export interface GamepadState {
  connected: boolean;
  index: number | null;
  /** linear speed from sticks (-1..1), transformed for driving */
  linear: number;
  /** angular speed from sticks (-1..1), transformed for driving */
  angular: number;
  /** Raw stick X axis (-1..1), not transformed – for map editing */
  rawX: number;
  /** Raw stick Y axis (-1..1), not transformed – for map editing */
  rawY: number;
  /** true while PS button is held (pause) */
  paused: boolean;
  /** Timestamp of last button press (for edge detection) */
  lastButtonPress: Record<number, number>;
}

const initial: GamepadState = {
  connected: false,
  index: null,
  linear: 0,
  angular: 0,
  rawX: 0,
  rawY: 0,
  paused: false,
  lastButtonPress: {},
};

export const gamepadStore: Writable<GamepadState> = writable(initial);

/** Button indices (standard mapping) */
export const GamepadButton = {
  A: 0,        // Cross on DS4
  B: 1,        // Circle
  X: 2,        // Square
  Y: 3,        // Triangle
  LB: 4,
  RB: 5,
  LT: 6,
  RT: 7,
  SELECT: 8,   // Share/Back
  START: 9,    // Options/Start
  L3: 10,      // Left stick click
  R3: 11,      // Right stick click
  DPAD_UP: 12,
  DPAD_DOWN: 13,
  DPAD_LEFT: 14,
  DPAD_RIGHT: 15,
  PS: 16,      // PS / Xbox logo
} as const;

export type GamepadButtonIndex = (typeof GamepadButton)[keyof typeof GamepadButton];
