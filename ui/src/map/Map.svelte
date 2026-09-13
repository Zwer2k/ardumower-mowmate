<script lang="ts">
  import { TextInput, Dropdown, Row, Grid, Loading, ProgressBar } from "carbon-components-svelte";
  import { onMount, tick } from "svelte";
  import Canvas from "./Canvas.svelte";
  import Exclusion from "./Exclusion.svelte";
  import Track from "./Track.svelte";
  import Perimeter from "./Perimeter.svelte";
  import Waypoints from "./Waypoints.svelte";
  import Dockpoints from "./Dockpoints.svelte";
  import MowerPosition from "./MowerPosition.svelte";
  import Obstacles from "./Obstacles.svelte";
  import { MapStore, cloneMap, buildMapSetData, calculatePresentation } from "./service";
  import { socketStore, socketService } from "../stores/socket";
  import { mowSettingsStore } from "./mow-settings";
  import { filterWaypointsByToggles } from "./core/waypoint-filter";
  import { openConfirm, openConfirmChoice } from "../stores/confirm-dialog";
  import { mapWorkflowStore, isMapDirty } from "./map-workflow";
  import { isMowerMapSynced, setMapDirty } from "./services/map-sync";
  import { get } from "svelte/store";
  import MowSettingsDialog from "./MowSettingsDialog.svelte";
  import MapToolbar from "./toolbar/MapToolbar.svelte";
  import MapManagementToolbar from "./toolbar/MapManagementToolbar.svelte";
  import MapEditToolbar from "./toolbar/MapEditToolbar.svelte";
  import MapCalculateToolbar from "./toolbar/MapCalculateToolbar.svelte";
  import MapScheduleToolbar from "./toolbar/MapScheduleToolbar.svelte";
  import MowerMapDialog from "./MowerMapDialog.svelte";
  import MapStatusOverlay from "./overlay/MapStatusOverlay.svelte";
  import MapGotoOverlay from "./overlay/MapGotoOverlay.svelte";
  import {
    categoryFromEditItemId,
    itemBelongsToCategory,
    deletePointByEditItemId,
    splitEdgeByEditItemId,
    addPointAtMowerPosition,
    startDrawMode,
    moveFloatingPoint,
    placeFloatingPoint,
    findCrossedCandidate,
    switchToCandidate,
    findPointIndex,
    removeFloatingPoint,
    buildPointId,
    buildInitialCandidates,
  } from "./interactions/map-edit";
  import { createGotoState } from "./interactions/map-goto";
  import { createCompassState } from "./interactions/map-compass";
  import {
    recordMapSnapshot,
    undoMapEdit,
    redoMapEdit,
    resetMapHistory,
    canUndoMapEdit,
    canRedoMapEdit,
  } from "./interactions/map-history";
  import { SaveSuccess } from "../stores/success";
  import { currentMapRotationStore, mapEditLock } from "./service";
  import { mapChunkProgress } from "./map-chunk-buffer";
  import type { Point, MapArea } from "./model";
  import type { MowSettingsData } from "../model";
  import { gamepadStore, GamepadButton } from "../stores/gamepad";
  import { gamepadMode } from "../stores/gamepad-mode";
  import { remoteControlOpen } from "../stores/remote-control";
  import { browser } from "$app/environment";
  import { onDestroy } from "svelte";

  let edit = false;
  let wasEditing = false;
  let showMowSettings = false;
  let showMowerMapDialog = false;
  let editItemId: string | null = null;
  let mapSyncTimer: ReturnType<typeof setTimeout> | null = null;
  let skipNextMapSync = true;
  let lastSyncedMap = "";
  const categoryOptions: { id: string; text: string }[] = [
    { id: "perimeter", text: "Edit perimeter" },
    { id: "dockpoints", text: "Edit docking points" },
    { id: "exclusion", text: "Edit exclusions" },
    { id: "waypoints", text: "Edit waypoints" },
  ];

  function defaultMapName(): string {
    return `Map ${$socketStore.maps.length + 1}`;
  }

  let editCategory: MapArea = "perimeter";

  let editPoint = false;
  let editEdge = false;

  $: editPoint = edit && !!editItemId && editItemId.indexOf("-point-") !== -1;
  $: editEdge = edit && !!editItemId && editItemId.indexOf("-edge-") !== -1;
  $: selectedExclusionMatch = editItemId?.match(/-exclusion-([0-9]+)/);
  $: selectedExclusionIndex = selectedExclusionMatch ? parseInt(selectedExclusionMatch[1]) : null;
  $: hasMowerFix = !!mowerPos && !(mowerPos.x === 0 && mowerPos.y === 0);
  $: canAdd = edit && !drawActive && hasMowerFix && (
    editEdge ||
    editPoint ||
    (editCategory === "perimeter" && perimeterPoints <= 1) ||
    (editCategory === "dockpoints" && dockpointsPoints <= 1) ||
    (editCategory === "waypoints" && waypointsPoints <= 1) ||
    (editCategory === "exclusion" && (
      ($MapStore.map?.exclusions.length ?? 0) === 0 ||
      (selectedExclusionIndex !== null && ($MapStore.map?.exclusions[selectedExclusionIndex].points.length ?? 0) <= 1)
    ))
  );
  $: canCreateExclusion = edit && !drawActive && hasMowerFix && editCategory === "exclusion";
  $: canDeleteExclusion = edit && !drawActive && editCategory === "exclusion" && selectedExclusionIndex !== null;

  // ─── Map management ────────────────────────────────────────────────────────
  let showManage = false;
  let showCalculate = false;
  let showSchedule = false;
  let selectedMapId: string = "";
  let dropdownSelectedId: string = "";

  function closePanels() {
    showManage = false;
    edit = false;
    showCalculate = false;
    showSchedule = false;
    editItemId = null;
    stopDraw();
  }

  function toggleManage() {
    if (showManage) {
      closePanels();
    } else {
      showManage = true;
      edit = false;
      showCalculate = false;
      showSchedule = false;
      stopDraw();
    }
  }

  function toggleEdit() {
    if (edit) {
      closePanels();
    } else {
      stopDraw();
      edit = true;
      showManage = false;
      showCalculate = false;
      showSchedule = false;
    }
  }

  function toggleCalculate() {
    if (showCalculate) {
      closePanels();
    } else {
      showCalculate = true;
      showManage = false;
      showSchedule = false;
      edit = false;
      stopDraw();
    }
  }

  function toggleSchedule() {
    if (showSchedule) {
      closePanels();
    } else {
      showSchedule = true;
      showManage = false;
      showCalculate = false;
      edit = false;
      stopDraw();
    }
  }

  function stopEditForMapChange() {
    if (mapSyncTimer) {
      clearTimeout(mapSyncTimer);
      mapSyncTimer = null;
    }
    // The snapshots belong to the map being left.
    resetMapHistory();
    edit = false;
    wasEditing = false;
    editItemId = null;
    stopDraw();
  }

  $: mapOptions = (() => {
    const seen = new Set<string>();
    const opts: { id: string; text: string }[] = [];
    const isNew = $socketStore.isNewMap || $mapWorkflowStore.state === "creating" || $mapWorkflowStore.state === "intercepting";
    if (isNew) {
      const name = $mapWorkflowStore.pendingName || defaultMapName();
      opts.push({
        id: "__unsaved__",
        text: `${name} ● (unsaved)`,
      });
    }
    for (const m of $socketStore.maps) {
      if (seen.has(m.id)) continue;
      seen.add(m.id);
      opts.push({
        id: m.id,
        // "●" marks a map with unsaved changes, so drafts stay recognizable
        // after switching away from them.
        text: `${m.name}${m.unsaved ? ' ●' : ''} (${m.area.toFixed(1)} m²)${m.id === $socketStore.activeMapId ? ' ★ default' : ''}`,
      });
    }
    return opts;
  })();

  $: {
    const isNew = $socketStore.isNewMap || $mapWorkflowStore.state === "creating" || $mapWorkflowStore.state === "intercepting";
    const baseId = isNew ? "__unsaved__" : ($socketStore.currentMapId || "");
    if (!$mapWorkflowStore.renameMode && $mapWorkflowStore.state !== "loading") {
      selectedMapId = baseId;
      dropdownSelectedId = baseId;
    }
  }

  // Undo history belongs to one map. Reset it whenever the backend hands us a
  // different map — a user-initiated switch goes through stopEditForMapChange(),
  // but a schedule run or an intercepted transfer does not.
  let historyMapId: string | null = null;
  $: {
    const id = $socketStore.currentMapId || "";
    if (historyMapId !== id) {
      historyMapId = id;
      resetMapHistory();
    }
  }

  $: effectiveMapId = $socketStore.currentMapId || "";
  $: effectiveMap = $socketStore.maps.find((m) => m.id === effectiveMapId);
  $: effectiveMapName = effectiveMap?.name || "";
  $: isDirty = $isMapDirty;
  $: selectedIsCurrentMap = selectedMapId === (effectiveMapId || "__unsaved__");
  $: canSave = isDirty && selectedIsCurrentMap && !$mapWorkflowStore.renameMode && !workflowBusy;
  $: canRevert = isDirty && selectedIsCurrentMap && !$mapWorkflowStore.renameMode && !workflowBusy;
  $: canRename = !!effectiveMapId;
  $: workflowBusy = $mapWorkflowStore.state === "loading" || $mapWorkflowStore.state === "saving" || $mapWorkflowStore.state === "renaming" || $mapWorkflowStore.state === "deleting";

  onMount(() => {
    socketService.sendListMaps();
    const unsubscribe = MapStore.subscribe(({ map }) => {
      if (skipNextMapSync) {
        skipNextMapSync = false;
        return;
      }
      if (!edit) return;
      if (mapSyncTimer) clearTimeout(mapSyncTimer);
      mapSyncTimer = setTimeout(() => {
        mapSyncTimer = null;
        if (edit && get(isMapDirty)) {
          const mapData = buildMapSetData(get(MapStore).map, compassRotation);
          const serializedMap = JSON.stringify(mapData);
          if (serializedMap !== lastSyncedMap) {
            lastSyncedMap = serializedMap;
            socketService.sendMap(mapData);
          }
        }
      }, 250);
    });
    onDestroy(() => {
      if (mapSyncTimer) clearTimeout(mapSyncTimer);
      unsubscribe();
    });
  });

  async function onSelectMap(e: CustomEvent) {
    const id = e.detail?.selectedId;
    if (!id) return;
    if (id === "__unsaved__") {
      selectedMapId = id;
      dropdownSelectedId = id;
      return;
    }
    if ($mapWorkflowStore.state === "loading" && id === $mapWorkflowStore.pendingLoadId) return;

    const dirty = get(isMapDirty);
    let discardCurrent = false;
    if (dirty && id !== effectiveMapId && !!effectiveMapId) {
      const choice = await openConfirmChoice({
        title: "Unsaved changes",
        message: "Save the changes before switching to another map?",
        confirmText: "Save",
        cancelText: "Discard",
        dismissText: "Cancel",
      });
      if (choice === "dismiss") {
        // Escape/X/"Cancel" used to discard the changes. Reset the dropdown
        // selection and stay on the current map instead.
        const keep = selectedMapId;
        selectedMapId = "";
        await tick();
        selectedMapId = keep;
        dropdownSelectedId = keep;
        return;
      }
      if (choice === "confirm") {
        socketService.sendMap(buildMapSetData(get(MapStore).map, compassRotation));
        onSaveMap();
      } else {
        mapWorkflowStore.resetDirtyState();
        discardCurrent = true;
      }
    }
    selectedMapId = id;
    dropdownSelectedId = id;
    stopEditForMapChange();
    mapWorkflowStore.startLoadMap(id);
    socketService.sendLoadMap(id, discardCurrent);
  }

  async function onNewMap() {
    if (isDirty && selectedIsCurrentMap) {
      const choice = await openConfirmChoice({
        title: "Unsaved changes",
        message: "Save the changes before creating a new map?",
        confirmText: "Save",
        cancelText: "Discard",
        dismissText: "Cancel",
      });
      if (choice === "dismiss") return;
      if (choice === "confirm") {
        socketService.sendMap(buildMapSetData(get(MapStore).map, compassRotation));
        onSaveMap();
      } else {
        mapWorkflowStore.resetDirtyState();
        socketService.sendDiscardMap();
      }
    }
    stopEditForMapChange();
    const defaultName = defaultMapName();
    currentMapRotationStore.set(0);
    mapWorkflowStore.startNewMap(defaultName);
    socketService.sendCreateMap(defaultName);
    showManage = true;
  }

  function onCopyMap() {
    if (!effectiveMapId) return;
    socketService.sendCopyMap(`${effectiveMapName || "Map"} copy`);
  }

  $: if (
    ($socketStore.currentMapMeta && !$socketStore.currentMapId) &&
    !$mapWorkflowStore.renameMode &&
    $mapWorkflowStore.state !== "loading" &&
    $mapWorkflowStore.state !== "saving" &&
    $mapWorkflowStore.state !== "deleting" &&
    !$socketStore.isLoadingMap &&
    $mapWorkflowStore.pendingName === ""
  ) {
    mapWorkflowStore.startNewMap(defaultMapName());
    showManage = true;
  }

  function onSaveMap() {
    if (mapSyncTimer) {
      clearTimeout(mapSyncTimer);
      mapSyncTimer = null;
    }
    if ($isMapDirty) {
      const mapData = buildMapSetData(get(MapStore).map, compassRotation);
      lastSyncedMap = JSON.stringify(mapData);
      socketService.sendMap(mapData);
    }
    const name = $mapWorkflowStore.pendingName || effectiveMapName || defaultMapName();
    mapWorkflowStore.startSaveMap(name, compassRotation);
    socketService.sendSaveMap(name, compassRotation);
  }

  function onUploadMap() {
    if (mapSyncTimer) {
      clearTimeout(mapSyncTimer);
      mapSyncTimer = null;
    }
    const mapData = buildMapSetData(get(MapStore).map, compassRotation);
    lastSyncedMap = JSON.stringify(mapData);
    socketService.sendMapAndUpload(mapData);
  }

  async function onDiscardMap() {
    const target = selectedMapId || dropdownSelectedId || effectiveMapId;
    if (!target) return;
    const message = target === "__unsaved__" || $socketStore.isNewMap
      ? "Discard the new map? Unsaved changes will be lost."
      : "Discard the changes and reload the saved version of this map?";
    const choice = await openConfirm({
      title: "Discard changes",
      message,
      confirmText: "Discard",
      cancelText: "Cancel",
      kind: "danger",
    });
    if (!choice) return;
    stopDraw();
    mapWorkflowStore.resetDirtyState();
    socketService.sendDiscardMap();
    if (target === "__unsaved__" || $socketStore.isNewMap) {
      // Das Backend ersetzt eine verworfene neue Karte durch eine leere Karte
      // ohne ID; ohne Folge-Load landete man sofort wieder im Namensdialog
      // der nächsten neuen Karte. Stattdessen zur Default-Karte zurück.
      const fallbackId = $socketStore.activeMapId || $socketStore.maps.find((m) => m.id !== target)?.id;
      if (fallbackId) {
        stopEditForMapChange();
        selectedMapId = fallbackId;
        dropdownSelectedId = fallbackId;
        mapWorkflowStore.startLoadMap(fallbackId);
        socketService.sendLoadMap(fallbackId);
      }
    }
  }

  function startRename() {
    mapWorkflowStore.startRename(effectiveMapName);
  }

  function confirmRename() {
    const currentMapId = $socketStore.currentMapId || undefined;
    const result = mapWorkflowStore.confirmRename(currentMapId, effectiveMapName);
    if (result.action === "save") {
      socketService.sendSaveMap(result.name, compassRotation);
    } else if (result.action === "rename") {
      socketService.sendRenameMap($socketStore.currentMapId, result.name);
    } else if (result.action === "edit") {
      edit = true;
      showManage = false;
      showCalculate = false;
      showSchedule = false;
      stopDraw();
    }
  }

  function cancelRename() {
    if ($mapWorkflowStore.state === "creating") {
      // Aborting the initial name dialog of a brand-new map discards it.
      // Select a fallback map locally before resetting the workflow, so the
      // reactive dropdown block sees a valid currentMapId and keeps it.
      const fallbackId = $socketStore.activeMapId || $socketStore.maps[0]?.id;
      const fallbackMap = $socketStore.maps.find((m) => m.id === fallbackId);
      if (fallbackId && fallbackMap) {
        socketStore.update((s) => ({
          ...s,
          currentMapId: fallbackId,
          currentMapMeta: {
            hash: fallbackMap.hash,
            crc: fallbackMap.crc,
            area: fallbackMap.area,
            rotation: fallbackMap.rotation,
          },
          isNewMap: false,
          currentMapUnsaved: false,
        }));
      }
      mapWorkflowStore.resetDirtyState();
      // Close management panel and stop any active editing/drawing.
      showManage = false;
      edit = false;
      stopDraw();
      // If a backend exists, also request the fallback map so the local
      // preview and backend state stay in sync.
      if (fallbackId) {
        selectedMapId = fallbackId;
        dropdownSelectedId = fallbackId;
        mapWorkflowStore.startLoadMap(fallbackId);
        socketService.sendLoadMap(fallbackId);
      }
      return;
    }
    const fallbackName = effectiveMapName || $mapWorkflowStore.pendingName || defaultMapName();
    mapWorkflowStore.cancelRename(fallbackName);
  }

  async function onDeleteMap() {
    const target = dropdownSelectedId || effectiveMapId;
    if (!target || target === "__unsaved__") return;
    const choice = await openConfirm({
      title: "Delete map",
      message: "Really delete this map? This cannot be undone.",
      confirmText: "Delete",
      cancelText: "Cancel",
      kind: "danger",
    });
    if (!choice) return;
    // The backend performs the deletion and switches to the first remaining
    // map. It then sends an updated mapList, which refreshes socketStore and
    // the dropdown.
    mapWorkflowStore.startDeleteMap(target);
    socketService.sendDeleteMap(target);
  }

  function onSetDefaultMap() {
    const target = dropdownSelectedId || effectiveMapId;
    if (!target || target === "__unsaved__") return;
    socketService.sendSetActiveMap(target);
  }

  function onImportMowerMap(
    map: import("./model").Map,
    rotation: number,
    dateTime?: string,
    source?: string,
    settings?: Partial<import("../model").MowSettingsData>,
  ) {
    currentMapRotationStore.set(((rotation % 360) + 360) % 360);
    MapStore.set({ map, presentation: calculatePresentation(map, 0) });
    if (settings) {
      socketService.sendMowSettings(settings as MowSettingsData);
    }
    // MowerMap::fromJson() (backend) expects the same X/Y convention as the
    // persisted map format (Y not flipped), unlike the setMap handler which
    // flips Y itself. Convert frontend points (Y-up) back to that convention
    // here, otherwise the imported map ends up mirrored on the Y axis.
    const toBackendPoint = (p: Point) => ({ ...p, X: p.x, Y: -p.y, x: undefined, y: undefined });
    socketService.sendImportMap(
      JSON.stringify({
        dateTime: dateTime ?? new Date().toISOString(),
        source: source ?? "MowMate",
        perimeter: map.perimeter.points.map(toBackendPoint),
        exclusions: map.exclusions.map((e) => e.points.map(toBackendPoint)),
        dockpoints: map.dockpoints.points.map(toBackendPoint),
        searchWire: map.searchWire.points.map(toBackendPoint),
        waypoints: map.waypoints.points.map(toBackendPoint),
        rotation,
      }),
      $mapWorkflowStore.pendingName || effectiveMapName || defaultMapName(),
      rotation,
    );
  }

  // ─── Point counts & sync state ─────────────────────────────────────────────
  $: busy = !!$socketStore.state?.progressOp;
  $: perimeterPoints = $MapStore.map?.perimeter.points.length ?? 0;
  $: exclusionPoints = $MapStore.map?.exclusions.map((e) => e.points.length) ?? [];
  $: dockpointsPoints = $MapStore.map?.dockpoints.points.length ?? 0;
  $: rawWaypoints = $MapStore.map?.waypoints.points ?? [];
  $: filteredWaypoints = $MapStore.map ? filterWaypointsByToggles(rawWaypoints, $MapStore.map, $mowSettingsStore) : [];
  $: waypointsPoints = rawWaypoints.length;
  $: totalPoints = perimeterPoints + dockpointsPoints + waypointsPoints + exclusionPoints.reduce((a, b) => a + b, 0);

  // ─── Edit/draw state ───────────────────────────────────────────────────────
  let drawActive = false;
  let drawArea: 'perimeter' | 'exclusion' | 'dockpoints' | 'waypoints' | null = null;
  let drawExclusionIndex: number | undefined = undefined;
  let drawCandidates: Array<{
    area: 'perimeter' | 'exclusion' | 'dockpoints' | 'waypoints';
    exclusionIndex?: number;
    begin: Point;
    end: Point;
  }> = [];
  let floatingPoint: Point | null = null;

  function selectEditCategory(e: CustomEvent) {
    const id = e.detail?.selectedId as MapArea | undefined;
    if (!id) return;
    editCategory = id;
    editItemId = null;
    stopDraw();
  }

  function onDeleteClick() {
    if (!editItemId) return;
    deletePointByEditItemId(editItemId);
  }

  function selectNewPoint(id: string) {
    editItemId = id;
  }

  function onSplitClick() {
    if (!editItemId) return;
    const newPointId = splitEdgeByEditItemId(editItemId);
    if (newPointId) {
      selectNewPoint(newPointId);
    }
  }

  function onAddClick() {
    if (!mowerPos) return;
    if (editCategory === "exclusion" && !editItemId && ($MapStore.map?.exclusions.length ?? 0) === 0) {
      onCreateExclusionClick();
      return;
    }
    const index = addPointAtMowerPosition(
    mowerPos,
    editItemId,
    editEdge,
    editPoint,
    editCategory
  );
    if (index !== null) {
      const newPointId = buildPointId(editItemId, index);
      if (newPointId) selectNewPoint(newPointId);
    }
  }

  function onCreateExclusionClick() {
    if (!mowerPos) return;
    recordMapSnapshot();
    MapStore.update((store) => {
      const map = { ...store.map };
      map.exclusions = [
        ...map.exclusions,
        {
          points: [{
            x: mowerPos.x,
            y: -mowerPos.y,
            delta: mowerPos.delta,
            timestamp: new Date().toISOString(),
            sol: mowerPos.solution,
          }],
        },
      ];
      return { ...store, map };
    });
    setMapDirty(true);
    const newIndex = ($MapStore.map?.exclusions.length ?? 1) - 1;
    editItemId = "map-0-exclusion-" + newIndex + "-point-0";
  }

  function onDeleteExclusionClick() {
    if (!editItemId || editCategory !== "exclusion") return;
    const match = editItemId.match(/-exclusion-([0-9]+)/);
    if (!match) return;
    const exclusionIndex = parseInt(match[1]);
    recordMapSnapshot();
    MapStore.update((store) => {
      const map = { ...store.map };
      map.exclusions = map.exclusions.filter((_, i) => i !== exclusionIndex);
      return { ...store, map };
    });
    setMapDirty(true);
    editItemId = null;
  }

  // ─── Gamepad map edit control ─────────────────────────────────────────────
  let gamepadUnsubscribe: (() => void) | null = null;
  let lastGamepadButtons: Record<number, number> = {};
  let gamepadMoveInterval: ReturnType<typeof setInterval> | null = null;
  let lastSelectedPointPos: Point | null = null;
  let gamepadGestureActive = false;

  // Set gamepad mode based on edit state: map-edit when drawing, point selected, or edge selected (for Add button)
  $: if (browser) {
    if (drawActive) {
      gamepadMode.set('map-edit');
    } else if (edit && editItemId && (editItemId.includes('-point-') || editItemId.includes('-edge-'))) {
      gamepadMode.set('map-edit');
    } else {
      gamepadMode.set('drive');
    }
  }

  function handleGamepadButton(buttonIdx: number) {
    if ($remoteControlOpen) return; // RC dialog open = drive only
    if (!edit) return;

    if (drawActive && floatingPoint) {
      // Draw mode: B (Circle) = place point, A/X = cancel
      if (buttonIdx === GamepadButton.B) {
        onDrawMapClick(new CustomEvent('mapclick', { detail: { x: floatingPoint.x, y: floatingPoint.y } }));
      } else if (buttonIdx === GamepadButton.A || buttonIdx === GamepadButton.X) {
        stopDraw();
      }
    } else if (editItemId && !drawActive) {
      // Point or edge selected: B (Circle) = add point at mower position (like UI Add button)
      if (buttonIdx === GamepadButton.B) {
        if (mowerPos) {
          onAddClick();
        }
      }
    }
  }

  function moveSelectedPoint(dx: number, dy: number) {
    if (!editItemId || !editItemId.includes('-point-')) return;
    // Extract point index from editItemId (format: "map-0-<category>-point-<index>")
    const match = editItemId.match(/-point-(\d+)$/);
    if (!match) return;
    const pointIdx = parseInt(match[1]);
    if (isNaN(pointIdx)) return;

    MapStore.update((store) => {
      const map = { ...store.map };
      const category = categoryFromEditItemId(editItemId);
      if (!category) return store;

      let points: Point[] | null = null;
      if (category === 'perimeter') {
        points = [...map.perimeter.points];
        map.perimeter = { ...map.perimeter, points };
      } else if (category === 'dockpoints') {
        points = [...map.dockpoints.points];
        map.dockpoints = { ...map.dockpoints, points };
      } else if (category === 'waypoints') {
        points = [...map.waypoints.points];
        map.waypoints = { ...map.waypoints, points };
      } else if (category === 'exclusion') {
        const exMatch = editItemId.match(/-exclusion-(\d+)/);
        if (exMatch) {
          const exIdx = parseInt(exMatch[1]);
          if (map.exclusions[exIdx]) {
            map.exclusions = [...map.exclusions];
            map.exclusions[exIdx] = { ...map.exclusions[exIdx], points: [...map.exclusions[exIdx].points] };
            points = map.exclusions[exIdx].points;
          }
        }
      }
      if (!points || pointIdx >= points.length) return store;

      points[pointIdx] = { ...points[pointIdx], x: points[pointIdx].x + dx, y: points[pointIdx].y + dy };
      setMapDirty(true);
      return { ...store, map };
    });
  }

  function gamepadEditLoop() {
    if ($remoteControlOpen) return;
    if (!edit) return;
    const gp = $gamepadStore;
    if (!gp.connected || gp.index === null) return;

    const moveScale = 0.02;
    // Use raw stick axes for map movement (not the transformed drive values)
    // rawX = left stick horizontal, rawY = left stick vertical (both -1..1)
    const rawDx = gp.rawX;
    const rawDy = gp.rawY;
    const magnitude = Math.sqrt(rawDx * rawDx + rawDy * rawDy);
    if (magnitude < 0.15) {
      // Deadzone: stop movement when stick released, and end the undo gesture.
      gamepadGestureActive = false;
      return;
    }
    if (!gamepadGestureActive) {
      gamepadGestureActive = true;
      recordMapSnapshot();
    }

    const dx = rawDx * moveScale;
    const dy = rawDy * moveScale;

    if (drawActive && floatingPoint) {
      // Draw mode: move floating point
      onMouseMove(new CustomEvent('mousemove', { detail: { x: floatingPoint.x + dx, y: floatingPoint.y + dy } }));
    } else if (editItemId && editItemId.includes('-point-') && !drawActive) {
      // Point selected: move selected point
      moveSelectedPoint(dx, dy);
    }
    // Edge selected: no movement, only Add button works
  }

  $: if (browser && edit && !$remoteControlOpen) {
    // Subscribe to gamepad when in edit mode and RC dialog is closed
    if (!gamepadUnsubscribe) {
      gamepadUnsubscribe = gamepadStore.subscribe((gp) => {
        for (const [idxStr, timestamp] of Object.entries(gp.lastButtonPress)) {
          const idx = parseInt(idxStr);
          if (!lastGamepadButtons[idx] || timestamp > lastGamepadButtons[idx]) {
            lastGamepadButtons[idx] = timestamp;
            handleGamepadButton(idx);
          }
        }
      });
      gamepadMoveInterval = setInterval(gamepadEditLoop, 50);
    }
  } else {
    if (gamepadUnsubscribe) {
      gamepadUnsubscribe();
      gamepadUnsubscribe = null;
    }
    if (gamepadMoveInterval) {
      clearInterval(gamepadMoveInterval);
      gamepadMoveInterval = null;
    }
    lastGamepadButtons = {};
    gamepadGestureActive = false;
  }

  onDestroy(() => {
    if (gamepadUnsubscribe) gamepadUnsubscribe();
    if (gamepadMoveInterval) clearInterval(gamepadMoveInterval);
    mapEditLock.set(false);
  });

  function onDrawClick() {
    if (drawActive) {
      stopDraw();
      return;
    }
    if (!editItemId) return;
    const result = startDrawMode(editItemId);
    if (!result) return;
    drawActive = true;
    drawArea = result.area;
    drawExclusionIndex = result.exclusionIndex;
    floatingPoint = result.midPoint;
    drawCandidates = buildInitialCandidates(
      result.area,
      result.exclusionIndex,
      result.begin,
      result.end,
      result.midPoint
    );
    selectNewPoint(result.newPointId);
  }

  function stopDraw() {
    if (!drawActive) return;
    if (floatingPoint && drawArea) {
      removeFloatingPoint(drawArea, drawExclusionIndex, floatingPoint);
    }
    drawActive = false;
    drawArea = null;
    drawExclusionIndex = undefined;
    drawCandidates = [];
    floatingPoint = null;
  }

  function doUndo() {
    if (!edit) return;
    // A half-finished draw gesture would leave a floating point behind.
    if (drawActive) stopDraw();
    if (undoMapEdit()) editItemId = null;
  }

  function doRedo() {
    if (!edit) return;
    if (drawActive) stopDraw();
    if (redoMapEdit()) editItemId = null;
  }

  function isTypingTarget(target: EventTarget | null): boolean {
    const el = target as HTMLElement | null;
    if (!el || !el.tagName) return false;
    const tag = el.tagName.toUpperCase();
    return tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT" || el.isContentEditable;
  }

  function onKeyDown(event: KeyboardEvent) {
    // Never steal keys from the rename field or any other text input.
    if (isTypingTarget(event.target)) return;

    const mod = event.ctrlKey || event.metaKey;
    const key = event.key.toLowerCase();
    // Redo first: Ctrl+Shift+Z also matches the undo combo.
    if (mod && (key === "y" || (key === "z" && event.shiftKey))) {
      event.preventDefault();
      doRedo();
      return;
    }
    if (mod && key === "z") {
      event.preventDefault();
      doUndo();
      return;
    }
    if (event.key === "Escape") {
      if (drawActive) stopDraw();
      else if (editItemId) editItemId = null;
      return;
    }
    if (edit && !drawActive && editPoint && (event.key === "Delete" || event.key === "Backspace")) {
      event.preventDefault();
      onDeleteClick();
    }
  }

  onMount(() => {
    window.addEventListener("keydown", onKeyDown);
    return () => {
      window.removeEventListener("keydown", onKeyDown);
    };
  });

  function onDrawMapClick(event: CustomEvent<{ x: number; y: number }>) {
    if (!drawActive || !floatingPoint || drawCandidates.length === 0) return;
    const { x, y } = event.detail;

    const crossed = findCrossedCandidate(
      x,
      y,
      drawArea,
      drawExclusionIndex,
      floatingPoint,
      drawCandidates
    );
    if (crossed) {
      const switched = switchToCandidate(
        crossed,
        floatingPoint,
        drawArea,
        drawExclusionIndex,
        drawCandidates
      );
      if (switched) {
        drawArea = switched.drawArea;
        drawExclusionIndex = switched.drawExclusionIndex;
        drawCandidates = switched.drawCandidates;
        floatingPoint = switched.floatingPoint;
      }
    }

    const placeResult = placeFloatingPoint(
      x,
      y,
      drawArea,
      drawExclusionIndex,
      floatingPoint,
      drawCandidates
    );
    if (placeResult) {
      drawArea = placeResult.drawArea;
      drawExclusionIndex = placeResult.drawExclusionIndex;
      drawCandidates = placeResult.drawCandidates;
      floatingPoint = placeResult.floatingPoint;
      const idx = findPointIndex(drawArea, drawExclusionIndex, floatingPoint);
      const newPointId = buildPointId(editItemId, idx);
      if (newPointId) selectNewPoint(newPointId);
    }
  }

  let mouseMapPos: Point | null = null;

  function onMouseMove(event: CustomEvent<{ x: number; y: number }>) {
    const { x, y } = event.detail;
    mouseMapPos = { x, y };

    if (!drawActive || !floatingPoint) return;

    const crossed = findCrossedCandidate(
      x,
      y,
      drawArea,
      drawExclusionIndex,
      floatingPoint,
      drawCandidates
    );
    if (crossed) {
      const switched = switchToCandidate(
        crossed,
        floatingPoint,
        drawArea,
        drawExclusionIndex,
        drawCandidates
      );
      if (switched) {
        drawArea = switched.drawArea;
        drawExclusionIndex = switched.drawExclusionIndex;
        drawCandidates = switched.drawCandidates;
        floatingPoint = switched.floatingPoint;
      }
    }

    const moveResult = moveFloatingPoint(
      x,
      y,
      drawArea,
      drawExclusionIndex,
      floatingPoint,
      drawCandidates
    );
    if (moveResult) {
      floatingPoint = moveResult.newFloatingPoint;
      drawCandidates = moveResult.newCandidates;
    }
  }

  // ─── Sync-Status: CRC aus Metadaten ────────────────────────────────────────
  $: hasState = $socketStore.state !== null;
  $: storedCrc = $socketStore.currentMapMeta?.crc ?? 0;
  $: sync = { needsUpload: hasState && !isMowerMapSynced($socketStore.state, $socketStore.currentMapId, storedCrc) };

  // Während des Editierens dürfen eingehende Map-Transfers (Echo des eigenen
  // setMap) den lokalen Zustand nicht überschreiben, siehe mapEditLock.
  $: mapEditLock.set(edit);

  $: if (!edit && wasEditing && $MapStore && $MapStore.map) {
    if (mapSyncTimer) {
      clearTimeout(mapSyncTimer);
      mapSyncTimer = null;
    }
    wasEditing = false;
    if ($isMapDirty) {
      const mapData = buildMapSetData($MapStore.map, compassRotation);
      const serializedMap = JSON.stringify(mapData);
      if (serializedMap !== lastSyncedMap) {
        lastSyncedMap = serializedMap;
        socketService.sendMap(mapData);
        // Closing the editor syncs silently; say so, otherwise it is unclear
        // whether the changes reached the modem.
        SaveSuccess.set({ action: "sync map", date: new Date() });
      }
    }
  } else if (edit) {
    wasEditing = true;
  }

  // ─── Goto state ────────────────────────────────────────────────────────────
  const goto = createGotoState();
  $: mowerPos = $socketStore.state?.position ?? null;
  $: hasMap = $MapStore?.map?.perimeter?.points?.length > 0;
  $: targetSet = $goto.targetSet;
  $: targetPos = $goto.targetPos;
  $: isDriving = $goto.isDriving;
  $: targetDist = $goto.targetDist;
  $: targetBearing = $goto.targetBearing;

  function onMapClick(event: CustomEvent<{ x: number; y: number }>) {
    const { x, y } = event.detail;
    mouseMapPos = { x, y };
    if (drawActive) {
      onDrawMapClick(event);
      return;
    }
    if (edit) return;
    if (!hasMap) return;
    if (showManage || showCalculate || showSchedule) return;
    goto.setTarget(x, y);
  }

  function startDrive() {
    if (!$goto.targetSet || !$goto.targetPos) return;
    socketService.sendNavigateTo($goto.targetPos.x, -$goto.targetPos.y);
    goto.startDrive();
  }

  function stopDrive() {
    socketService.sendJoystickMove(0, 0);
    goto.stopDrive();
  }

  function clearTarget() {
    socketService.sendJoystickMove(0, 0);
    goto.clearTarget();
  }

  // ─── Compass state ─────────────────────────────────────────────────────────
  const compassState = createCompassState();
  $: compassRotation = $currentMapRotationStore;
