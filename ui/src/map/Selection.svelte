<script lang="ts">
  // Auswahlfläche einer Subkarte (Bereich): violett gestrichelt; im
  // Bearbeitungsmodus "Edit selection" lassen sich die Ecken verschieben.
  import { createEventDispatcher } from "svelte";
  import Edge from "./Edge.svelte";
  import { pointsForPolygon, pointsToEdges } from "./geometry";
  import type { Area, Point as PointType } from "./model";
  import Point from "./Point.svelte";
  import type { MapArea } from "./model";

  export let value: Area;
  export let selectionId: string;
  export let edit: boolean = false;
  export let editCategory: MapArea = "selection";
  export let editItemId: null | string = null;
  export let drawActive: boolean = false;
  export let onMove: (points: PointType[]) => void = () => {};

  const dispatch = createEventDispatcher();
  const lineWidth = 0.04;
  $: isActive = editCategory === "selection";

  function updatePoint(index: number, x: number, y: number) {
    value = {
      ...value,
      points: value.points.map((p, i) => (i === index ? { ...p, x, y } : p)),
    };
    onMove(value.points);
  }
</script>

{#if value.points.length >= 3}
  <polygon
    fill="rgba(138, 63, 252, 0.08)"
    stroke="#8a3ffc"
    stroke-width={lineWidth}
    stroke-dasharray="0.2 0.1"
    pointer-events="none"
    points={pointsForPolygon(value.points)}
  />
{/if}

{#if edit && isActive}
  {#each pointsToEdges(value.points) as edge, index}
    <Edge value={edge} mapItemId={selectionId + "-edge-" + index} bind:editItemId strokeWidth={0.05} />
  {/each}
  {#each value.points as point, index}
    <Point
      value={point}
      mapItemId={selectionId + "-point-" + index}
      bind:editItemId
      {drawActive}
      r={0.08}
      on:move={(e) => { dispatch("pointmove", { x: e.detail.x, y: e.detail.y }); updatePoint(index, e.detail.x, e.detail.y); }}
    />
  {/each}
{/if}
