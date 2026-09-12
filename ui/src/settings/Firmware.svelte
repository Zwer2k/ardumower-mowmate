<script lang="ts">
  import {
    StructuredList,
    StructuredListBody,
    StructuredListRow,
    StructuredListCell
  } from "carbon-components-svelte";
  import Group from "./Group.svelte";
  import {InfoStore} from "../stores/info"
</script>

<Group title="Firmware" open={true}>
  {#if $InfoStore}
    <StructuredList>
      <StructuredListBody>
        <StructuredListRow>
          <StructuredListCell>Tag</StructuredListCell>
          <StructuredListCell>{$InfoStore.git_tag}</StructuredListCell>
        </StructuredListRow>
        <StructuredListRow>
          <StructuredListCell>Timestamp</StructuredListCell>
          <StructuredListCell>{$InfoStore.git_time}</StructuredListCell>
        </StructuredListRow>
        <StructuredListRow>
          <StructuredListCell>Source Version</StructuredListCell>
          <StructuredListCell>{$InfoStore.git_hash}</StructuredListCell>
        </StructuredListRow>
        <StructuredListRow>
          <StructuredListCell>Build Time</StructuredListCell>
          <StructuredListCell>{$InfoStore.build_time}</StructuredListCell>
        </StructuredListRow>
      </StructuredListBody>
    </StructuredList>
  {/if}
</Group>
