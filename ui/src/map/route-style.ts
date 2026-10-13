import { writable } from "svelte/store";

// VORÜBERGEHEND: Vergleich zweier Darstellungen der Mähbahnen auf Main-Dashboard
// und Live-Karte. "width": offene Bahnen so breit wie die Schnittbreite, gemähte
// dünn. "cassandra": wie CaSSAndRA, alle Linien 2 px, offen grün, gemäht blass.
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
