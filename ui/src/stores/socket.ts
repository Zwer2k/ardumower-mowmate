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
} from "../model";
import { clearWaypointsBuffer, handleMapChunk, resetMapChunkBuffer } from "../map/map-chunk-buffer";
import { MapPointType } from "../map/map-chunk-buffer";
import { mowSettingsStore } from "../map/mow-settings";
import { currentMapRotationStore, updateDrivenTrack } from "../map/service";
import { mapWorkflowStore } from "../map/workflow/map-workflow-store";
import { setMapDirty } from "../map/services/map-sync";
import { saveCachedMap, isCachedMapCurrent, loadCachedMap } from "../map/map-cache";
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

class SocketService {
  private restartTimer: NodeJS.Timeout | null = null;
  private reconnect = true;
  private reconnectAttempts = 0;
  private maxReconnectAttempts = 10;
  private connectionTimeout: NodeJS.Timeout | null = null;
  private heartbeatInterval: NodeJS.Timeout | null = null;
  private livenessInterval: NodeJS.Timeout | null = null;
  private lastMessageTime: number = 0;
  private isPageVisible = true;
  private pendingMessages: RequestSocketMessage[] = [];
  private nextMapSyncId = 1;
  private pendingUpload: { mapData: import("../model").MapSetData; syncId: number; mapId: string } | null = null;
  private pendingUploadRetry: ReturnType<typeof setTimeout> | null = null;

  constructor() {
    if (browser) {
      document.addEventListener(
        "visibilitychange",
        this.handleVisibilityChange.bind(this),
      );
    }
  }

