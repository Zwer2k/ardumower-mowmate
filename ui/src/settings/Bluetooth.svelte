<script lang="ts">
  import type { ChangeEventValue, Settings } from "../model";
  import { InlineNotification } from "carbon-components-svelte";
  import CheckboxSetting from "../widget/CheckboxSetting.svelte";
  import TextSetting from "../widget/TextSetting.svelte";
import BluetoothClean from "./BluetoothClean.svelte";
  import Group from "./Group.svelte";

  export let settings: Settings.Bluetooth;
  export let original: Settings.Bluetooth;

  export let ps4ControllerSettings: Settings.PS4Controller;

  const enableChange = (ev: ChangeEventValue) => {
    ps4ControllerSettings.enabled = false;
  }
</script>

<Group title="Bluetooth" {settings} {original}>
  <InlineNotification
    kind="info"
    lowContrast
    hideCloseButton
    title="Bluetooth slows down WiFi."
    subtitle="Bluetooth and WiFi share one radio. While Bluetooth is on, the modem has to keep WiFi in power-save mode: the web UI, map transfers and firmware uploads get much slower (measured about 190 ms instead of 30 ms response time, 6-9 KB/s instead of 300-570 KB/s). Turn Bluetooth off unless you use the Bluetooth app."
  />
  <CheckboxSetting
    label="Control and monitor your ArduMower with Bluetooth"
    helpText="Only needed for the Bluetooth app."
    key="bluetooth.enabled"
    bind:value={settings.enabled}
    bind:original={original.enabled}
    on:change={(e) => enableChange(e.detail)}
  />
  <svelte:fragment slot="enabled">
    <CheckboxSetting
      label="Bluetooth pairings require a PIN code"
      key="bluetooth.pin_enabled"
      bind:value={settings.pin_enabled}
      bind:original={original.pin_enabled}
    />

    <TextSetting
      label="PIN"
      key="bluetooth.pin"
      placeholder="Bluetooth pairing PIN"
      kind="password"
      disabled={!settings.pin_enabled}
      bind:value={settings.pin}
      bind:original={original.pin}
    />

    <BluetoothClean
      bind:bluetoothSettings={settings}
      bind:ps4ControllerSettings={ps4ControllerSettings}
    />
  </svelte:fragment>
</Group>
