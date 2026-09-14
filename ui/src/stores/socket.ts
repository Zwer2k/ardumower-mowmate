import { writable, get } from "svelte/store";
import { browser } from "$app/environment";
import type {
  ModemLog,
  ModemLogSettings,
  DesiredState,
  LogLine,
  RequestSocketMessage,
  State,
  Stats,
  ValueDescriptions,
  ConsoleLine,
  ConsoleResponseData,
  MapRaw,
  MapMeta,
  MapListData,
  SensorSummary,
  GpsDetails,
  UbxResponse,
  MowSettings,
  MowSettingsData,
  ScheduleData,
  ObstaclesData,
} from "../model";
import {
  ResponseDataType,
  RequestDataType,
  type ClockData,
  type DrivenTrackData,
  type FirmwareStatusData,
  type RouteReportData,
} from "../model";
import { routeReportStore, clearRouteReport } from "../map/route-report";
import { clearWaypointsBuffer, handleMapChunk, resetMapChunkBuffer, resetMapTransferTracking } from "../map/map-chunk-buffer";
import { MapPointType } from "../map/map-chunk-buffer";
import { mowSettingsStore } from "../map/mow-settings";
import { currentMapRotationStore, updateDrivenTrack } from "../map/service";
import { mapWorkflowStore } from "../map/workflow/map-workflow-store";
import { setMapDirty } from "../map/services/map-sync";
import type { MapWorkflowStore, MapWorkflowState } from "../map/workflow/map-workflow-store";

let workflowModule: any = null;
async function getWorkflowModule(): Promise<any> {
  if (!workflowModule) {
    workflowModule = await import("../map/map-workflow");
  }
  return workflowModule;
}

async function getMapWorkflowStore(): Promise<MapWorkflowStore> {
  return mapWorkflowStore;
}

function initMapWorkflowBindings() {
  mapWorkflowStore.setSocketStateProvider(() => get(socketStore));
  mapWorkflowStore.setSocketUpdate((fn) => socketStore.update(fn));
}

export type { SocketState };

export interface SocketState {
  socket: WebSocket | null;
  connected: boolean;
  valueDescriptions: ValueDescriptions | null;
  state: State | null;
  stats: Stats | null;
  desiredState: DesiredState | null;
  sensorSummary: SensorSummary | null;
  gpsDetails: GpsDetails | null;
  ubxResponse: UbxResponse | null;
  modemLog: LogLine[];
  consoleLines: ConsoleLine[];
  modemDbgLevel: number;
  mapRaw: MapRaw | null;
  mapEnabled: boolean;
  liveMapEnabled: boolean;
  gpsDashboardEnabled: boolean;
  maps: MapMeta[];
  activeMapId: string;
  currentMapId: string;
  currentMapMeta: { hash: string; crc: number; area: number; rotation: number } | null;
  currentMapUnsaved: boolean;
  isLoadingMap: boolean;
  isNewMap: boolean;
  schedule: ScheduleData | null;
  clock: ClockData | null;
  obstacles: ObstaclesData | null;
}

const initialState: SocketState = {
  socket: null,
  connected: false,
  valueDescriptions: null,
  state: null,
  stats: null,
  desiredState: null,
  sensorSummary: null,
  gpsDetails: null,
  ubxResponse: null,
  modemLog: [],
  consoleLines: [],
  modemDbgLevel: 15,
  mapRaw: null,
  mapEnabled: false,
  liveMapEnabled: false,
  gpsDashboardEnabled: false,
  maps: [],
  activeMapId: "",
  currentMapId: "",
  currentMapMeta: null,
  currentMapUnsaved: false,
  isLoadingMap: false,
  isNewMap: false,
  schedule: null,
  clock: null,
  obstacles: null,
};

export const socketStore = writable<SocketState>(initialState);
if (typeof window !== "undefined") {
  (window as any).socketStore = socketStore;
}

/** Dedizierter Store für MotorPlot-Daten. Wird im WS-Handler befüllt und
 *  ist NICHT von clearConsoleLines() betroffen (vermeidet Race Condition
 *  mit dem Terminal, das sofort nach Empfang der Lines cleart). */
export const motorPlotStore = writable<ConsoleLine[]>([]);
export const mapMetaStore = writable<{ hash: string; crc: number; area: number; rotation: number } | null>(null);

export function clearMotorPlotStore() {
  motorPlotStore.set([]);
}

export interface FlashProgress {
  /** 0..100 */
  progress: number;
  source: "modem" | "mower";
  timestamp: number;
}

/** Flash-Fortschritt beider Firmware-Typen. Wird aus dem gemeinsamen Socket
 *  gespeist, damit der Firmware-Dialog keine zweite WebSocket-Verbindung
 *  aufbauen muss – der Modem hat nur wenige Client-Slots und gerade während
 *  des Flashens ist der Heap knapp. */
export const flashProgressStore = writable<FlashProgress | null>(null);

export function resetFlashProgress() {
  flashProgressStore.set(null);
}

export interface FirmwareStatus {
  /** null = seit dem Verbindungsaufbau noch nichts vom Modem gehört. */
  reachable: boolean | null;
  checking: boolean;
  updateAvailable: boolean;
  /** false = das Modem hat seit dem Boot noch nicht bei GitHub nachgesehen. */
  checked: boolean;
  current: string | null;
  latest: string | null;
  error: string | null;
}

const emptyFirmwareStatus: FirmwareStatus = {
  reachable: null,
  checking: false,
  updateAvailable: false,
  checked: false,
  current: null,
  latest: null,
  error: null,
};

