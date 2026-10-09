<script lang="ts">
    import type { Snippet } from "svelte";

    // Eine Zeile einer StatusCard: Beschriftung links, Wert rechts. Der Wert
    // kommt als Text (value/unit) oder als eigenes Markup (children).
    let {
        label,
        value = undefined,
        unit = "",
        hint = "",
        children = undefined,
    }: {
        label: string;
        value?: string | number;
        unit?: string;
        hint?: string;
        children?: Snippet;
    } = $props();
</script>

<dt title={hint || undefined}>{label}</dt>
<dd>
    {#if children}
        {@render children()}
    {:else}
        <span class="value">{value ?? "—"}</span>{#if unit && value !== undefined && value !== "—"}<span class="unit">{unit}</span>{/if}
    {/if}
</dd>

<style lang="scss">
    dt {
        font-size: 0.75rem;
        color: #525252;
        align-self: center;
        white-space: nowrap;
    }
    dd {
        margin: 0;
        font-size: 0.875rem;
        text-align: right;
        font-variant-numeric: tabular-nums;
        display: flex;
        justify-content: flex-end;
        align-items: center;
        gap: 0.25rem;
        flex-wrap: wrap;
        min-width: 0;
    }
    .value {
        font-weight: 600;
        color: #161616;
    }
    .unit {
        color: #525252;
        font-size: 0.75rem;
    }
    dd :global(.bx--tag) {
        margin: 0;
    }
</style>
