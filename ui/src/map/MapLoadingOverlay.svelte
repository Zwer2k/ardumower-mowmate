<script lang="ts">
  // Fortschritt beim Übertragen der Karte vom Modem in den Browser. Wird über
  // die Kartenfläche gelegt (Editor und Dashboard).
  import { socketStore } from "../stores/socket";
  import { mapChunkProgress } from "./map-chunk-buffer";
  import ProgressOverlay from "../widget/ProgressOverlay.svelte";
  import { operationProgress } from "../stores/progress";
</script>

<!-- Gehört die Übertragung zu einer Berechnung, zeigt sie der Balken der Operation -->
{#if ($socketStore.isLoadingMap || $mapChunkProgress) && !$operationProgress?.includesTransfer}
  {#if $mapChunkProgress && $mapChunkProgress.total > 0}
    <ProgressOverlay
      percent={Math.min(100, Math.round(($mapChunkProgress.received / $mapChunkProgress.total) * 100))}
      label={$mapChunkProgress.label}
    />
  {:else}
    <ProgressOverlay label="Loading map..." />
  {/if}
{/if}
