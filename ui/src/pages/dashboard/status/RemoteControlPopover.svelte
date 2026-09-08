<script lang="ts">
    import { browser } from '$app/environment';
    import { fade, scale } from 'svelte/transition';
    import { socketStore } from '../../../stores/socket';
    import Joystick from './Joystick.svelte';
    import GamepadControl from './GamepadControl.svelte';
    import ControlButtons from './ControlButtons.svelte';
    import IconJoystick from "carbon-icons-svelte/lib/GameConsole.svelte";
    import IconMenuDots from "carbon-icons-svelte/lib/OverflowMenuVertical.svelte";
    import { remoteControlOpen } from '../../../stores/remote-control';
    import { RobotCommandService } from '../../../service';

    let open = $state(false);
    let overlayOpen = $state(false);
    let isSmallScreen = $state(false);
    let wrapperRef: HTMLDivElement | null = $state(null);
    let settingsOpen = $state(false);

    // Keep global store in sync so map editor knows when RC dialog is open
    $effect(() => {
        remoteControlOpen.set(open || overlayOpen);
    });

    function updateSmallScreen() {
        if (!browser) return;
        isSmallScreen = window.matchMedia('(max-width: 640px)').matches;
    }

    function toggle() {
        if (!open && !overlayOpen) {
            open = true;
            return;
        }
        if (open && isSmallScreen && !overlayOpen) {
            open = false;
            overlayOpen = true;
            return;
        }
        open = false;
        overlayOpen = false;
        settingsOpen = false;
    }

    function close() {
        open = false;
        overlayOpen = false;
        settingsOpen = false;
    }

    function toggleSettings() {
        settingsOpen = !settingsOpen;
    }

    async function saveDefaults() {
        await RobotCommandService.send('saveMowerDefaults' as any);
        settingsOpen = false;
    }

    async function resetDefaults() {
        await RobotCommandService.send('resetMowerDefaults' as any);
        settingsOpen = false;
    }

    // Close when clicking outside the wrapper
    function onDocClick(e: MouseEvent) {
        if (!open) return;
        if (wrapperRef && !wrapperRef.contains(e.target as Node)) {
            open = false;
        }
    }

    // Close on Escape key
    function onKeyDown(e: KeyboardEvent) {
        if (e.key === 'Escape' && open) {
            open = false;
        }
    }

    $effect(() => {
        if (!browser) return;
        updateSmallScreen();
        const mq = window.matchMedia('(max-width: 640px)');
        const onMqChange = () => updateSmallScreen();
        mq.addEventListener('change', onMqChange);
        document.addEventListener('click', onDocClick);
        document.addEventListener('keydown', onKeyDown);
        return () => {
            mq.removeEventListener('change', onMqChange);
            document.removeEventListener('click', onDocClick);
            document.removeEventListener('keydown', onKeyDown);
        };
    });
</script>