/** Firmware-Stand aus Sicht des Modems: Das Modem selbst fragt GitHub im
 *  Hintergrund ab und lädt später auch die Firmware – nicht der Browser. Nur
 *  seine Sicht entscheidet, ob es ein Update gibt und ob die GitHub-Quelle im
 *  Firmware-Dialog angeboten wird. */
export const firmwareStatusStore = writable<FirmwareStatus>(emptyFirmwareStatus);

/** Während einer laufenden Prüfung den letzten bekannten Stand behalten, sonst
 *  flackert die GitHub-Quelle im Firmware-Dialog kurz weg. */
export function applyFirmwareStatus(data: FirmwareStatusData) {
  firmwareStatusStore.update((prev) => ({
    reachable: data.checking ? prev.reachable : data.reachable,
    checking: data.checking,
    updateAvailable: data.updateAvailable,
    checked: data.checked,
    current: data.current ?? prev.current,
    latest: data.latest ?? prev.latest,
    error: data.error ?? null,
  }));
}

export function resetFirmwareStatus() {
  firmwareStatusStore.set(emptyFirmwareStatus);
}

function setFlashProgress(progress: number, source: "modem" | "mower") {
  flashProgressStore.set({
    progress: Math.max(0, Math.min(100, progress)),
    source,
    timestamp: Date.now(),
  });
}

class SocketService {
  private static readonly CONNECT_TIMEOUT_MS = 15000;
  private static readonly PING_INTERVAL_MS = 30000;
  private static readonly PONG_TIMEOUT_MS = 15000;
  private static readonly RECONNECT_BASE_MS = 3000;
  private static readonly RECONNECT_MAX_MS = 30000;
  private static readonly STABLE_CONNECTION_MS = 60000;
  private static readonly MAX_PENDING_MESSAGES = 50;

  private restartTimer: NodeJS.Timeout | null = null;
  private reconnect = true;
  private reconnectAttempts = 0;
  private connectionTimeout: NodeJS.Timeout | null = null;
  private stableConnectionTimer: NodeJS.Timeout | null = null;
  private pingInterval: NodeJS.Timeout | null = null;
  private pongTimeout: NodeJS.Timeout | null = null;
  private isPageVisible = true;
  private pendingMessages: RequestSocketMessage[] = [];
  private nextMapSyncId = 1;
  private pendingUpload: { mapData: import("../model").MapSetData; syncId: number; mapId: string } | null = null;
  private pendingUploadRetry: ReturnType<typeof setTimeout> | null = null;
  // Last plain setMap() (editor sync). The backend rejects setMap while it is
  // streaming the map to clients (mapAck.accepted=false); without a retry the
  // edit was silently lost, because the editor only resends when the map
  // changed again.
  private lastSetMap: { mapData: import("../model").MapSetData; syncId: number; mapId: string; attempts: number } | null = null;
  private setMapRetry: ReturnType<typeof setTimeout> | null = null;
  private static readonly SET_MAP_MAX_RETRIES = 20;
  private readonly boundVisibilityChange = this.handleVisibilityChange.bind(this);

  constructor() {
    if (browser) {
      document.addEventListener("visibilitychange", this.boundVisibilityChange);
    }
  }

