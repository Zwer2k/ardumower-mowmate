<script lang="ts">
  import { Button, NumberInput } from "carbon-components-svelte";
  import IconClear from "carbon-icons-svelte/lib/CloseOutline.svelte";
  import { Busy } from "../stores/busy";
  import { Invalid } from "../stores/invalid";
  import { TextService } from "../text";

  export let label: string;
  export let key: string;
  export let helpText: string = "";
  export let value: number;
  export let original: number;
  export let disabled: boolean = false;
  export let required: boolean = true;
  export let step: number = 0.000001;
  export let min: number | undefined = undefined;
  export let max: number | undefined = undefined;

  let displayValue: number | "";
  $: displayValue = value ?? "";

  let dirty = false;
  $: dirty = value !== original;

  let labelMod = label;
  $: labelMod = dirty ? `${label} (*)` : label;

  let invalid = false;
  let invalidText: string | undefined;
  $: invalid = $Invalid === key;
  $: invalidText = !invalid ? undefined : TextService.invalidTextFor(key);

  function handleChange(e: CustomEvent<number | string | null>) {
    const v = e.detail;
    if (v === "" || v === null || v === undefined || Number.isNaN(Number(v))) {
      value = 0;
    } else {
      value = Number(v);
    }
  }

  function revert() {
    value = original;
    Invalid.set("");
  }
</script>

<main>
  <NumberInput
    bind:value={displayValue}
    on:change={handleChange}
    labelText={labelMod}
    helperText={helpText}
    disabled={disabled || $Busy}
    {required}
    {invalid}
    {invalidText}
    {step}
    {min}
    {max}
  />
  {#if dirty}
    <Button
      on:click={revert}
      disabled={$Busy}
      iconDescription="Revert changes"
      kind="ghost"
      icon={IconClear}
    />
  {/if}
</main>

<style lang="scss">
  main {
    display: flex;
    flex-direction: row;
  }
</style>
