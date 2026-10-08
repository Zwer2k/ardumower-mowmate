<script lang="ts">
  import { onMount } from "svelte";
  import {
    Button,
    ProgressBar,
    StructuredList,
    StructuredListBody,
    StructuredListRow,
    StructuredListCell,
  } from "carbon-components-svelte";
  import IconRenew from "carbon-icons-svelte/lib/Renew.svelte";
  import Group from "./Group.svelte";
  import { getModemInfo } from "../firmware/service";
  import type { MemoryInfo } from "../model";

  // Eigene Abfrage statt InfoStore: der wird nur einmal beim Laden der Seite
  // gefüllt, die Belegung soll aber aktuell sein.
  let memory: MemoryInfo | null = null;
  let error = "";
  let loading = false;

  async function refresh() {
    loading = true;
    error = "";
    try {
      memory = (await getModemInfo()).memory ?? null;
      if (!memory) error = "The modem firmware does not report memory usage yet.";
    } catch (e) {
      error = "Could not read memory usage.";
    } finally {
      loading = false;
    }
  }

  onMount(refresh);

  function format(bytes: number): string {
    if (bytes >= 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
    return `${(bytes / 1024).toFixed(0)} KB`;
  }

  type Row = { label: string; used: number; total: number; note?: string };
  $: rows = memory
    ? ([
        { label: "Storage (SPIFFS: maps, settings)", used: memory.fs_used, total: memory.fs_total },
        {
          label: "RAM (internal)",
          used: memory.heap_total - memory.heap_free,
          total: memory.heap_total,
          note: `lowest free since start: ${format(memory.heap_min_free)}`,
        },
        ...(memory.psram_total > 0
          ? [{ label: "PSRAM", used: memory.psram_total - memory.psram_free, total: memory.psram_total }]
          : []),
      ] as Row[])
    : [];
</script>

<Group title="Storage" open={true}>
  <StructuredList>
    <StructuredListBody>
      {#each rows as row}
        {@const percent = row.total > 0 ? Math.round((row.used / row.total) * 100) : 0}
        <StructuredListRow>
          <StructuredListCell>{row.label}</StructuredListCell>
          <StructuredListCell>
            <ProgressBar
              value={percent}
              max={100}
              size="sm"
              status={percent >= 90 ? "error" : "active"}
              helperText={`${format(row.used)} of ${format(row.total)} used (${percent}%)${row.note ? ", " + row.note : ""}`}
            />
          </StructuredListCell>
        </StructuredListRow>
      {/each}
    </StructuredListBody>
  </StructuredList>
  {#if error}
    <p class="error">{error}</p>
  {/if}
  <Button kind="ghost" size="small" icon={IconRenew} disabled={loading} on:click={refresh}>Refresh</Button>
</Group>

<style>
  .error {
    color: #da1e28;
    margin-bottom: 0.5rem;
  }
</style>
