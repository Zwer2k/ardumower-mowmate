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
  let mowBorderCcw: boolean = false;
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

  function handleOpen() {
    const s = $mowSettingsStore;
    pattern = s.pattern;
    roundWidth(s.width);
    angle = s.angle;
    distanceToBorder = s.distanceToBorder;
    borderLaps = s.borderLaps;
    mowBorderCcw = s.mowBorderCcw;
    simplifyEpsilon = s.simplifyEpsilon ?? 0.02;
    checkTurnRadius = s.checkTurnRadius ?? 0.3;
  }

  function handleOk() {
    socketService.sendMowSettings({
      pattern,
      width,
      angle,
      distanceToBorder,
      borderLaps,
      mowBorderCcw,
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
      label="Distance to border (m)"
      bind:value={distanceToBorder}
      min={0}
      max={5}
      step={0.05}
    />

    <NumberInput
      label="Border laps"
      bind:value={borderLaps}
      min={0}
      max={6}
      step={1}
    />

    <div class="mow-settings-toggles">
      <Toggle
        labelText="Mow border CCW"
        toggled={mowBorderCcw}
        on:toggle={(e) => { mowBorderCcw = e.detail.toggled; }}
      />
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
</style>
