<script lang="ts">
  import { getContext, createEventDispatcher } from "svelte";
  import { readable, type Readable } from "svelte/store";
  import { pointer } from "d3-selection";
  import type { Point } from "./model";
  import { recordMapSnapshot } from "./interactions/map-history";
  import { activePointerKind } from "./pointer-type";

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
    pixelsPerUnit?: Readable<number>;
  }

  const dragContext = getContext<DragContext>("map-drag");
  // Fallback store when the point is rendered outside a Canvas (tests, previews).
  const pixelsPerUnit: Readable<number> = dragContext?.pixelsPerUnit ?? readable(0);

  let hovered = false;

  // Hover highlight only for mouse/pen. Browsers emulate mouseenter after a
  // tap and never emulate mouseleave until the next tap elsewhere, which
  // would leave a stale blue marker on the touched (not selected) point.
  function pointerEnter(event: PointerEvent) {
    if (event.pointerType !== "touch") hovered = true;
  }
  function pointerLeave() {
    hovered = false;
  }
  $: if ($activePointerKind === "touch") hovered = false;
  let isDragging = false;

  // With a mouse or pen the hit area matches the visible point (or the
  // caller's explicit hitR) exactly, so neighbouring points stay selectable.
  // A finger is far less precise, so while the user is touching the screen
  // the invisible hit area is enlarged to a finger-sized target measured in
  // screen pixels. Measuring in pixels (not metres) keeps the target the same
  // size on screen at every zoom level; otherwise zooming in for precise
  // editing would let a touch pick a point several dozen pixels away.
  // The decision follows the pointer actually in use, not the device
  // capabilities: on a laptop with touchscreen a mouse user keeps exact hits.
  const touchHitPx = 22;        // ~44 px touch target diameter
  const touchHitFactor = 2.5;   // fallback when the screen scale is unknown
  $: baseHitR = hitR != null && hitR > r ? hitR : r;
  $: touchHitR = $pixelsPerUnit > 0 ? touchHitPx / $pixelsPerUnit : baseHitR * touchHitFactor;
  $: effectiveHitR = $activePointerKind === "touch" ? Math.max(baseHitR, touchHitR) : baseHitR;

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

  // Set when a touch on this point's hit area was handed over to a nearer
  // point; the browser still fires the click on this element afterwards.
  let suppressClick = false;

  function click() {
    if (suppressClick) {
      suppressClick = false;
      return;
    }
    editItemId = mapItemId;
  }

  let currentPointerId: number | null = null;

  // Enlarged touch hit areas of neighbouring points overlap each other and
  // cover short edges completely (points are drawn above edges). SVG
  // hit-testing then delivers the event to the element drawn last, not to the
  // nearest one. Look at every point and edge hit area under the finger and
  // hand the gesture over to the nearest element:
  //  - a point whose visible circle is touched always wins,
  //  - otherwise the nearest geometry (point centre or edge segment) wins.
  type HitTarget = { el: Element; kind: "point" | "edge" };

  function distToSegment(px: number, py: number, x1: number, y1: number, x2: number, y2: number) {
    const dx = x2 - x1;
    const dy = y2 - y1;
    const len2 = dx * dx + dy * dy;
    let t = len2 > 0 ? ((px - x1) * dx + (py - y1) * dy) / len2 : 0;
    t = Math.max(0, Math.min(1, t));
    return Math.hypot(px - (x1 + t * dx), py - (y1 + t * dy));
  }

  function nearestHitElement(event: PointerEvent): HitTarget | null {
    if (!dragContext?.contentGroup || typeof document.elementsFromPoint !== "function") return null;
    const [px, py] = pointer(event, dragContext.contentGroup);
    let best: HitTarget | null = null;
    let bestDist = Infinity;
    let onVisiblePoint: HitTarget | null = null;
    let onVisibleDist = Infinity;
    for (const el of document.elementsFromPoint(event.clientX, event.clientY)) {
      const ds = (el as HTMLElement).dataset;
      if (!ds) continue;
      let d: number;
      let kind: "point" | "edge";
      if (ds.mapPointCx !== undefined) {
        const cx = Number(ds.mapPointCx);
        const cy = Number(ds.mapPointCy);
        if (!Number.isFinite(cx) || !Number.isFinite(cy)) continue;
        d = Math.hypot(cx - px, cy - py);
        kind = "point";
        const pr = Number(ds.mapPointR);
        if (Number.isFinite(pr) && d <= pr && d < onVisibleDist) {
          onVisibleDist = d;
          onVisiblePoint = { el, kind };
        }
      } else if (ds.mapEdgeX1 !== undefined) {
        const x1 = Number(ds.mapEdgeX1), y1 = Number(ds.mapEdgeY1);
        const x2 = Number(ds.mapEdgeX2), y2 = Number(ds.mapEdgeY2);
        if (![x1, y1, x2, y2].every(Number.isFinite)) continue;
        d = distToSegment(px, py, x1, y1, x2, y2);
        kind = "edge";
      } else {
        continue;
      }
      if (d < bestDist) {
        bestDist = d;
        best = { el, kind };
      }
    }
    return onVisiblePoint ?? best;
  }

  function startDrag(event: PointerEvent) {
    if (!dragContext) return;

    // Prevent the map's d3-zoom (or the browser's default touch scrolling)
    // from taking over this gesture.
    event.stopPropagation();
    event.preventDefault();

    if (event.isTrusted && event.pointerType === "touch") {
      const nearest = nearestHitElement(event);
      if (nearest && nearest.kind === "edge") {
        // Edges select on click; the real click will land on this point and
        // must be ignored.
        suppressClick = true;
        nearest.el.dispatchEvent(new MouseEvent("click", {
          bubbles: true,
          cancelable: true,
          clientX: event.clientX,
          clientY: event.clientY,
        }));
        return;
      }
      if (nearest && nearest.el !== event.currentTarget) {
        suppressClick = true;
        nearest.el.dispatchEvent(new PointerEvent("pointerdown", {
          bubbles: true,
          cancelable: true,
          clientX: event.clientX,
          clientY: event.clientY,
          screenX: event.screenX,
          screenY: event.screenY,
          pointerId: event.pointerId,
          pointerType: event.pointerType,
          isPrimary: event.isPrimary,
          button: event.button,
          buttons: event.buttons,
        }));
        return;
      }
    }

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
    data-map-point-cx={value.x}
    data-map-point-cy={value.y}
    data-map-point-r={r}
    on:click={click}
    on:pointerdown={startDrag}
    on:pointerenter={pointerEnter}
    on:pointerleave={pointerLeave}
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
    data-map-point-cx={value.x}
    data-map-point-cy={value.y}
    data-map-point-r={r}
    on:click={click}
    on:pointerdown={startDrag}
    on:pointerenter={pointerEnter}
    on:pointerleave={pointerLeave}
    style="cursor: pointer;"
  >
    <title>{value.x.toFixed(2)}, {(-value.y).toFixed(2)}</title>
  </circle>
{/if}
