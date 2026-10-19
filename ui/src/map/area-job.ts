// Bereiche (Subkarten) einer Hauptkarte für Mähaufträge
import type { MapMeta } from "../model";

export interface JobTreeEntry {
  map: MapMeta;
  depth: number;
  label: string;
}

/** Hauptkarte, zu der die Karte id gehört */
export function rootMapOf(id: string, maps: MapMeta[]): MapMeta | undefined {
  let m = maps.find((x) => x.id === id);
  for (let depth = 0; m?.parentId && depth < 9; depth++) {
    const parent = maps.find((x) => x.id === m!.parentId);
    if (!parent) break;
    m = parent;
  }
  return m;
}

/** Hauptkarte ("whole map") und ihre Bereiche als Baum, nach Namen sortiert */
export function jobTree(root: MapMeta, maps: MapMeta[]): JobTreeEntry[] {
  const byName = (a: MapMeta, b: MapMeta) =>
    a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" });
  const out: JobTreeEntry[] = [];
  const add = (m: MapMeta, depth: number) => {
    if (depth > 8 || out.some((e) => e.map.id === m.id)) return;
    out.push({ map: m, depth, label: depth === 0 ? `${m.name} (whole map)` : m.name });
    for (const c of maps.filter((x) => x.parentId === m.id).sort(byName)) add(c, depth + 1);
  };
  add(root, 0);
  return out;
}

/** Anzeigename eines Auftrags: die Namen in Auftragsreihenfolge */
export function jobName(ids: string[], maps: MapMeta[]): string {
  return ids.map((id) => maps.find((m) => m.id === id)?.name ?? id).join(", ");
}