</script>

<div class="map-dashboard">
  <div class="map-toolbar-wrap" class:schedule-open={showSchedule}>
    <Grid class="map-toolbar">
      <Row class="map-toolbar-main">
        <div class="toolbar-main-group">
          <div class="toolbar-dropdown">
            {#if $mapWorkflowStore.renameMode}
              <TextInput
                placeholder="Map name"
                bind:value={$mapWorkflowStore.pendingName}
              />
            {:else}
              <Dropdown
                placeholder="Select map"
                items={mapOptions}
                selectedId={selectedMapId}
                on:select={onSelectMap}
              />
            {/if}
          </div>
          <MapToolbar
            {workflowBusy}
            renameMode={$mapWorkflowStore.renameMode}
            {showManage}
            {edit}
            {showCalculate}
            {showSchedule}
            {canSave}
            {canRevert}
            canConfirmRename={!!$mapWorkflowStore.pendingName && (
              $mapWorkflowStore.state === "creating" ||
              $mapWorkflowStore.state === "intercepting" ||
              $mapWorkflowStore.pendingName !== effectiveMapName
            )}
            onToggleManage={toggleManage}
            onToggleEdit={toggleEdit}
            onToggleCalculate={toggleCalculate}
            onToggleSchedule={toggleSchedule}
            onSaveMap={onSaveMap}
            onDiscardMap={onDiscardMap}
            onConfirmRename={confirmRename}
            onCancelRename={cancelRename}
          />
        </div>
        {#if !showManage && !edit && !showCalculate && !showSchedule}
          <div class="toolbar-goto-group">
            {#if hasMap}
              {#if targetSet}
                <span class="goto-badge">{targetDist.toFixed(1)}m / {targetBearing.toFixed(0)}°</span>
                {#if isDriving}
                  <button class="goto-btn stop" on:click={stopDrive}>Stop</button>
                {:else}
                  <button class="goto-btn drive" on:click={startDrive}>Drive</button>
                {/if}
                <button class="goto-btn clear" on:click={clearTarget}>✕</button>
              {:else}
                <span class="goto-hint">Click map to set target</span>
              {/if}
            {:else}
              <span class="goto-hint">No map loaded</span>
            {/if}
          </div>
        {/if}
      </Row>

      {#if showManage}
        <MapManagementToolbar
          workflow={$mapWorkflowStore}
          {canRename}
          {effectiveMapId}
          pendingName={$mapWorkflowStore.pendingName}
          {effectiveMapName}
          previousMapName={$mapWorkflowStore.lastBackendMapName}
          {workflowBusy}
          startRename={startRename}
          onDeleteMap={onDeleteMap}
          onSetDefaultMap={onSetDefaultMap}
          onNewMap={onNewMap}
          onCopyMap={onCopyMap}
          onOpenMowerMap={() => (showMowerMapDialog = true)}
        />
      {/if}

      {#if edit}
        <MapEditToolbar
          {edit}
          {editCategory}
          categoryItems={categoryOptions}
          {editPoint}
          {editEdge}
          {drawActive}
          {canAdd}
          {canCreateExclusion}
          {canDeleteExclusion}
          canUndo={$canUndoMapEdit}
          canRedo={$canRedoMapEdit}
          onUndo={doUndo}
          onRedo={doRedo}
          onSelectCategory={selectEditCategory}
          onDrawClick={onDrawClick}
          onSplitClick={onSplitClick}
          onAddClick={onAddClick}
          onDeleteClick={onDeleteClick}
          onCreateExclusionClick={onCreateExclusionClick}
          onDeleteExclusionClick={onDeleteExclusionClick}
        />
      {/if}

      {#if showCalculate}
        <MapCalculateToolbar
          {busy}
          onOpenMowSettings={() => (showMowSettings = true)}
        />
      {/if}

      {#if showSchedule}
        <MapScheduleToolbar />
      {/if}
    </Grid>
  </div>
  <div class="map-canvas-wrapper" class:hidden={showSchedule}>
    {#if $socketStore.isLoadingMap || $mapChunkProgress}
      <div class="map-loading-overlay" aria-live="polite">
        <div class="map-loading-panel">
          {#if $mapChunkProgress && $mapChunkProgress.total > 0}
            {@const loadingPercent = Math.min(100, Math.round(($mapChunkProgress.received / $mapChunkProgress.total) * 100))}
            <ProgressBar
              value={loadingPercent}
              max={100}
              helperText={`${$mapChunkProgress.label} (${loadingPercent}%)`}
            />
          {:else}
            <Loading small withOverlay={false} description="Loading map" />
            <span>Loading map...</span>
          {/if}
        </div>
      </div>
    {/if}
    <MapStatusOverlay
      compassRotation={compassRotation}
      socketState={$socketStore}
      {perimeterPoints}
      {exclusionPoints}
      {dockpointsPoints}
      {waypointsPoints}
      {totalPoints}
      needsUpload={sync.needsUpload}
      onUploadMap={onUploadMap}
      {selectedExclusionIndex}
      onCompassDown={compassState.onDown}
      mouseMapPos={edit ? mouseMapPos : null}
    />
    <Canvas
      compassRotation={compassRotation}
      on:mapclick={onMapClick}
      on:mousemove={onMouseMove}
    >
      {#if $MapStore && $MapStore.map}
        <Track />
        <Perimeter
          value={$MapStore.map.perimeter}
          perimiterId="map-0-perimeter"
          {edit}
          {editCategory}
          {drawActive}
          bind:editItemId
          on:pointmove={(e) => { mouseMapPos = { x: e.detail.x, y: e.detail.y }; }}
          onMove={(points) => {
            MapStore.update((s) => ({
              ...s,
              map: {
                ...cloneMap(s.map),
                perimeter: { points: points.map((p) => ({ ...p })) },
              },
            }));
            setMapDirty(true);
          }}
        />
        {#if $MapStore.map.searchWire.points.length > 1}
          <polyline
            fill="none"
            stroke="#0f766e"
            stroke-width="0.04"
            stroke-dasharray="0.12 0.08"
            points={$MapStore.map.searchWire.points.map((p) => `${p.x},${p.y}`).join(" ")}
          />
        {/if}
        {#each $MapStore.map.exclusions as exclusion, index}
          <Exclusion
            value={exclusion}
            exclusionId={"map-0-exclusion-" + index}
            {edit}
            {editCategory}
            {drawActive}
            bind:editItemId
            onMove={(points) => {
              MapStore.update((s) => {
                const updatedExclusions = s.map.exclusions.map((e, i) =>
                  i === index ? { points: points.map((p) => ({ ...p })) } : e
                );
                return {
                  ...s,
                  map: { ...cloneMap(s.map), exclusions: updatedExclusions },
                };
              });
              setMapDirty(true);
            }}
          />
        {/each}
        <Waypoints
          value={{ points: edit && editCategory === "waypoints" ? rawWaypoints : filteredWaypoints }}
          waypointsId="map-0-waypoints"
          {edit}
          {editCategory}
          {drawActive}
          bind:editItemId
          onMove={(points) => {
            MapStore.update((s) => ({
              ...s,
              map: {
                ...cloneMap(s.map),
                waypoints: { points: points.map((p) => ({ ...p })) },
              },
            }));
            setMapDirty(true);
          }}
        />
        <Dockpoints
          value={$MapStore.map.dockpoints}
          dockpointsId="map-0-dockpoints"
          {edit}
          {editCategory}
          {drawActive}
          bind:editItemId
          onMove={(points) => {
            MapStore.update((s) => ({
              ...s,
              map: {
                ...cloneMap(s.map),
                dockpoints: { points: points.map((p) => ({ ...p })) },
              },
            }));
            setMapDirty(true);
          }}
        />
        <MowerPosition position={mowerPos} />
        <Obstacles obstacles={$socketStore.obstacles} />

        {#if drawActive && floatingPoint}
          {#each drawCandidates.filter((c) => c.begin === floatingPoint || c.end === floatingPoint) as c}
            {@const other = c.begin === floatingPoint ? c.end : c.begin}
            <line
              x1={floatingPoint.x}
              y1={floatingPoint.y}
              x2={other.x}
              y2={other.y}
              stroke="#e65100"
              stroke-width="0.04"
              stroke-dasharray="0.08, 0.08"
              opacity="0.7"
              pointer-events="none"
            />
          {/each}
        {/if}

        <MapGotoOverlay
          {targetPos}
          {mowerPos}
          showTarget={targetSet && !showManage && !edit && !showCalculate && !showSchedule}
          showLine={targetSet && !!mowerPos && (mowerPos.x !== 0 || mowerPos.y !== 0) && !showManage && !edit && !showCalculate && !showSchedule}
        />
      {/if}
    </Canvas>
    {#if targetSet && !showManage && !edit && !showCalculate && !showSchedule}
      <div class="goto-floater">
        <span class="goto-badge">{targetDist.toFixed(1)}m / {targetBearing.toFixed(0)}°</span>
        {#if isDriving}
          <button class="goto-btn stop" on:click={stopDrive}>Stop</button>
        {:else}
          <button class="goto-btn drive" on:click={startDrive}>Drive</button>
        {/if}
        <button class="goto-btn clear" on:click={clearTarget}>✕</button>
      </div>
    {/if}
  </div>
</div>

<MowSettingsDialog bind:open={showMowSettings} />

<MowerMapDialog
  bind:open={showMowerMapDialog}
  map={$MapStore.map}
  rotation={compassRotation}
  onImport={onImportMowerMap}
/>

<style>
  .map-dashboard {
    position: relative;
    width: 100%;
    height: 100%;
    padding-top: 48px;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    box-sizing: border-box;
  }
  .map-toolbar-wrap {
    position: relative;
    flex-shrink: 0;
    container-type: inline-size;
    width: 100%;
    max-width: 1000px;
    margin: 0 auto;
  }
  .map-toolbar-wrap.schedule-open {
    flex: 1 1 auto;
    min-height: 0;
    overflow: hidden;
    display: flex;
    flex-direction: column;
  }
  .map-toolbar-wrap.schedule-open :global(.map-toolbar) {
    flex: 1 1 auto;
    min-height: 0;
    height: 100%;
    width: 100%;
  }
  .map-canvas-wrapper {
    position: relative;
    flex: 1;
    min-height: 0;
    overflow: hidden;
  }
  .map-canvas-wrapper.hidden {
    display: none;
  }
  .map-loading-overlay {
    position: absolute;
    inset: 0;
    z-index: 200;
    display: grid;
    place-items: center;
    background: rgba(255, 255, 255, 0.72);
    backdrop-filter: blur(1px);
  }
  .map-loading-panel {
    width: min(24rem, calc(100% - 2rem));
    padding: 1rem;
    background: #ffffff;
    border: 1px solid #d6d6d6;
    border-radius: 4px;
    box-shadow: 0 4px 16px rgba(0, 0, 0, 0.14);
  }
  .map-loading-panel :global(.bx--loading--small) {
    margin-right: 0.75rem;
    vertical-align: middle;
  }
  .goto-floater {
    position: absolute;
    bottom: 1rem;
    left: 50%;
    transform: translateX(-50%);
    display: flex;
    align-items: center;
    gap: 0.25rem;
    background: rgba(255, 255, 255, 0.9);
    padding: 0.5rem;
    border-radius: 4px;
    border: 1px solid #e0e0e0;
    z-index: 10;
  }
  .goto-badge {
    font-size: 0.75em;
    padding: 3px 8px;
    background: #fff3e0;
    border: 1px solid #ffab00;
    border-radius: 4px;
    color: #b06000;
    font-family: monospace;
    margin-right: 4px;
  }
  .goto-btn {
    padding: 4px 10px;
    border: 1px solid #ccc;
    border-radius: 4px;
    font-size: 0.75em;
    cursor: pointer;
    background: #f4f4f4;
  }
  .goto-btn.drive {
    background: #e8f5e9;
    border-color: #4caf50;
    color: #2e7d32;
  }
  .goto-btn.stop {
    background: #ffebee;
    border-color: #ef5350;
    color: #c62828;
  }
  .goto-btn.clear {
    padding: 4px 8px;
  }
  .goto-hint {
    font-size: 0.75em;
    color: #888;
    font-style: italic;
    padding-left: 0.5rem;
  }
  :global(.map-toolbar .bx--row) {
    flex-wrap: wrap;
    align-items: center;
  }
  :global(.map-toolbar .bx--list-box) {
    height: 32px;
    max-height: 32px;
  }
  :global(.map-toolbar .bx--list-box__field) {
    height: 32px;
  }
  :global(.map-toolbar .bx--list-box__menu) {
    top: 32px;
  }
  .toolbar-main-group {
    display: flex;
    flex: 1 1 auto;
    align-items: center;
    gap: 0.25rem;
    min-width: 0;
  }
  .toolbar-dropdown {
    flex: 1 1 auto;
    min-width: 8rem;
  }
  .toolbar-dropdown :global(.bx--dropdown),
  .toolbar-dropdown :global(.bx--text-input) {
    width: 100%;
  }
  .toolbar-goto-group {
    display: flex;
    flex: 0 0 auto;
    align-items: center;
    justify-content: flex-end;
    gap: 0.25rem;
    margin-left: auto;
    padding-left: 0.75rem;
  }
  @container (max-width: 800px) {
    .toolbar-main-group {
      width: 100%;
    }
  }
</style>
