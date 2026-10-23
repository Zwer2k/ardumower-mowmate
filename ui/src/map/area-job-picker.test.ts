import { render, fireEvent } from "@testing-library/svelte";
import { tick } from "svelte";
import { describe, expect, it } from "vitest";
import AreaJobPicker from "./AreaJobPicker.svelte";
import type { MapMeta } from "../model";

const meta = (id: string, name: string, parentId?: string): MapMeta =>
  ({ id, name, area: 1, hash: "", crc: 0, rotation: 0, timestamp: 0, unsaved: false, ...(parentId ? { parentId } : {}) });
const maps = [meta("m", "Garden"), meta("m1", "Front", "m"), meta("n", "Lawn"), meta("n1", "Back", "n")];

function checked(container: HTMLElement): string[] {
  return [...container.querySelectorAll<HTMLInputElement>("input[type=checkbox]")]
    .filter((i) => i.checked)
    .map((i) => i.closest(".bx--checkbox-wrapper")?.textContent?.trim() ?? "");
}

async function chooseMap(container: HTMLElement, getByText: (t: string, o?: never) => HTMLElement, name: string) {
  await fireEvent.click(container.querySelector(".bx--list-box__field")!);
  await tick();
  await fireEvent.click(getByText(name, { selector: ".bx--list-box__menu-item__option" } as never));
  await tick();
}

describe("AreaJobPicker", () => {
  it("shows no map until one is chosen when nothing is preselected", async () => {
    const { container, getByText } = render(AreaJobPicker, { maps, mapId: "", selected: [], preselectMap: false });
    await tick();
    expect(container.querySelectorAll("input[type=checkbox]").length).toBe(0);
    expect(container.textContent).toContain("Choose a map");
    await chooseMap(container, getByText, "Lawn");
    expect([...container.querySelectorAll("input[type=checkbox]")].length).toBe(2);
  });

  it("ticks a map without areas as soon as it is chosen", async () => {
    const solo = [...maps, meta("p", "Patch")];
    const { container, getByText } = render(AreaJobPicker, { maps: solo, mapId: "", selected: [], preselectMap: false });
    await tick();
    await chooseMap(container, getByText, "Patch");
    expect(checked(container)).toEqual(["Patch (whole map)"]);
  });

  it("clears the ticks when another map is chosen, also the second time", async () => {
    const { container, getByText, getByLabelText } = render(AreaJobPicker, { maps, mapId: "m", selected: [] });
    await fireEvent.click(getByLabelText("Front"));
    await tick();
    expect(checked(container)).toEqual(["Front"]);
    await chooseMap(container, getByText, "Lawn");
    expect(checked(container)).toEqual([]);
    await fireEvent.click(getByLabelText("Back"));
    await tick();
    expect(checked(container)).toEqual(["Back"]);
    await chooseMap(container, getByText, "Garden");
    expect(checked(container)).toEqual([]);
  });
});

import MowNowDialog from "../pages/dashboard/main/MowNowDialog.svelte";
import { socketStore } from "../stores/socket";
import { mowNowSelectionStore } from "../stores/mow-job";

describe("MowNowDialog", () => {
  it("keeps the selected map and areas when reopened, whatever map is loaded", async () => {
    socketStore.update((s) => ({ ...s, maps, currentMapId: "m" }));
    mowNowSelectionStore.set(null);
    const { getByText, getByLabelText, rerender } = render(MowNowDialog, { open: true });
    await tick();
    const box = () => document.body as HTMLElement;
    expect(checked(box())).toEqual(["Garden (whole map)"]);
    await chooseMap(box(), (t, o) => getByText(t, o), "Lawn");
    expect(checked(box())).toEqual([]);
    await fireEvent.click(getByLabelText("Back"));
    await tick();
    await chooseMap(box(), (t, o) => getByText(t, o), "Garden");
    expect(checked(box())).toEqual([]);
    await fireEvent.click(getByLabelText("Front"));
    await tick();
    await rerender({ open: false });
    await tick();
    await rerender({ open: true });
    await tick();
    expect(checked(box())).toEqual(["Front"]);
    // Auch mit einer anderen geladenen Karte: die zuletzt gewählte Karte
    await rerender({ open: false });
    socketStore.update((s) => ({ ...s, currentMapId: "n" }));
    await tick();
    await rerender({ open: true });
    await tick();
    expect(checked(box())).toEqual(["Front"]);
    expect(box().textContent).toContain("Areas of Garden");
  });
});

import JobPreviewMap from "./JobPreviewMap.svelte";
import { areaExportStore } from "../stores/socket";

describe("JobPreviewMap", () => {
  it("numbers the areas once the modem's data arrives, already the first time", async () => {
    socketStore.update((s) => ({ ...s, maps, currentMapId: "n" }));
    areaExportStore.set(null);
    const { container } = render(JobPreviewMap, { areaIds: ["m1", "m"], overlapOnce: false, mode: "areas" });
    await tick();
    expect(container.querySelectorAll("text").length).toBe(0);
    areaExportStore.set({
      mapId: "m",
      geometry: { perimeter: [[0, 0], [10, 0], [10, 10], [0, 10]], exclusions: [], dockpoints: [], rotation: 0 },
      areas: [{ id: "m1", name: "Front", parentId: "m", map: { selection: [{ X: 0, Y: 0 }, { X: 5, Y: 0 }, { X: 5, Y: 5 }] } }],
    });
    await tick();
    expect([...container.querySelectorAll("text")].map((t) => t.textContent)).toEqual(["1", "2"]);
  });
});

