<script lang="ts">
  import { Tag, Tile } from "carbon-components-svelte";
  import type { SensorSummary } from "../../../model";

  export let summary: SensorSummary | null = null;

  interface SensorEvent {
    time: string;
    text: string;
    kind: "trigger" | "clear" | "info";
  }

  let events: SensorEvent[] = [];
  let lastSummary: SensorSummary | null = null;

  function formatTime(ts: number): string {
    const d = new Date(ts);
    return d.toLocaleTimeString("de-DE", { hour12: false, hour: "2-digit", minute: "2-digit", second: "2-digit" });
  }

  function nowTime(): string {
    return new Date().toLocaleTimeString("de-DE", { hour12: false, hour: "2-digit", minute: "2-digit", second: "2-digit" });
  }

  function pushEvent(text: string, kind: "trigger" | "clear" | "info") {
    events = [{ time: nowTime(), text, kind }, ...events].slice(0, 20);
  }

  function boolChanged(prev: boolean, curr: boolean, name: string) {
    if (!prev && curr) pushEvent(`${name} ausgelöst`, "trigger");
    if (prev && !curr) pushEvent(`${name} frei`, "clear");
  }

  $: {
    if (summary != null && lastSummary != null) {
      boolChanged(lastSummary.sonar_obstacle, summary.sonar_obstacle, "Sonar");
      boolChanged(lastSummary.bumper_obstacle, summary.bumper_obstacle, "Bumper");
      boolChanged(lastSummary.lidar_obstacle, summary.lidar_obstacle, "Lidar");
      boolChanged(lastSummary.lift_triggered, summary.lift_triggered, "Lift");
      boolChanged(lastSummary.rain_triggered, summary.rain_triggered, "Rain");

      if (lastSummary.sonar_near_obstacle !== summary.sonar_near_obstacle) {
        pushEvent(`Sonar near ${summary.sonar_near_obstacle ? "erkannt" : "frei"}`, summary.sonar_near_obstacle ? "trigger" : "clear");
      }
      if (lastSummary.bumper_left !== summary.bumper_left) {
        pushEvent(`Bumper links ${summary.bumper_left ? "ausgelöst" : "frei"}`, summary.bumper_left ? "trigger" : "clear");
      }
      if (lastSummary.bumper_right !== summary.bumper_right) {
        pushEvent(`Bumper rechts ${summary.bumper_right ? "ausgelöst" : "frei"}`, summary.bumper_right ? "trigger" : "clear");
      }
    }
    if (summary != null) {
      lastSummary = { ...summary };
    }
  }
</script>

