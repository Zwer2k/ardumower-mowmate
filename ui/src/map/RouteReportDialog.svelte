<script lang="ts">
  import { Modal } from "carbon-components-svelte";
  import type { RouteFinding } from "../model";
  import { routeReportStore, formatDuration, formatLength } from "./route-report";

  export let open = false;
  /** Called with the clicked finding. The parent marks the waypoint on the
   *  map and closes this dialog. */
  export let onSelectFinding: (f: RouteFinding) => void = () => {};

  $: report = $routeReportStore;

  const severityLabel = ["Info", "Warning", "Error"];

  function findingText(f: RouteFinding): string {
    const miss = f.miss != null ? f.miss.toFixed(2) : "?";
    switch (f.kind) {
      case 0:
        return `Waypoint ${f.idx}: ${(f.ang ?? 0).toFixed(0)}° corner, ` +
               `${miss} m of straight run-in missing`;
      case 1:
        return `Waypoints ${f.idx}/${f.idx2}: corners overlap by ${miss} m, ` +
               `no room to settle between them`;
      case 2:
        return `Waypoint ${f.idx}: segment has practically zero length`;
      default:
        return `Waypoint ${f.idx}: unknown issue`;
    }
  }

  function severityClass(sev: number): string {
    if (sev >= 2) return "error";
    if (sev === 1) return "warning";
    return "info";
  }

  $: shown = report?.findings ?? [];
  $: hiddenCount = report ? Math.max(0, report.findingsTotal - shown.length) : 0;
</script>

<Modal
  passiveModal
  bind:open
  modalHeading="Route check"
  hasScrollingContent={true}
  on:click:button--primary={() => (open = false)}
  on:close={() => (open = false)}
>
  {#if !report}
    <p class="empty">No route has been calculated yet.</p>
  {:else}
    <div class="stats">
      <div class="stat"><span class="label">Length</span><span class="value">{formatLength(report.totalLength)}</span></div>
      <div class="stat"><span class="label">Waypoints</span><span class="value">{report.pointCount}</span></div>
      <div class="stat"><span class="label">Rotations on the spot</span><span class="value">{report.rotationCount}</span></div>
      <div class="stat"><span class="label">Steered corners</span><span class="value">{report.trackedCorners}</span></div>
      <div class="stat">
        <span class="label">Mowing time, estimate</span>
        <span class="value">{formatDuration(report.estimatedSeconds)}</span>
      </div>
    </div>

    <p class="hint">
      Checked against a minimum turn radius of {report.turnRadius.toFixed(2)} m.
      The check never changes the route, and findings never block an upload.
    </p>

    {#if shown.length === 0}
      <p class="ok">No problems found. Every corner has enough room for the tracker.</p>
    {:else}
      <p class="hint">Select a finding to mark it on the map.</p>
      <div class="findings">
        {#each shown as f}
          <button
            type="button"
            class="finding {severityClass(f.sev)}"
            title="Mark on the map"
            on:click={() => onSelectFinding(f)}
          >
            <span class="sev">{severityLabel[f.sev] ?? "Info"}</span>
            <span class="text">{findingText(f)}</span>
          </button>
        {/each}
      </div>
      {#if hiddenCount > 0}
        <p class="more">{hiddenCount} further finding{hiddenCount === 1 ? "" : "s"} not listed.</p>
      {/if}
    {/if}
  {/if}
</Modal>

<style lang="scss">
  .stats {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
    gap: 6px;
    margin-bottom: 12px;
  }
  .stat {
    display: flex;
    flex-direction: column;
    padding: 6px 8px;
    background: #f4f4f4;
    border-left: 3px solid #8d8d8d;
  }
  .stat .label {
    font-size: 0.75rem;
    color: #555;
  }
  .stat .value {
    font-size: 1rem;
    font-weight: 600;
    font-family: monospace;
  }
  .hint {
    font-size: 0.75rem;
    color: #555;
    margin-bottom: 12px;
  }
  .ok {
    padding: 8px;
    background: #defbe6;
    border-left: 3px solid #24a148;
    font-size: 0.875rem;
  }
  .empty {
    font-size: 0.875rem;
    color: #555;
  }
  .findings {
    max-height: 40vh;
    overflow-y: auto;
    border: 1px solid #e0e0e0;
  }
  .finding {
    display: flex;
    gap: 8px;
    width: 100%;
    padding: 4px 8px;
    font: inherit;
    font-size: 0.8125rem;
    text-align: left;
    background: none;
    border: none;
    border-bottom: 1px solid #f0f0f0;
    cursor: pointer;
  }
  .finding:hover,
  .finding:focus-visible {
    background: #e8e8e8;
  }
  .finding:last-child {
    border-bottom: none;
  }
  .finding .sev {
    flex: 0 0 4.5rem;
    font-weight: 600;
    font-size: 0.75rem;
  }
  .finding.error .sev {
    color: #da1e28;
  }
  .finding.warning .sev {
    color: #b28600;
  }
  .finding.info .sev {
    color: #0043ce;
  }
  .finding .text {
    line-break: anywhere;
  }
  .more {
    margin-top: 6px;
    font-size: 0.75rem;
    color: #555;
  }
</style>
