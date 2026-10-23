<script lang="ts" context="module">
  let instanceCounter = 0;
</script>

<script lang="ts">
  import { Toggle } from "carbon-components-svelte";
  import { mowSettingsStore, mowAreasUpdatingStore } from "./mow-settings";
  import { socketService, mowSettingsPendingStore, pendingOperationStore } from "../stores/socket";
  import { mowJobStore, JOB_TOGGLE_AREA, JOB_TOGGLE_BORDER, JOB_TOGGLE_EXCLUSION_BORDER } from "../stores/mow-job";
  import { routeStyleStore } from "./route-style";

  // showRouteStyle: Schalter für die Bahndarstellung (nur im Karteneditor)
  // jobMode: Die Schalter gehören zum angezeigten Mähauftrag (Main-Dashboard),
  // nicht zur geladenen Karte. Ein Umschalten setzt den Auftrag neu zusammen.
  // hintOutside: "Updating…" nicht hier anzeigen (die Kachel änderte sonst ihre
  // Höhe), sondern über mowAreasUpdatingStore an anderer Stelle
  // keepSaved: Umschalten macht die geladene Karte nicht "ungespeichert" (Main-Dashboard)
  let { horizontal = false, showTitle = true, labelPrefix = "", showRouteStyle = false, jobMode = false,
        hintOutside = false, keepSaved = false } = $props();

  const instanceId = `mow-area-toggles-${++instanceCounter}`;
  const areaId = `${instanceId}-area`;
  const borderId = `${instanceId}-border`;
  const exclBorderId = `${instanceId}-excl-border`;
  const routeStyleId = `${instanceId}-route-style`;

  // Bahnen in Schnittbreite zeichnen (siehe route-style.ts)
  let cutWidthTracks = $state($routeStyleStore === "width");
  $effect(() => {
    routeStyleStore.set(cutWidthTracks ? "width" : "cassandra");
  });

  let doMowArea = $state($mowSettingsStore.doMowArea ?? true);
  let doMowBorder = $state($mowSettingsStore.doMowBorder ?? false);
  let doMowExclusionBorder = $state($mowSettingsStore.doMowExclusionBorder ?? false);

  // Track previous store values to only update local state when they actually change
  let prevDoMowArea = $mowSettingsStore.doMowArea ?? true;
  let prevDoMowBorder = $mowSettingsStore.doMowBorder ?? false;
  let prevDoMowExclusionBorder = $mowSettingsStore.doMowExclusionBorder ?? false;

  // Auftragsmodus: Werte aus dem Auftrag; beim Verlassen wieder die der Karte
  let prevJobToggles = -1;
  let wasJobMode = false;
  $effect(() => {
    const job = $mowJobStore;
    if (jobMode && job) {
      if (job.toggles !== prevJobToggles || !wasJobMode) {
        prevJobToggles = job.toggles;
        doMowArea = (job.toggles & JOB_TOGGLE_AREA) !== 0;
        doMowBorder = (job.toggles & JOB_TOGGLE_BORDER) !== 0;
        doMowExclusionBorder = (job.toggles & JOB_TOGGLE_EXCLUSION_BORDER) !== 0;
      }
      wasJobMode = true;
    } else if (wasJobMode) {
      wasJobMode = false;
      prevJobToggles = -1;
      const s = $mowSettingsStore;
      doMowArea = s.doMowArea ?? true;
      doMowBorder = s.doMowBorder ?? false;
      doMowExclusionBorder = s.doMowExclusionBorder ?? false;
    }
  });

  let jobPending = $derived(jobMode && $pendingOperationStore?.op === "previewJob");
  let locked = $derived(jobMode ? jobPending : $mowSettingsPendingStore);
  $effect(() => {
    if (!hintOutside) return;
    mowAreasUpdatingStore.set(locked);
    return () => mowAreasUpdatingStore.set(false);
  });

  $effect(() => {
    if (jobMode) return;
    const s = $mowSettingsStore;
    const newArea = s.doMowArea ?? true;
    const newBorder = s.doMowBorder ?? false;
    const newExcl = s.doMowExclusionBorder ?? false;
    if (newArea !== prevDoMowArea) {
      prevDoMowArea = newArea;
      doMowArea = newArea;
    }
    if (newBorder !== prevDoMowBorder) {
      prevDoMowBorder = newBorder;
      doMowBorder = newBorder;
    }
    if (newExcl !== prevDoMowExclusionBorder) {
      prevDoMowExclusionBorder = newExcl;
      doMowExclusionBorder = newExcl;
    }
  });

  // Send user-initiated changes to backend (local state differs from store-tracked value)

  function sendToggle(updates: Partial<{
    doMowArea: boolean;
    doMowBorder: boolean;
    doMowExclusionBorder: boolean;
  }>) {
    mowSettingsStore.update((settings) => ({ ...settings, ...updates }));
    socketService.sendMowSettings(updates, keepSaved);
  }

  function onChange(e: Event) {
    const target = e.target as HTMLInputElement;
    if (!target || target.type !== 'checkbox') return;
    const checked = target.checked;
    const job = $mowJobStore;
    if (jobMode && job) {
      const bit = target.id === areaId ? JOB_TOGGLE_AREA
        : target.id === borderId ? JOB_TOGGLE_BORDER
        : target.id === exclBorderId ? JOB_TOGGLE_EXCLUSION_BORDER : 0;
      if (!bit) return;
      const toggles = checked ? (job.toggles | bit) : (job.toggles & ~bit);
      // Nur filtern: dynamische Areas behalten ihre Route und ihren Winkel
      socketService.sendPreviewJob(job.parts.map((p) => p.id), job.overlapOnce, toggles, true);
      return;
    }
    if (target.id === areaId) {
      sendToggle({ doMowArea: checked });
    } else if (target.id === borderId) {
      sendToggle({ doMowBorder: checked });
    } else if (target.id === exclBorderId) {
      sendToggle({ doMowExclusionBorder: checked });
    }
  }

  function changeListener(node: HTMLElement) {
    node.addEventListener('change', onChange);
    return {
      destroy() {
        node.removeEventListener('change', onChange);
      }
    };
  }
