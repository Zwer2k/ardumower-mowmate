<script lang="ts">
  // Bereiche für den nächsten Mähauftrag wählen. Das Modem setzt den Auftrag
  // zusammen; die Karte zeigt ihn als Vorschau, hochgeladen und gestartet
  // wird er mit Upload und Start.
  import { Button, Modal, Toggle } from "carbon-components-svelte";
  import { areaMarksStore } from "../../../map/area-marks";
  import { socketStore, socketService } from "../../../stores/socket";
  import { mowJobStore, isJobPreview, mowNowSelectionStore } from "../../../stores/mow-job";
  import AreaJobPicker from "../../../map/AreaJobPicker.svelte";
  import { portal } from "../../../widget/portal";
  import { mapWorkflowStore } from "../../../map/map-workflow";

  export let open = false;

  $: maps = $socketStore.maps ?? [];
  $: currentMapId = $socketStore.currentMapId || "";
  $: previewActive = isJobPreview($mowJobStore, $socketStore.state);

  let selected: string[] = [];
  let overlapOnce = false;

  // Beim Öffnen zuerst die Vorauswahl einlesen, danach jede Änderung merken,
  // damit sie beim nächsten Öffnen wieder da ist
  let initialised = false;
  $: if (open && !initialised) {
    initialised = true;
    handleOpen();
  }
  $: if (!open) initialised = false;
  $: if (open && initialised) mowNowSelectionStore.set({ selected, overlapOnce });

  function handleOpen() {
    // Vorauswahl: die Bereiche des angezeigten Auftrags (Vorschau oder auf dem
    // Mäher), sonst die zuletzt gewählten, sonst die geladene Karte. Gezeigt
    // wird dabei die Karte dieser Bereiche, nicht zwingend die geladene.
    const exists = (id: string) => maps.some((m) => m.id === id);
    const job = $mowJobStore;
    if (job && job.complete && job.parts.length > 0) {
      const fromJob = job.parts.map((p) => p.id).filter(exists);
      if (fromJob.length > 0) {
        selected = fromJob;
        overlapOnce = job.overlapOnce;
        return;
      }
    }
    const remembered = $mowNowSelectionStore;
    if (remembered) {
      const kept = remembered.selected.filter(exists);
      if (kept.length > 0) {
        selected = kept;
        overlapOnce = remembered.overlapOnce;
        return;
      }
    }
    const current = maps.find((m) => m.id === currentMapId);
    selected = current && !current.outdated && !current.id.startsWith("__t_") ? [current.id] : [];
  }

  $: unsavedSelected = selected.some((id) => maps.find((m) => m.id === id)?.unsaved);
  $: dynamicSelected = selected.some((id) => maps.find((m) => m.id === id)?.dynamicAngle);

  function handleShow() {
    if (selected.length === 0) return;
    if (selected.length === 1) {
      // Nur eine Karte (oder ein Bereich): kein Auftrag, sondern die Karte
      // ganz normal laden. Dann gelten die normalen Mow-areas-Schalter mit
      // ihrem Zwischenspeicher, und Upload/Start laufen wie bei jeder Karte.
      // Ungespeicherte Änderungen der bisherigen Karte behält das Modem als Entwurf.
      const id = selected[0];
      if ($mowJobStore) socketService.sendClearJob();
      if (id !== currentMapId) {
        mapWorkflowStore.startLoadMap(id);
        socketService.sendLoadMap(id, false, true);
      } else if (maps.find((m) => m.id === id)?.dynamicAngle) {
        // Schon geladen: Route mit neuem Winkel
        socketService.sendCalculateWaypoints();
      }
      open = false;
      return;
    }
    // Die Mow-areas-Schalter des bisherigen Auftrags bleiben
    socketService.sendPreviewJob(selected, overlapOnce, $mowJobStore?.toggles ?? 7);
    open = false;
  }

  function handleShowLoadedMap() {
    socketService.sendClearJob();
    open = false;
  }
</script>

<div use:portal>
<Modal
  id="choose-areas-modal"
  bind:open
  modalHeading="Choose areas"
  size="sm"
  hasScrollingContent={true}
  primaryButtonText={selected.length === 1 ? "Load map" : "Show on map"}
  primaryButtonDisabled={selected.length === 0}
  secondaryButtonText="Cancel"
  on:click:button--primary={handleShow}
  on:click:button--secondary={() => (open = false)}
  on:close={() => (open = false)}
>
  <AreaJobPicker {maps} mapId={currentMapId} bind:selected bind:overlapOnce />
  {#if selected.length > 0}
    <p class="hint">
      {selected.length === 1
        ? "A single map or area is loaded as the current map; Upload and Start work as usual."
        : "The map shows the job first, every area in its own colour. Upload sends it to the mower, Start mows it."}
      The saved versions are used{unsavedSelected ? ", unsaved changes are not included" : ""}. A job larger than
      the mower's memory is split into parts; after each part the mower charges and the next part starts.
      {#if dynamicSelected}
        Areas with a random or dynamic angle have no saved route: it is calculated now with a new angle,
        Recalculate gives another one, and Upload sends the route shown.
      {/if}
    </p>
  {/if}
  {#if previewActive}
    <Button kind="ghost" size="small" on:click={handleShowLoadedMap}>Show the loaded map again</Button>
  {/if}
  <div class="area-marks">
    <Toggle
      size="sm"
      labelText="Mark areas with a random or dynamic angle on the map"
      labelA="Off"
      labelB="On"
      bind:toggled={$areaMarksStore}
    />
  </div>
</Modal>
</div>

<style>
  .area-marks {
    margin-top: 1rem;
  }
  .hint {
    font-size: 0.75rem;
    color: var(--cds-text-02, #525252);
    margin-top: 0.5rem;
  }
  /* Höchstens so hoch wie der sichtbare Bildschirm (--app-height, siehe
     +layout.svelte): Der Inhalt scrollt, die Knöpfe bleiben sichtbar. Ohne das
     ragte der Dialog unten aus dem Bild, mit Verlauf über dem unteren Teil. */
  :global(#choose-areas-modal.bx--modal) {
    height: var(--app-height, 100dvh) !important;
  }
  :global(#choose-areas-modal .bx--modal-container) {
    display: flex !important;
    flex-direction: column !important;
    max-height: calc(var(--app-height, 100dvh) - 2rem) !important;
  }
  :global(#choose-areas-modal .bx--modal-content) {
    flex: 1 1 auto !important;
    min-height: 0 !important;
    overflow-y: auto !important;
    margin-bottom: 0 !important;
  }
  :global(#choose-areas-modal .bx--modal-content--overflow-indicator) {
    display: none !important;
  }
</style>
