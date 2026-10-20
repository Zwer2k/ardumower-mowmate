<script lang="ts">
  import {
    Button,
    Modal,
    TextArea,
    InlineNotification,
  } from "carbon-components-svelte";
  import IconCopy from "carbon-icons-svelte/lib/Copy.svelte";
  import IconDocumentDownload from "carbon-icons-svelte/lib/DocumentDownload.svelte";
  import IconDocumentImport from "carbon-icons-svelte/lib/DocumentImport.svelte";
  import {
    exportMowerMap,
    importMowerMap,
    isValidMowerMap,
    areasFromModem,
    selectAreas,
    toggleArea,
    areaDepth,
    type ExportedArea,
  } from "./core/mower-map";
  import { Checkbox } from "carbon-components-svelte";
  import { socketStore, socketService, areaExportStore } from "../stores/socket";
  import {
    exportGeoJson,
    importGeoJson,
    isGeoJsonFeatureCollection,
  } from "./core/map-formats";
  import type { GeoJsonReference, MapFormat } from "./core/map-formats";
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
    areas?: ExportedArea[],
  ) => void;

  let mode: "export" | "import" = "export";
  let format: MapFormat = "mower";
  let jsonText = "";
  let importError = "";
  let exportError = "";
  let fileInput: HTMLInputElement;
  let fileName = "";

  // Bereiche: beim Export die der geladenen Hauptkarte (vom Modem), beim
  // Import die der Datei; jeweils wählbar
  let areaList: ExportedArea[] = [];
  let areaKeep: boolean[] = [];
  $: currentMapId = $socketStore.currentMapId || "";
  $: currentIsMain = !($socketStore.maps.find((m) => m.id === currentMapId)?.parentId);
  $: hasAreas = currentIsMain && $socketStore.maps.some((m) => m.parentId === currentMapId);

  let wasOpen = false;
  $: if (open && !wasOpen) {
    wasOpen = true;
    mode = "export";
    format = "mower";
    areaList = [];
    areaKeep = [];
    if (hasAreas) socketService.requestAreas(currentMapId);
    jsonText = exportMowerMap(map, { rotation });
    importError = "";
    exportError = "";
    fileName = "";
  }
  $: if (!open) wasOpen = false;

  // Antwort des Modems: alle Bereiche vorauswählen
  $: if (open && mode === "export" && $areaExportStore && $areaExportStore.mapId === currentMapId) {
    const list = areasFromModem($areaExportStore.areas, currentMapId);
    if (list.length !== areaList.length) {
      areaList = list;
      areaKeep = list.map(() => true);
      jsonText = doExport();
    }
  }

  function onToggleArea(i: number, on: boolean) {
    if (areaKeep[i] === on) return; // Carbon meldet "check" auch beim Rendern
    areaKeep = toggleArea(areaList, areaKeep, i, on);
    if (mode === "export") jsonText = doExport();
  }

  // Import: Bereiche der eingefügten bzw. gewählten Datei
  $: if (mode === "import") {
    const parsed = format === "mower" && jsonText.trim() ? importMowerMap(jsonText.trim()) : null;
    const list = parsed?.areas ?? [];
    if (JSON.stringify(list.map((a) => [a.name, a.parent])) !== JSON.stringify(areaList.map((a) => [a.name, a.parent]))) {
      areaList = list;
      areaKeep = list.map(() => true);
    }
  }

  /**
   * Referenz fuer die CaSSAndRA-Umrechnung:
   * - relative: immer (0,0), die ausgeblendeten lon/lat-Werte werden ignoriert
   *   (wie die Firmware, die im Relative-Modus AT+P mit 0/0 sendet)
   * - absolute: konfigurierte lon/lat, undefined wenn nicht gesetzt
   */
  function geoJsonReference(): GeoJsonReference | undefined {
    const position = get(BackendSettings)?.position;
    if (position?.mode !== "absolute") return { lon: 0, lat: 0 };
    const configured = Number.isFinite(position.lon) && Number.isFinite(position.lat) &&
      (position.lon !== 0 || position.lat !== 0);
    return configured ? { lon: position.lon, lat: position.lat } : undefined;
  }

  function doExport(): string {
    if (format === "geojson") {
      const reference = geoJsonReference();
      if (!reference) {
        exportError = "Configure the reference longitude/latitude in Position settings before exporting CaSSAndRA GeoJSON.";
        return "";
      }
      exportError = "";
      return exportGeoJson(map, reference);
    }
    exportError = "";
    return exportMowerMap(map, { rotation, areas: selectAreas(areaList, areaKeep) });
  }

  function switchMode(newMode: "export" | "import") {
    mode = newMode;
    importError = "";
    exportError = "";
    areaList = [];
    areaKeep = [];
    if (newMode === "export" && hasAreas) {
      areaExportStore.set(null);
      socketService.requestAreas(currentMapId);
    }
    if (mode === "export") {
      jsonText = doExport();
    } else {
      jsonText = "";
      fileName = "";
    }
  }

  async function handleFileSelected(event: Event) {
    const input = event.currentTarget as HTMLInputElement;
    const file = input.files?.[0];
    input.value = "";
    if (!file) return;
    try {
      jsonText = await file.text();
      fileName = file.name;
      importError = "";
      format = isGeoJsonFeatureCollection(jsonText.trim()) ? "geojson" : "mower";
    } catch (e) {
      importError = `Could not read file ${file.name}.`;
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
      importError = "Select a file or paste JSON first.";
      return;
    }

    if (format === "geojson" || isGeoJsonFeatureCollection(trimmed)) {
      const absolute = get(BackendSettings)?.position?.mode === "absolute";
      const reference = geoJsonReference();
      const result = importGeoJson(trimmed, reference);
      if (!result) {
        importError = !absolute
          ? "Failed to import CaSSAndRA GeoJSON. The file contains absolute WGS84 coordinates: switch Position mode to Absolute and set the reference longitude/latitude."
          : reference
            ? `Failed to import CaSSAndRA GeoJSON. Check Position reference longitude/latitude (${reference.lon}, ${reference.lat}).`
            : "Failed to import CaSSAndRA GeoJSON. Configure the reference longitude/latitude in Position settings first.";
        return;
      }
      onImport(result.map, result.rotation, new Date().toISOString(), "CaSSAndRA GeoJSON");
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
    onImport(result.map, result.rotation, result.dateTime, result.source, result.settings,
      selectAreas(result.areas, result.areas.map((_, i) => areaKeep[i] ?? true)));
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

  {#if mode === "import"}
    <div class="export-actions">
      <input
        bind:this={fileInput}
        type="file"
        accept=".json,.geojson,application/json,application/geo+json"
        on:change={handleFileSelected}
        hidden
      />
      <Button kind="secondary" size="small" on:click={() => fileInput.click()} icon={IconDocumentImport}>
        Choose file…
      </Button>
      {#if fileName}
        <span class="file-name">{fileName}</span>
      {/if}
    </div>
  {/if}

  {#if format === "mower" && areaList.length > 0}
    <fieldset class="area-select">
      <legend>{mode === "export" ? "Include areas" : "Import areas"}</legend>
      {#each areaList as a, i}
        <div style:padding-left={`${areaDepth(areaList, i) * 1.25}rem`}>
          <Checkbox labelText={a.name} checked={areaKeep[i]} on:check={(e) => onToggleArea(i, e.detail)} />
        </div>
      {/each}
      <p class="area-hint">
        Areas keep their selection and mow settings. Their routes are not included: calculate them again after the import.
      </p>
    </fieldset>
  {:else if mode === "export" && format === "mower" && hasAreas}
    <p class="area-hint">Loading the areas of this map…</p>
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
    labelText={mode === "export" ? "Mower map JSON" : "Paste map JSON or choose a file"}
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
    align-items: center;
    gap: 0.5rem;
    margin-bottom: 1rem;
  }
  .area-select {
    margin-bottom: 1rem;
  }
  .area-select legend {
    font-size: 0.75rem;
    margin-bottom: 0.25rem;
  }
  .area-hint {
    font-size: 0.75rem;
    color: var(--cds-text-02, #525252);
    margin: 0.25rem 0 1rem;
  }
  .file-name {
    font-size: 0.875rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
</style>
