import { writable } from "svelte/store";

// Main-Dashboard: Flächen der Areas mit zufälligem/dynamischem Winkel farbig
// mit ihrer Nummer markieren (Schalter in "Choose areas"). Gilt je Browser.
const KEY = "mowmate_area_marks";

function load(): boolean {
  try {
    return localStorage.getItem(KEY) !== "off";
  } catch {
    return true;
  }
}

export const areaMarksStore = writable<boolean>(typeof window === "undefined" ? true : load());

areaMarksStore.subscribe((v) => {
  if (typeof window === "undefined") return;
  try {
    localStorage.setItem(KEY, v ? "on" : "off");
  } catch {
    // ohne Speicher gilt die Wahl nur bis zum Neuladen
  }
});
