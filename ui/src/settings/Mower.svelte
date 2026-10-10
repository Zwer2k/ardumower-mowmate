<script lang="ts">
  import type { Settings } from "../model";
  import CheckboxSetting from "../widget/CheckboxSetting.svelte";
  import Group from "./Group.svelte";

  export let settings: Settings.Mower;
  export let original: Settings.Mower;

  // Ältere Firmware kennt das Feld nicht: dann wie bisher erlaubt.
  $: if (settings.support_firmware_upload === undefined) settings.support_firmware_upload = true;
  $: if (original.support_firmware_upload === undefined) original.support_firmware_upload = true;
</script>

<Group title="Mower capabilities" {settings} {original}>
  <CheckboxSetting
    label="Support cutter speed"
    key="mower.support_cutter_speed"
    helpText="Mower can set the cutter speed. Hides the Cutter speed slider when off."
    bind:value={settings.support_cutter_speed}
    bind:original={original.support_cutter_speed}
  />
  <CheckboxSetting
    label="Support cutter height"
    key="mower.support_cutter_height"
    helpText="Mower has a motorized cutter height adjustment. Hides the Mow Ht slider when off."
    bind:value={settings.support_cutter_height}
    bind:original={original.support_cutter_height}
  />
  <CheckboxSetting
    label="Has sonar"
    key="mower.has_sonar"
    helpText="Mower has sonar sensors. Hides the Sonar toggle when off."
    bind:value={settings.has_sonar}
    bind:original={original.has_sonar}
  />
  <CheckboxSetting
    label="Mower firmware upload"
    key="mower.support_firmware_upload"
    helpText="Flash the mower controller's firmware through the modem (STM32 boards wired to BOOT0/NRST, e.g. MOW800). Hides the Mower Firmware option in the firmware dialog when off."
    bind:value={settings.support_firmware_upload}
    bind:original={original.support_firmware_upload}
  />
</Group>
