<script lang="ts">
  import type { Info, Settings } from "../model";
  import type { Option } from "../model/ui";
  import SelectSetting from "../widget/SelectSetting.svelte";
  import Group from "./Group.svelte";

  export let settings: Settings.Terminal;
  export let original: Settings.Terminal;
  export let info: Info | undefined = undefined;

  const bauds: Option<number>[] = [
    { label: "Auto (detect)", value: 0 },
    ...[9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600].map((b) => ({ label: `${b}`, value: b })),
  ];

  const formats: Option<"8N1" | "8E1" | "8O1">[] = [
    { label: "8N1 (Sunray)", value: "8N1" },
    { label: "8E1 (even parity)", value: "8E1" },
    { label: "8O1 (odd parity)", value: "8O1" },
  ];

  function stateText(i: Info | undefined): string {
    if (!i?.terminal_baud) return "";
    switch (i.terminal_baud_state) {
      case "auto": return `Detected: ${i.terminal_baud} baud.`;
      case "detecting": return "Detecting the baud rate…";
      case "failed": return `No readable text at any baud rate; using ${i.terminal_baud} baud.`;
      case "unconfirmed": return `Using ${i.terminal_baud} baud; no text from the mower yet to confirm it.`;
      default: return `Using ${i.terminal_baud} baud.`;
    }
  }

  $: baudHelp = settings.baud === 0
    ? `Tries the common baud rates when the received text is unreadable. Needs output from the mower. ${original.baud === 0 ? stateText(info) : ""}`
    : "Baud rate of the mower console on the terminal port.";
</script>

<Group title="Terminal" {settings} {original}>
  <SelectSetting
    label="Baud rate"
    key="terminal.baud"
    helpText={baudHelp}
    bind:value={settings.baud}
    bind:original={original.baud}
    options={bauds}
  />
  <SelectSetting
    label="Data format"
    key="terminal.format"
    helpText="Data bits, parity and stop bits. Cannot be detected automatically; Sunray uses 8N1."
    bind:value={settings.format}
    bind:original={original.format}
    options={formats}
  />
</Group>
