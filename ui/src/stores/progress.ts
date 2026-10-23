// Ein Fortschrittsbalken für Operationen des Modems. Nach einer Berechnung
// (oder dem Löschen der Wegpunkte) überträgt das Modem die neue Karte; früher
// lief dafür ein zweiter, halbtransparenter Balken über der Karte, während der
// erste noch bei "Sending route..." stand. Jetzt geht die Übertragung als
// letzter Abschnitt in denselben Balken.
import { derived, writable } from "svelte/store";
import { socketStore, pendingOperationStore } from "./socket";
import { mapChunkProgress } from "../map/map-chunk-buffer";
import { mowJobTransferStore } from "./mow-job";

/** Operationen, auf die eine Übertragung der Karte folgt */
const OPS_WITH_TRANSFER = new Set(["calculate", "clear"]);
/** Anteil der Übertragung am Balken (Prozent) */
const TRANSFER_SHARE = 20;
/** So lange nach dem Ende der Operation auf den Beginn der Übertragung warten
 *  (ms); bei schlechtem WLAN kommt das erste Stück der Karte spät */
const TRANSFER_WAIT_MS = 30000;

export interface ProgressView {
  /** null: Spinner ohne Prozentangabe */
  percent: number | null;
  label: string;
  /** Die Kartenübertragung ist Teil dieses Balkens (MapLoadingOverlay schweigt) */
  includesTransfer: boolean;
}

// Übertragung, die zu einer Operation gehört
let linkedTransfer = false;
// Operation mit Übertragung lief beim letzten Stand noch
let wasBusyWithTransfer = false;
// Ende dieser Operation; bis TRANSFER_WAIT_MS danach gehört eine beginnende
// Übertragung noch dazu ("Complete" kann vor dem ersten Kartenstück ankommen)
let finishedAt = 0;
// Neu auswerten, wenn die Wartezeit abläuft
const waitExpired = writable(0);
let waitTimer: ReturnType<typeof setTimeout> | null = null;

export const operationProgress = derived(
  [socketStore, mapChunkProgress, pendingOperationStore, mowJobTransferStore, waitExpired],
  ([$socket, $chunk, $pending, $jobTransfer]): ProgressView | null => {
    const state = $socket?.state;
    const op = state?.progressOp ?? "";
    const pct = state?.progressPct ?? 0;
    const busy = !!op && pct < 100;
    const withTransfer = OPS_WITH_TRANSFER.has(op);
    const now = Date.now();

    // Fehlgeschlagen: Es kommt keine Karte
    if (wasBusyWithTransfer && !busy && state?.progressMsg !== "Failed") {
      finishedAt = now;
      if (waitTimer) clearTimeout(waitTimer);
      waitTimer = setTimeout(() => {
        waitTimer = null;
        waitExpired.update((n) => n + 1);
      }, TRANSFER_WAIT_MS);
    }
    wasBusyWithTransfer = busy && withTransfer;
    const waiting = !busy && finishedAt > 0 && now - finishedAt < TRANSFER_WAIT_MS;

    if (!$chunk) linkedTransfer = false;
    else if ((busy && withTransfer) || waiting) linkedTransfer = true;

    if (linkedTransfer && $chunk) {
      finishedAt = 0;
      const fraction = $chunk.total > 0 ? Math.min(1, $chunk.received / $chunk.total) : 0;
      return {
        percent: Math.round(100 - TRANSFER_SHARE + TRANSFER_SHARE * fraction),
        label: $chunk.label || "Loading map",
        includesTransfer: true,
      };
    }
    if (busy) {
      return {
        percent: withTransfer ? Math.round((pct * (100 - TRANSFER_SHARE)) / 100) : pct,
        label: state?.progressMsg || "Working...",
        includesTransfer: false,
      };
    }
    // Operation fertig, die Karte kommt gleich: Balken stehen lassen
    if (waiting) return { percent: 100 - TRANSFER_SHARE, label: "Loading map", includesTransfer: true };
    // Vorschau eines Auftrags: Die Route kommt nach dem Zusammensetzen in Stücken
    if ($pending?.op === "previewJob" && $jobTransfer && $jobTransfer.total > 0) {
      return {
        percent: Math.max(1, Math.round((100 * $jobTransfer.received) / $jobTransfer.total)),
        label: "Loading route",
        includesTransfer: false,
      };
    }
    if ($pending) return { percent: null, label: $pending.label, includesTransfer: false };
    return null;
  },
);
