<script lang="ts">
    import type { Snippet } from "svelte";
    import { Tile } from "carbon-components-svelte";

    // Karte im Stil des Haupt-Dashboards: Carbon-Tile mit Icon und Titel,
    // darunter Zeilen aus Beschriftung und Wert (StatusRow).
    let { title, icon = "", children }: { title: string; icon?: string; children: Snippet } = $props();
</script>

<Tile class="status-card">
    <div class="card-header">
        {#if icon}<span class="card-icon">{icon}</span>{/if}
        <span class="card-title">{title}</span>
    </div>
    <dl class="card-rows">
        {@render children()}
    </dl>
</Tile>

<style lang="scss">
    :global(.status-card) {
        padding: 0.75rem 1rem !important;
        min-height: 0 !important;
        height: 100%;
    }
    .card-header {
        display: flex;
        align-items: center;
        gap: 0.5rem;
        margin-bottom: 0.5rem;
        padding-bottom: 0.375rem;
        border-bottom: 1px solid #e0e0e0;
    }
    .card-icon {
        font-size: 1.125rem;
        line-height: 1;
    }
    .card-title {
        font-size: 0.875rem;
        font-weight: 600;
        color: #161616;
    }
    .card-rows {
        display: grid;
        grid-template-columns: max-content 1fr;
        column-gap: 1rem;
        row-gap: 0.25rem;
        margin: 0;
    }
</style>
