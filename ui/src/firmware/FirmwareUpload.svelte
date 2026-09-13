<script lang="ts">
  import {
    ComposedModal,
    Button,
    ModalHeader,
    ModalFooter,
    ModalBody,
    ProgressBar,
    FileUploaderButton,
    Dropdown,
    InlineNotification,
  } from "carbon-components-svelte";
  import type { Readable } from "svelte/store";
  import { onDestroy } from "svelte";
  import { FirmwareFlashStatus, FirmwareUploader, FirmwareUploadStatus, FirmwareUploadType } from "./service";
  import { checkFirmwareUpdates, firmwareUpdateStore } from "./update-store";
  import {
    firmwareStatusStore,
    flashProgressStore,
    resetFlashProgress,
    socketService,
  } from "../stores/socket";

  export let open: boolean = false;

  let uploadType: FirmwareUploadType = FirmwareUploadType.modem;
  // Startwert ist die lokale Datei: Die GitHub-Quelle kommt erst dazu, wenn das
  // Modem bestätigt hat, dass es github.com selbst erreicht.
  let source: "github" | "file" = "file";
  let sourcePinned = false;
  let checkRequested = false;
  let ref: null | HTMLInputElement;
  let selectedReleaseVersion = "";
  let downloadError: string | null = null;
  let downloading = false;

  const unreachableReason = (error: string | null): string => {
    switch (error) {
      case "wifi-disconnected":
        return "The modem is not connected to a WiFi network. Install from a local file instead.";
      case "clock-not-synced":
        return "The modem clock is not set yet, so it cannot validate the GitHub certificate. Install from a local file instead.";
      case "low-memory":
        return "The modem skipped its scheduled check because memory was tight. Use Check again to try right now.";
      case "update-active":
        return "The modem is busy with another firmware update. Install from a local file instead.";
      case null:
        return "The modem has not checked GitHub yet. Install from a local file instead.";
      default:
        return `The modem could not reach GitHub (${error}). Install from a local file instead.`;
    }
  };

  const githubSourceOption = { id: "github", text: "GitHub Release" };
  const fileSourceOption = { id: "file", text: "Local file" };

  const uploadTypeOptions = [
    { id: FirmwareUploadType.modem, text: "Modem Firmware" },
    { id: FirmwareUploadType.mower, text: "Mower Firmware" }
  ];

  let fileSize = 0;

  let uploader = new FirmwareUploader();

  $: releaseOptions = $firmwareUpdateStore.releases.map((release) => ({
    id: release.version,
    text: `${release.version}${release.version === $firmwareUpdateStore.releases[0]?.version ? " (latest)" : ""}`,
  }));
  $: if (!selectedReleaseVersion && $firmwareUpdateStore.releases.length > 0) {
    selectedReleaseVersion = $firmwareUpdateStore.releases[0].version;
  }

  // Nicht der Browser lädt die Firmware, sondern das Modem – also entscheidet
  // dessen Erreichbarkeit, ob die GitHub-Quelle überhaupt angeboten wird.
  $: githubAvailable = $firmwareStatusStore.reachable === true;
  // Solange das Modem noch kein Ergebnis und keinen Grund gemeldet hat, ist
  // "nicht erreichbar" schlicht falsch – der Hintergrund-Check läuft erst kurz
  // nach dem Start an.
  $: githubChecking =
    $firmwareStatusStore.checking ||
    ($firmwareStatusStore.reachable === null && !$firmwareStatusStore.error);
  $: sourceOptions = githubAvailable
    ? [githubSourceOption, fileSourceOption]
    : [fileSourceOption];
  $: githubUnreachableReason = unreachableReason($firmwareStatusStore.error);
  // Quelle nie unter einem laufenden Upload wegziehen – daran hängt die Anzeige.
  $: sourceSwitchable = $uploaderStatus < FirmwareUploadStatus.uploading;
  $: if (sourceSwitchable && !githubAvailable && source === "github") source = "file";
  $: if (sourceSwitchable && githubAvailable && !sourcePinned && uploadType === FirmwareUploadType.modem) {
    source = "github";
  }

  // Wer den Dialog öffnet, will jetzt updaten und nicht auf den nächsten
  // Hintergrund-Lauf warten – also einmalig sofort nachsehen lassen.
  $: if (open && !$firmwareStatusStore.checked && !$firmwareStatusStore.checking && !checkRequested) {
    checkRequested = true;
    socketService.sendRequestFirmwareStatus(true);
  }
  $: if (!open) checkRequested = false;

  $: if (open && githubAvailable && !$firmwareUpdateStore.loaded && !$firmwareUpdateStore.loading) {
    void checkFirmwareUpdates();
  }

  // Das Modem kennt aus seinem Hintergrund-Check nur die neuste Version. Kommt
  // der Browser selbst nicht an die GitHub-API, bleibt wenigstens die.
  $: espLatestVersion = $firmwareStatusStore.latest;

  function uploadChange(e: CustomEvent<ReadonlyArray<File>>) {
    if (!(ref && ref.files && ref.files.length > 0)) {
      uploader.file = null;
      return;
    }
    const file = ref.files[0];
    uploader.file = file;
    fileSize = file.size;

    if (file !== null) {
      uploader.upload(uploadType);
    }
  }

  function handleUploadTypeChange(e: CustomEvent<{ selectedId: FirmwareUploadType }>) {
    uploadType = e.detail.selectedId;
    if (uploadType === FirmwareUploadType.mower) {
      source = "file";
    }
    resetUploadState();
  }

  function handleSourceChange(e: CustomEvent<{ selectedId: "github" | "file" }>) {
    source = e.detail.selectedId;
    sourcePinned = true;
    resetUploadState();
  }

  async function installVersion(version: string | null) {
    if (!version || downloading) return;

    resetUploadState();
    downloading = true;
    try {
      await uploader.installGithubRelease(version);
    } catch (error) {
      downloadError = error instanceof Error ? error.message : String(error);
    } finally {
      downloading = false;
    }
  }

  function resetUploadState() {
    uploader.file = null;
    fileSize = 0;
    flashProgress = null;
    flashStatus = null;
    flashError = null;
    downloadError = null;
    stopFlashWatch();
    if (ref) {
      ref.value = '';
    }
  }

  async function primary() {
    open = false;
  }

  let flashProgress: number | null = null;
  let flashStatus: FirmwareFlashStatus | null = null;
  let flashError: string | null = null;
  let watchdogTimer: ReturnType<typeof setTimeout> | null = null;
  let flashWatchSource: "modem" | "mower" | null = null;
  let lastFlashTimestamp = 0;

  const FLASH_WATCHDOG_MS = 5 * 60 * 1000; // 5 minutes max for a flash

  function stopWatchdog() {
    if (watchdogTimer) {
      clearTimeout(watchdogTimer);
      watchdogTimer = null;
    }
  }

  function startWatchdog() {
    stopWatchdog();
    watchdogTimer = setTimeout(() => {
      watchdogTimer = null;
      if (flashStatus !== FirmwareFlashStatus.success) {
        console.error('[FirmwareUpload] flash watchdog timeout');
        flashError = 'Zeitüberschreitung beim Flashen';
        flashStatus = FirmwareFlashStatus.error;
        stopFlashWatch();
      }
    }, FLASH_WATCHDOG_MS);
  }

  // Der Flash-Fortschritt kommt über die gemeinsame Socket-Verbindung
  // (flashProgressStore). Eine eigene WebSocket-Verbindung nur für diesen
  // Dialog kostete einen der wenigen Client-Slots des Modems und zusätzlichen
  // Heap – ausgerechnet während des Flashens.
  function startFlashWatch(source: "modem" | "mower") {
    if (flashWatchSource === source) return;
    flashWatchSource = source;
    lastFlashTimestamp = 0;
    resetFlashProgress();
    startWatchdog();
  }

  function stopFlashWatch() {
    flashWatchSource = null;
    lastFlashTimestamp = 0;
    stopWatchdog();
    resetFlashProgress();
  }

  $: {
    const update = $flashProgressStore;
    if (
      update &&
      flashWatchSource === update.source &&
      update.timestamp !== lastFlashTimestamp &&
      flashStatus !== FirmwareFlashStatus.error
    ) {
      lastFlashTimestamp = update.timestamp;
      flashProgress = update.progress;
      if (flashProgress >= 100) {
        flashStatus = FirmwareFlashStatus.success;
        stopWatchdog();
      } else {
        flashStatus = FirmwareFlashStatus.clear;
      }
    }
  }

  function close() {
    const isSuccess =
      ($uploaderStatus === FirmwareUploadStatus.success && uploadType === FirmwareUploadType.modem) ||
      (flashStatus === FirmwareFlashStatus.success && uploadType === FirmwareUploadType.mower);

    if (isSuccess) document.location.reload();
    resetUploadState();
  }

  let uploaderStatus: Readable<FirmwareUploadStatus> = uploader.status;
  let uploaderProgress: Readable<number> = uploader.progress;
  let githubDownloadProgress: Readable<number> = uploader.githubDownloadProgress;
  let githubFlashProgress: Readable<number> = uploader.githubFlashProgress;
  let githubBuffered: Readable<boolean> = uploader.githubBuffered;

  $: if (source === "file" && uploadType === FirmwareUploadType.modem && $uploaderStatus === FirmwareUploadStatus.expectReboot) {
    startFlashWatch("modem");
  } else if (uploadType === FirmwareUploadType.mower && $uploaderStatus === FirmwareUploadStatus.success) {
    startFlashWatch("mower");
  }

  $: if (uploadType === FirmwareUploadType.modem && $uploaderStatus === FirmwareUploadStatus.success) {
    flashProgress = 100;
    flashStatus = FirmwareFlashStatus.success;
    stopWatchdog();
  }

  $: flashInProgress =
    $uploaderStatus === FirmwareUploadStatus.uploading ||
    $uploaderStatus === FirmwareUploadStatus.expectReboot ||
    (uploadType === FirmwareUploadType.mower &&
      $uploaderStatus === FirmwareUploadStatus.success &&
      flashStatus !== FirmwareFlashStatus.success &&
      flashStatus !== FirmwareFlashStatus.error);

  onDestroy(() => {
    stopWatchdog();
  });
