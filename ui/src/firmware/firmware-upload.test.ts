import { render, fireEvent, waitFor } from "@testing-library/svelte";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import FirmwareUpload from "./FirmwareUpload.svelte";
import { applyFirmwareStatus, resetFirmwareStatus } from "../stores/socket";

/** Antwort von /api/modem/info – der Uploader fragt sie vor jedem Upload ab. */
const modemInfo = {
  git_hash: "abc",
  git_time: "",
  git_tag: "v1.4.0",
  build_time: "",
  uptime: 10000,
  firmware_target: "esp32-s3",
};

let githubStatus: Record<string, unknown>;

function mockFetch() {
  return vi.fn(async (input: any) => {
    const url = String(input);
    if (url.startsWith("/api/modem/info")) {
      return new Response(JSON.stringify(modemInfo), { status: 200 });
    }
    if (url.startsWith("/api/modem/ota/github/status")) {
      return new Response(JSON.stringify(githubStatus), { status: 200 });
    }
    if (url.startsWith("/api/modem/ota/github")) {
      return new Response(JSON.stringify({ success: true }), { status: 200 });
    }
    throw new Error(`unexpected fetch: ${url}`);
  });
}

/** XHR, der den Upload startet und dann offen bleibt. */
class PendingXhr {
  static last: PendingXhr | null = null;
  upload = { addEventListener: vi.fn() };
  status = 200;
  responseText = "";
  open = vi.fn();
  send = vi.fn();
  addEventListener = vi.fn();
  constructor() {
    PendingXhr.last = this;
  }
}

const checkedStatus = {
  reachable: true,
  checking: false,
  updateAvailable: true,
  checked: true,
  current: "v1.4.0",
  latest: "v1.5.0",
  target: "esp32-s3",
  versions: ["v1.5.0", "v1.4.0"],
};

