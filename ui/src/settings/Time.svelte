<script lang="ts">
  import type { Settings } from "../model";
  import type { Option } from "../model/ui";
  import SelectSetting from "../widget/SelectSetting.svelte";
  import TextSetting from "../widget/TextSetting.svelte";
  import Group from "./Group.svelte";

  export let settings: Settings.Time;
  export let original: Settings.Time;

  const timezones: Option<string>[] = [
    { label: "Central Europe (CET/CEST)", value: "CET-1CEST,M3.5.0,M10.5.0/3" },
    { label: "Western Europe (WET/WEST)", value: "WET0WEST,M3.5.0/1,M10.5.0/3" },
    { label: "United Kingdom (GMT/BST)", value: "GMT0BST,M3.5.0/1,M10.5.0/3" },
    { label: "UTC", value: "UTC0" },
    { label: "Eastern US (EST/EDT)", value: "EST5EDT,M3.2.0,M11.1.0" },
    { label: "Central US (CST/CDT)", value: "CST6CDT,M3.2.0,M11.1.0" },
    { label: "Mountain US (MST/MDT)", value: "MST7MDT,M3.2.0,M11.1.0" },
    { label: "Pacific US (PST/PDT)", value: "PST8PDT,M3.2.0,M11.1.0" },
    { label: "Japan (JST)", value: "JST-9" },
  ];
</script>

<Group title="Time & NTP" {settings} {original}>
  <SelectSetting
    label="Timezone"
    key="time.tz"
    helpText="POSIX timezone string used for the scheduler and all local time display."
    bind:value={settings.tz}
    bind:original={original.tz}
    options={timezones}
  />
  <TextSetting
    label="NTP server 1"
    key="time.ntp_server1"
    helpText="Primary NTP server. Can be a hostname or an IP address."
    bind:value={settings.ntp_server1}
    bind:original={original.ntp_server1}
  />
  <TextSetting
    label="NTP server 2"
    key="time.ntp_server2"
    helpText="Fallback NTP server. Can be a hostname or an IP address."
    bind:value={settings.ntp_server2}
    bind:original={original.ntp_server2}
  />
</Group>