{#if browser}
<div class="rc-wrapper" bind:this={wrapperRef}>
    <button class="rc-trigger"
            class:active={open}
            onclick={toggle}
            aria-label="Remote Control"
            title="Remote Control">
        <IconJoystick />
    </button>

    {#if open}
    <div class="rc-popover">
        <div class="rc-popover-header">
            <span class="rc-title">🎮 Remote Control</span>
            <div class="rc-header-actions">
                <button class="rc-settings" class:open={settingsOpen} onclick={toggleSettings} aria-label="Settings" title="Settings">
                    <IconMenuDots />
                </button>
                <button class="rc-close" onclick={close} aria-label="Close">✕</button>
            </div>
        </div>
        {#if settingsOpen}
        <div class="rc-settings-overlay" transition:fade={{ duration: 100 }} onclick={() => settingsOpen = false}></div>
        <div class="rc-settings-menu" transition:scale={{ duration: 120, start: 0.9 }}>
            <button type="button" class="rc-settings-item" onclick={saveDefaults}>
                💾 Set as Default
            </button>
            <button type="button" class="rc-settings-item" onclick={resetDefaults}>
                🔄 Reset to Defaults
            </button>
        </div>
        {/if}
        <div class="rc-popover-body">
            <div class="rc-joystick-col">
                <Joystick size={180} />
                <GamepadControl />
            </div>
            <div class="rc-divider"></div>
            <div class="rc-controls-col">
                <ControlButtons desiredState={$socketStore.desiredState} />
            </div>
        </div>
    </div>
    {/if}

    {#if overlayOpen}
    <div class="rc-joystick-overlay" aria-label="Joystick overlay">
        <Joystick size={220} showReadout={false} />
    </div>
    {/if}
</div>
{/if}

<style lang="scss">
    .rc-wrapper {
        position: relative;
        display: flex;
        align-items: center;
        height: 100%;
    }

    .rc-trigger {
        display: flex;
        align-items: center;
        justify-content: center;
        width: 48px;
        height: 48px;
        background: transparent;
        border: none;
        color: #f4f4f4;
        cursor: pointer;
        transition: background 0.15s;
        padding: 0;
    }

    .rc-trigger:hover,
    .rc-trigger.active {
        background: #333;
    }

    .rc-trigger :global(svg) {
        width: 20px;
        height: 20px;
        fill: currentColor;
    }

    .rc-popover {
        position: absolute;
        top: 48px;
        right: 0;
        width: 520px;
        background: white;
        border: 1px solid #ddd;
        border-radius: 0 0 8px 8px;
        box-shadow: 0 8px 24px rgba(0,0,0,0.15);
        z-index: 9999;
        padding: 16px;
        display: flex;
        flex-direction: column;
        gap: 12px;
        animation: rc-slide-in 0.15s ease-out;
    }

    @keyframes rc-slide-in {
        from { opacity: 0; transform: translateY(-8px); }
        to   { opacity: 1; transform: translateY(0); }
    }

    .rc-popover-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        border-bottom: 1px solid #eee;
        padding-bottom: 8px;
    }

    .rc-title {
        font-size: 0.95em;
        font-weight: 600;
        color: #333;
    }

    .rc-close {
        width: 28px;
        height: 28px;
        display: flex;
        align-items: center;
        justify-content: center;
        background: transparent;
        border: none;
        border-radius: 4px;
        color: #888;
        cursor: pointer;
        font-size: 1em;
        line-height: 1;
        transition: background 0.15s, color 0.15s;

        &:hover {
            background: #f4f4f4;
            color: #333;
        }
    }

    .rc-header-actions {
        display: flex;
        align-items: center;
        gap: 4px;
    }

    .rc-settings {
        width: 28px;
        height: 28px;
        display: flex;
        align-items: center;
        justify-content: center;
        background: transparent;
        border: none;
        border-radius: 4px;
        color: #888;
        cursor: pointer;
        transition: background 0.15s, color 0.15s;

        &:hover,
        &.open {
            background: #f4f4f4;
            color: #333;
        }

        :global(svg) {
            width: 18px;
            height: 18px;
            fill: currentColor;
        }
    }

    .rc-settings-overlay {
        position: fixed;
        inset: 0;
        z-index: 10000;
        cursor: default;
    }

    .rc-settings-menu {
        position: absolute;
        top: 36px;
        right: 32px;
        z-index: 10001;
        min-width: 180px;
        display: flex;
        flex-direction: column;
        padding: 4px;
        background: white;
        border: 1px solid #e0e0e0;
        border-radius: 8px;
        box-shadow: 0 4px 16px rgba(0, 0, 0, 0.15), 0 1px 4px rgba(0, 0, 0, 0.1);
    }

    .rc-settings-item {
        display: flex;
        align-items: center;
        gap: 8px;
        width: 100%;
        padding: 8px 12px;
        background: transparent;
        border: none;
        border-radius: 4px;
        color: #333;
        font-size: 0.85em;
        cursor: pointer;
        text-align: left;
        transition: background 0.15s;

        &:hover {
            background: #f0f0f0;
        }
    }

    .rc-popover-body {
        display: flex;
        gap: 16px;
        align-items: flex-start;
    }

    .rc-joystick-col {
        flex: 0 0 200px;
        width: 200px;
        display: flex;
        flex-direction: column;
        align-items: center;
    }

    .rc-divider {
        width: 1px;
        background: #ddd;
        align-self: stretch;
        min-height: 180px;
    }

    .rc-controls-col {
        flex: 1;
        min-width: 0;
    }

    .rc-joystick-overlay {
        position: fixed;
        right: 0;
        bottom: 0;
        padding: 12px;
        z-index: 9999;
        background: transparent;
        pointer-events: none;
    }

    .rc-joystick-overlay :global(*) {
        pointer-events: auto;
    }

    /* ─── Smartphone / narrow screens ────────────────────────────────────── */
    @media (max-width: 640px) {
        .rc-popover {
            position: fixed;
            top: 48px;
            left: 0;
            right: 0;
            width: 100vw;
            height: calc(100vh - 48px);
            border-radius: 0;
            border: none;
            border-top: 1px solid #ddd;
            box-shadow: none;
            padding: 12px;
            justify-content: flex-start;
            overflow-y: auto;
        }

        .rc-popover-body {
            flex-direction: column;
            align-items: center;
            gap: 12px;
        }

        .rc-divider {
            display: none;
        }

        .rc-joystick-col {
            width: 100%;
            align-items: center;
        }

        .rc-controls-col {
            width: 100%;
        }
    }
</style>
