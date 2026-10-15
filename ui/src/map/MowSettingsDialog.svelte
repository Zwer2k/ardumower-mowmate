<script lang="ts">
  import { Button, Modal, NumberInput, Select, SelectItem, Toggle } from "carbon-components-svelte";
  import { mowSettingsStore } from "./mow-settings";
  import { socketService } from "../stores/socket";

  export let open = false;

  let pattern: number = 0;
  let width: number = 0.3;
  let angle: number = 0;
  let distanceToBorder: number = 0;
  let borderLaps: number = 0;
  let distanceToExclusions: number = 0;
  let exclusionLaps: number = 0;
  let mowBorderCcw: boolean = false;

  // Reihenfolge der Abschnitte. Ohne gespeicherte Reihenfolge gilt die frühere:
  // "Mow border CCW" an = Ränder zuerst, aus = Fläche zuerst.
  type RouteBlock = "area" | "border" | "exclusions";
  const blockLabels: Record<RouteBlock, string> = {
    area: "Area",
    border: "Border",
    exclusions: "Exclusion borders",
  };
  let order: RouteBlock[] = ["area", "border", "exclusions"];
  let exclusionLapsWithArea = false;

  function parseOrder(text: string | undefined, ccw: boolean): RouteBlock[] {
    const parts = (text ?? "").split(",").map((t) => t.trim()).filter(Boolean) as RouteBlock[];
    const valid = parts.length === 3 && new Set(parts).size === 3 && parts.every((p) => p in blockLabels);
    if (valid) return parts;
    return ccw ? ["border", "exclusions", "area"] : ["area", "border", "exclusions"];
  }

  // Mit "while passing" stecken die Exclusion-Runden in der Fläche und haben
  // keinen eigenen Platz: Der Eintrag wird ausgeblendet. Ausgeschaltet
  // erscheint er wieder an seiner bisherigen Stelle.
  $: visibleOrder = exclusionLapsWithArea ? order.filter((b) => b !== "exclusions") : order;

  function moveBlock(index: number, delta: number) {
    const target = index + delta;
    if (target < 0 || target >= visibleOrder.length) return;
    const next = [...visibleOrder];
    [next[index], next[target]] = [next[target], next[index]];
    if (!exclusionLapsWithArea) {
      order = next;
      return;
    }
    // ausgeblendeten Eintrag an seiner Stelle behalten
    const hiddenAt = order.indexOf("exclusions");
    next.splice(Math.min(hiddenAt, next.length), 0, "exclusions");
    order = next;
  }
  let simplifyEpsilon: number = 0.02;
  let checkTurnRadius: number = 0.3;

  const patterns = [
    { id: 0, text: "Lines" },
    { id: 1, text: "Squares" },
    { id: 2, text: "Rings" },
  ];

  function roundWidth(v: number) {
    width = Math.round(v * 100) / 100;
  }

  // Counts track widths (the planner offsets the mow area by
  // distanceToBorder * width), so only whole numbers are meaningful.
  function roundDistanceToBorder() {
    distanceToBorder = Math.max(0, Math.round(distanceToBorder || 0));
  }

  function roundDistanceToExclusions() {
    distanceToExclusions = Math.max(0, Math.round(distanceToExclusions || 0));
  }

  function handleOpen() {
    const s = $mowSettingsStore;
    pattern = s.pattern;
    roundWidth(s.width);
    angle = s.angle;
    distanceToBorder = s.distanceToBorder;
    borderLaps = s.borderLaps;
    distanceToExclusions = s.distanceToExclusions ?? 0;
    exclusionLaps = s.exclusionLaps ?? s.borderLaps;
    mowBorderCcw = s.mowBorderCcw;
    order = parseOrder(s.routeOrder, s.mowBorderCcw);
    exclusionLapsWithArea = s.exclusionLapsWithArea ?? false;
    simplifyEpsilon = s.simplifyEpsilon ?? 0.02;
    checkTurnRadius = s.checkTurnRadius ?? 0.3;
  }

  function handleOk() {
    roundDistanceToBorder();
    roundDistanceToExclusions();
    socketService.sendMowSettings({
      pattern,
      width,
      angle,
      distanceToBorder,
      borderLaps,
      distanceToExclusions,
      exclusionLaps,
      mowBorderCcw,
      routeOrder: order.join(","),
      exclusionLapsWithArea,
      simplifyEpsilon,
      checkTurnRadius,
    });
    open = false;
  }

  function handleCancel() {
    open = false;
  }
</script>

<Modal
  bind:open
  on:open={handleOpen}
  title="Mow Settings"
  size="sm"
  id="mow-settings-modal"
  hasScrollingContent={true}
  primaryButtonText="Apply"
  secondaryButtonText="Cancel"
  on:click:button--primary={handleOk}
  on:click:button--secondary={handleCancel}
  on:submit={handleOk}
  on:close={handleCancel}