<Tile class="status-card">
  <div class="card-header"><span class="card-icon">🛡️</span><span class="card-title">Sensoren</span></div>
  {#if summary != null}
    <div class="sensor-grid">
      <div class="sensor-group">
        <div class="group-title">Sonar (cm)</div>
        <div class="sensor-values">
          <span class:value-active={summary.sonar_left < 30}>L {summary.sonar_left.toFixed(0)}</span>
          <span class:value-active={summary.sonar_center < 30}>M {summary.sonar_center.toFixed(0)}</span>
          <span class:value-active={summary.sonar_right < 30}>R {summary.sonar_right.toFixed(0)}</span>
        </div>
        <div class="sensor-flags">
          {#if summary.sonar_obstacle}<Tag type="red" size="sm">Hindernis</Tag>{/if}
          {#if summary.sonar_near_obstacle}<Tag type="warm-gray" size="sm">nah</Tag>{/if}
          {#if !summary.sonar_obstacle && !summary.sonar_near_obstacle}<Tag type="green" size="sm">frei</Tag>{/if}
        </div>
      </div>
      <div class="sensor-group">
        <div class="group-title">Bumper</div>
        <div class="sensor-flags">
          {#if summary.bumper_left}<Tag type="red" size="sm">links</Tag>{/if}
          {#if summary.bumper_right}<Tag type="red" size="sm">rechts</Tag>{/if}
          {#if summary.bumper_obstacle}<Tag type="red" size="sm">Hindernis</Tag>{/if}
          {#if summary.bumper_near_obstacle}<Tag type="warm-gray" size="sm">nah</Tag>{/if}
          {#if !summary.bumper_left && !summary.bumper_right && !summary.bumper_obstacle && !summary.bumper_near_obstacle}<Tag type="green" size="sm">frei</Tag>{/if}
        </div>
      </div>
      <div class="sensor-group">
        <div class="group-title">Lidar</div>
        <div class="sensor-flags">
          {#if summary.lidar_obstacle}<Tag type="red" size="sm">Hindernis</Tag>{/if}
          {#if summary.lidar_near_obstacle}<Tag type="warm-gray" size="sm">nah</Tag>{/if}
          {#if !summary.lidar_obstacle && !summary.lidar_near_obstacle}<Tag type="green" size="sm">frei</Tag>{/if}
        </div>
      </div>
      <div class="sensor-group">
        <div class="group-title">Hebung / Regen</div>
        <div class="sensor-flags">
          <Tag type={summary.lift_triggered ? "red" : "green"} size="sm">Hebung {summary.lift_triggered ? "aktiv" : "nein"}</Tag>
          <Tag type={summary.rain_triggered ? "blue" : "green"} size="sm">Regen {summary.rain_triggered ? "ja" : "nein"}</Tag>
        </div>
      </div>
    </div>
  {:else}
    <div class="no-data">Keine Sensordaten verfügbar</div>
  {/if}
  {#if events.length > 0}
    <div class="group-title events-title">Ereignisse</div>
    <div class="log-list">
      {#each events as ev}
        <div class="log-entry" class:trigger={ev.kind === "trigger"} class:clear={ev.kind === "clear"}>
          <span class="log-time">{ev.time}</span>
          <span class="log-action">{ev.text}</span>
        </div>
      {/each}
    </div>
  {/if}
</Tile>

<style lang="scss">
  .card-header {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    margin-bottom: 0.5rem;
    padding-bottom: 0.375rem;
    border-bottom: 1px solid #e0e0e0;
  }
  .card-icon {
    font-size: 1.125rem;
    line-height: 1;
  }
  .card-title {
    font-size: 0.875rem;
    font-weight: 600;
    color: #161616;
  }
  .sensor-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 6px;
  }
  .sensor-group {
    border: 1px solid #e0e0e0;
    padding: 6px;
    background: #f4f4f4;
  }
  .group-title {
    font-size: 0.75rem;
    color: #525252;
    margin-bottom: 4px;
  }
  .events-title {
    margin-top: 0.75rem;
  }
  .sensor-values {
    display: flex;
    gap: 6px;
    font-variant-numeric: tabular-nums;
    font-size: 0.8125rem;
    font-weight: 600;
  }
  .sensor-values span {
    padding: 1px 4px;
    background: #e0e0e0;
  }
  .sensor-values .value-active {
    background: #ffd7d9;
    color: #a2191f;
  }
  .sensor-flags {
    display: flex;
    flex-wrap: wrap;
    gap: 4px;
    margin-top: 4px;
  }
  .sensor-flags :global(.bx--tag) {
    margin: 0;
  }
  .no-data {
    color: #6f6f6f;
    font-size: 0.875rem;
    text-align: center;
    padding: 8px 0;
  }
  .log-list {
    display: flex;
    flex-direction: column;
    gap: 2px;
    max-height: 12rem;
    overflow-y: auto;
  }
  .log-entry {
    display: flex;
    gap: 0.5rem;
    font-size: 0.75rem;
    padding: 2px 4px;
  }
  .log-time {
    font-variant-numeric: tabular-nums;
    color: #525252;
  }
  .log-entry.trigger {
    background: #fff1f1;
    color: #a2191f;
  }
  .log-entry.clear {
    background: #defbe6;
    color: #0e6027;
  }
</style>
