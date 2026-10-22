import { describe, expect, it, vi } from "vitest";
import { socketService, socketStore } from "./socket";
import { RequestDataType } from "../model";

const mapData = (n: number) => ({
  perimeter: [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 1, y: 1 }],
  exclusions: [], dockpoints: [], searchWire: [],
  waypoints: Array.from({ length: n }, (_, i) => ({ x: i, y: 0 })),
  selection: [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 1, y: 1 }],
  rotation: 0,
}) as never;

describe("setMap after a calculation", () => {
  it("leaves the waypoints out for a selection change and calculates after the ack", () => {
    socketStore.update((s) => ({ ...s, currentMapId: "area-1" }));
    const svc = socketService as unknown as Record<string, unknown>;
    const sent: { type: number; data: Record<string, unknown> }[] = [];
    const target = socketService as unknown as { sendMessage: (m: unknown) => void };
    const spy = vi.spyOn(target, "sendMessage").mockImplementation((m) => { sent.push(m as (typeof sent)[number]); });
    socketService.sendMap(mapData(5000), { keepWaypoints: true });
    const setMap = sent.find((m) => m.type === RequestDataType.setMap)!;
    expect(setMap.data.waypoints).toEqual([]);
    expect(setMap.data.keepWaypoints).toBe(true);
    expect(setMap.data.waypointCount).toBe(5000);

    socketService.sendCalculateWaypoints();
    expect(sent.some((m) => m.type === RequestDataType.calculateWaypoints)).toBe(false);
    // Bestätigung des Modems simulieren
    svc.lastSetMap = null;
    (svc.flushCalculateAfterSync as () => void).call(socketService);
    expect(sent.some((m) => m.type === RequestDataType.calculateWaypoints)).toBe(true);
    spy.mockRestore();
  });
});
