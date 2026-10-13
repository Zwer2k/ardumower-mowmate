import { writable, get, type Readable } from "svelte/store";
import type { SocketState } from "../../stores/socket";
import { socketService, socketStore } from "../../stores/socket";

export interface GotoState {
  targetSet: boolean;
  targetPos: { x: number; y: number } | null;
  isDriving: boolean;
  /** Der Mäher fährt die Route (Job MOW) seit dem Start gesehen */
  routeStarted: boolean;
  targetDist: number;
  targetBearing: number;
}

// Sunray fährt eine AT+R-Route im Job MOW und wechselt am Ziel auf IDLE.
const JOB_MOW = 1;
// Sunray gilt ein Ziel bei 0,1 m als erreicht, bleibt der Mäher davor hängen,
// nach 8 s auch bei bis zu 0,5 m.
export const GOTO_REACHED_DISTANCE = 0.6;

const NO_TARGET = {
  targetSet: false,
  targetPos: null,
  isDriving: false,
  routeStarted: false,
  targetDist: 0,
  targetBearing: 0,
};

export interface GotoStore {
  subscribe: ReturnType<typeof writable<GotoState>>['subscribe'];
  setTarget(x: number, y: number): void;
  startDrive(): void;
  stopDrive(): void;
  clearTarget(): void;
  /** Mit jeder neuen Mäherposition aufrufen: Entfernung, Richtung, Ziel erreicht */
  updateFromMower(mowerPos: { x: number; y: number } | null, job: number | null): void;
  /** Ziel verwerfen, ohne dem Mäher etwas zu senden */
  reset(): void;
}

export function createGotoState(): GotoStore {
  const store = writable<GotoState>({ ...NO_TARGET });

  const update = (partial: Partial<GotoState>) => {
    store.update((state) => ({ ...state, ...partial }));
  };

  return {
    subscribe: store.subscribe,
    setTarget(x: number, y: number) {
      update(setGotoTarget(x, y));
    },
    startDrive() {
      update(startDriveImpl(get(store).targetPos));
    },
    stopDrive() {
      update(stopDriveImpl());
    },
    clearTarget() {
      update(clearTargetImpl());
    },
    updateFromMower(mowerPos, job) {
      const state = get(store);
      const next = updateGotoFromMower(state, mowerPos, job);
      if (next !== state) store.set(next);
    },
    reset() {
      store.set({ ...NO_TARGET });
    },
  };
}

/**
 * Gemeinsames Goto-Ziel von Karteneditor und Main-Dashboard: ein Ziel, das in
 * einer Ansicht gesetzt wurde, ist auch in der anderen zu sehen und zu fahren.
 */
export const gotoStore = createGotoState();

let gotoMapId: string | null = null;
socketStore.subscribe((s) => {
  // Ein Ziel gehört zur Karte, auf der es gesetzt wurde.
  if (gotoMapId !== null && s.currentMapId !== gotoMapId && !get(gotoStore).isDriving) gotoStore.reset();
  gotoMapId = s.currentMapId;
  gotoStore.updateFromMower(s.state?.position ?? null, s.state?.job ?? null);
});

/** Zeit, in der ein zweiter Klick das Ziel setzt */
export const GOTO_SELECT_MS = 2500;

export interface MapSelection {
  /** true, solange der Hinweis "Tap again ..." gezeigt wird */
  hint: Readable<boolean>;
  /** Klick auf die Karte; true = Goto-Ziel jetzt setzen */
  click(): boolean;
  /** Andere Aktion (Karte verschoben, Bedienelement benutzt) */
  interact(): void;
  destroy(): void;
}

/**
 * Der erste Klick auf die Karte wählt sie nur aus und zeigt einen Hinweis;
 * erst ein zweiter Klick innerhalb von ms setzt das Ziel. Weitere Klicks
 * innerhalb von ms nach dem vorigen versetzen das Ziel direkt. Danach, oder
 * nach einer anderen Aktion, braucht es wieder zwei Klicks.
 */
export function createMapSelection(ms = GOTO_SELECT_MS): MapSelection {
  const hint = writable(false);
  let selected = false;
  let timer: ReturnType<typeof setTimeout> | null = null;
  const reset = () => {
    if (timer) clearTimeout(timer);
    timer = null;
    selected = false;
    hint.set(false);
  };
  const restart = () => {
    if (timer) clearTimeout(timer);
    timer = setTimeout(reset, ms);
  };
  return {
    hint: { subscribe: hint.subscribe },
    click() {
      restart();
      if (!selected) {
        selected = true;
        hint.set(true);
        return false;
      }
      hint.set(false);
      return true;
    },
    interact: reset,
    destroy: reset,
  };
}

/**
 * Entfernung/Richtung zum Ziel nachführen und das Ende der Fahrt erkennen:
 * Wechselt der Mäher nach der Route von MOW zurück, ist die Fahrt vorbei. Steht
 * er am Ziel, verschwindet das Ziel; sonst (abgebrochen, Fehler) bleibt es für
 * einen neuen Versuch stehen.
 */
export function updateGotoFromMower(
  state: GotoState,
  mowerPos: { x: number; y: number } | null,
  job: number | null
): GotoState {
  let next = updateGotoFromMowerPosition(state, mowerPos);
  if (!next.isDriving || job === null) return next;
  if (job === JOB_MOW) {
    return next.routeStarted ? next : { ...next, routeStarted: true };
  }
  if (!next.routeStarted) return next;  // Mäher hat die Fahrt noch nicht begonnen
  if (mowerPos && next.targetDist <= GOTO_REACHED_DISTANCE) return { ...NO_TARGET };
  return { ...next, isDriving: false, routeStarted: false };
}

export function updateGotoFromMowerPosition(
  state: GotoState,
  mowerPos: { x: number; y: number } | null
): GotoState {
  if (!mowerPos || !state.targetPos) return state;
  const mx = mowerPos.x;
  const my = -mowerPos.y;
  const tx = state.targetPos.x;
  const ty = state.targetPos.y;
  const dx = tx - mx;
  const dy = ty - my;
  const targetDist = Math.sqrt(dx * dx + dy * dy);
  const targetBearing = (Math.atan2(dx, -dy) * 180) / Math.PI;
  if (targetDist === state.targetDist && targetBearing === state.targetBearing) return state;
  return { ...state, targetDist, targetBearing };
}

export function setGotoTarget(x: number, y: number): Partial<GotoState> {
  return {
    targetSet: true,
    targetPos: { x, y },
    isDriving: false,
    routeStarted: false,
    targetDist: 0,
    targetBearing: 0,
  };
}

function startDriveImpl(targetPos: { x: number; y: number } | null): Partial<GotoState> {
  if (!targetPos) return {};
  socketService.sendNavigateTo(targetPos.x, -targetPos.y);
  return { isDriving: true, routeStarted: false };
}

function stopDriveImpl(): Partial<GotoState> {
  socketService.sendJoystickMove(0, 0);
  return { isDriving: false, routeStarted: false };
}

function clearTargetImpl(): Partial<GotoState> {
  socketService.sendJoystickMove(0, 0);
  return { ...NO_TARGET };
}

export function canShowGoto(
  $socketStore: SocketState,
  edit: boolean,
  showManage: boolean,
  showCalculate: boolean,
  needsUpload: boolean
): boolean {
  return (
    !edit &&
    !showManage &&
    !showCalculate &&
    !needsUpload &&
    $socketStore.state !== null
  );
}

export function hasMap($MapStore: { map?: { perimeter?: { points?: unknown[] } } }): boolean {
  return ($MapStore?.map?.perimeter?.points?.length ?? 0) > 0;
}
