<script lang="ts">
  import { Button, Row, Column } from "carbon-components-svelte";
  import IconPen from "carbon-icons-svelte/lib/Pen.svelte";
  import IconTrashCan from "carbon-icons-svelte/lib/TrashCan.svelte";
  import IconStar from "carbon-icons-svelte/lib/Star.svelte";
  import IconNew from "carbon-icons-svelte/lib/DocumentAdd.svelte";
  import IconCopy from "carbon-icons-svelte/lib/Copy.svelte";
  import IconImportExport from "carbon-icons-svelte/lib/Cloud.svelte";
  import type { MapWorkflow } from "../workflow/map-workflow-store";

  export let workflow: MapWorkflow;
  export let canRename: boolean;
  export let effectiveMapId: string;
  export let pendingName: string;
  export let effectiveMapName: string;
  export let previousMapName: string;
  export let startRename: () => void;
  export let onDeleteMap: () => void;
  export let onSetDefaultMap: () => void;
  export let onNewMap: () => void;
  export let onCopyMap: () => void;
  export let onOpenMowerMap: () => void;
  export let workflowBusy: boolean;
</script>

<Row class="map-mgmt-row">
  <Column style="flex-shrink: 0;">
    <div class="toolbar-btn-row">
      <Button
        kind="secondary"
        size="small"
        disabled={!canRename || workflow.renameMode || workflowBusy}
        icon={IconPen}
        iconDescription="Rename map"
        on:click={startRename}
      >
        <span class="btn-label">Rename</span>
      </Button>
      <Button
        kind="danger"
        size="small"
        disabled={!effectiveMapId || workflow.renameMode || workflowBusy}
        on:click={onDeleteMap}
        icon={IconTrashCan}
        iconDescription="Delete map"
      >
        <span class="btn-label">Del</span>
      </Button>
      <Button
        kind="tertiary"
        size="small"
        disabled={!effectiveMapId || workflow.renameMode || workflowBusy}
        on:click={onSetDefaultMap}
        icon={IconStar}
        iconDescription="Set as default map"
      >
        <span class="btn-label">Default</span>
      </Button>
      <Button
        kind="secondary"
        size="small"
        disabled={workflow.renameMode || workflowBusy}
        on:click={onNewMap}
        icon={IconNew}
        iconDescription="New map"
      >
        <span class="btn-label">New</span>
      </Button>
      <Button
        kind="secondary"
        size="small"
        disabled={!effectiveMapId || workflow.renameMode || workflowBusy}
        on:click={onCopyMap}
        icon={IconCopy}
        iconDescription="Copy map"
      >
        <span class="btn-label">Copy</span>
      </Button>
      <Button
        kind="tertiary"
        size="small"
        disabled={workflow.renameMode || workflowBusy}
        on:click={onOpenMowerMap}
        icon={IconImportExport}
        iconDescription="Import / Export Mower map"
      >
        <span class="btn-label">JSON</span>
      </Button>
      {#if pendingName && pendingName !== effectiveMapName}
        <span class="pending-map-name" title="Bisheriger Kartenname">→ {previousMapName}</span>
      {/if}
    </div>
  </Column>
</Row>

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
  .pending-map-name {
    margin-left: 0.5rem;
    padding: 0.25rem 0.5rem;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    background: #fff3e0;
    color: #b06000;
    border: 1px solid #ffcc80;
    border-radius: 4px;
    font-size: 0.85em;
    font-weight: 600;
    text-align: center;
    white-space: nowrap;
    max-width: 200px;
    overflow: hidden;
    text-overflow: ellipsis;
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