</script>

<div class="mow-area-toggles" class:horizontal role="group" aria-label="Mow area toggles" use:changeListener on:wheel|stopPropagation>
  {#if showTitle}
    <span class="toggles-title">Mow areas</span>
  {/if}
  <!-- Gesperrt, bis das Modem die Änderung bestätigt: es filtert die Route neu
       und schickt Punktzahl, CRC und Verbindungswege, das kann 1-2 s dauern. -->
  <Toggle
    id={areaId}
    labelText="{labelPrefix}Area"
    size="sm"
    disabled={locked}
    bind:toggled={doMowArea}
  />
  <Toggle
    id={borderId}
    labelText="{labelPrefix}Border"
    size="sm"
    disabled={locked}
    bind:toggled={doMowBorder}
  />
  <Toggle
    id={exclBorderId}
    labelText="{labelPrefix}Excl. border"
    size="sm"
    disabled={locked}
    bind:toggled={doMowExclusionBorder}
  />
  {#if locked && !hintOutside}
    <span class="pending-hint" aria-live="polite">Updating…</span>
  {/if}
  <!-- Bahnen zusätzlich in Schnittbreite zeichnen -->
  {#if showRouteStyle}
  <span class="route-style-toggle" title="Draw the tracks as wide as the mower cuts (Settings → Mower → Cut width) to show overlaps and gaps">
    <Toggle
      id={routeStyleId}
      labelText="Tracks: cut width"
      size="sm"
      bind:toggled={cutWidthTracks}
    />
  </span>
  {/if}
</div>

<style>
  .pending-hint {
    font-size: 0.75rem;
    color: #525252;
  }
  .route-style-toggle {
    border-top: 1px solid #e0e0e0;
    padding-top: 0.25rem;
  }
  .mow-area-toggles {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    background: rgba(255, 255, 255, 0.9);
    border: 1px solid #e0e0e0;
    border-radius: 4px;
    padding: 0.5rem;
    font-size: 0.75rem;
  }
  .mow-area-toggles.horizontal {
    flex-direction: row;
    align-items: center;
    gap: 1rem;
  }
  .mow-area-toggles.horizontal :global(.bx--toggle__text--off),
  .mow-area-toggles.horizontal :global(.bx--toggle__text--on) {
    display: none;
  }
  .toggles-title {
    font-weight: 600;
    color: #525252;
    margin-bottom: 0.25rem;
  }
  .mow-area-toggles :global(.bx--toggle-input-small + .bx--toggle-input__label > .bx--toggle__switch) {
    margin-top: 0.25rem;
  }
  .mow-area-toggles :global(.bx--form-item) {
    margin-bottom: 0 !important;
  }
</style>
