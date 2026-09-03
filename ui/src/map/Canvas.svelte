<script lang="ts">
  import { onMount, setContext } from "svelte";
  import { zoom } from "d3-zoom";
  import { select, pointer } from "d3-selection";

  import { MapStore } from "./service";
  import { createEventDispatcher } from "svelte";

  const dispatch = createEventDispatcher();

  export let compassRotation = 0;

  $: vb = $MapStore.presentation.viewBox.split(' ').map(Number);
  $: vx = vb[0] || 0;
  $: vy = vb[1] || 0;
  $: vw = vb[2] || 1;
  $: vh = vb[3] || 1;
  $: cx = vx + vw / 2;
  $: cy = vy + vh / 2;

  $: transformStr = `rotate(${compassRotation}, ${cx}, ${cy})`;

  interface DragContext {
    svg: SVGSVGElement;
    contentGroup: SVGGElement;
    setPanEnabled(v: boolean): void;
  }

  let svg: SVGSVGElement;
  let g: SVGGElement;
  let contentGroup: SVGGElement;
  let panEnabled = true;

  let pointerDown: { id: number; x: number; y: number; time: number } | null = null;

  setContext<DragContext>("map-drag", {
    get svg() { return svg; },
    get contentGroup() { return contentGroup; },
    setPanEnabled(v: boolean) { panEnabled = v; }
  });

  onMount(() => {
    if (svg && g) {
      const zoomBehaviour = zoom()
        .filter(() => panEnabled)
        .on("zoom", ({ transform }) => {
          const { k, x, y } = transform;
          select(g).attr("transform", `translate(${x}, ${y}) scale(${k})`);
        });
      select(svg).call(zoomBehaviour);
    }
  });

  function emitMapClick(clientX: number, clientY: number) {
    if (!contentGroup || !svg) return;
    const [x, y] = pointer({ clientX, clientY } as PointerEvent, contentGroup);
    dispatch('mapclick', { x, y });
  }

  function handlePointerDown(event: PointerEvent) {
    if (!contentGroup || !svg) return;
    pointerDown = {
      id: event.pointerId,
      x: event.clientX,
      y: event.clientY,
      time: performance.now(),
    };
  }

  function handlePointerUp(event: PointerEvent) {
    if (!pointerDown || event.pointerId !== pointerDown.id) return;
    const dx = event.clientX - pointerDown.x;
    const dy = event.clientY - pointerDown.y;
    const dt = performance.now() - pointerDown.time;
    pointerDown = null;
    if (dt < 300 && Math.sqrt(dx * dx + dy * dy) < 8) {
      emitMapClick(event.clientX, event.clientY);
    }
  }

  function handlePointerMove(event: PointerEvent) {
    if (!contentGroup || !svg) return;
    const [x, y] = pointer(event, contentGroup);
    dispatch('mousemove', { x, y });
  }
</script>

<main>
  <!-- svelte-ignore a11y_click_events_have_key_events a11y_no_static_element_interactions -->
  <svg
    bind:this={svg}
    viewBox={$MapStore.presentation.viewBox}
    preserveAspectRatio="xMidYMid meet"
    width="100%"
    height="100%"
    onpointerdown={handlePointerDown}
    onpointerup={handlePointerUp}
    onpointermove={handlePointerMove}
  >
    <g bind:this={g}>
      <g bind:this={contentGroup} transform={transformStr}>
        <slot />
      </g>
    </g>
  </svg>
</main>

<style>
  main {
    width: 100%;
    height: 100%;
    display: block;
  }
  svg {
    width: 100%;
    height: 100%;
    display: block;
    touch-action: none;
  }
</style>
