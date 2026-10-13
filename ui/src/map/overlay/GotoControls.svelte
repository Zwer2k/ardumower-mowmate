<script lang="ts">
  import { gotoStore } from "../interactions/map-goto";

  /** Hinweis nach dem ersten Klick auf die Karte */
  export let hint = false;
  /** false blendet alles aus (z. B. im Bearbeitungsmodus) */
  export let visible = true;
</script>

{#if visible && hint}
  <div class="goto-floater goto-hint" class:above-goto={$gotoStore.targetSet}>Tap again to set a go-to target</div>
{/if}
{#if visible && $gotoStore.targetSet}
  <div class="goto-floater">
    <span class="goto-badge">{$gotoStore.targetDist.toFixed(1)}m / {$gotoStore.targetBearing.toFixed(0)}°</span>
    {#if $gotoStore.isDriving}
      <button class="goto-btn stop" on:click={() => gotoStore.stopDrive()}>Stop</button>
    {:else}
      <button class="goto-btn drive" on:click={() => gotoStore.startDrive()}>Drive</button>
    {/if}
    <button class="goto-btn clear" on:click={() => gotoStore.clearTarget()}>✕</button>
  </div>
{/if}

<style>
  .goto-floater {
    position: absolute;
    bottom: 1rem;
    left: 50%;
    transform: translateX(-50%);
    display: flex;
    align-items: center;
    gap: 0.25rem;
    background: rgba(255, 255, 255, 0.9);
    padding: 0.5rem;
    border-radius: 4px;
    border: 1px solid #e0e0e0;
    z-index: 10;
    white-space: nowrap;
  }
  .goto-hint {
    font-size: 0.75em;
    color: #525252;
    pointer-events: none;
  }
  .goto-hint.above-goto {
    bottom: 4.5rem;
  }
  .goto-badge {
    font-size: 0.75em;
    padding: 3px 8px;
    background: #fff3e0;
    border: 1px solid #ffab00;
    border-radius: 4px;
    color: #b06000;
    font-family: monospace;
    margin-right: 4px;
  }
  .goto-btn {
    padding: 4px 10px;
    border: 1px solid #ccc;
    border-radius: 4px;
    font-size: 0.75em;
    cursor: pointer;
    background: #f4f4f4;
  }
  .goto-btn.drive {
    background: #e8f5e9;
    border-color: #4caf50;
    color: #2e7d32;
  }
  .goto-btn.stop {
    background: #ffebee;
    border-color: #ef5350;
    color: #c62828;
  }
  .goto-btn.clear {
    padding: 4px 8px;
  }
</style>