  connect() {
    if (!browser) {
      return;
    }

    // NOTE: never run this inside socketStore.update(). Svelte stores are not
    // re-entrant: a nested update() writes first and the outer callback's
    // return value then overwrites it again — the freshly created socket would
    // be dropped from the store, its open handler would consider itself stale
    // and close, and its close handler would skip the reconnect. The result was
    // a UI that stayed disconnected after switching browser tabs.
    const state = get(socketStore);

    if (
      state.socket != null &&
      (state.socket.readyState === WebSocket.CONNECTING ||
        state.socket.readyState === WebSocket.CLOSING ||
        state.socket.readyState === WebSocket.OPEN)
    ) {
      return;
    }

    if (!this.reconnect || !this.isPageVisible) {
      return;
    }

    this.clearAllTimers();
    this.pendingMessages = [];

    if (state.socket != null) {
      try {
        state.socket.close();
      } catch (_) {}
    }

    {
      let host = location.host;
      const wsProtocol = location.protocol === "https:" ? "wss:" : "ws:";

      try {
        const socket = new WebSocket(wsProtocol + "//" + host + "/ws");

        this.connectionTimeout = setTimeout(() => {
          if (socket && socket.readyState === WebSocket.CONNECTING) {
            socket.close();
          }
        }, SocketService.CONNECT_TIMEOUT_MS);

        socket.addEventListener("open", () => {
          // Ignoriere Events von veralteten Sockets
          if (get(socketStore).socket !== socket) {
            try { socket.close(); } catch (_) {}
            return;
          }

          if (this.connectionTimeout) {
            clearTimeout(this.connectionTimeout);
            this.connectionTimeout = null;
          }

          if (this.stableConnectionTimer) {
            clearTimeout(this.stableConnectionTimer);
          }
          this.stableConnectionTimer = setTimeout(() => {
            if (socket.readyState === WebSocket.OPEN) {
              this.reconnectAttempts = 0;
            }
            this.stableConnectionTimer = null;
          }, SocketService.STABLE_CONNECTION_MS);

          this.startHeartbeat(socket);

          // Neue Verbindung = möglicherweise neu gestarteter Modem mit
          // zurückgesetzten Transfer-IDs.
          resetMapTransferTracking();

          // Ohne force: das Modem antwortet aus dem Ergebnis seines letzten
          // Hintergrund-Checks, es geht dafür nicht ins Netz.
          this.sendRequestFirmwareStatus();

          socketStore.update((s) => ({ ...s, connected: true }));

          for (const message of this.pendingMessages) {
            socket.send(JSON.stringify(message));
          }
          this.pendingMessages = [];

          this.requestMowSettings();

  // Map-Workflow-Store frühzeitig laden, damit setSocketUpdate und
  // setSocketStateProvider registriert sind, bevor der Benutzer interagiert.
  initMapWorkflowBindings();
  getMapWorkflowStore().catch(() => {});
        });

        socket.addEventListener("error", (error) => {
          // Fehler führen in der Regel zu einem close-Event, daher kümmert
          // sich der close-Handler um den Reconnect. Hier nur Timer stoppen.
          if (this.connectionTimeout) {
            clearTimeout(this.connectionTimeout);
            this.connectionTimeout = null;
          }
          this.clearHeartbeatTimers();
          // Socket im close-Handler auf null setzen; hier nur explizit
          // schließen, falls der Browser kein close-Event feuert.
          try { socket.close(); } catch (_) {}
        });

        socket.addEventListener("close", () => {
          // Ignoriere Events von veralteten Sockets
          if (get(socketStore).socket !== socket) {
            return;
          }

          this.clearAllTimers();
          socketStore.update((s) => ({ ...s, socket: null, connected: false }));
          // Nach einem Reconnect kann das Netz ein anderes sein – erneut fragen.
          resetFirmwareStatus();

          if (
            this.reconnect &&
            this.isPageVisible
          ) {
            this.reconnectAttempts = Math.min(this.reconnectAttempts + 1, 6);
            const delay = Math.min(
              SocketService.RECONNECT_BASE_MS * Math.pow(2, this.reconnectAttempts - 1),
              SocketService.RECONNECT_MAX_MS,
            );
            this.restartTimer = setTimeout(() => {
              this.connect();
            }, delay);
          }
        });

        socket.addEventListener("message", async (message: any) => {
          try {
            const jsonData = JSON.parse(message.data);
            const msgType = jsonData.type as ResponseDataType;
            if (msgType === ResponseDataType.pong) {
              if (this.pongTimeout) {
                clearTimeout(this.pongTimeout);
                this.pongTimeout = null;
              }
              return;
            }

            // Modem-Flash-Fortschritt: {"status":{"progress":n}} – ohne type.
            if (
              jsonData.status &&
              typeof jsonData.status.progress === "number"
            ) {
              setFlashProgress(jsonData.status.progress, "modem");
              return;
            }

            // Map-Workflow-Store wird nur bei Map-relevanten Nachrichten
            // gebraucht; bei allen anderen Nachrichten (z. B. Upload-Progress
            // über mowerConsole) sparen wir den Import und vermeiden
            // unnötige Verzögerungen.
            let mwf: MapWorkflowStore | null = null;
            let workflowState: MapWorkflowState | null = null;
            let wasDeletingMap = false;
            if (msgType === ResponseDataType.map || msgType === ResponseDataType.mapList) {
              mwf = mapWorkflowStore;
              workflowState = get(mwf).state;
              wasDeletingMap = workflowState === "deleting";
            }

            let workflowRotationToSet: number | null = null;
            let workflowNewMapReceived = false;
            let workflowInterceptedMapReceived = false;
            let workflowStartInterceptedMapRename: { id: string; name: string; rotation: number } | null = null;
            let workflowFinishSaveMap: { id: string; name: string; rotation: number } | null = null;
            let workflowFinishRename: { id: string; name: string } | null = null;
            let workflowFinishLoadMap: { id: string; name: string; rotation: number } | null = null;
            let workflowLoadFailed = false;
            let workflowFinishDelete = false;

            socketStore.update((state) => {
              const newState = { ...state };
              switch (jsonData.type) {
                case ResponseDataType.hello:
                  newState.valueDescriptions =
                    jsonData.data as ValueDescriptions;
                  newState.modemDbgLevel = newState.valueDescriptions.logLevel;
                  newState.mapEnabled = !!(jsonData.data as any).mapEnabled;
                  newState.liveMapEnabled = !!(jsonData.data as any).liveMapEnabled;
                  newState.gpsDashboardEnabled = !!(jsonData.data as any).gpsDashboardEnabled;
                  break;
                case ResponseDataType.mowerState: {
                  const st = (jsonData.data as State) || null;
                  if (st) {
                    if (jsonData.progressPct !== undefined) st.progressPct = jsonData.progressPct;
                    if (jsonData.progressMsg !== undefined) st.progressMsg = jsonData.progressMsg;
                    // progressOp is sent as a top-level field by the backend but is
                    // not part of the `State` TS type. Preserve it on the object
                    // for the UI by assigning to `any` so Map.svelte can read it.
                    if (jsonData.progressOp !== undefined) (st as any).progressOp = jsonData.progressOp;
                  }
                  newState.state = st;
                }
                  break;
                case ResponseDataType.mowerStats:
                  newState.stats = jsonData.data as Stats;
                  break;
                case ResponseDataType.desiredState:
                  newState.desiredState = jsonData.data as DesiredState;
                  break;
                case ResponseDataType.modemLog:
                  newState.modemLog = [
                    ...newState.modemLog,
                    ...(jsonData.data as ModemLog).log,
                  ];
                  if (newState.modemLog.length > 1000) {
                    newState.modemLog = newState.modemLog.slice(-1000);
                  }
                  break;
                case ResponseDataType.mowerConsole: {
                  const incomingLines = (jsonData.data as ConsoleResponseData)
                    .lines;
                  // Mower-Firmware-Upload sendet Fortschritt als StatusMessage
                  // (ohne lines) über denselben Kanal.
                  if (!Array.isArray(incomingLines)) {
                    const progress = (jsonData.data as any)?.progress;
                    if (typeof progress === "number") {
                      setFlashProgress(progress, "mower");
                    }
                    break;
                  }
                  newState.consoleLines = [
                    ...newState.consoleLines,
                    ...incomingLines,
                  ];
                  if (newState.consoleLines.length > 1000) {
                    newState.consoleLines = newState.consoleLines.slice(-1000);
                  }
                  // MotorPlot-Daten separat sammeln (nicht von Terminal clearen)
                  motorPlotStore.update((existing) => [
                    ...existing,
                    ...incomingLines,
                  ]);
                  break;
                }
                case ResponseDataType.map: {
                  const data = jsonData.data as any;
                  if (data && data.complete) {
                    handleMapChunk({
                      transferId: jsonData.transferId,
                      transferTotal: data.transferTotal,
                      pointType: MapPointType.Waypoints,
                      total: 0,
                      complete: true,
                    });
                    break;
                  }
                  if (data && data.reset) {
                    handleMapChunk({
                      transferId: jsonData.transferId,
                      pointType: data.pointType ?? MapPointType.Perimeter,
                      total: 0,
                      reset: true,
                    } as any);
                    break;
                  }
                  if (data && data.meta) {
                    const meta = { hash: data.meta.hash, crc: data.meta.crc || 0, area: data.meta.area, rotation: data.meta.rotation || 0 };
                    newState.currentMapMeta = meta;
                    mapMetaStore.set(meta);
                    currentMapRotationStore.set(meta.rotation);
                    // Wenn wir gerade eine Karte laden, merken wir uns die Rotation
                    // und beenden den Ladezustand, sobald mapList ankommt.
                    if (newState.isLoadingMap) {
                      workflowRotationToSet = meta.rotation;
                    }
                    // Abgefangene/neue Karte erkennen: Meta ist da, aber keine
                    // currentMapId und wir befinden uns nicht im Lade-Modus.
                    // Während einer Löschoperation darf dieser Pfad nicht
                    // auslösen, sonst springt die UI in den Rename-Modus für
                    // eine gerade gelöschte Karte.
                    // Echte "New Map"-Workflows (isNewMap == true) bekommen
                    // die bestehende Behandlung. Abgefangene Karten (ohne
                    // isNewMap) bekommen eine eigene Rename-Abfrage, ohne
                    // sofort in SPIFFS zu schreiben.
                    if (!newState.currentMapId && !newState.isLoadingMap && !wasDeletingMap) {
                      if (newState.isNewMap) {
                        workflowNewMapReceived = true;
                      } else {
                        workflowInterceptedMapReceived = true;
                      }
                    }
                  }
                  // Map-Chunk-Logik: Chunks sammeln, MapStore wird im Buffer gesetzt
                  if (
                    data &&
                    data.startIndex !== undefined &&
                    data.points
                  ) {
                    handleMapChunk({ ...data, transferId: jsonData.transferId, transferTotal: data.transferTotal });
                  }
                  break;
                }
                case ResponseDataType.mapAck: {
                  const data = jsonData.data as {
                    hash: string;
                    crc: number;
                    area: number;
                    rotation: number;
                    unsaved: boolean;
                    syncId: number;
                    mapId: string;
                    accepted: boolean;
                  };
                  if (!data.accepted) {
                    if (this.pendingUpload?.syncId === data.syncId) {
                      if (this.pendingUpload.mapId === data.mapId) {
                        this.schedulePendingUploadRetry();
                      } else {
                        this.pendingUpload = null;
                      }
                    } else if (this.lastSetMap?.syncId === data.syncId) {
                      if (this.lastSetMap.mapId === data.mapId && data.mapId === newState.currentMapId) {
                        this.scheduleSetMapRetry();
                      } else {
                        this.lastSetMap = null;
                      }
                    }
                    break;
                  }
                  if (this.lastSetMap?.syncId === data.syncId) {
                    this.lastSetMap = null;
                  }
                  if (data.mapId !== newState.currentMapId) break;
                  const meta = {
                    hash: data.hash,
                    crc: data.crc || 0,
                    area: data.area,
                    rotation: data.rotation || 0,
                  };
                  newState.currentMapMeta = meta;
                  newState.currentMapUnsaved = data.unsaved;
                  mapMetaStore.set(meta);
                  setMapDirty(data.unsaved);
                  if (this.pendingUpload?.syncId === data.syncId && this.pendingUpload.mapId === data.mapId) {
                    this.pendingUpload = null;
                    this.sendUploadMap(data.mapId);
                  }
                  break;
                }
                case ResponseDataType.mapList: {
                  const listData = jsonData.data as MapListData;
                  newState.maps = listData.maps || [];
                  newState.activeMapId = listData.activeId || "";
                  const previousMapId = newState.currentMapId;
                  const wasLoadingMap = newState.isLoadingMap;
                  const hasCurrentMap = Boolean(listData.currentId);
                  // Discarding a new map and loading the fallback map can
                  // produce an intermediate mapList with an empty currentId.
                  // Keep the optimistic load target until its response arrives.
                  if (hasCurrentMap || !wasLoadingMap) {
                    newState.currentMapId = listData.currentId || "";
                  }
                  if (hasCurrentMap || !wasLoadingMap) {
                    newState.isLoadingMap = false;
                  }
                  const currentMapEntry = newState.maps.find((m) => m.id === newState.currentMapId);
                  newState.currentMapUnsaved = currentMapEntry?.unsaved || false;
                  // Der Backend-Flag ist der maßgebliche dirty-Status. Er muss
                  // immer synchronisiert werden, auch beim initialen Browser-Reload
                  // oder nach einem finishLoadMap, damit ein unsaved Zustand erhalten
                  // bleibt.
                  setMapDirty(newState.currentMapUnsaved);
                  if (newState.currentMapId && (hasCurrentMap || !wasLoadingMap)) {
                    newState.isNewMap = false;
                    const map = newState.maps.find((m) => m.id === newState.currentMapId);
                    if (map) {
                      // finishSaveMap nur auslösen, wenn der Workflow gerade
                      // auf das Speichern wartet. Andernfalls würde jede
                      // eingehende mapList den Snapshot überschreiben und den
                      // Dirty-Status ungewollt zurücksetzen.
                      if (workflowState === "saving") {
                        workflowFinishSaveMap = { id: map.id, name: map.name, rotation: map.rotation };
                      }
                      // Rename wird wie eine Bearbeitung behandelt: Name
                      // aktualisieren, aber Dirty-Status bleibt erhalten.
                      if (workflowState === "renaming") {
                        workflowFinishRename = { id: map.id, name: map.name };
                      }
                      // finishLoadMap muss immer aufgerufen werden, wenn wir gerade
                      // eine Karte geladen haben, auch wenn die ID gleich geblieben ist.
                      // Sonst bleibt der Workflow im "loading"-Zustand hängen und "New"
                      // bleibt deaktiviert.
                      if (wasLoadingMap || previousMapId !== newState.currentMapId) {
                        workflowFinishLoadMap = { id: map.id, name: map.name, rotation: map.rotation };
                      }
                      // Abgefangene Karte erkannt: wenn currentMapId eine
                      // Transient-ID ist, neu geladen wurde UND das Backend
                      // flaggt, dass sie noch einen Namen braucht, erst dann
                      // wird der Rename-Dialog geöffnet. Nach dem ersten
                      // Umbenennen wird requiresRename false, damit ein
                      // Browser-Reload nicht erneut in den Rename-Modus
                      // springt.
                      if (newState.currentMapId.startsWith("__t_") &&
                          (previousMapId !== newState.currentMapId || previousMapId === "") &&
                          map.requiresRename) {
                        workflowStartInterceptedMapRename = { id: map.id, name: map.name, rotation: map.rotation };
                        // finishLoadMap würde renameMode im idle-Zustand
                        // zurücksetzen; stattdessen starten wir den
                        // Intercept-Workflow mit den Daten aus der Map-Liste.
                        workflowFinishLoadMap = null;
                      }
                    }
                  } else if (newState.isNewMap) {
                    workflowNewMapReceived = true;
                  } else if (wasLoadingMap && hasCurrentMap) {
                    // Laden hat keine currentMapId geliefert -> Zustand zurücksetzen,
                    // damit der Workflow nicht ewig in "loading" bleibt. Gleichzeitig
                    // currentMapId/meta leeren, damit die UI nicht auf einer Karte
                    // hängt, die im Backend nicht mehr geladen ist.
                    newState.currentMapId = "";
                    newState.currentMapMeta = null;
                    newState.isNewMap = false;
                    workflowLoadFailed = true;
                  }
                  if (wasDeletingMap) {
                    workflowFinishDelete = true;
                  }
                  break;
                }
                case ResponseDataType.sensorSummary:
                  newState.sensorSummary = jsonData.data as SensorSummary;
                  break;
                case ResponseDataType.gpsDetails:
                  newState.gpsDetails = jsonData.data as GpsDetails;
                  break;
                case ResponseDataType.ubxResponse:
                  newState.ubxResponse = jsonData.data as UbxResponse;
                  break;
                case ResponseDataType.mowSettings:
                  if ((jsonData.data as MowSettings & { mapId?: string }).mapId === newState.currentMapId) {
                    mowSettingsStore.set(jsonData.data as MowSettings);
                  }
                  break;
                case ResponseDataType.mowerMap: {
                  const data = jsonData.data as { json: string };
                  if (data?.json) {
                    const event = new CustomEvent("mower-map-export", { detail: data.json });
                    window.dispatchEvent(event);
                  }
                  break;
                }
                case ResponseDataType.drivenTrack:
                  updateDrivenTrack(jsonData.data as DrivenTrackData);
                  break;
                case ResponseDataType.obstacles:
                  newState.obstacles = jsonData.data as ObstaclesData;
                  break;
                case ResponseDataType.schedule:
                  newState.schedule = jsonData.data as ScheduleData;
                  break;
                case ResponseDataType.clock:
                  newState.clock = jsonData.data as ClockData;
                  break;
                case ResponseDataType.firmwareStatus:
                  applyFirmwareStatus(jsonData.data as FirmwareStatusData);
                  break;
                case ResponseDataType.routeReport:
                  routeReportStore.set(jsonData.data as RouteReportData);
                  break;
                default:
              }
              return newState;
            });

            // Map-Workflow-Updates außerhalb von socketStore.update ausführen,
            // um zirkuläre Imports und asynchrone Store-Updates zu vermeiden.
            if (workflowRotationToSet !== null) {
              mwf.update((w) => ({ ...w, lastBackendRotation: workflowRotationToSet! }));
            }
            if (workflowInterceptedMapReceived) {
              const mapsCount = socketStore ? get(socketStore).maps.length + 1 : 1;
              mwf.startInterceptedMapRename(`Karte ${mapsCount}`);
            }
            if (workflowStartInterceptedMapRename) {
              mwf.startInterceptedMapRename(
                workflowStartInterceptedMapRename.name || `Karte ${socketStore ? get(socketStore).maps.length + 1 : 1}`,
                workflowStartInterceptedMapRename.id,
                workflowStartInterceptedMapRename.name,
                workflowStartInterceptedMapRename.rotation,
              );
            }
            if (workflowNewMapReceived) {
              const mapsCount = socketStore ? get(socketStore).maps.length + 1 : 1;
              mwf.onNewMapReceived(`Karte ${mapsCount}`);
            }
            if (workflowFinishSaveMap) {
              mwf.finishSaveMap(workflowFinishSaveMap.id, workflowFinishSaveMap.name, workflowFinishSaveMap.rotation);
            }
            if (workflowFinishRename) {
              mwf.finishRename(workflowFinishRename.name);
            }
            if (workflowFinishLoadMap) {
              mwf.finishLoadMap(workflowFinishLoadMap.id, workflowFinishLoadMap.name, workflowFinishLoadMap.rotation);
            }
            if (workflowLoadFailed) {
              mwf.setError("The map could not be loaded");
            }
            if (workflowFinishDelete) {
              mwf.finishDelete();
            }

            // Map-relevante Nachrichten: nichts mehr automatisch
            // berechnen; Dirty-Status wird explizit von UI-Aktionen und
            // Speichern/Verwerfen gesteuert.
          } catch (error) {
            console.error(
              "[Socket] Error parsing message:",
              error,
              message.data,
            );
          }
        });

        socketStore.update((s) => ({ ...s, socket }));
      } catch (error) {
        this.clearAllTimers();
        this.reconnectAttempts = Math.min(this.reconnectAttempts + 1, 6);
        if (
          this.reconnect &&
          this.isPageVisible
        ) {
          const delay = Math.min(
            SocketService.RECONNECT_BASE_MS * Math.pow(2, this.reconnectAttempts - 1),
            SocketService.RECONNECT_MAX_MS,
          );
          this.restartTimer = setTimeout(() => {
            this.connect();
          }, delay);
        }
      }
    }
  }

