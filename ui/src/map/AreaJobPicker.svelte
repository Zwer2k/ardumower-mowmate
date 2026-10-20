<script lang="ts">
  // Auswahl der Bereiche eines Mähauftrags: Baum der Hauptkarte zum Ankreuzen,
  // Reihenfolge der gewählten Bereiche und Überlappungs-Option. Für "Mow now"
  // und den Zeitplaner.
  import { createEventDispatcher } from "svelte";
  import { Button, Checkbox, Toggle } from "carbon-components-svelte";
  import PortalDropdown from "../widget/PortalDropdown.svelte";
  import type { MapMeta } from "../model";
  import { jobTree, rootMapOf } from "./area-job";

  export let maps: MapMeta[] = [];
  /** Eine Karte der Hauptkarte, deren Bereiche angeboten werden */
  export let mapId = "";
  export let selected: string[] = [];
  export let overlapOnce = false;
  /** Ohne Auswahl die Karte von mapId zeigen; aus: erst nach Wahl einer Karte */
  export let preselectMap = true;

  const dispatch = createEventDispatcher<{ change: { selected: string[]; overlapOnce: boolean } }>();

  // Alle gespeicherten Hauptkarten, auch ohne Bereiche; ein Auftrag gehört zu einer
  $: mainMaps = maps
    .filter((m) => !m.parentId && !m.id.startsWith("__t_"))
    .sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" }));
  // Nur beim ersten Anzeigen (bzw. wenn die Karte verschwindet) aus mapId
  // ableiten: Danach wählt der Nutzer, auch wenn die Auswahl leer wird.
  let rootId = "";
  $: if (!mainMaps.some((m) => m.id === rootId) && (preselectMap || selected.length > 0 || rootId !== "")) {
    // Eine vorhandene Auswahl bestimmt die Karte, sonst mapId
    const fromMap = rootMapOf(selected[0] ?? mapId, maps)?.id ?? "";
    rootId = mainMaps.some((m) => m.id === fromMap) ? fromMap : (preselectMap ? (mainMaps[0]?.id ?? "") : "");
  }
  // Kommt eine Auswahl von außen (gemerkte Auswahl, Zeitplan-Eintrag), zeigt
  // die Liste deren Karte
  $: if (selected.length > 0) {
    const fromSelection = rootMapOf(selected[0], maps)?.id ?? "";
    if (fromSelection !== rootId && mainMaps.some((m) => m.id === fromSelection)) rootId = fromSelection;
  }
  $: root = maps.find((m) => m.id === rootId);
  $: entries = root && !root.id.startsWith("__t_") ? jobTree(root, maps) : [];

  function selectRoot(id: string) {
    if (id === rootId) return;
    rootId = id;
    // Ohne Bereiche gibt es nur die ganze Karte: gleich ankreuzen
    selected = maps.some((m) => m.parentId === id) ? [] : [id];
    emit();
  }

  function emit() {
    dispatch("change", { selected, overlapOnce });
  }

  function toggle(id: string, on: boolean) {
    if (on === selected.includes(id)) return; // Carbon meldet "check" auch beim Rendern
    selected = on ? [...selected, id] : selected.filter((x) => x !== id);
    emit();
  }

  function move(index: number, delta: number) {
    const target = index + delta;
    if (target < 0 || target >= selected.length) return;
    const next = [...selected];
    [next[index], next[target]] = [next[target], next[index]];
    selected = next;
    emit();
  }

  const nameOf = (id: string) => maps.find((m) => m.id === id)?.name ?? `${id} (deleted)`;
</script>

{#if mainMaps.length > 1 || !rootId}
  <PortalDropdown
    titleText="Map"
    label="Choose a map"
    size="sm"
    items={mainMaps.map((m) => ({ id: m.id, text: m.name }))}
    selectedId={rootId}
    on:select={(e) => selectRoot(e.detail.selectedId)}
  />
{/if}
{#if !rootId && mainMaps.length > 0}
  <!-- noch keine Karte gewählt -->
{:else if entries.length === 0}
  <p class="hint">Save a map first. It and its areas can then be combined into one job.</p>
{:else}
  <h6 class="group-title">{entries.length > 1 ? `Areas of ${root?.name}` : root?.name}</h6>
  <div class="area-list">
    {#each entries as e (e.map.id)}
      <div class="area-row" style:padding-left={`${e.depth * 1.25}rem`}>
        <Checkbox
          labelText={e.label}
          checked={selected.includes(e.map.id)}
          disabled={!!e.map.outdated && !selected.includes(e.map.id)}
          on:check={(ev) => toggle(e.map.id, ev.detail)}
        />
        {#if e.map.outdated}
          <span class="hint warn" title="Calculate the route of this area again before mowing it">⚠ recalculate</span>
        {/if}
      </div>
    {/each}
  </div>

  {#if selected.length > 0}
    <h6 class="group-title">Order</h6>
    <ol class="job-order">
      {#each selected as id, i (id)}
        <li>
          <span class="job-order-label">{i + 1}. {nameOf(id)}</span>
          <Button kind="ghost" size="small" iconDescription="Earlier" disabled={i === 0} on:click={() => move(i, -1)}>▲</Button>
          <Button kind="ghost" size="small" iconDescription="Later" disabled={i === selected.length - 1} on:click={() => move(i, 1)}>▼</Button>
        </li>
      {/each}
    </ol>
  {/if}

  {#if selected.length > 1}
    <Toggle
      labelText="Mow overlapping parts only once"
      toggled={overlapOnce}
      on:toggle={(e) => {
        if (e.detail.toggled === overlapOnce) return;
        overlapOnce = e.detail.toggled;
        emit();
      }}
    />
    <p class="hint">
      Off: every area is mowed completely, overlaps twice. On: tracks lying in an earlier area of the job are left out.
    </p>
  {/if}
{/if}

<style>
  .group-title {
    margin: 0.75rem 0 0.25rem;
  }
  .area-list {
    display: flex;
    flex-direction: column;
  }
  .area-row {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .job-order {
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .job-order li {
    display: flex;
    align-items: center;
    gap: 0.25rem;
  }
  .job-order-label {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .hint {
    font-size: 0.75rem;
    color: var(--cds-text-02, #525252);
    margin-top: 0.5rem;
  }
  .hint.warn {
    color: #8e6a00;
    margin-top: 0;
  }
</style>
