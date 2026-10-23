<script lang="ts">
  // Zeitplaner-Vorschau oben links auf der Karte: zwischen den nummerierten
  // Flächen und der Route vom Modem umschalten (als Textlink).
  import { onDestroy } from "svelte";
  import { schedulePreviewStore } from "../../stores/schedule";
  import { socketService } from "../../stores/socket";
  import { mowJobStore } from "../../stores/mow-job";

  $: preview = $schedulePreviewStore;
  // Route vorhanden, wenn der Auftrag des Modems genau zu diesem Eintrag passt
  // (Zeitplaner-Aufträge laufen mit allen Mow areas)
  $: routeReady = !!preview && !!$mowJobStore && $mowJobStore.complete && $mowJobStore.toggles === 7 &&
    $mowJobStore.overlapOnce === preview.overlapOnce &&
    $mowJobStore.parts.length === preview.areaIds.length &&
    $mowJobStore.parts.every((p, i) => p.id === preview!.areaIds[i]);

  // Welcher Auftrag zuletzt angefragt wurde und wann
  $: key = preview ? JSON.stringify([preview.areaIds, preview.overlapOnce]) : "";
  let requestedKey = "";
  let requestedAt = 0;
  let now = Date.now();
  const clock = setInterval(() => (now = Date.now()), 1000);
  let debounce: ReturnType<typeof setTimeout> | null = null;
  onDestroy(() => {
    clearInterval(clock);
    if (debounce) clearTimeout(debounce);
  });

  function request() {
    if (!preview || preview.areaIds.length === 0) return;
    requestedKey = key;
    requestedAt = Date.now();
    now = requestedAt;
    socketService.sendPreviewJob(preview.areaIds, preview.overlapOnce);
  }

  // Im Modus Route: ändern sich Bereiche, Reihenfolge oder die Überlappungs-
  // Option, die passende Route neu anfordern (kurz verzögert, damit schnelles
  // Anklicken nicht viele Anfragen auslöst). Vorher blieb die Anzeige leer.
  $: if (preview?.mode === "route" && !routeReady && key !== requestedKey) {
    if (debounce) clearTimeout(debounce);
    const wanted = key;
    debounce = setTimeout(() => {
      debounce = null;
      if (wanted === key && !routeReady && $schedulePreviewStore?.mode === "route") request();
    }, 400);
  }

  $: waiting = preview?.mode === "route" && !routeReady &&
    (key !== requestedKey || now - requestedAt < 90000);
  $: failed = preview?.mode === "route" && !routeReady && !waiting;

  function toggle() {
    if (!preview) return;
    schedulePreviewStore.set({ ...preview, mode: preview.mode === "route" ? "areas" : "route" });
  }
</script>

{#if preview}
  <div class="schedule-preview-toggle">
    {#if waiting}
      <span class="waiting">Putting the route together…</span>
      <button type="button" class="link" on:click={toggle}>Show areas</button>
    {:else if failed}
      <span class="waiting">No route from the modem.</span>
      <button type="button" class="link" on:click={request}>Try again</button>
      <button type="button" class="link" on:click={toggle}>Show areas</button>
    {:else}
      <button type="button" class="link" on:click={toggle}>
        {preview.mode === "route" ? "Show areas" : "Show route"}
      </button>
    {/if}
  </div>
{/if}

<style>
  .schedule-preview-toggle {
    position: absolute;
    top: 0.5rem;
    left: 0.5rem;
    z-index: 5;
    display: flex;
    gap: 0.75rem;
    padding: 0.125rem 0.5rem;
    background: rgba(255, 255, 255, 0.85);
    border-radius: 2px;
    font-size: 0.875rem;
  }
  .link {
    background: none;
    border: none;
    padding: 0;
    color: var(--cds-link-01, #0f62fe);
    cursor: pointer;
    font: inherit;
    text-decoration: underline;
  }
  .waiting {
    color: #525252;
  }
</style>
