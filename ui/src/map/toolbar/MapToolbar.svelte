<script lang="ts">
  import { Button } from "carbon-components-svelte";
  import IconEdit from "carbon-icons-svelte/lib/Edit.svelte";
  import IconTools from "carbon-icons-svelte/lib/Tools.svelte";
  import IconMagicWand from "carbon-icons-svelte/lib/MagicWand.svelte";
  import IconCalendar from "carbon-icons-svelte/lib/Calendar.svelte";
  import IconSave from "carbon-icons-svelte/lib/Save.svelte";
  import IconUndo from "carbon-icons-svelte/lib/Undo.svelte";
  import IconCheckmark from "carbon-icons-svelte/lib/Checkmark.svelte";
  import IconClose from "carbon-icons-svelte/lib/Close.svelte";

  export let workflowBusy: boolean;
  export let renameMode: boolean;
  export let showManage: boolean;
  export let edit: boolean;
  export let showCalculate: boolean;
  export let showSchedule: boolean;
  export let canConfirmRename: boolean;
  export let canSave: boolean;
  export let canRevert: boolean;
  export let onToggleManage: () => void;
  export let onToggleEdit: () => void;
  export let onToggleCalculate: () => void;
  export let onToggleSchedule: () => void;
  export let onSaveMap: () => void;
  export let onDiscardMap: () => void;
  export let onConfirmRename: () => void;
  export let onCancelRename: () => void;
</script>

<div class="toolbar-btn-row">
  {#if renameMode}
    <Button
      kind="primary"
      size="small"
      disabled={!canConfirmRename}
      on:click={onConfirmRename}
      icon={IconCheckmark}
      iconDescription="Confirm rename"
    >
      <span class="btn-label">OK</span>
    </Button>
    <Button
      kind="secondary"
      size="small"
      on:click={onCancelRename}
      icon={IconClose}
      iconDescription="Cancel rename"
    >
      <span class="btn-label">Cancel</span>
    </Button>
  {:else}
    <Button
      kind={showManage ? "primary" : "secondary"}
      size="small"
      icon={IconTools}
      iconDescription="Manage maps"
      tooltipPosition="top"
      on:click={onToggleManage}
    />
    <Button
      kind={edit ? "primary" : "secondary"}
      size="small"
      icon={IconEdit}
      iconDescription="Edit map"
      tooltipPosition="top"
      on:click={onToggleEdit}
    />
    <Button
      kind={showCalculate ? "primary" : "secondary"}
      size="small"
      icon={IconMagicWand}
      iconDescription="Calculate waypoints"
      tooltipPosition="top"
      on:click={onToggleCalculate}
    />
    <Button
      kind={showSchedule ? "primary" : "secondary"}
      size="small"
      icon={IconCalendar}
      iconDescription="Schedule"
      tooltipPosition="top"
      on:click={onToggleSchedule}
    />
    <Button
      kind="primary"
      size="small"
      disabled={!canSave}
      icon={IconSave}
      iconDescription="Save map"
      tooltipPosition="top"
      on:click={onSaveMap}
    />
    <Button
      kind="tertiary"
      size="small"
      disabled={!canRevert}
      icon={IconUndo}
      iconDescription="Revert changes"
      tooltipPosition="top"
      on:click={onDiscardMap}
    />
  {/if}
</div>

<style>
  .toolbar-btn-row {
    display: flex;
    flex: 0 0 auto;
    flex-wrap: nowrap;
    gap: 0.25rem;
  }
  .btn-label {
    margin-left: 0.25rem;
  }
  .toolbar-btn-row :global(.bx--btn) {
    padding-left: 0.3125rem !important;
    padding-right: 0.3125rem !important;
  }
  .toolbar-btn-row :global(.bx--btn .bx--btn__icon) {
    position: static;
    margin-left: 0.5rem;
    margin-right: 0.25rem;
  }

  /* Carbon icon-only buttons render the tooltip inside the button, which gives
     it the same stacking context as the rest of the toolbar. The Carbon header
     has z-index: 6000, so the tooltip must be lifted above it to be visible when
     it points upward from the top toolbar row. */
  .toolbar-btn-row :global(.bx--btn--icon-only .bx--assistive-text) {
    z-index: 9100;
  }

  @media (max-width: 800px) {
    .btn-label {
      display: none;
    }
    .toolbar-btn-row :global(.bx--btn) {
      width: 2rem;
      padding: 0 !important;
      justify-content: center;
    }
    .toolbar-btn-row :global(.bx--btn .bx--btn__icon) {
      position: static;
      margin: 0;
    }
  }
</style>