describe("firmware dialog while an upload is running", () => {
  beforeEach(() => {
    resetFirmwareStatus();
    githubStatus = {
      active: true,
      success: false,
      buffered: true,
      downloadProgress: 40,
      downloadTotal: 100,
      flashProgress: 0,
      flashTotal: 100,
    };
    vi.stubGlobal("fetch", mockFetch());
    vi.stubGlobal("XMLHttpRequest", PendingXhr);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("keeps the github download progress when a re-check reports versions", async () => {
    applyFirmwareStatus(checkedStatus);
    const { getByText, queryByText, container } = render(FirmwareUpload, { open: true });

    await waitFor(() => getByText(/Install v1.5.0/));
    await fireEvent.click(getByText(/Install v1.5.0/));

    await waitFor(() => expect(container.textContent).toMatch(/Firmware herunterladen/));
    expect(queryByText(/Install v1.5.0/)).toBeNull();

    // Neuer Check läuft an und liefert danach die Liste.
    applyFirmwareStatus({ ...checkedStatus, checking: true, checked: false, versions: undefined });
    await waitFor(() => expect(container.textContent).toMatch(/Firmware herunterladen/));
    applyFirmwareStatus({ ...checkedStatus, versions: ["v1.6.0", "v1.5.0", "v1.4.0"] });

    await new Promise((r) => setTimeout(r, 0));
    expect(container.textContent).toMatch(/Firmware herunterladen/);
    expect(queryByText(/Install v1.6.0/)).toBeNull();
  });

  it("keeps the file upload progress when a re-check reports versions", async () => {
    // Noch kein Ergebnis vom Modem: Der Dialog zeigt nur die lokale Datei an.
    applyFirmwareStatus({ reachable: false, checking: true, updateAvailable: false, checked: false });
    const { container, queryByText } = render(FirmwareUpload, { open: true });

    const input = await waitFor(() => {
      const el = container.querySelector('input[type="file"]') as HTMLInputElement;
      if (!el) throw new Error("no file input");
      return el;
    });

    const file = new File(["x".repeat(64)], "esp32-s3-firmware.bin");
    Object.defineProperty(input, "files", { value: [file], configurable: true });
    await fireEvent.change(input);

    await waitFor(() => expect(container.textContent).toMatch(/Upload progress/));

    // Der Hintergrund-Check meldet jetzt Erreichbarkeit und Versionen.
    applyFirmwareStatus(checkedStatus);
    await new Promise((r) => setTimeout(r, 0));

    expect(container.textContent).toMatch(/Upload progress/);
    expect(queryByText(/Install v1.5.0/)).toBeNull();
    expect(container.querySelector('input[type="file"]')).toBeNull();
  });
});

describe("manual selection while the release list is refreshing", () => {
  beforeEach(() => {
    resetFirmwareStatus();
    githubStatus = {
      active: true,
      success: false,
      buffered: true,
      downloadProgress: 40,
      downloadTotal: 100,
      flashProgress: 0,
      flashTotal: 100,
    };
    vi.stubGlobal("fetch", mockFetch());
    vi.stubGlobal("XMLHttpRequest", PendingXhr);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  async function pickFile(container: HTMLElement) {
    const input = await waitFor(() => {
      const el = container.querySelector('input[type="file"]') as HTMLInputElement;
      if (!el) throw new Error("no file input");
      return el;
    });
    const file = new File(["x".repeat(64)], "firmware.bin");
    Object.defineProperty(input, "files", { value: [file], configurable: true });
    await fireEvent.change(input);
  }

  async function selectDropdownItem(container: HTMLElement, label: string, item: string) {
    const wrapper = [...container.querySelectorAll(".bx--list-box__wrapper")].find((el) =>
      el.querySelector(".bx--label")?.textContent?.includes(label),
    ) as HTMLElement | undefined;
    if (!wrapper) throw new Error(`no dropdown for ${label}`);
    await fireEvent.click(wrapper.querySelector(".bx--list-box__field") as HTMLElement);
    const entry = [...wrapper.querySelectorAll(".bx--list-box__menu-item")].find((el) =>
      el.textContent?.includes(item),
    ) as HTMLElement | undefined;
    if (!entry) throw new Error(`no item ${item} in ${label}`);
    await fireEvent.click(entry);
  }

  it("manual type selection: file upload survives the arriving version list", async () => {
    applyFirmwareStatus({ reachable: false, checking: true, updateAvailable: false, checked: false });
    const { container } = render(FirmwareUpload, { open: true });

    await selectDropdownItem(container, "Select firmware type", "Modem Firmware");
    await pickFile(container);
    await waitFor(() => expect(container.textContent).toMatch(/Upload progress/));

    applyFirmwareStatus(checkedStatus);
    await new Promise((r) => setTimeout(r, 0));

    expect(container.textContent).toMatch(/Upload progress/);
    expect(container.textContent).not.toMatch(/Install v1.5.0/);
  });

  it("manual source selection: file upload survives the arriving version list", async () => {
    applyFirmwareStatus({ ...checkedStatus, versions: [] });
    const { container } = render(FirmwareUpload, { open: true });

    await selectDropdownItem(container, "Update source", "Local file");
    await pickFile(container);
    await waitFor(() => expect(container.textContent).toMatch(/Upload progress/));

    applyFirmwareStatus({ ...checkedStatus, checking: true, checked: false, versions: undefined });
    applyFirmwareStatus(checkedStatus);
    await new Promise((r) => setTimeout(r, 0));

    expect(container.textContent).toMatch(/Upload progress/);
    expect(container.textContent).not.toMatch(/Install v1.5.0/);
  });

  it("manual mower selection: flash progress survives the arriving version list", async () => {
    applyFirmwareStatus({ reachable: false, checking: true, updateAvailable: false, checked: false });
    const { container } = render(FirmwareUpload, { open: true });

    await selectDropdownItem(container, "Select firmware type", "Mower Firmware");
    await pickFile(container);
    await waitFor(() => expect(container.textContent).toMatch(/Upload progress/));

    applyFirmwareStatus(checkedStatus);
    await new Promise((r) => setTimeout(r, 0));

    expect(container.textContent).toMatch(/Upload progress/);
    expect(container.textContent).not.toMatch(/Install v1.5.0/);
  });
});
