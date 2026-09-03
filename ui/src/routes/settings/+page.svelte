<script lang="ts">
  import { beforeNavigate } from "$app/navigation";
  import { get } from "svelte/store";
  import { Dirty } from "../../stores/dirty";
  import { BackendSettings } from "../../stores/backend";
  import { FrontendSettings } from "../../stores/frontend";
  import SettingsList from "../../settings/SettingsList.svelte";

  beforeNavigate(({ cancel }) => {
    if (!get(Dirty)) return;

    const discard = window.confirm(
      "Ungespeicherte Einstellungen verwerfen?"
    );
    if (!discard) {
      cancel();
      return;
    }

    const backendSettings = get(BackendSettings);
    if (backendSettings) {
      FrontendSettings.set(JSON.parse(JSON.stringify(backendSettings)));
    }
  });
</script>

<SettingsList />
