<script lang="ts">
    import IconInformation from "carbon-icons-svelte/lib/Information.svelte";
    import { firmwareStatusStore } from '../stores/socket';
    import { firmwareDialogOpen } from '../firmware/update-store';

    // Das Modem prüft GitHub im Hintergrund. Erst wenn es dort eine neuere
    // Version gefunden hat, taucht das Symbol im Header auf.
    let status = $derived($firmwareStatusStore);
    let title = $derived(
        status.latest
            ? `Firmware ${status.latest} available (installed: ${status.current ?? 'unknown'})`
            : 'Firmware update available',
    );
</script>

{#if status.updateAvailable}
    <button
        class="fw-badge"
        onclick={() => firmwareDialogOpen.set(true)}
        aria-label={title}
        {title}>
        <IconInformation />
    </button>
{/if}

<style>
    .fw-badge {
        display: flex;
        align-items: center;
        justify-content: center;
        width: 48px;
        height: 48px;
        background: transparent;
        border: none;
        color: #f1c21b;
        cursor: pointer;
        transition: background 0.15s;
        padding: 0;

        &:hover {
            background: #333;
        }

        :global(svg) {
            width: 20px;
            height: 20px;
            fill: currentColor;
        }
    }
</style>
