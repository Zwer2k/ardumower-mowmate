<script lang="ts">
  import type { Settings } from "../model";
  import CheckboxSetting from "../widget/CheckboxSetting.svelte";
  import Group from "./Group.svelte";

  export let settings: Settings.Mower;
  export let original: Settings.Mower;
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
</Group>
