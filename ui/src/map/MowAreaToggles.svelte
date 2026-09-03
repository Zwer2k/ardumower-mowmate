<script lang="ts" context="module">
  let instanceCounter = 0;
</script>

<script lang="ts">
  import { Toggle } from "carbon-components-svelte";
  import { mowSettingsStore } from "./mow-settings";
  import { socketService } from "../stores/socket";

  let { horizontal = false, showTitle = true, labelPrefix = "" } = $props();

  const instanceId = `mow-area-toggles-${++instanceCounter}`;
  const areaId = `${instanceId}-area`;
  const borderId = `${instanceId}-border`;
  const exclBorderId = `${instanceId}-excl-border`;

  let doMowArea = $state($mowSettingsStore.doMowArea ?? true);
  let doMowBorder = $state($mowSettingsStore.doMowBorder ?? false);
  let doMowExclusionBorder = $state($mowSettingsStore.doMowExclusionBorder ?? false);

  // Track previous store values to only update local state when they actually change
  let prevDoMowArea = $mowSettingsStore.doMowArea ?? true;
  let prevDoMowBorder = $mowSettingsStore.doMowBorder ?? false;
  let prevDoMowExclusionBorder = $mowSettingsStore.doMowExclusionBorder ?? false;

  $effect(() => {
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
    const s = $mowSettingsStore;
    socketService.sendMowSettings({
      pattern: s.pattern,
      width: s.width,
      angle: s.angle,
      distanceToBorder: s.distanceToBorder,
      borderLaps: s.borderLaps,
      mowBorderCcw: s.mowBorderCcw,
      doMowArea: updates.doMowArea ?? s.doMowArea,
      doMowPerimeter: updates.doMowBorder ?? s.doMowPerimeter,
      doMowBorder: updates.doMowBorder ?? s.doMowBorder,
      doMowExclusions: s.doMowExclusions,
      doMowExclusionBorder: updates.doMowExclusionBorder ?? s.doMowExclusionBorder,
    });
  }

  function onChange(e: Event) {
    const target = e.target as HTMLInputElement;
    if (!target || target.type !== 'checkbox') return;
    const checked = target.checked;
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
  <Toggle
    id={areaId}
    labelText="{labelPrefix}Area"
    size="sm"
    bind:toggled={doMowArea}
  />
  <Toggle
    id={borderId}
    labelText="{labelPrefix}Border"
    size="sm"
    bind:toggled={doMowBorder}
  />
  <Toggle
    id={exclBorderId}
    labelText="{labelPrefix}Excl. border"
    size="sm"
    bind:toggled={doMowExclusionBorder}
  />
</div>

<style>
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
