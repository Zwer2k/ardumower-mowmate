<script lang="ts">
  // Zeitplaner als eigene Seite: links Liste und Editor (ein Scrollbereich),
  // daneben die Karte des gewählten Eintrags mit seinen Bereichen oder seiner
  // Route. Die Vorschau braucht die Karte nicht geladen.
  import MapScheduleToolbar from "../../../map/toolbar/MapScheduleToolbar.svelte";
  import JobPreviewMap from "../../../map/JobPreviewMap.svelte";
  import SchedulePreviewToggle from "../../../map/overlay/SchedulePreviewToggle.svelte";
  import { schedulePreviewStore } from "../../../stores/schedule";
</script>

<div class="schedule-dashboard">
  <aside class="schedule-side">
    <MapScheduleToolbar />
  </aside>
  <div class="schedule-map">
    {#if $schedulePreviewStore}
      <JobPreviewMap
        areaIds={$schedulePreviewStore.areaIds}
        overlapOnce={$schedulePreviewStore.overlapOnce}
        mode={$schedulePreviewStore.mode}
      />
      <SchedulePreviewToggle />
    {:else}
      <p class="hint">Select an entry to see its areas on the map.</p>
    {/if}
  </div>
</div>

<style>
  .schedule-dashboard {
    position: relative;
    width: 100%;
    height: 100%;
    padding-top: 48px;
    box-sizing: border-box;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    /* clip: Der Browser darf die Seite auch beim Fokussieren nicht verschieben */
    overflow: clip;
  }
  /* Handy: Editor über der Karte, höchstens die halbe Höhe, ein Scrollbereich */
  .schedule-side {
    /* Bezug für absolut positionierte Carbon-Eingaben (z. B. das versteckte
       Feld eines Toggle): Sonst lagen sie außerhalb des Scrollbereichs, und
       der Fokus schob beim Umschalten die ganze Seite nach oben. */
    position: relative;
    flex: 0 1 auto;
    max-height: 55%;
    min-height: 0;
    overflow-y: auto;
    overflow-x: hidden;
    overscroll-behavior: contain;
    /* Carbon-Row im Editor hat negative Ränder, die Column gleicht sie aus */
    padding: 0.5rem 1rem 0;
    border-bottom: 1px solid #e0e0e0;
    background: var(--cds-ui-background, #fff);
  }
  .schedule-map {
    position: relative;
    flex: 1 1 auto;
    min-height: 12rem;
    background: var(--cds-ui-01, #f4f4f4);
  }
  .hint {
    position: absolute;
    top: 50%;
    left: 0;
    right: 0;
    text-align: center;
    color: #525252;
  }
  /* PC: Editor links über die volle Höhe, die Karte füllt den Rest */
  @media (min-width: 900px) {
    .schedule-dashboard {
      flex-direction: row;
    }
    .schedule-side {
      flex: 0 0 auto;
      width: min(28rem, 38%);
      max-height: none;
      border-bottom: none;
      border-right: 1px solid #e0e0e0;
    }
  }
</style>