>
  <div class="mow-settings-form">
    <Select
      labelText="Pattern"
      selected={pattern}
      on:update={(e) => { pattern = e.detail; }}
    >
      {#each patterns as p}
        <SelectItem value={p.id} text={p.text} />
      {/each}
    </Select>

    <NumberInput
      label="Track width (m)"
      bind:value={width}
      min={0.01}
      max={1.0}
      step={0.01}
      on:change={() => roundWidth(width)}
    />

    <NumberInput
      label="Angle (°)"
      bind:value={angle}
      min={0}
      max={359}
      step={1}
    />

    <NumberInput
      label="Distance to border"
      helperText="Number of track widths the mow area is kept away from the perimeter."
      bind:value={distanceToBorder}
      min={0}
      max={5}
      step={1}
      on:change={roundDistanceToBorder}
    />

    <NumberInput
      label="Border laps"
      helperText="Laps along the perimeter."
      bind:value={borderLaps}
      min={0}
      max={6}
      step={1}
    />

    <NumberInput
      label="Distance to exclusions"
      helperText="Number of track widths the mow area is kept away from the exclusions."
      bind:value={distanceToExclusions}
      min={0}
      max={5}
      step={1}
      on:change={roundDistanceToExclusions}
    />

    <NumberInput
      label="Exclusion laps"
      helperText="Laps around each exclusion, each one track width further out. Switched on and off with Mow Excl. border."
      bind:value={exclusionLaps}
      min={0}
      max={6}
      step={1}
    />

    <div class="settings-group">
      <h6 class="group-title">Order</h6>
      <ol class="route-order">
        {#each visibleOrder as block, i (block)}
          <li>
            <span class="route-order-label">
              {blockLabels[block]}{#if block === "area" && exclusionLapsWithArea}<span class="route-order-hint"> with exclusion borders</span>{/if}
            </span>
            <Button kind="ghost" size="small" iconDescription="Earlier" disabled={i === 0}
              on:click={() => moveBlock(i, -1)}>▲</Button>
            <Button kind="ghost" size="small" iconDescription="Later" disabled={i === visibleOrder.length - 1}
              on:click={() => moveBlock(i, 1)}>▼</Button>
          </li>
        {/each}
      </ol>
      <div class="mow-settings-toggles">
        <Toggle
          labelText="Mow exclusion borders while passing"
          toggled={exclusionLapsWithArea}
          on:toggle={(e) => { exclusionLapsWithArea = e.detail.toggled; }}
        />
        <p class="route-order-help">
          Mows the laps around each exclusion during the area, where the area pattern comes closest, instead of
          driving to every exclusion afterwards. Saves empty runs.
        </p>
        <Toggle
          labelText="Border laps counter-clockwise"
          toggled={mowBorderCcw}
          on:toggle={(e) => { mowBorderCcw = e.detail.toggled; }}
        />
      </div>
    </div>

    <div class="settings-group">
      <h6 class="group-title">Route calculation</h6>
      <NumberInput
        label="Simplification threshold (m)"
        helperText="Removes waypoints that deviate less than this from a straight line. Larger values give fewer waypoints."
        bind:value={simplifyEpsilon}
        min={0}
        max={0.5}
        step={0.01}
      />
    </div>

    <div class="settings-group">
      <h6 class="group-title">Route check &mdash; does not change the route</h6>
      <NumberInput
        label="Minimum turn radius (m)"
        helperText="Only used to flag corners in the route check report. Rule of thumb: mowing speed divided by the controller's maximum turn rate."
        bind:value={checkTurnRadius}
        min={0}
        max={5}
        step={0.05}
      />
    </div>
  </div>
</Modal>

<style>
  .mow-settings-form {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    padding: 0.5rem 0;
  }

  .mow-settings-toggles {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 1rem;
  }

  .mow-settings-toggles :global(.bx--toggle) {
    width: 100%;
  }

  .settings-group {
    border-top: 1px solid #e0e0e0;
    padding-top: 0.75rem;
  }

  .group-title {
    margin: 0 0 0.5rem 0;
    font-size: 0.75rem;
    font-weight: 600;
    letter-spacing: 0.02em;
    color: #525252;
    text-transform: uppercase;
  }

  /* Deaktivierte Inputs leicht grau einfärben, damit sie sichtbar deaktiviert
     sind, aber trotzdem zum Dialog passen. */
  :global(#mow-settings-modal .bx--number input:disabled) {
    background-color: #e0e0e0 !important;
  }

  /* Make this dialog a flex column so the content area shrinks to fit between
     header and footer, scrolls internally, and never pushes the buttons out of
     the viewport. */
  :global(#mow-settings-modal .bx--modal-container) {
    display: flex !important;
    flex-direction: column !important;
    max-height: 90vh !important;
  }
  :global(#mow-settings-modal .bx--modal-content) {
    flex: 1 1 auto !important;
    min-height: 0 !important;
    overflow-y: auto !important;
    margin-bottom: 0 !important;
  }
  :global(#mow-settings-modal .bx--modal-content--overflow-indicator) {
    display: none !important;
  }
  .route-order {
    list-style: none;
    margin: 0 0 0.5rem;
    padding: 0;
  }
  .route-order li {
    display: flex;
    align-items: center;
    gap: 0.25rem;
    padding: 0.125rem 0;
    border-bottom: 1px solid #e0e0e0;
  }
  .route-order li::before {
    content: counter(list-item) ".";
    width: 1.25rem;
    color: #525252;
  }
  .route-order-label {
    flex: 1;
    font-size: 0.875rem;
  }
  .route-order-hint {
    font-size: 0.75rem;
    color: #6f6f6f;
  }
  .route-order-help {
    font-size: 0.75rem;
    color: #525252;
    margin: -0.25rem 0 0.5rem;
  }
</style>