  sendMessage(message: RequestSocketMessage) {
    socketStore.update((state) => {
      if (
        browser &&
        state.socket &&
        state.socket.readyState === WebSocket.OPEN
      ) {
        state.socket.send(JSON.stringify(message));
      } else if (browser) {
        // Begrenzen: während einer längeren Trennung (z. B. Firmware-Flash)
        // würde die Queue sonst unbegrenzt wachsen. Die ältesten Nachrichten
        // sind auch die, die nach dem Reconnect am wenigsten aussagen.
        if (this.pendingMessages.length >= SocketService.MAX_PENDING_MESSAGES) {
          this.pendingMessages.shift();
        }
        this.pendingMessages.push(message);
      }
      return state;
    });
  }

  setLogLevel(logLevel: number) {
    socketStore.update((state) => {
      const newState = { ...state, modemDbgLevel: logLevel };

      if (
        browser &&
        state.socket &&
        state.socket.readyState === WebSocket.OPEN
      ) {
        const settings: RequestSocketMessage = {
          type: RequestDataType.modemLogSettings,
          data: { logLevel } as ModemLogSettings,
        };
        state.socket.send(JSON.stringify(settings));
      }

      return newState;
    });
  }

  sendConsoleCommand(cmd: string) {
    const req: RequestSocketMessage = {
      type: RequestDataType.mowerConsoleRequest,
      data: { cmd } as ConsoleRequestData,
    };
    this.sendMessage(req);
  }

