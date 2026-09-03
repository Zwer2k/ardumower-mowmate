<script lang="ts">
  import type { Settings } from "../model";
  import type { Option } from "../model/ui";
  import SelectSetting from "../widget/SelectSetting.svelte";
  import NumberSetting from "../widget/NumberSetting.svelte";
  import Group from "./Group.svelte";

  export let settings: Settings.Position;
  export let original: Settings.Position;

  const modes: Option<"relative" | "absolute">[] = [
    { label: "Relative", value: "relative" },
    { label: "Absolute", value: "absolute" },
  ];

</script>

<Group title="Robot position" {settings} {original}>
  <SelectSetting
    label="Position mode"
    key="position.mode"
    bind:value={settings.mode}
    bind:original={original.mode}
    options={modes}
  />
  {#if settings.mode === "absolute"}
    <NumberSetting
      label="Reference longitude"
      key="position.lon"
      helpText="Reference longitude used for absolute coordinates"
      min={-180}
      max={180}
      step={0.000001}
      bind:value={settings.lon}
      bind:original={original.lon}
    />
    <NumberSetting
      label="Reference latitude"
      key="position.lat"
      helpText="Reference latitude used for absolute coordinates"
      min={-90}
      max={90}
      step={0.000001}
      bind:value={settings.lat}
      bind:original={original.lat}
    />
  {/if}
</Group>
