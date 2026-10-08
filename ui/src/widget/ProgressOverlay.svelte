<script lang="ts">
  import { Loading, ProgressBar } from "carbon-components-svelte";

  // Fortschritt in Prozent; ohne Wert (oder 0) läuft ein Spinner.
  export let percent: number | null = null;
  export let label = "";
  // fixed: über dem ganzen Inhalt unterhalb der Kopfzeile, sonst über dem
  // nächsten positionierten Elternelement (z. B. der Karte).
  export let fixed = false;
</script>

<div class="progress-overlay" class:fixed aria-live="polite">
  <div class="progress-panel">
    {#if percent && percent > 0}
      <ProgressBar value={percent} max={100} helperText={`${label} (${percent}%)`} />
    {:else}
      <Loading small withOverlay={false} description={label} />
      <span>{label}</span>
    {/if}
  </div>
</div>

<style>
  .progress-overlay {
    position: absolute;
    inset: 0;
    z-index: 200;
    display: grid;
    place-items: center;
    background: rgba(255, 255, 255, 0.72);
    backdrop-filter: blur(1px);
  }
  .progress-overlay.fixed {
    position: fixed;
    top: 3rem;
    z-index: 8000;
  }
  .progress-panel {
    width: min(24rem, calc(100% - 2rem));
    padding: 1rem;
    background: #ffffff;
    border: 1px solid #d6d6d6;
    border-radius: 4px;
    box-shadow: 0 4px 16px rgba(0, 0, 0, 0.14);
  }
  .progress-panel :global(.bx--loading--small) {
    margin-right: 0.75rem;
    vertical-align: middle;
  }
</style>