  clearConsoleLines() {
    socketStore.update((state) => ({ ...state, consoleLines: [] }));
  }

  requestGpsDetails() {
    const req: RequestSocketMessage = {
      type: RequestDataType.requestGpsDetails,
      data: {},
    };
    this.sendMessage(req);
  }

  stopGpsDetails() {
    const req: RequestSocketMessage = {
      type: RequestDataType.stopGpsDetails,
      data: {},
    };
    this.sendMessage(req);
  }

  requestSensorSummary() {
    const req: RequestSocketMessage = {
      type: RequestDataType.requestSensorSummary,
      data: {},
    };
    this.sendMessage(req);
  }

  stopSensorSummary() {
    const req: RequestSocketMessage = {
      type: RequestDataType.stopSensorSummary,
      data: {},
    };
    this.sendMessage(req);
  }

  sendUbx(hex: string) {
    const req: RequestSocketMessage = {
      type: RequestDataType.requestUbx,
      data: { hex },
    };
    this.sendMessage(req);
  }

  sendJoystickMove(linear: number, angular: number) {
    const req: RequestSocketMessage = {
      type: RequestDataType.joystickMove,
      data: { linear, angular },
    };
    this.sendMessage(req);
  }

  sendNavigateTo(x: number, y: number) {
    const req: RequestSocketMessage = {
      type: RequestDataType.navigateTo,
      data: { x, y },
    };
    this.sendMessage(req);
  }

