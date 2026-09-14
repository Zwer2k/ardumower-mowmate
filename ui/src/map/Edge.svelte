<script lang="ts">
  import { getContext } from "svelte";
  import { readable, type Readable } from "svelte/store";
  import type { Edge } from "./model";
  import { activePointerKind } from "./pointer-type";

  interface DragContext {
    pixelsPerUnit?: Readable<number>;
  }
  const dragContext = getContext<DragContext>("map-drag");
  const pixelsPerUnit: Readable<number> = dragContext?.pixelsPerUnit ?? readable(0);

  export let value: Edge;
  export let mapItemId: string = "";
  export let editItemId: null | string = null;

  export let stroke = "yellow";
  export let strokeChoose = "black";
  export let strokeActive = "black";
  export let strokePassive = "grey";
  export let strokeWidth = 0.1;
  export let hitStrokeWidth = Math.max(strokeWidth * 3, 0.05);

  $: stroke = editItemId === null ? strokeChoose : mapItemId === editItemId ? strokeActive : strokePassive

  // Finger-sized hit stroke (screen pixels) while touching; exact otherwise.
  // Same reasoning as in Point.svelte.
  const touchHitPx = 22;
  $: touchHitWidth = $pixelsPerUnit > 0 ? (2 * touchHitPx) / $pixelsPerUnit : hitStrokeWidth * 2.5;
  $: effectiveHitWidth =
    $activePointerKind === "touch" ? Math.max(hitStrokeWidth, touchHitWidth) : hitStrokeWidth;

  function click(event: MouseEvent) {
    event.stopPropagation();
    editItemId = mapItemId
  }
</script>

<line
  x1={value.begin.x}
  y1={value.begin.y}
  x2={value.end.x}
  y2={value.end.y}
  {stroke}
  stroke-width={strokeWidth}
  pointer-events="none"
  role="none"
/>

<line
  x1={value.begin.x}
  y1={value.begin.y}
  x2={value.end.x}
  y2={value.end.y}
  stroke="transparent"
  stroke-width={effectiveHitWidth}
  pointer-events="all"
  role="none"
  data-map-edge-x1={value.begin.x}
  data-map-edge-y1={value.begin.y}
  data-map-edge-x2={value.end.x}
  data-map-edge-y2={value.end.y}
  on:click={(e) => click(e)}
  style="cursor: pointer;"
/>
