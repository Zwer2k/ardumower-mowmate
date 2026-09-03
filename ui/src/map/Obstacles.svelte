<script lang="ts">
  import type { ObstaclesData } from "../model";

  export let obstacles: ObstaclesData | null = null;

  // SVG-Koordinaten: y wird invertiert (Map-Koordinaten → SVG)
  function toSvgPath(points: { x: number; y: number }[]): string {
    if (!points || points.length === 0) return "";
    const parts = points.map((p, i) => {
      const cmd = i === 0 ? "M" : "L";
      return `${cmd}${p.x},${-p.y}`;
    });
    return parts.join(" ") + " Z";
  }
</script>

{#if obstacles && obstacles.polygons.length > 0}
  <g class="obstacles-layer">
    {#each obstacles.polygons as poly (poly.crc)}
      <path
        d={toSvgPath(poly.points)}
        fill="rgba(255, 102, 0, 0.35)"
        stroke="#FF6600"
        stroke-width="0.06"
        stroke-linejoin="round"
      />
    {/each}
  </g>
{/if}

<style>
  .obstacles-layer {
    pointer-events: none;
  }
</style>
