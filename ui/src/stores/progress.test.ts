import { describe, expect, it } from "vitest";
import { get } from "svelte/store";
import { socketStore } from "./socket";
import { mapChunkProgress } from "../map/map-chunk-buffer";
import { operationProgress } from "./progress";
import { pendingOperationStore } from "./socket";
import { mowJobTransferStore } from "./mow-job";

function setOp(op: string, pct: number, msg = "") {
  socketStore.update((s) => ({
    ...s,
    state: { ...(s.state ?? {}), progressOp: op, progressPct: pct, progressMsg: msg } as typeof s.state,
  }));
}

describe("operationProgress", () => {
  it("shows the calculation and the following map transfer in one bar", () => {
    const unsubscribe = operationProgress.subscribe(() => {});
    setOp("calculate", 50, "Calculating waypoints...");
    expect(get(operationProgress)).toMatchObject({ percent: 40, includesTransfer: false });

    setOp("calculate", 96, "Sending route...");
    mapChunkProgress.set({ received: 50, total: 100, label: "Loading waypoints" });
    expect(get(operationProgress)).toMatchObject({ percent: 90, label: "Loading waypoints", includesTransfer: true });

    // "Complete" vor dem Ende der Übertragung: derselbe Balken läuft weiter
    setOp("calculate", 100, "Complete");
    expect(get(operationProgress)).toMatchObject({ percent: 90, includesTransfer: true });

    // Übertragung fertig: Balken weg
    mapChunkProgress.set(null);
    expect(get(operationProgress)).toBeNull();
    unsubscribe();
  });

  it("bridges the gap when Complete arrives before the first map chunk", () => {
    const unsubscribe = operationProgress.subscribe(() => {});
    setOp("calculate", 96, "Sending route...");
    setOp("calculate", 100, "Complete");
    expect(get(operationProgress)).toMatchObject({ percent: 80, includesTransfer: true });
    mapChunkProgress.set({ received: 0, total: 100, label: "Preparing map" });
    expect(get(operationProgress)).toMatchObject({ percent: 80, label: "Preparing map", includesTransfer: true });
    mapChunkProgress.set(null);
    expect(get(operationProgress)).toBeNull();
    unsubscribe();
  });

  it("keeps a plain map load on the map overlay", () => {
    const unsubscribe = operationProgress.subscribe(() => {});
    setOp("save", 100, "Map saved");
    mapChunkProgress.set({ received: 10, total: 100, label: "Loading map" });
    expect(get(operationProgress)).toBeNull();
    mapChunkProgress.set(null);
    unsubscribe();
  });

  it("shows the job route while it arrives after the preview was put together", () => {
    const unsubscribe = operationProgress.subscribe(() => {});
    setOp("", 100);
    pendingOperationStore.set({ op: "previewJob", label: "Loading route..." });
    expect(get(operationProgress)).toMatchObject({ percent: null, label: "Loading route..." });
    mowJobTransferStore.set({ received: 600, total: 1200 });
    expect(get(operationProgress)).toMatchObject({ percent: 50, label: "Loading route" });
    mowJobTransferStore.set(null);
    pendingOperationStore.set(null);
    expect(get(operationProgress)).toBeNull();
    unsubscribe();
  });

  it("does not wait for a transfer that ended before Complete arrived", () => {
    const unsubscribe = operationProgress.subscribe(() => {});
    setOp("calculate", 40, "Calculating waypoints...");
    setOp("calculate", 96, "Sending route...");
    mapChunkProgress.set({ received: 100, total: 200, label: "Loading waypoints" });
    expect(get(operationProgress)).toMatchObject({ percent: 90, includesTransfer: true });
    // Übertragung fertig, das Modem schickt "Complete" erst danach
    mapChunkProgress.set(null);
    expect(get(operationProgress)).toMatchObject({ percent: 100, includesTransfer: true });
    setOp("calculate", 100, "Complete");
    expect(get(operationProgress)).toBeNull();
    unsubscribe();
  });
});
