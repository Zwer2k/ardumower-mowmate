import { writable } from "svelte/store";

/** Offen-Zustand des Firmware-Dialogs. Er wird aus dem Overflow-Menü und aus
 *  dem Update-Symbol im Header geöffnet, gerendert wird er nur einmal. */
export const firmwareDialogOpen = writable(false);