  sendMap(mapData: import("../model").MapSetData) {
    const syncId = this.nextMapSyncId++;
    const mapId = get(socketStore).currentMapId;
    this.clearSetMapRetry();
    this.lastSetMap = { mapData, syncId, mapId, attempts: 0 };
    this.sendLastSetMap();
  }

  private sendLastSetMap() {
    const last = this.lastSetMap;
    if (!last) return;
    const req: RequestSocketMessage = {
      type: RequestDataType.setMap,
      data: { ...last.mapData, syncId: last.syncId, mapId: last.mapId },
    };
    this.sendMessage(req);
  }

  private scheduleSetMapRetry() {
    const last = this.lastSetMap;
    if (!last) return;
    if (last.attempts >= SocketService.SET_MAP_MAX_RETRIES) {
      console.warn("[Socket] setMap rejected repeatedly, giving up (sync", last.syncId, ")");
      this.lastSetMap = null;
      return;
    }
    last.attempts += 1;
    this.clearSetMapRetry();
    this.setMapRetry = setTimeout(() => {
      this.setMapRetry = null;
      this.sendLastSetMap();
    }, 300);
  }

  private clearSetMapRetry() {
    if (this.setMapRetry) {
      clearTimeout(this.setMapRetry);
      this.setMapRetry = null;
    }
  }

