<script lang="ts">
  import "carbon-components-svelte/css/g10.css";
  import { Button } from "carbon-components-svelte";
  import IconClear from "carbon-icons-svelte/lib/Close.svelte";
  import IconSave from "carbon-icons-svelte/lib/Checkmark.svelte";

  import { Busy } from "../stores/busy";
  import { Dirty } from "../stores/dirty";
  import { FrontendSettings } from "../stores/frontend";
  import { BackendSettings } from "../stores/backend";
  import type { Readable } from "svelte/store";
  import type { Settings } from "../model";

  // Read current value from store without creating a subscription.
  // This avoids the endless loop: subscribe -> set -> subscribe -> ...
  const getSnapshot = (store: Readable<Settings>): Settings | undefined => {
    let value: Settings | undefined;
    const unsub = store.subscribe((s: Settings) => { value = s; });
    unsub();
    return value;
  };

  function save() {
    const snapshot = getSnapshot(FrontendSettings);
    if (snapshot) {
      BackendSettings.commit(JSON.parse(JSON.stringify(snapshot)));
    }
  }

  function revert() {
    const snapshot = getSnapshot(BackendSettings);
    if (snapshot) {
      FrontendSettings.set(JSON.parse(JSON.stringify(snapshot)));
    }
  }
</script>

{#if $Dirty}
  <Button
    on:click={save}
    disabled={$Busy}
    kind="primary"
    icon={IconSave}
    iconDescription="Save changes"
  />
  <Button
    on:click={revert}
    disabled={$Busy}
    kind="secondary"
    icon={IconClear}
    iconDescription="Revert changes"
  />
{/if}
