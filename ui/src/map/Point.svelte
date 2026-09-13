<script lang="ts">
  import { getContext, createEventDispatcher } from "svelte";
  import { browser } from "$app/environment";
  import { pointer } from "d3-selection";
  import type { Point } from "./model";
  import { recordMapSnapshot } from "./interactions/map-history";

  const dispatch = createEventDispatcher();

  export let value: Point;
  export let mapItemId: string = ""
  export let editItemId: null | string = null;
  export let stroke = "yellow";
  export let strokeChoose = "black";
  export let strokeActive = "black";
  export let strokePassive = "grey";
  export let fill = "transparent";
  export let fillActive = "transparent";
  export let fillPassive = "transparent";
  export let drawActive: boolean = false;
  export let strokeWidth = 0.035;
  export let r = 0.18;
  export let hitR: number | null = null;

  interface DragContext {
    svg: SVGSVGElement;
    contentGroup: SVGGElement;
    setPanEnabled(v: boolean): void;
  }

  const dragContext = getContext<DragContext>("map-drag");

  let hovered = false;
  let isDragging = false;

  const isTouchDevice = browser &&
    (window.matchMedia("(pointer: coarse)").matches ||
      "ontouchstart" in window ||
      navigator.maxTouchPoints > 0);

  // With a mouse the hit area matches the visible point exactly, so
  // neighbouring points stay selectable. A finger is far less precise than the
  // 8 cm circle the map draws, so on coarse pointers the invisible hit area is
  // enlarged. An explicit hitR from the caller always wins.
  const touchHitFactor = 2.5;
  $: effectiveHitR =
    hitR != null && hitR > r ? hitR : isTouchDevice ? r * touchHitFactor : r;

  $: stroke =
    isDragging
      ? "black"
      : drawActive
        ? strokePassive
        : mapItemId === editItemId
          ? strokeActive
          : hovered
            ? "blue"
            : editItemId === null
              ? strokeChoose
              : strokePassive;
  $: fill =
    isDragging
      ? "black"
      : drawActive
        ? fillPassive
        : editItemId === null
          ? fillActive
          : mapItemId === editItemId
            ? fillActive
            : fillPassive;

  function click() {
    editItemId = mapItemId;
  }

  let currentPointerId: number | null = null;

  function startDrag(event: PointerEvent) {
    if (!dragContext) return;

    // Prevent the map's d3-zoom (or the browser's default touch scrolling)
    // from taking over this gesture.
    event.stopPropagation();
    event.preventDefault();

    editItemId = mapItemId;
    // One undo step per drag, not per pointermove.
    recordMapSnapshot();
    isDragging = true;
    currentPointerId = event.pointerId;
    dragContext.setPanEnabled(false);

    const target = event.currentTarget as Element;
    try {
      target.setPointerCapture(event.pointerId);
    } catch {
      // setPointerCapture can fail in some browsers/environments.
    }

    window.addEventListener("pointermove", handleWindowPointerMove, true);
    window.addEventListener("pointerup", handleWindowPointerUp, true);
    window.addEventListener("pointercancel", handleWindowPointerCancel, true);
    window.addEventListener("contextmenu", preventContextMenuDuringDrag, true);
  }

  function handleWindowPointerMove(event: PointerEvent) {
    if (event.pointerId !== currentPointerId || !isDragging || !dragContext) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    const [x, y] = pointer(event, dragContext.contentGroup);
    dispatch("move", { x, y });
  }

  function handleWindowPointerUp(event: PointerEvent) {
    if (event.pointerId !== currentPointerId) return;
    stopDrag();
  }

  function handleWindowPointerCancel(event: PointerEvent) {
    if (event.pointerId !== currentPointerId) return;
    stopDrag();
  }

  function preventContextMenuDuringDrag(e: Event) {
    e.preventDefault();
  }

  function stopDrag() {
    isDragging = false;
    currentPointerId = null;
    dragContext?.setPanEnabled(true);
    window.removeEventListener("pointermove", handleWindowPointerMove, true);
    window.removeEventListener("pointerup", handleWindowPointerUp, true);
    window.removeEventListener("pointercancel", handleWindowPointerCancel, true);
    window.removeEventListener("contextmenu", preventContextMenuDuringDrag, true);
  }

</script>

{#if effectiveHitR > r}
  <!-- Visible marker: does not receive pointer events so the larger hit
       area above it always wins. -->
  <circle
    cx={value.x}
    cy={value.y}
    r={r}
    {stroke}
    {fill}
    stroke-width={strokeWidth}
    pointer-events="none"
    role="none"
  >
    <title>{value.x.toFixed(2)}, {(-value.y).toFixed(2)}</title>
  </circle>

  <!-- Invisible, enlarged hit area on touch devices. It captures the pointer
       and blocks map panning for the whole drag. -->
  <circle
    cx={value.x}
    cy={value.y}
    r={effectiveHitR}
    fill="transparent"
    stroke="transparent"
    pointer-events="all"
    role="none"
    on:click={click}
    on:pointerdown={startDrag}
    on:mouseenter={() => (hovered = true)}
    on:mouseleave={() => (hovered = false)}
    style="cursor: pointer;"
  />
{:else}
  <circle
    cx={value.x}
    cy={value.y}
    r={r}
    {stroke}
    {fill}
    stroke-width={strokeWidth}
    role="none"
    on:click={click}
    on:pointerdown={startDrag}
    on:mouseenter={() => (hovered = true)}
    on:mouseleave={() => (hovered = false)}
    style="cursor: pointer;"
  >
    <title>{value.x.toFixed(2)}, {(-value.y).toFixed(2)}</title>
  </circle>
{/if}
