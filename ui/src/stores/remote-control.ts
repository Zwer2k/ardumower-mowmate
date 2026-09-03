// Remote-Control-Dialog-Status: teilt mit, ob der RC-Dialog offen ist.
// Wenn offen, soll das Gamepad nur die Fahrsteuerung bedienen, nicht den Map-Editor.
import { writable } from 'svelte/store';

export const remoteControlOpen = writable(false);
