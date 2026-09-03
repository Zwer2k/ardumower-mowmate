// Zentraler Gamepad-Mode: bestimmt, ob das Gamepad den Mower fährt oder die Map editiert.
// - 'drive': normale Fahrsteuerung (Mower bewegen)
// - 'map-edit': Map-Editor (Punkt bewegen/platzieren)
// Wird von Map.svelte gesetzt (wenn Edit-Modus aktiv) und von GamepadControl gelesen.
import { writable, type Writable } from 'svelte/store';

export type GamepadMode = 'drive' | 'map-edit';

export const gamepadMode: Writable<GamepadMode> = writable('drive');
