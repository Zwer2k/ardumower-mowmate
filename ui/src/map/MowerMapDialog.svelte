<script lang="ts">
  import {
    Button,
    Modal,
    TextArea,
    InlineNotification,
  } from "carbon-components-svelte";
  import IconCopy from "carbon-icons-svelte/lib/Copy.svelte";
  import IconDocumentDownload from "carbon-icons-svelte/lib/DocumentDownload.svelte";
  import {
    exportMowerMap,
    importMowerMap,
    isValidMowerMap,
  } from "./core/mower-map";
  import {
    exportGeoJson,
    importGeoJson,
    isGeoJsonFeatureCollection,
  } from "./core/map-formats";
  import type { MapFormat } from "./core/map-formats";
  import type { Map } from "../model";
  import { SaveSuccess } from "../stores/success";
  import type { MowSettingsData } from "../model";
  import { BackendSettings } from "../stores/backend";
  import { get } from "svelte/store";

  export let open = false;
  export let map: Map;
  export let rotation: number = 0;
  export let onImport: (
    map: Map,
    rotation: number,
    dateTime?: string,
    source?: string,
    settings?: Partial<MowSettingsData>,
  ) => void;

  let mode: "export" | "import" = "export";
  let format: MapFormat = "mower";
  let jsonText = "";
  let importError = "";
  let exportError = "";

  $: if (open) {
    mode = "export";
    format = "mower";
    jsonText = exportMowerMap(map, { rotation });
    importError = "";
    exportError = "";
  }

  function doExport(): string {
    if (format === "geojson") {
      const position = get(BackendSettings)?.position;
      const hasReference = position && Number.isFinite(position.lon) && Number.isFinite(position.lat) &&
        (position.lon !== 0 || position.lat !== 0);
      if (!hasReference) {
        exportError = "Configure the reference longitude/latitude in Position settings before exporting CaSSAndRA GeoJSON.";
        return "";
      }
      exportError = "";
      return exportGeoJson(map, { lon: position.lon, lat: position.lat });
    }
    exportError = "";
    return exportMowerMap(map, { rotation });
  }

  function switchMode(newMode: "export" | "import") {
    mode = newMode;
    importError = "";
    exportError = "";
    if (mode === "export") {
      jsonText = doExport();
    } else {
      jsonText = "";
    }
  }

  function switchFormat(newFormat: MapFormat) {
    format = newFormat;
    if (mode === "export") {
      jsonText = doExport();
    }
  }

  function copyWithFallback(text: string): boolean {
    if (typeof navigator !== "undefined" && navigator.clipboard) {
      void navigator.clipboard.writeText(text);
      return true;
    }
    if (typeof document === "undefined") {
      return false;
    }
    const ta = document.createElement("textarea");
    ta.value = text;
    ta.setAttribute("readonly", "");
    ta.style.position = "fixed";
    ta.style.left = "-9999px";
    document.body.appendChild(ta);
    ta.focus();
    ta.select();
    let ok = false;
    try {
      ok = document.execCommand("copy");
    } catch {
      ok = false;
    }
    document.body.removeChild(ta);
    return ok;
  }

  function handleCopy() {
    if (copyWithFallback(jsonText)) {
      SaveSuccess.set({ action: "copy mower map", date: new Date() });
    } else {
      exportError = "Could not copy to clipboard. Please use Download or select and copy manually.";
    }
  }

  function handleDownload() {
    const blob = new Blob([jsonText], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = format === "geojson" ? "map.geojson" : "mower-map.json";
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }

  function handleImport() {
    const trimmed = jsonText.trim();
    if (!trimmed) {
      importError = "Paste JSON first.";
      return;
    }

    if (format === "geojson" || isGeoJsonFeatureCollection(trimmed)) {
      const position = get(BackendSettings)?.position;
      const hasReference = position && Number.isFinite(position.lon) && Number.isFinite(position.lat) &&
        (position.lon !== 0 || position.lat !== 0);
      const result = importGeoJson(trimmed, hasReference ? { lon: position.lon, lat: position.lat } : undefined);
      if (!result) {
        importError = hasReference
          ? `Failed to import CaSSAndRA GeoJSON. Check Position reference longitude/latitude (${position.lon}, ${position.lat}).`
          : "Failed to import CaSSAndRA GeoJSON. Configure the reference longitude/latitude in Position settings first.";
        return;
      }
      onImport(result.map, result.rotation, new Date().toISOString(), "CaSSAndRA GeoJSON");
      SaveSuccess.set({ action: "import geojson map", date: new Date() });
      open = false;
      return;
    }

    if (!isValidMowerMap(trimmed)) {
      importError = "Invalid Mower map JSON: at least 3 perimeter points required.";
      return;
    }
    const result = importMowerMap(trimmed);
    if (!result) {
      importError = "Failed to import map.";
      return;
    }
    onImport(result.map, result.rotation, result.dateTime, result.source, result.settings);
    SaveSuccess.set({ action: "import mower map", date: new Date() });
    open = false;
  }

  function handleClose() {
    open = false;
  }
</script>

<Modal
  bind:open
  modalHeading="Mower Map Import / Export"
  primaryButtonText={mode === "import" ? "Import" : "Close"}
  secondaryButtonText={mode === "import" ? "Close" : undefined}
  on:click:button--primary={mode === "import" ? handleImport : handleClose}
  on:click:button--secondary={mode === "import" ? handleClose : undefined}
  on:close={handleClose}
>
  <div class="mode-toggle">
    <Button
      kind={mode === "export" ? "primary" : "tertiary"}
      size="small"
      on:click={() => switchMode("export")}
    >
      Export
    </Button>
    <Button
      kind={mode === "import" ? "primary" : "tertiary"}
      size="small"
      on:click={() => switchMode("import")}
    >
      Import
    </Button>
  </div>

  <div class="format-toggle">
    <Button
      kind={format === "mower" ? "primary" : "tertiary"}
      size="small"
      on:click={() => switchFormat("mower")}
    >
      Grauonline JSON
    </Button>
    <Button
      kind={format === "geojson" ? "primary" : "tertiary"}
      size="small"
      on:click={() => switchFormat("geojson")}
    >
      CaSSAndRA GeoJSON
    </Button>
  </div>

  {#if mode === "export"}
    <div class="export-actions">
      <Button kind="secondary" size="small" on:click={handleCopy} icon={IconCopy}>
        Copy to clipboard
      </Button>
      <Button kind="secondary" size="small" on:click={handleDownload} icon={IconDocumentDownload}>
        Download
      </Button>
    </div>
  {/if}

  {#if exportError}
    <InlineNotification
      kind="error"
      title="Export error"
      subtitle={exportError}
      hideCloseButton
    />
  {/if}

  {#if importError}
    <InlineNotification
      kind="error"
      title="Import error"
      subtitle={importError}
      hideCloseButton
    />
  {/if}

  <TextArea
    labelText={mode === "export" ? "Mower map JSON" : "Paste Mower map JSON"}
    placeholder={mode === "import" ? '{ "perimeter": [{"x":0,"y":0}, ...] }' : ""}
    bind:value={jsonText}
    rows={14}
    disabled={mode === "export"}
  />
</Modal>

<style>
  .mode-toggle {
    display: flex;
    gap: 0.5rem;
    margin-bottom: 1rem;
  }
  .format-toggle {
    display: flex;
    gap: 0.5rem;
    margin-bottom: 1rem;
  }
  .export-actions {
    display: flex;
    gap: 0.5rem;
    margin-bottom: 1rem;
  }
</style>
