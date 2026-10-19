<script lang="ts">
  // Zeichnet eine Auswahlfläche per Ziehen: Rechteck (am Bildschirm
  // ausgerichtet, auch bei gedrehter Karte) oder Lasso (Freihand). Solange ein
  // Werkzeug aktiv ist, verschiebt Ziehen nicht die Karte.
  import { getContext, onDestroy } from "svelte";
  import type { Point } from "./model";

  export let mode: "rect" | "lasso" | null = null;
  export let onDone: (points: Point[]) => void = () => {};

  const drag = getContext<{
    svg: SVGSVGElement;
    contentGroup: SVGGElement;
    setPanEnabled(v: boolean): void;
  }>("map-drag");

  let start: { x: number; y: number } | null = null;   // Bildschirm
  let screenPath: { x: number; y: number }[] = [];
  let preview: Point[] = [];

  $: drag?.setPanEnabled(mode === null);
  onDestroy(() => drag?.setPanEnabled(true));

  function toMap(clientX: number, clientY: number): Point {
    const m = drag.contentGroup.getScreenCTM();
    if (!m) return { x: 0, y: 0 };
    const pt = drag.svg.createSVGPoint();
    pt.x = clientX;
    pt.y = clientY;
    const p = pt.matrixTransform(m.inverse());
    return { x: p.x, y: p.y };
  }

  function rectCorners(a: { x: number; y: number }, b: { x: number; y: number }): Point[] {
    return [
      toMap(a.x, a.y),
      toMap(b.x, a.y),
      toMap(b.x, b.y),
      toMap(a.x, b.y),
    ];
  }

  function onPointerDown(e: PointerEvent) {
    if (!mode) return;
    e.stopPropagation();
    (e.currentTarget as Element).setPointerCapture(e.pointerId);
    start = { x: e.clientX, y: e.clientY };
    screenPath = [start];
    preview = [toMap(e.clientX, e.clientY)];
  }

  function onPointerMove(e: PointerEvent) {
    if (!mode || !start) return;
    e.stopPropagation();
    if (mode === "rect") {
      preview = rectCorners(start, { x: e.clientX, y: e.clientY });
      return;
    }
    // Lasso: neuer Punkt erst nach 6 px Bewegung, sonst entstehen sehr viele
    const last = screenPath[screenPath.length - 1];
    if (Math.hypot(e.clientX - last.x, e.clientY - last.y) < 6) return;
    screenPath = [...screenPath, { x: e.clientX, y: e.clientY }];
    preview = [...preview, toMap(e.clientX, e.clientY)];
  }

  function onPointerUp(e: PointerEvent) {
    if (!mode || !start) return;
    e.stopPropagation();
    const points = mode === "rect" ? rectCorners(start, { x: e.clientX, y: e.clientY }) : preview;
    const tooSmall = mode === "rect"
      ? Math.abs(e.clientX - start.x) < 10 || Math.abs(e.clientY - start.y) < 10
      : points.length < 3;
    start = null;
    screenPath = [];
    preview = [];
    if (!tooSmall) onDone(points);
  }
</script>

{#if mode}
  <!-- fängt Ziehen ab, solange ein Werkzeug aktiv ist -->
  <rect
    x="-100000" y="-100000" width="200000" height="200000"
    fill="transparent"
    style="cursor: crosshair; touch-action: none"
    role="presentation"
    on:pointerdown={onPointerDown}
    on:pointermove={onPointerMove}
    on:pointerup={onPointerUp}
    on:pointercancel={onPointerUp}
  />
  {#if preview.length >= 2}
    <polygon
      points={preview.map((p) => `${p.x},${p.y}`).join(" ")}
      fill="rgba(138, 63, 252, 0.12)"
      stroke="#8a3ffc"
      stroke-width="0.04"
      stroke-dasharray="0.2 0.1"
      pointer-events="none"
    />
  {/if}
{/if}
