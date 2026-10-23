<script lang="ts">
  import BluetoothSettings from "./Bluetooth.svelte";
  import PS4ControllerSettings from "./PS4Controller.svelte";
  import GeneralSettings from "./General.svelte";
  import TimeSettings from "./Time.svelte";
  import WebSettings from "./Web.svelte";
  import WiFiSettings from "./WiFi.svelte";
  import MqttSettings from "./Mqtt.svelte";
  import PrometheusSettings from "./Prometheus.svelte";
  import PositionSettings from "./Position.svelte";
  import MowerSettings from "./Mower.svelte";
  import TerminalSettings from "./Terminal.svelte";
  import Firmware from "./Firmware.svelte";
  import Storage from "./Storage.svelte";
  import { FrontendSettings as settings } from "../stores/frontend";
  import { BackendSettings as original } from "../stores/backend";
  import { InfoStore as info } from "../stores/info";
  import Reload from "./Reload.svelte";

  let debug;
  $: debug = JSON.stringify($settings, null, 2);
</script>

<Reload/>
{#if $settings}
  <div class="settings">
    <GeneralSettings
      bind:settings={$settings.general}
      bind:original={$original.general}
    />
    <WebSettings
      bind:settings={$settings.web}
      bind:original={$original.web}
    />
    <WiFiSettings
      bind:settings={$settings.wifi}
      bind:original={$original.wifi}
    />
    <TimeSettings
      bind:settings={$settings.time}
      bind:original={$original.time}
    />
    <!-- Funktionen, die die Firmware nicht enthält (*_supported: false), ausblenden;
         ältere Firmware meldet sie nicht und hat alle -->
    {#if $info?.ble_supported !== false}
    <BluetoothSettings
      bind:settings={$settings.bluetooth}
      bind:original={$original.bluetooth}
      bind:ps4ControllerSettings={$settings.ps4controller}
    />
    {/if}
    {#if !$settings.bluetooth.enabled && $settings.ps4controller && $original.ps4controller}
      <PS4ControllerSettings
        bind:settings={$settings.ps4controller}
        bind:original={$original.ps4controller}
        bind:bluetoothSettings={$settings.bluetooth}
        bind:info={$info}
      />
    {/if}
    {#if $info?.mqtt_supported !== false}
    <MqttSettings
      bind:settings={$settings.mqtt}
      bind:original={$original.mqtt}
      bind:allSettings={$settings}
    />
    {/if}
    {#if $info?.prometheus_supported !== false}
    <PrometheusSettings
      bind:settings={$settings.prometheus}
      bind:original={$original.prometheus}
    />
    {/if}
    <PositionSettings
      bind:settings={$settings.position}
      bind:original={$original.position}
    />
    {#if $settings.mower && $original.mower}
      <MowerSettings
        bind:settings={$settings.mower}
        bind:original={$original.mower}
      />
    {/if}
    {#if ($info?.terminal_supported ?? $info?.terminal_available) && $settings.mower?.support_terminal !== false && $settings.terminal && $original.terminal}
      <TerminalSettings
        bind:settings={$settings.terminal}
        bind:original={$original.terminal}
        info={$info}
      />
    {/if}
    <Firmware />
    <Storage />
  </div>
{/if}
  <!-- <pre>
    {debug}
  </pre> -->

<style lang="scss">
  .settings {
    :global(.bx--form-item) {
        margin-top: 0.5rem;
    }

    :global(.bx--label) {
        margin-bottom: 0.1rem;
    }

    :global(.bx--toggle-input__label .bx--toggle__switch) {
        margin-top: 0.1rem !important;
    }
  }
</style>