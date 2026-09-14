<script lang="ts">
  import type { Point } from "../model";

  /** Waypoints a route-check finding points at. Drawn on top of everything
   *  else and independent of the edit mode, so a finding can be located
   *  without entering the editor first. */
  export let points: Point[] = [];
</script>

{#each points as p}
  <g pointer-events="none">
    <circle
      cx={p.x}
      cy={p.y}
      r="0.12"
      fill="none"
      stroke="#da1e28"
      stroke-width="0.05"
    >
      <animate
        attributeName="r"
        values="0.12;0.45;0.12"
        dur="1.4s"
        repeatCount="indefinite"
      />
      <animate
        attributeName="opacity"
        values="1;0.15;1"
        dur="1.4s"
        repeatCount="indefinite"
      />
    </circle>
    <circle
      cx={p.x}
      cy={p.y}
      r="0.06"
      fill="#da1e28"
      stroke="#ffffff"
      stroke-width="0.02"
    />
  </g>
{/each}
