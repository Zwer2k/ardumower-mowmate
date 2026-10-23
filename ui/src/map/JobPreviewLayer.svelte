<script lang="ts">
  // Vorschau eines Mähauftrags auf der Karte (Zeitplaner): die Flächen der
  // Bereiche in Farbe mit ihrer Nummer, oder die Route vom Modem.
  import { onDestroy } from "svelte";
  import { socketStore, socketService, areaExportStore } from "../stores/socket";
  import { mowJobStore, jobPartProgress, JOB_COLORS } from "../stores/mow-job";
  import { rootMapOf } from "./area-job";

  export let areaIds: string[] = [];
  export let overlapOnce = false;
  export let mode: "areas" | "route" = "areas";
  /** Perimeter der geladenen Karte (UI-Koordinaten), für "ganze Karte" */
  export let perimeter: { x: number; y: number }[] = [];
  /** Auch ohne Bereiche beim Modem anfragen (es liefert die Geometrie mit) */
  export let requestAlways = false;
  /** Nur diese Bereiche zeichnen (Farbe und Nummer bleiben die ihres Platzes im Auftrag) */
  export let onlyIds: string[] | null = null;
  /** Bereits bekannte Auswahlflächen (UI-Koordinaten), z. B. der geladenen Karte;
   *  leer = ganze Karte. Sie gehen den Daten vom Modem vor. */
  export let knownSelections: Record<string, { x: number; y: number }[]> = {};

  type P = { x: number; y: number };

  $: maps = $socketStore.maps ?? [];
  $: root = areaIds.length > 0 ? rootMapOf(areaIds[0], maps) : undefined;

  // Fläche eines Bereichs; null = noch nicht bekannt (dann nicht zeichnen,
  // früher stand dort die ganze Karte in Bereichsfarbe mit der Nummer in der Mitte).
  // Alles, wovon die Flächen abhängen, als Argument: Svelte verfolgt in $:
  // nur direkt genannte Werte.
  function selectionOf(id: string, exported: typeof $areaExportStore, outer: P[], main: typeof root,
                       known: Record<string, P[]>): P[] | null {
    if (main && id === main.id) return outer;
    const k = known[id];
    if (k) return k.length >= 3 ? k : outer;
    if (!exported || exported.mapId !== main?.id) return null;
    const area = exported.areas.find((a) => a.id === id);
    if (!area) return null;
    const sel = (area.map?.selection ?? []) as { X: number; Y: number }[];
    if (sel.length < 3) return outer; // leere Auswahl = ganze Karte
    return sel.map((p) => ({ x: p.X, y: -p.Y }));
  }

  $: parts = areaIds.map((id, i) => ({
    id,
    index: i + 1,
    color: JOB_COLORS[i % JOB_COLORS.length],
    poly: selectionOf(id, $areaExportStore, perimeter, root, knownSelections),
  })).filter((p) => !onlyIds || onlyIds.includes(p.id));

  // Auswahlflächen vom Modem holen. Die Antwort kann verloren gehen (während
  // einer Kartenübertragung hält das Modem andere Nachrichten zurück), daher
  // fehlende Flächen alle 3 s erneut anfragen, höchstens 5 Mal je Auftrag.
  const RETRY_MS = 3000;
  const MAX_REQUESTS = 5;
  $: missing = !!root && (parts.some((p) => p.poly === null) ||
    (requestAlways && $areaExportStore?.mapId !== root.id));
  $: requestKey = root ? `${root.id}|${areaIds.join(",")}` : "";
  let requestedKey = "";
  let requestedAt = 0;
  let requestCount = 0;
  let tick = 0;
  const retryTimer = setInterval(() => (tick += 1), RETRY_MS);
  onDestroy(() => clearInterval(retryTimer));
  $: requestIfMissing(missing, requestKey, tick);
  function requestIfMissing(isMissing: boolean, key: string, _tick: number) {
    if (!isMissing || !root) return;
    if (key !== requestedKey) {
      requestedKey = key;
      requestCount = 0;
    } else if (Date.now() - requestedAt < RETRY_MS || requestCount >= MAX_REQUESTS) {
      return;
    }
    requestedAt = Date.now();
    requestCount += 1;
    socketService.requestAreas(root.id);
  }

  // Route vom Modem, wenn sie genau zu diesem Auftrag gehört
  $: routeMatches = !!$mowJobStore && $mowJobStore.complete && $mowJobStore.toggles === 7 &&
    $mowJobStore.overlapOnce === overlapOnce &&
    $mowJobStore.parts.length === areaIds.length && $mowJobStore.parts.every((p, i) => p.id === areaIds[i]);
  $: route = mode === "route" && routeMatches && $mowJobStore
    ? jobPartProgress($mowJobStore.points, $mowJobStore.parts, -1, false) : [];

  $: bounds = (() => {
    const pts = perimeter.length ? perimeter : parts.flatMap((p) => p.poly ?? []);
    if (pts.length === 0) return 10;
    const xs = pts.map((p) => p.x), ys = pts.map((p) => p.y);
    return Math.max(Math.max(...xs) - Math.min(...xs), Math.max(...ys) - Math.min(...ys));
  })();
  $: labelSize = Math.max(0.3, bounds / 30);

  const pts = (poly: P[]) => poly.map((p) => `${p.x},${p.y}`).join(" ");
  function centroid(poly: P[]): P {
    const n = poly.length || 1;
    return { x: poly.reduce((s, p) => s + p.x, 0) / n, y: poly.reduce((s, p) => s + p.y, 0) / n };
  }
</script>

<g class="job-preview" pointer-events="none">
  {#if mode === "areas"}
    {#each parts as part}
      {#if part.poly && part.poly.length >= 3}
        <polygon points={pts(part.poly)} fill={part.color} fill-opacity="0.3"
          stroke={part.color} stroke-width="2" stroke-dasharray="6 4" vector-effect="non-scaling-stroke" />
      {/if}
    {/each}
    {#each parts as part}
      {#if part.poly && part.poly.length >= 3}
        {@const c = centroid(part.poly)}
        <circle cx={c.x} cy={c.y} r={labelSize * 0.8} fill={part.color} />
        <text x={c.x} y={c.y} font-size={labelSize} text-anchor="middle" dominant-baseline="central"
          fill="white" font-weight="600">{part.index}</text>
      {/if}
    {/each}
  {:else}
    {#each route as part}
      {#each part.todo as seg}
        <polyline points={pts(seg)} fill="none" stroke={part.color} stroke-width="2" stroke-opacity="0.9"
          vector-effect="non-scaling-stroke" stroke-linejoin="round" stroke-linecap="round" />
      {/each}
    {/each}
  {/if}
</g>