  sendMapAndUpload(mapData: import("../model").MapSetData) {
    const syncId = this.nextMapSyncId++;
    const mapId = get(socketStore).currentMapId;
    this.pendingUpload = { mapData, syncId, mapId };
    this.sendPendingUploadMap();
  }

  private sendPendingUploadMap() {
    const pendingUpload = this.pendingUpload;
    if (!pendingUpload) return;
    const req: RequestSocketMessage = {
      type: RequestDataType.setMap,
      data: {
        ...pendingUpload.mapData,
        syncId: pendingUpload.syncId,
        mapId: pendingUpload.mapId,
      },
    };
    this.sendMessage(req);
  }

  private schedulePendingUploadRetry() {
    if (this.pendingUploadRetry) clearTimeout(this.pendingUploadRetry);
    this.pendingUploadRetry = setTimeout(() => {
      this.pendingUploadRetry = null;
      this.sendPendingUploadMap();
    }, 150);
  }

  sendUploadMap(mapId = get(socketStore).currentMapId) {
    const req: RequestSocketMessage = {
      type: RequestDataType.uploadMap,
      data: { mapId },
    };
    this.sendMessage(req);
  }

  sendMowSettings(data: Partial<MowSettingsData>) {
    const req: RequestSocketMessage = {
      type: RequestDataType.setMowSettings,
      data: { ...data, mapId: get(socketStore).currentMapId },
    };
    this.sendMessage(req);
  }

  requestMowSettings() {
    const req: RequestSocketMessage = {
      type: RequestDataType.requestMowSettings,
      data: {},
    };
    this.sendMessage(req);
  }

  sendClearWaypoints() {
    const req: RequestSocketMessage = {
      type: RequestDataType.clearWaypoints,
      data: {},
    };
    this.sendMessage(req);
    clearWaypointsBuffer();
  }

  sendCalculateWaypoints() {
    const req: RequestSocketMessage = {
      type: RequestDataType.calculateWaypoints,
      data: {},
    };
    this.sendMessage(req);
    resetMapChunkBuffer();
    // The old report belongs to the old route.
    clearRouteReport();
  }

  sendListMaps() {
    const req: RequestSocketMessage = {
      type: RequestDataType.listMaps,
      data: {},
    };
    this.sendMessage(req);
  }

  sendCreateMap(name: string) {
    const req: RequestSocketMessage = {
      type: RequestDataType.createMap,
      data: { name },
    };
    this.sendMessage(req);
  }

  sendCopyMap(name: string) {
    const req: RequestSocketMessage = {
      type: RequestDataType.copyMap,
      data: { name },
    };
    this.sendMessage(req);
  }

  sendLoadMap(id: string, discardCurrent = false) {
    this.pendingUpload = null;
    if (this.pendingUploadRetry) {
      clearTimeout(this.pendingUploadRetry);
      this.pendingUploadRetry = null;
    }
    // A pending editor sync belongs to the map being left.
    this.lastSetMap = null;
    this.clearSetMapRetry();
    // currentMapId sofort auf die Ziel-ID setzen, damit das Frontend während
    // des Ladens weiß, welche Karte geladen wird, und finishLoadMap korrekt
    // ausgelöst wird. startLoadMap versucht denselben Zustand über updateSocket
    // zu setzen, falls das Workflow-Store bereits registriert ist.
    socketStore.update((s) => ({ ...s, currentMapMeta: null, currentMapId: id, isLoadingMap: true, isNewMap: false }));
    mapMetaStore.set(null);
    // The report describes the route of the map we are leaving.
    clearRouteReport();
    const req: RequestSocketMessage = {
      type: RequestDataType.loadMap,
      data: { id, discardCurrent },
    };
    this.sendMessage(req);
  }

  sendSaveMap(name: string, rotation: number = 0) {
    // Während des Speicherns temporär den unsaved-Zustand zurücksetzen, damit
    // das Dropdown nicht zwischenzeitlich einen ungültigen Eintrag anzeigt.
    // Die Rotation wird beibehalten, da sie gerade gespeichert wird.
    socketStore.update((s) => ({ ...s, currentMapMeta: null, isLoadingMap: false }));
    mapMetaStore.set(null);
    const req: RequestSocketMessage = {
      type: RequestDataType.saveMap,
      data: { name, rotation },
    };
    this.sendMessage(req);
  }

