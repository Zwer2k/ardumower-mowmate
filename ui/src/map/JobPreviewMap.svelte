<script lang="ts">
  // Zeitplaner-Vorschau für einen Eintrag, dessen Karte nicht geladen ist:
  // eigene Karte über der ganzen Kartenfläche, mit der Geometrie vom Modem.
  import JobPreviewLayer from "./JobPreviewLayer.svelte";
  import { areaExportStore } from "../stores/socket";
  import { mowJobStore, parseOutline, type MapOutline } from "../stores/mow-job";
  import { socketStore } from "../stores/socket";
  import { rootMapOf } from "./area-job";

  export let areaIds: string[] = [];
  export let overlapOnce = false;
  export let mode: "areas" | "route" = "areas";

  $: root = areaIds.length > 0 ? rootMapOf(areaIds[0], $socketStore.maps ?? []) : undefined;
  $: fromAreas = $areaExportStore && $areaExportStore.mapId === root?.id ? parseOutline($areaExportStore.geometry) : undefined;
  // Im Modus Route bringt der Auftrag seine Geometrie selbst mit
  $: outline = (mode === "route" ? $mowJobStore?.geometry : undefined) ?? fromAreas;

  $: viewBox = (() => {
    const o: MapOutline | undefined = outline;
    if (!o || o.perimeter.length === 0) return "0 0 1 1";
    const xs = o.perimeter.map((p) => p.x), ys = o.perimeter.map((p) => p.y);
    const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys);
    const pad = Math.max(maxX - minX, maxY - minY) * 0.04 + 0.5;
    return `${minX - pad} ${minY - pad} ${maxX - minX + 2 * pad} ${maxY - minY + 2 * pad}`;
  })();
  $: vb = viewBox.split(" ").map(Number);
  $: rotate = outline ? `rotate(${outline.rotation}, ${vb[0] + vb[2] / 2}, ${vb[1] + vb[3] / 2})` : "";
  const pts = (poly: { x: number; y: number }[]) => poly.map((p) => `${p.x},${p.y}`).join(" ");
</script>

<div class="job-preview-map">
  <svg viewBox={viewBox} preserveAspectRatio="xMidYMid meet">
    <g transform={rotate}>
      {#if outline}
        <polygon points={pts(outline.perimeter)} fill="rgba(36,161,72,0.08)" stroke="#24a148" stroke-width="1.5"
          vector-effect="non-scaling-stroke" />
        {#each outline.exclusions as ex}
          <polygon points={pts(ex)} fill="rgba(218,30,40,0.15)" stroke="#da1e28" stroke-width="1" vector-effect="non-scaling-stroke" />
        {/each}
        {#if outline.dockpoints.length > 1}
          <polyline points={pts(outline.dockpoints)} fill="none" stroke="orange" stroke-width="2" vector-effect="non-scaling-stroke" />
        {/if}
      {/if}
      <JobPreviewLayer {areaIds} {overlapOnce} {mode} perimeter={outline?.perimeter ?? []} requestAlways />
    </g>
  </svg>
  {#if !outline}
    <p class="loading">Loading {root?.name ?? "the map"}…</p>
  {/if}
</div>

<style>
  .job-preview-map {
    position: absolute;
    inset: 0;
    z-index: 2;
    background: var(--cds-ui-01, #f4f4f4);
  }
  svg {
    width: 100%;
    height: 100%;
  }
  .loading {
    position: absolute;
    top: 50%;
    left: 0;
    right: 0;
    text-align: center;
    color: #525252;
  }
</style>