import SchedulePreviewToggle from "./overlay/SchedulePreviewToggle.svelte";
import { schedulePreviewStore } from "../stores/schedule";
import { socketService } from "../stores/socket";
import { vi } from "vitest";

describe("SchedulePreviewToggle", () => {
  it("asks for the route again when the areas change in route mode", async () => {
    vi.useFakeTimers();
    const sent = vi.spyOn(socketService, "sendPreviewJob").mockImplementation(() => {});
    schedulePreviewStore.set({ areaIds: ["m1"], overlapOnce: true, mode: "areas" });
    const { getByText } = render(SchedulePreviewToggle);
    await tick();
    await fireEvent.click(getByText("Show route"));
    await vi.advanceTimersByTimeAsync(500);
    expect(sent).toHaveBeenLastCalledWith(["m1"], true);
    schedulePreviewStore.update((p) => p && { ...p, areaIds: ["m1", "m"] });
    await vi.advanceTimersByTimeAsync(500);
    expect(sent).toHaveBeenLastCalledWith(["m1", "m"], true);
    expect(sent).toHaveBeenCalledTimes(2);
    expect(getByText("Putting the route together…")).toBeTruthy();
    sent.mockRestore();
    vi.useRealTimers();
  });
});

import MowAreaToggles from "./MowAreaToggles.svelte";
import { mowJobStore } from "../stores/mow-job";

describe("MowAreaToggles in job mode", () => {
  it("recomposes the job without the switched-off part", async () => {
    const sent = vi.spyOn(socketService, "sendPreviewJob").mockImplementation(() => {});
    mowJobStore.set({
      id: "job:m1,m", crc: 1, timestamp: 1, overlapOnce: true, total: 2, complete: true,
      parts: [{ id: "m1", name: "Front", start: 0 }, { id: "m", name: "Garden", start: 1 }],
      points: [{ x: 0, y: 0 }, { x: 1, y: 1 }], segmentStarts: [0], segment: 0, toggles: 7,
    });
    const { container } = render(MowAreaToggles, { jobMode: true, labelPrefix: "Mow " });
    await tick();
    const inputs = [...container.querySelectorAll<HTMLInputElement>("input[type=checkbox]")];
    expect(inputs.map((i) => i.checked)).toEqual([true, true, true]);
    await fireEvent.click(inputs[1]);
    await tick();
    expect(sent).toHaveBeenLastCalledWith(["m1", "m"], true, 5);
    sent.mockRestore();
    mowJobStore.set(null);
  });
});

describe("MowNowDialog with a single map", () => {
  it("loads the map instead of putting a job together", async () => {
    socketStore.update((s) => ({ ...s, maps, currentMapId: "m" }));
    mowNowSelectionStore.set({ selected: ["n"], overlapOnce: false });
    mowJobStore.set(null);
    const load = vi.spyOn(socketService, "sendLoadMap").mockImplementation(() => {});
    const preview = vi.spyOn(socketService, "sendPreviewJob").mockImplementation(() => {});
    const { getByText } = render(MowNowDialog, { open: true });
    await tick();
    await fireEvent.click(getByText("Load map"));
    expect(load).toHaveBeenCalledWith("n", false, true);
    expect(preview).not.toHaveBeenCalled();
    load.mockRestore();
    preview.mockRestore();
  });
});

import JobPreviewLayer from "./JobPreviewLayer.svelte";

describe("JobPreviewLayer", () => {
  const perimeter = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: -10 }, { x: 0, y: -10 }];

  it("draws nothing for an area whose selection is not known yet, and asks again", async () => {
    vi.useFakeTimers();
    socketStore.update((s) => ({ ...s, maps, currentMapId: "m1" }));
    areaExportStore.set(null);
    const asked = vi.spyOn(socketService, "requestAreas").mockImplementation(() => {});
    const { container } = render(JobPreviewLayer, { areaIds: ["m1"], perimeter });
    await tick();
    // früher: die ganze Karte in Bereichsfarbe, Nummer in der Mitte
    expect(container.querySelectorAll("polygon").length).toBe(0);
    expect(asked).toHaveBeenCalledTimes(1);
    // Antwort ging verloren: erneut fragen
    await vi.advanceTimersByTimeAsync(3100);
    expect(asked).toHaveBeenCalledTimes(2);
    asked.mockRestore();
    vi.useRealTimers();
  });

  it("uses a known selection without waiting for the modem", async () => {
    socketStore.update((s) => ({ ...s, maps, currentMapId: "m1" }));
    areaExportStore.set(null);
    const asked = vi.spyOn(socketService, "requestAreas").mockImplementation(() => {});
    const sel = [{ x: 0, y: 0 }, { x: 5, y: 0 }, { x: 5, y: -5 }];
    const { container } = render(JobPreviewLayer, { areaIds: ["m1"], perimeter, knownSelections: { m1: sel } });
    await tick();
    expect(container.querySelector("polygon")?.getAttribute("points")).toBe("0,0 5,0 5,-5");
    expect(asked).not.toHaveBeenCalled();
    asked.mockRestore();
  });
});