  sendRenameMap(id: string, name: string) {
    const req: RequestSocketMessage = {
      type: RequestDataType.renameMap,
      data: { id, name },
    };
    this.sendMessage(req);
  }

  sendDeleteMap(id: string) {
    const req: RequestSocketMessage = {
      type: RequestDataType.deleteMap,
      data: { id },
    };
    this.sendMessage(req);
  }

  sendDiscardMap() {
    const req: RequestSocketMessage = {
      type: RequestDataType.discardMap,
      data: {},
    };
    this.sendMessage(req);
  }

  sendSetActiveMap(id: string) {
    const req: RequestSocketMessage = {
      type: RequestDataType.setActiveMap,
      data: { id },
    };
    this.sendMessage(req);
  }

  sendImportMap(json: string, name: string, rotation: number = 0) {
    const req: RequestSocketMessage = {
      type: RequestDataType.importMap,
      data: { json, name, rotation },
    };
    this.sendMessage(req);
  }

  sendExportMap() {
    const req: RequestSocketMessage = {
      type: RequestDataType.exportMap,
      data: {},
    };
    this.sendMessage(req);
  }

  sendSetSchedule(schedule: ScheduleData) {
    const req: RequestSocketMessage = {
      type: RequestDataType.setSchedule,
      data: schedule,
    };
    this.sendMessage(req);
  }

  sendSaveSchedule() {
    const req: RequestSocketMessage = {
      type: RequestDataType.saveSchedule,
      data: {},
    };
    this.sendMessage(req);
  }

  sendRequestSchedule() {
    const req: RequestSocketMessage = {
      type: RequestDataType.requestSchedule,
      data: {},
    };
    this.sendMessage(req);
  }

  sendRequestFirmwareStatus(force = false) {
    const req: RequestSocketMessage = {
      type: RequestDataType.requestFirmwareStatus,
      data: { force },
    };
    this.sendMessage(req);
  }

  sendRequestClock() {
    const req: RequestSocketMessage = {
      type: RequestDataType.requestClock,
      data: {},
    };
    this.sendMessage(req);
  }

  sendCommand(action: string, payload?: Record<string, any>) {
    const req: RequestSocketMessage = {
      type: RequestDataType.robotCommand,
      data: { action, ...(payload || {}) },
    };
    this.sendMessage(req);
  }

  disconnect() {
    this.reconnect = false;
    this.clearAllTimers();
    this.pendingMessages = [];

    socketStore.update((state) => {
      if (state.socket) {
        if (
          state.socket.readyState === WebSocket.OPEN ||
          state.socket.readyState === WebSocket.CONNECTING
        ) {
          state.socket.close();
        }
      }
      return { ...state, socket: null, connected: false };
    });
  }

  private clearAllTimers() {
    if (this.restartTimer != null) {
      clearTimeout(this.restartTimer);
      this.restartTimer = null;
    }
    if (this.connectionTimeout != null) {
      clearTimeout(this.connectionTimeout);
      this.connectionTimeout = null;
    }
    if (this.stableConnectionTimer != null) {
      clearTimeout(this.stableConnectionTimer);
      this.stableConnectionTimer = null;
    }
    this.clearHeartbeatTimers();
  }

  private clearHeartbeatTimers() {
    if (this.pingInterval != null) {
      clearInterval(this.pingInterval);
      this.pingInterval = null;
    }
    if (this.pongTimeout != null) {
      clearTimeout(this.pongTimeout);
      this.pongTimeout = null;
    }
  }

  private startHeartbeat(socket: WebSocket) {
    this.clearHeartbeatTimers();
    this.pingInterval = setInterval(() => {
      if (socket.readyState !== WebSocket.OPEN || this.pongTimeout != null) return;

      try {
        socket.send(JSON.stringify({ type: RequestDataType.ping, data: {} }));
        this.pongTimeout = setTimeout(() => {
          this.pongTimeout = null;
          if (socket.readyState === WebSocket.OPEN) {
            try { socket.close(1000, "Pong timeout"); } catch (_) {}
          }
        }, SocketService.PONG_TIMEOUT_MS);
      } catch (_) {
        try { socket.close(); } catch (_) {}
      }
    }, SocketService.PING_INTERVAL_MS);
  }

  private handleVisibilityChange() {
    if (!browser) return;

    this.isPageVisible = !document.hidden;

    if (this.isPageVisible) {
      const socket = get(socketStore).socket;
      if (!socket || socket.readyState !== WebSocket.OPEN) {
        this.reconnectAttempts = 0;
        this.connect();
      } else {
        // Der Socket hat das Ausblenden überlebt, aber clearAllTimers() hat den
        // Heartbeat gestoppt. Ohne Neustart bleibt die Verbindung für den Rest
        // der Sitzung ungeprüft und halb offene Verbindungen fallen nicht auf.
        this.startHeartbeat(socket);
      }
    } else {
      this.clearAllTimers();
    }
  }

  destroy() {
    if (!browser) return;

    // bind() erzeugt bei jedem Aufruf eine neue Funktion; mit einem frisch
    // gebundenen Handler hätte removeEventListener nichts entfernt.
    document.removeEventListener("visibilitychange", this.boundVisibilityChange);
    this.disconnect();
  }
}

export const socketService = new SocketService();
