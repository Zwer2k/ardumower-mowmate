<script lang="ts">
    import { socketService } from '../../../stores/socket';
    import { gamepadStore } from '../../../stores/gamepad';
    import { gamepadMode } from '../../../stores/gamepad-mode';
    import { remoteControlOpen } from '../../../stores/remote-control';

    // ─── Constants ──────────────────────────────────────────────────────────
    const SEND_INTERVAL = 200;

    // ─── State (derived from global gamepad store) ────────────────────────────
    let connected = $derived($gamepadStore.connected);
    let linearSpeed = $derived($gamepadStore.linear);
    let angularSpeed = $derived($gamepadStore.angular);
    let paused = $derived($gamepadStore.paused);
    let lastSendTime = 0;

    // ─── Send drive commands ─────────────────────────────────────────────────
    function sendNow(force = false) {
        // Only send drive commands when in drive mode OR when RC dialog is open
        // (RC dialog open = explicit user intent to drive, overriding map edit mode)
        if ($gamepadMode === 'map-edit' && !$remoteControlOpen) return;
        const now = Date.now();
        if (!force && now - lastSendTime < SEND_INTERVAL) return;
        lastSendTime = now;
        socketService.sendJoystickMove(linearSpeed, angularSpeed);
    }

    function sendZero() {
        if ($gamepadMode === 'map-edit' && !$remoteControlOpen) return;
        socketService.sendJoystickMove(0, 0);
    }

    // React to stick changes from global store
    $effect(() => {
        // Only send when values actually changed and not paused
        if (paused) {
            sendZero();
            return;
        }
        if (linearSpeed !== 0 || angularSpeed !== 0) {
            sendNow();
        }
    });
</script>

<div class="gamepad-control">
    <div class="gp-status">
        <span class="gp-led" class:on={connected}></span>
        <span class="gp-label-status">
            {connected ? '🎮 Controller verbunden' : 'Kein Controller'}
        </span>
    </div>

    {#if connected}
        <div class="gp-readout">
            <span class="gp-val">Lin: {linearSpeed.toFixed(2)}</span>
            <span class="gp-val">Ang: {angularSpeed.toFixed(2)}</span>
        </div>
    {:else}
        <p class="gp-hint">
            Controller per USB/Bluetooth koppeln, dann einen Knopf drücken.
            PS-Knopf = Steuerung pausieren.
        </p>
    {/if}
</div>

<style lang="scss">
    .gamepad-control {
        width: 100%;
        padding: 8px 0 0 0;
        font-size: 0.85em;
        box-sizing: border-box;
    }

    .gp-status {
        display: flex;
        align-items: center;
        gap: 8px;
        font-weight: 600;
        color: #333;
    }

    .gp-led {
        width: 10px;
        height: 10px;
        border-radius: 50%;
        background: #ccc;
        transition: background 0.3s;

        &.on {
            background: #2a9d8f;
            box-shadow: 0 0 6px #2a9d8f;
        }
    }

    .gp-readout {
        display: flex;
        gap: 8px;
        margin-top: 6px;
        font-family: monospace;
        font-size: 0.9em;
    }

    .gp-val {
        display: inline-block;
        min-width: 7ch;
        text-align: center;
        background: #f4f4f4;
        padding: 2px 6px;
        border-radius: 4px;
        border: 1px solid #ddd;
    }

    .gp-hint {
        margin-top: 6px;
        color: #666;
        font-size: 0.9em;
        line-height: 1.3;
    }
</style>
