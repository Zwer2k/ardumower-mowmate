import { get } from "svelte/store";
import { currentMapRotationStore } from "../service";
import { setMapDirty } from "../services/map-sync";

export interface CompassDragState {
  rotationManuallySet: boolean;
  lastRotationMapId: string | null;
  setRotation: (r: number) => void;
  reset: () => void;
  onDown: (event: MouseEvent) => void;
}

export function createCompassState(): CompassDragState {
  const state: CompassDragState = {
    rotationManuallySet: false,
    lastRotationMapId: null,
    setRotation: (r: number) => {
      const normalized = ((r % 360) + 360) % 360;
      currentMapRotationStore.set(normalized);
      setMapDirty(true);
    },
    reset: () => {
      currentMapRotationStore.set(0);
    },
    onDown: (event: MouseEvent) => {
      const nextState = { ...state, rotationManuallySet: true };
      Object.assign(state, nextState);
      event.preventDefault();
      const btn = event.currentTarget as HTMLElement;
      const rect = btn.getBoundingClientRect();
      const centerX = rect.left + rect.width / 2;
      const centerY = rect.top + rect.height / 2;
      const startAngle = Math.atan2(event.clientY - centerY, event.clientX - centerX) * 180 / Math.PI;
      let lastAngle = startAngle;
      let didDrag = false;
      let currentRotation = get(currentMapRotationStore);

      function onMove(e: MouseEvent) {
        didDrag = true;
        const angle = Math.atan2(e.clientY - centerY, e.clientX - centerX) * 180 / Math.PI;
        let delta = angle - lastAngle;
        if (delta > 180) delta -= 360;
        if (delta < -180) delta += 360;
        currentRotation = ((currentRotation + delta) % 360 + 360) % 360;
        state.setRotation(currentRotation);
        lastAngle = angle;
      }

      function onUp() {
        window.removeEventListener("mousemove", onMove);
        window.removeEventListener("mouseup", onUp);
        if (!didDrag) {
          currentRotation = ((currentRotation + 90) % 360 + 360) % 360;
          state.setRotation(currentRotation);
        }
      }

      window.addEventListener("mousemove", onMove);
      window.addEventListener("mouseup", onUp);
    },
  };
  return state;
}

export function updateCompassFromMeta(
  state: CompassDragState,
  currentMapId: string | undefined,
  metaRotation: number | undefined
): CompassDragState {
  let next = state;
  if (currentMapId !== undefined && currentMapId !== state.lastRotationMapId) {
    next = { ...next, lastRotationMapId: currentMapId, rotationManuallySet: false };
  }
  if (!next.rotationManuallySet && metaRotation !== undefined) {
    const r = ((metaRotation % 360) + 360) % 360;
    if (get(currentMapRotationStore) !== r) {
      currentMapRotationStore.set(r);
    }
  }
  return next;
}
