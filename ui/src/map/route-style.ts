import { writable } from "svelte/store";

// Darstellung der Mähbahnen im Karteneditor (Schalter "Tracks: cut width"):
// "width": zusätzlich Bänder in der Schnittbreite aus den Einstellungen
// (mower.cut_width) unter der Route, "cassandra": nur die Route.
export type RouteStyle = "width" | "cassandra";

const KEY = "mowmate_route_style";

function load(): RouteStyle {
  try {
    const v = localStorage.getItem(KEY);
    return v === "cassandra" ? "cassandra" : "width";
  } catch {
    return "width";
  }
}

export const routeStyleStore = writable<RouteStyle>(typeof window === "undefined" ? "width" : load());

routeStyleStore.subscribe((v) => {
  if (typeof window === "undefined") return;
  try {
    localStorage.setItem(KEY, v);
  } catch {
    // ohne Speicher gilt die Wahl nur bis zum Neuladen
  }
});