  connect() {
    if (!browser) {
      return;
    }

    socketStore.update((state) => {
      if (
        state.socket != null &&
        (state.socket.readyState === WebSocket.CONNECTING ||
         state.socket.readyState === WebSocket.CLOSING)
      ) {
        return state;
      }

      if (state.socket != null && state.socket.readyState === WebSocket.OPEN) {
        return state;
      }

      if (!this.reconnect || !this.isPageVisible) {
        return state;
      }

      this.clearAllTimers();
      this.pendingMessages = [];

      if (state.socket != null) {
        state.socket.close();
      }

      let host = location.host;
      const wsProtocol = location.protocol === "https:" ? "wss:" : "ws:";

      try {
        const socket = new WebSocket(wsProtocol + "//" + host + "/ws");

        this.connectionTimeout = setTimeout(() => {
          if (socket && socket.readyState === WebSocket.CONNECTING) {
            socket.close();
          }
        }, 5000);

        socket.addEventListener("open", () => {
          // Ignoriere Events von veralteten Sockets
          let isCurrent = false;
          socketStore.update((s) => {
            isCurrent = s.socket === socket;
            return s;
          });
          if (!isCurrent) {
            try { socket.close(); } catch (_) {}
            return;
          }

          this.reconnectAttempts = 0;

          if (this.connectionTimeout) {
            clearTimeout(this.connectionTimeout);
            this.connectionTimeout = null;
          }

          this.startHeartbeat(socket);
          this.startLivenessCheck(socket);

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
          if (this.heartbeatInterval) {
            clearInterval(this.heartbeatInterval);
            this.heartbeatInterval = null;
          }
          if (this.livenessInterval) {
            clearInterval(this.livenessInterval);
            this.livenessInterval = null;
          }
          // Socket im close-Handler auf null setzen; hier nur explizit
          // schließen, falls der Browser kein close-Event feuert.
          try { socket.close(); } catch (_) {}
        });

        socket.addEventListener("close", () => {
          // Ignoriere Events von veralteten Sockets
          let isCurrent = false;
          socketStore.update((s) => {
            isCurrent = s.socket === socket;
            return s;
          });
          if (!isCurrent) {
            return;
          }

          this.clearAllTimers();
          socketStore.update((s) => ({ ...s, socket: null, connected: false }));

          if (
            this.reconnect &&
            this.isPageVisible &&
            this.reconnectAttempts < this.maxReconnectAttempts
          ) {
            this.reconnectAttempts++;
            const delay = Math.min(
              1000 * Math.pow(2, this.reconnectAttempts - 1),
              30000,
            );
            this.restartTimer = setTimeout(() => {
              this.connect();
            }, delay);
          }
        });

        socket.addEventListener("message", async (message: any) => {
          this.lastMessageTime = Date.now();
          try {
            const jsonData = JSON.parse(message.data);
            const msgType = jsonData.type as ResponseDataType;

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
                  // (ohne lines). Solche Nachrichten ignorieren, damit der
                  // Upload-Dialog in FirmwareUpload.svelte sie verarbeitet.
                  if (!Array.isArray(incomingLines)) {
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
                    }
                    break;
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
              mwf.setError("Karte konnte nicht geladen werden");
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

        return { ...state, socket };
      } catch (error) {
        this.clearAllTimers();
        this.reconnectAttempts++;
        if (
          this.reconnect &&
          this.reconnectAttempts < this.maxReconnectAttempts &&
          this.isPageVisible
        ) {
          const delay = Math.min(
            1000 * Math.pow(2, this.reconnectAttempts - 1),
            30000,
          );
          this.restartTimer = setTimeout(() => {
            this.connect();
          }, delay);
        }
        return state;
      }
    });
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
    const req: RequestSocketMessage = {
      type: RequestDataType.setMap,
      data: { ...mapData, syncId, mapId },
    };
    this.sendMessage(req);
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
    // currentMapId sofort auf die Ziel-ID setzen, damit das Frontend während
    // des Ladens weiß, welche Karte geladen wird, und finishLoadMap korrekt
    // ausgelöst wird. startLoadMap versucht denselben Zustand über updateSocket
    // zu setzen, falls das Workflow-Store bereits registriert ist.
    socketStore.update((s) => ({ ...s, currentMapMeta: null, currentMapId: id, isLoadingMap: true, isNewMap: false }));
    mapMetaStore.set(null);
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
    if (this.heartbeatInterval != null) {
      clearInterval(this.heartbeatInterval);
      this.heartbeatInterval = null;
    }
    if (this.livenessInterval != null) {
      clearInterval(this.livenessInterval);
      this.livenessInterval = null;
    }
  }

  private startLivenessCheck(socket: WebSocket) {
    this.lastMessageTime = Date.now();
    if (this.livenessInterval) {
      clearInterval(this.livenessInterval);
      this.livenessInterval = null;
    }
    this.livenessInterval = setInterval(() => {
      if (socket.readyState !== WebSocket.OPEN) {
        if (this.livenessInterval) {
          clearInterval(this.livenessInterval);
          this.livenessInterval = null;
        }
        return;
      }
      if (Date.now() - this.lastMessageTime > 45000) {
        // Keine Nachricht in 45 Sekunden - Verbindung wahrscheinlich tot (z.B. ESP-Neustart)
        try { socket.close(1000, "Liveness timeout"); } catch (_) {}
      }
    }, 15000);
  }

  private startHeartbeat(socket: WebSocket) {
    if (this.heartbeatInterval) {
      clearInterval(this.heartbeatInterval);
    }

    this.heartbeatInterval = setInterval(() => {
      if (socket && socket.readyState === WebSocket.OPEN) {
        try {
          socket.send(JSON.stringify({ type: "ping" }));
        } catch (error) {
          if (socket) {
            socket.close();
          }
        }
      }
    }, 30000);
  }

  private handleVisibilityChange() {
    if (!browser) return;

    this.isPageVisible = !document.hidden;

    if (this.isPageVisible) {
      socketStore.update((state) => {
        if (!state.socket || state.socket.readyState !== WebSocket.OPEN) {
          this.reconnectAttempts = 0;
          this.connect();
        }
        return state;
      });
    } else {
      this.clearAllTimers();
    }
  }

  destroy() {
    if (!browser) return;

    document.removeEventListener(
      "visibilitychange",
      this.handleVisibilityChange.bind(this),
    );
    this.disconnect();
  }
}

export const socketService = new SocketService();