</script>

<style>
  :global(.progress-bar-container .bx--progress-bar) {
    width: 100% !important;
  }
  
  :global(.progress-bar-container .bx--progress-bar__track) {
    width: 100% !important;
  }

  /* Fix for 100% progress bar width */
  :global(.progress-bar-container .bx--progress-bar--finished .bx--progress-bar__bar) {
    transform: scaleX(1) !important;
  }

  :global(.progress-bar-container .bx--progress-bar__bar[style*="100"]) {
    transform: scaleX(1) !important;
  }
  
  /* Ensure dropdown doesn't cause scrollbars */
  :global(.bx--modal-container) {
    overflow: visible !important;
  }
  
  :global(.bx--modal-content) {
    overflow: visible !important;
  }
  
  :global(.bx--dropdown__wrapper) {
    z-index: 9999;
  }
</style>

<ComposedModal on:click:button--primary={primary} bind:open on:close={close}>
  <ModalHeader title="Firmware Update" />
  <ModalBody hasForm={true}>
    {#if flashInProgress}
      <InlineNotification kind="warning" title="Do not interrupt power" hideCloseButton lowContrast />
    {/if}
    {#if $uploaderStatus < FirmwareUploadStatus.fileSelected}
      <div style="width: 100%; margin-bottom: 1rem; position: relative; z-index: 1000;">
        <Dropdown
          titleText="Select firmware type"
          items={uploadTypeOptions}
          selectedId={uploadType}
          on:select={handleUploadTypeChange}
          direction="bottom"
        />
      </div>
      {#if uploadType === FirmwareUploadType.modem}
        {#if githubChecking}
          <p style="margin-bottom: 1rem;">Checking whether the modem can reach GitHub...</p>
        {:else if githubAvailable}
          <div style="width: 100%; margin-bottom: 1rem; position: relative; z-index: 999;">
            <Dropdown
              titleText="Update source"
              items={sourceOptions}
              selectedId={source}
              on:select={handleSourceChange}
              direction="bottom"
            />
          </div>
        {:else}
          <InlineNotification
            kind="info"
            title="GitHub not reachable from the modem"
            subtitle={githubUnreachableReason}
            hideCloseButton
            lowContrast
          />
          <Button kind="ghost" on:click={() => socketService.sendRequestFirmwareStatus(true)}>Check again</Button>
        {/if}
      {/if}
    {/if}
    <div style="width: 100%;">
      {#if source === "file" && $uploaderStatus >= FirmwareUploadStatus.fileSelected}
        <div class="progress-bar-container" style="width: 100%; margin-bottom: 1rem;">
          <ProgressBar
            value={$uploaderProgress}
            max={100}
            status={
              $uploaderStatus == FirmwareUploadStatus.error ? 'error' :
              (uploadType === FirmwareUploadType.modem && flashStatus === FirmwareFlashStatus.success) ? 'finished' :
              (uploadType === FirmwareUploadType.mower && $uploaderStatus === FirmwareUploadStatus.success) ? 'finished' :
              (uploadType === FirmwareUploadType.modem && $uploaderStatus === FirmwareUploadStatus.success) ? 'finished' :
              undefined
            }
            helperText="Upload progress"
          />
        </div>
      {/if}
      {#if source === "github" && $uploaderStatus >= FirmwareUploadStatus.uploading && $uploaderStatus < FirmwareUploadStatus.success}
        {#if $githubBuffered}
          <div class="progress-bar-container" style="width: 100%; margin-bottom: 1rem;">
            <ProgressBar
              value={$githubDownloadProgress}
              max={100}
              status={$uploaderStatus === FirmwareUploadStatus.error ? 'error' : $githubDownloadProgress >= 100 ? 'finished' : undefined}
              helperText={`Firmware herunterladen (${Math.round($githubDownloadProgress)}%)`}
            />
          </div>
          {#if $githubDownloadProgress >= 100 || $githubFlashProgress > 0 || $uploaderStatus === FirmwareUploadStatus.expectReboot}
            <div class="progress-bar-container" style="width: 100%; margin-bottom: 1rem;">
              <ProgressBar
                value={$githubFlashProgress}
                max={100}
                status={$uploaderStatus === FirmwareUploadStatus.error ? 'error' : $githubFlashProgress >= 100 ? 'finished' : undefined}
                helperText={$uploaderStatus === FirmwareUploadStatus.expectReboot
                  ? "Firmware geflasht. Warte auf Neustart..."
                  : `Firmware flashen (${Math.round($githubFlashProgress)}%)`}
              />
            </div>
          {/if}
        {:else}
          <div class="progress-bar-container" style="width: 100%; margin-bottom: 1rem;">
            <ProgressBar
              value={$uploaderProgress}
              max={100}
              status={$uploaderStatus === FirmwareUploadStatus.error ? 'error' : undefined}
              helperText={$uploaderStatus === FirmwareUploadStatus.expectReboot
                ? "Firmware geflasht. Warte auf Neustart..."
                : `Firmware herunterladen und flashen (${Math.round($uploaderProgress)}%)`}
            />
          </div>
        {/if}
      {/if}
      {#if flashProgress != null || (uploadType === FirmwareUploadType.mower && $uploaderStatus === FirmwareUploadStatus.success)}
        <div class="progress-bar-container" style="width: 100%; margin-bottom: 1rem;">
          <ProgressBar
            value={flashStatus === FirmwareFlashStatus.success ? 100 : (flashProgress != null ? Math.round(Math.min(Math.max(flashProgress, 0), 100)) : 0)}
            max={100}
            status={flashStatus == FirmwareFlashStatus.success ? 'finished' : flashStatus == FirmwareFlashStatus.error ? 'error' : undefined }
            helperText="Firmware wird geflasht... ({flashStatus === FirmwareFlashStatus.success ? 100 : (flashProgress != null ? Math.round(flashProgress) : 0)}%)"
          />
        </div>
      {/if}
    </div>
    {#if $uploaderStatus < FirmwareUploadStatus.uploading && source === "github"}
      {#if $firmwareUpdateStore.loading}
        <p>Loading releases from GitHub...</p>
      {:else if releaseOptions.length > 0}
        <div class="release-picker">
          <Dropdown
            titleText="Version"
            items={releaseOptions}
            bind:selectedId={selectedReleaseVersion}
            direction="bottom"
          />
          <p>
            Installed: {$firmwareUpdateStore.modemInfo?.git_tag || $firmwareUpdateStore.modemInfo?.git_hash}
            · Target: {$firmwareUpdateStore.modemInfo?.firmware_target}
          </p>
          <Button on:click={() => installVersion(selectedReleaseVersion)} disabled={downloading}>
            Install {selectedReleaseVersion}
          </Button>
        </div>
      {:else if espLatestVersion}
        <InlineNotification
          kind="info"
          title="Release list unavailable in this browser"
          subtitle="This browser cannot reach the GitHub API, so no version can be picked. The modem can still install the latest release it found."
          hideCloseButton
          lowContrast
        />
        <div class="release-picker">
          <p>Installed: {$firmwareStatusStore.current ?? "unknown"} · Latest: {espLatestVersion}</p>
          <Button on:click={() => installVersion(espLatestVersion)} disabled={downloading}>
            Install {espLatestVersion}
          </Button>
        </div>
      {:else if $firmwareUpdateStore.error}
        <InlineNotification
          kind="error"
          title="GitHub releases unavailable"
          subtitle={$firmwareUpdateStore.error}
          hideCloseButton
          lowContrast
        />
        <Button kind="ghost" on:click={() => checkFirmwareUpdates(true)}>Retry</Button>
      {:else}
        <InlineNotification
          kind="warning"
          title="No compatible firmware found"
          subtitle="No release contains firmware for this ESP target."
          hideCloseButton
          lowContrast
        />
      {/if}
      {#if downloadError}
        <InlineNotification kind="error" title="Download failed" subtitle={downloadError} hideCloseButton lowContrast />
      {/if}
    {/if}
    {#if source === "file" && $uploaderStatus < FirmwareUploadStatus.uploading}
      <p>Select the firmware update file on your computer.</p>
      <FileUploaderButton
        bind:ref
        on:change={uploadChange}
        disabled={fileSize > 0}
        accept={[".bin"]}
        labelText="Select..."
      />
    {/if}
    {#if source === "file" && $uploaderStatus >= FirmwareUploadStatus.fileSelected}
      <p>Size: {fileSize} bytes</p>
    {/if}

    {#if $uploaderStatus === FirmwareUploadStatus.uploading}
      <p>{source === "github" ? "Downloading and installing the modem firmware..." : `Uploading the ${uploadType} firmware update...`}</p>
    {/if}
    {#if $uploaderStatus === FirmwareUploadStatus.success && uploadType === FirmwareUploadType.mower && flashStatus !== FirmwareFlashStatus.success}
      <p>The {uploadType} firmware has been uploaded successfully.</p>
      <p>Flashing {uploadType} firmware in progress... {flashError ? `(${flashError})` : ''}</p>
    {/if}
    
    {#if $uploaderStatus === FirmwareUploadStatus.expectReboot && uploadType === FirmwareUploadType.modem}
      <p>The modem firmware was downloaded, verified, and installed successfully.</p>
      <p>Waiting for the modem to restart...</p>
    {/if}

    {#if $uploaderStatus === FirmwareUploadStatus.error}
      <p>The {uploadType} firmware update failed!</p>
      <p>The error message is <i>{uploader.error}</i></p>
    {/if}

    {#if flashStatus === FirmwareFlashStatus.error}
      <p>The {uploadType} firmware flash failed!</p>
      {#if flashError}
        <p>The error message is <i>{flashError}</i></p>
      {/if}
    {/if}

    {#if ($uploaderStatus === FirmwareUploadStatus.success && uploadType === FirmwareUploadType.modem) || 
         (flashStatus === FirmwareFlashStatus.success && uploadType === FirmwareUploadType.mower)}
      <p>The {uploadType} firmware update has been installed successfully.</p>
    {/if}
  </ModalBody>
  <ModalFooter
    secondaryButtonText="Cancel"
    primaryButtonText="Close"
    primaryButtonDisabled={!(
      ($uploaderStatus === FirmwareUploadStatus.success && uploadType === FirmwareUploadType.modem) || 
      (flashStatus === FirmwareFlashStatus.success && uploadType === FirmwareUploadType.mower) ||
      (flashStatus === FirmwareFlashStatus.error)
    )}
  />
</ComposedModal>
