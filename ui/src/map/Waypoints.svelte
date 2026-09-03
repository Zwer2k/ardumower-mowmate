<script lang="ts">
  import { createEventDispatcher } from "svelte";
  import { pointsToEdges, edgeArrowPath } from "./geometry";
  import type { Waypoints as WaypointsModel, Point as PointType } from "./model";
  import Point from "./Point.svelte";
  import Edge from "./Edge.svelte";
  import type { MapArea } from "./model";

  export let value: WaypointsModel;
  export let waypointsId: string;
  export let edit: boolean = false;
  export let editCategory: MapArea = "waypoints";
  export let editItemId: null | string = null;
  export let drawActive: boolean = false;
  export let onMove: (points: PointType[]) => void = () => {};

  const dispatch = createEventDispatcher();

  function updatePoint(index: number, x: number, y: number) {
    value = {
      ...value,
      points: value.points.map((p, i) => i === index ? { x, y } : p),
    };
    onMove(value.points);
  }

  const arrowSize = 0.06;
  const arrowHw = 0.03;
  const lineWidth = 0.015;
  const pointRadiusMm = 20; // 1.5 mm visible waypoint diameter in edit mode
  const hitExtraMm = 0;     // extra hit area beyond the visible point
  const pointRadius = pointRadiusMm / 1000;
  const hitRadius = (pointRadiusMm + hitExtraMm) / 1000;
  $: isActive = editCategory === "waypoints";
  $: routeSegments = value.points.reduce((segments: PointType[][], point) => {
    if (point.routeBreakBefore || segments.length === 0) segments.push([]);
    segments[segments.length - 1].push(point);
    return segments;
  }, []);
</script>

{#if value.points.length > 1}
  {#each routeSegments as segment}
    {#if segment.length > 1}
      <polyline
        fill="none"
        stroke="blue"
        stroke-width={lineWidth}
        points={segment.map(p => `${p.x},${p.y}`).join(" ")}
      />
      <path d={edgeArrowPath(segment, arrowSize, arrowHw)} stroke="blue" stroke-width={lineWidth} fill="none" stroke-linecap="round" stroke-linejoin="round" />
    {/if}
  {/each}
{/if}

{#if edit && isActive}
  {#each pointsToEdges(value.points, false) as edge, index}
    {@const isConnectorEdge = value.points[index]?.conn || value.points[index + 1]?.conn}
    {#if !isConnectorEdge}
      <Edge
        value={edge}
        mapItemId={waypointsId + "-edge-" + index}
        bind:editItemId
        strokeChoose="blue"
        strokeWidth={0.015}
      />
    {/if}
  {/each}
{/if}

{#each value.points as point, index}
  {#if !point.conn}
    <circle cx={point.x} cy={point.y} r={pointRadius} fill="blue" pointer-events="none" />
  {/if}
{/each}

{#if edit && isActive}
  {#each value.points as point, index}
    {#if !point.conn}
      <Point
        value={point}
        mapItemId={waypointsId + "-point-" + index}
        bind:editItemId
        {drawActive}
        strokeChoose="blue"
        fill="blue"
        fillActive="red"
        fillChoose="blue"
        fillPassive="blue"
        r={pointRadius}
        hitR={hitRadius}
        on:move={(e) => { dispatch('pointmove', { x: e.detail.x, y: e.detail.y }); updatePoint(index, e.detail.x, e.detail.y); }}
      />
    {/if}
  {/each}
{/if}
