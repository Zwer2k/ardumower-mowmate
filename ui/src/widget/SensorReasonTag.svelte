<script lang="ts">
  import { Tag } from "carbon-components-svelte";
  import { SensorDescriptions, sensorTagType } from "../model";

  /** Sunray stateSensor (Grund des letzten Stopps), 0 = keiner */
  export let sensor: number;
</script>

<Tag type={sensorTagType(sensor)} size="sm" title="Grund des letzten Stopps oder Fehlers">
  {SensorDescriptions[sensor] ?? `#${sensor}`}
</Tag>
