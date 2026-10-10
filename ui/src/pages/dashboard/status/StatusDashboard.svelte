<script lang="ts">
    import { Tag, Tile } from "carbon-components-svelte";
    import StatusCard from "./StatusCard.svelte";
    import StatusRow from "./StatusRow.svelte";
    import SensorEvents from "./SensorEvents.svelte";
    import { socketStore, socketService } from '../../../stores/socket';
    import { MapStore } from '../../../map/service';
    import { isMowerMapSynced } from '../../../map/services/map-sync';
    import { getModemInfo, type ApiModemInfoResponse } from '../../../firmware/service';
    import { SensorDescriptions } from '../../../model';
    import { page } from '$app/stores';
    import { browser } from '$app/environment';

    type TagType = "green" | "red" | "blue" | "cyan" | "teal" | "gray" | "warm-gray" | "magenta" | "purple";

    // Das Dashboard bleibt im DOM und wird nur per CSS versteckt; Sensor-Daten
    // und Modem-Infos werden nur abgefragt, solange es sichtbar ist.
    const visible = $derived(browser && $page.url?.searchParams?.get('dashboard') === 'status');

    let modemInfo = $state<ApiModemInfoResponse | null>(null);
    $effect(() => {
        if (!visible) {
            socketService.stopSensorSummary();
            return;
        }
        socketService.requestSensorSummary();
        let cancelled = false;
        const poll = async () => {
            try {
                const info = await getModemInfo();
                if (!cancelled) modemInfo = info;
            } catch (_) {}
        };
        poll();
        const timer = setInterval(poll, 10000);
        return () => {
            cancelled = true;
            clearInterval(timer);
        };
    });

    const mower = $derived($socketStore.state);
    const stats = $derived($socketStore.stats);
    const desired = $derived($socketStore.desiredState);
    const desc = $derived($socketStore.valueDescriptions);
    const pos = $derived(mower?.position);
    const totalPoints = $derived($MapStore?.map?.waypoints?.points?.length ?? 0);
    const hasMap = $derived(($MapStore?.map?.perimeter?.points?.length ?? 0) > 0);
    const mapSynced = $derived(isMowerMapSynced(mower, $socketStore.currentMapId, $socketStore.currentMapMeta?.crc ?? 0));

    function jobTag(job: number | undefined): TagType {
        switch (job) {
            case 1: return "green";   // MOW
            case 2: return "blue";    // CHARGE
            case 3: return "red";     // ERROR
            case 4: return "cyan";    // DOCK
            default: return "gray";   // IDLE
        }
    }

    function solutionTag(solution: number | undefined): { type: TagType; text: string } {
        switch (solution) {
            case 2: return { type: "green", text: "Fix" };
            case 1: return { type: "warm-gray", text: "Float" };
            case 0: return { type: "red", text: "Ungültig" };
            default: return { type: "gray", text: "—" };
        }
    }

    function rssiTag(rssi: number): { type: TagType; text: string } {
        if (rssi >= -60) return { type: "green", text: "gut" };
        if (rssi >= -70) return { type: "teal", text: "ok" };
        if (rssi >= -80) return { type: "warm-gray", text: "schwach" };
        return { type: "red", text: "sehr schwach" };
    }

    // Sunray zählt die Dauern in Sekunden hoch.
    function duration(seconds: number | undefined): string {
        const s = Number.isFinite(seconds) && (seconds ?? 0) > 0 ? Math.round(seconds!) : 0;
        if (s < 60) return `${s} s`;
        const m = Math.floor(s / 60);
        if (m < 60) return `${m} min`;
        const h = Math.floor(m / 60);
        return `${h} h ${m % 60} min`;
    }

    function uptime(ms: number | undefined): string {
        return duration((ms ?? 0) / 1000);
    }

    function kb(bytes: number | undefined): string {
        return ((bytes ?? 0) / 1024).toFixed(0);
    }

    function temp(value: number | undefined): string {
        return value != null && value > -100 && value < 200 ? value.toFixed(1) : "—";
    }

    // Anteil der Mähzeit mit Fix, Float und ungültiger Position.
    const gpsShare = $derived.by(() => {
        const fix = stats?.mow_fix ?? 0, float = stats?.mow_float ?? 0, invalid = stats?.mow_invalid ?? 0;
        const total = fix + float + invalid;
        if (total <= 0) return null;
        return {
            fix: (fix / total) * 100,
            float: (float / total) * 100,
            invalid: (invalid / total) * 100,
        };
    });
</script>

<div class="dashboard-content">
    {#if browser && desc != null}
        <!-- Kurzüberblick -->
        <div class="summary">
            <Tile class="summary-tile">
                <span class="summary-label">Status</span>
                <Tag type={jobTag(mower?.job)} size="sm">{desc.job[mower?.job ?? 0] ?? "—"}</Tag>
            </Tile>
            <Tile class="summary-tile">
                <span class="summary-label">Meldung</span>
                {#if (mower?.sensor ?? 0) === 0}
                    <Tag type="green" size="sm">keine</Tag>
                {:else}
                    <Tag type="red" size="sm">{SensorDescriptions[mower?.sensor ?? 0] ?? `#${mower?.sensor}`}</Tag>
                {/if}
            </Tile>
            <Tile class="summary-tile">
                <span class="summary-label">Akku</span>
                <span class="summary-value">{(mower?.battery_voltage ?? 0).toFixed(1)} V</span>
            </Tile>
            <Tile class="summary-tile">
                <span class="summary-label">GPS</span>
                <Tag type={solutionTag(pos?.solution).type} size="sm">{solutionTag(pos?.solution).text}</Tag>
            </Tile>
            <Tile class="summary-tile">
                <span class="summary-label">Karte</span>
                {#if !hasMap}
                    <Tag type="gray" size="sm">keine</Tag>
                {:else if mapSynced}
                    <Tag type="green" size="sm">synchron</Tag>
                {:else}
                    <Tag type="red" size="sm">nicht synchron</Tag>
                {/if}
            </Tile>
            <Tile class="summary-tile">
                <span class="summary-label">WLAN</span>
                {#if modemInfo?.network?.connected}
                    <Tag type={rssiTag(modemInfo.network.rssi).type} size="sm">{modemInfo.network.rssi} dBm</Tag>
                {:else}
                    <Tag type="gray" size="sm">—</Tag>
                {/if}
            </Tile>
        </div>

        <div class="cards">
            <StatusCard title="Mäher" icon="🤖">
                <StatusRow label="Status">
                    <Tag type={jobTag(mower?.job)} size="sm">{desc.job[mower?.job ?? 0] ?? "—"}</Tag>
                </StatusRow>
                <StatusRow label="Meldung" value={SensorDescriptions[mower?.sensor ?? 0] ?? `#${mower?.sensor}`} />
                <StatusRow label="Soll-Betrieb" hint="Zuletzt vom Modem angeforderter Betrieb; -1 = keiner" value={desired && desired.op >= 0 ? (desc.job[desired.op] ?? `#${desired.op}`) : undefined} />
                <StatusRow label="Geschwindigkeit" value={desired?.speed?.toFixed(2)} unit="m/s" />
                <StatusRow label="Mähmotor" value={desired ? (desired.mower_motor_enabled ? "an" : "aus") : undefined} />
                <StatusRow label="Fix-Timeout" value={desired?.fix_timeout} unit="s" />
                <StatusRow label="Neu starten nach Ende" value={desired ? (desired.finish_and_restart ? "ja" : "nein") : undefined} />
                <StatusRow label="Karten-CRC" hint="CRC der Karte im Mäher; muss zur gespeicherten Karte passen">
                    <span class="mono">{mower?.map_crc ?? 0}</span>
                    {#if hasMap}
                        <Tag type={mapSynced ? "green" : "red"} size="sm">{mapSynced ? "synchron" : "abweichend"}</Tag>
                    {/if}
                </StatusRow>
            </StatusCard>

            <StatusCard title="Akku" icon="🔋">
                <StatusRow label="Spannung" value={(mower?.battery_voltage ?? 0).toFixed(2)} unit="V" />
                <StatusRow label="Strom" value={Math.abs(mower?.amps ?? 0).toFixed(2)} unit="A" />
                <StatusRow label="Zustand">
                    {#if mower?.job === 2}
                        <Tag type="blue" size="sm">lädt</Tag>
                    {:else}
                        <Tag type="gray" size="sm">Akkubetrieb</Tag>
                    {/if}
                </StatusRow>
            </StatusCard>

            <StatusCard title="Position & GPS" icon="📡">
                <StatusRow label="Lösung">
                    <Tag type={solutionTag(pos?.solution).type} size="sm">{solutionTag(pos?.solution).text}</Tag>
                </StatusRow>
                <StatusRow label="Genauigkeit" value={(pos?.accuracy ?? 0).toFixed(2)} unit="m" />
                <StatusRow label="Korrekturdaten-Alter" value={(pos?.age ?? 0).toFixed(1)} unit="s" />
                <StatusRow label="Satelliten" value={`${pos?.visible_satellites ?? 0} (${pos?.visible_satellites_dgps ?? 0} DGPS)`} />
                <StatusRow label="Position" value={`E ${(pos?.x ?? 0).toFixed(2)} · N ${(pos?.y ?? 0).toFixed(2)}`} unit="m" />
                <StatusRow label="Richtung" value={pos?.delta != null ? ((pos.delta * 180) / Math.PI).toFixed(0) : undefined} unit="°" />
                <StatusRow label="Ziel" value={mower?.target ? `E ${mower.target.x.toFixed(2)} · N ${mower.target.y.toFixed(2)}` : undefined} unit="m" />
                <StatusRow label="Wegpunkt" value={`${(pos?.mow_point_index ?? -1) + 1} / ${totalPoints}`} />
            </StatusCard>

            <StatusCard title="Mähstatistik" icon="📊">
                <StatusRow label="Mähzeit" value={duration(stats?.mow)} />
                <StatusRow label="Ladezeit" value={duration(stats?.charge)} />
                <StatusRow label="Leerlauf" value={duration(stats?.idle)} />
                <StatusRow label="Strecke" value={(stats?.mow_traveled ?? 0).toFixed(0)} unit="m" />
                <StatusRow label="GPS beim Mähen" hint="Anteil der Mähzeit mit Fix, Float und ungültiger Position">
                    {#if gpsShare}
                        <div class="share-bar" title={`Fix ${gpsShare.fix.toFixed(0)} %, Float ${gpsShare.float.toFixed(0)} %, ungültig ${gpsShare.invalid.toFixed(0)} %`}>
                            <span class="share fix" style:width={`${gpsShare.fix}%`}></span>
                            <span class="share float" style:width={`${gpsShare.float}%`}></span>
                            <span class="share invalid" style:width={`${gpsShare.invalid}%`}></span>
                        </div>
                        <span class="share-text">{gpsShare.fix.toFixed(0)} % Fix</span>
                    {:else}
                        <span class="value">—</span>
                    {/if}
                </StatusRow>
            </StatusCard>

            <StatusCard title="Störungen & Erholung" icon="🚧">
                <StatusRow label="Hindernisse" value={stats?.obstacles ?? 0} />
                <StatusRow label="davon Sonar / Bumper" value={`${stats?.sonar_triggered ?? 0} / ${stats?.bumper_triggered ?? 0}`} />
                <StatusRow label="GPS-Bewegungs-Timeout" value={stats?.gps_motion_timeout ?? 0} />
                <StatusRow label="IMU ausgelöst" value={stats?.imu_triggered ?? 0} />
                <StatusRow label="Erholt aus Ungültig" value={stats?.invalid_recoveries ?? 0} />
                <StatusRow label="Erholt Float → Fix" value={stats?.float_recoveries ?? 0} />
                <StatusRow label="GPS-Sprünge" value={stats?.gps_jumps ?? 0} />
                <StatusRow label="Prüfsummenfehler GPS / DGPS" value={`${stats?.gps_chk_sum_errors ?? 0} / ${stats?.dgps_chk_sum_errors ?? 0}`} />
                <StatusRow label="Max. Korrekturdaten-Alter" value={(stats?.max_dpgs_age ?? 0).toFixed(1)} unit="s" />
            </StatusCard>

            <StatusCard title="Mäher-Controller" icon="🧩">
                <StatusRow label="Max. Zykluszeit" value={(stats?.max_cycle ?? 0).toFixed(2)} unit="s" />
                <StatusRow label="Serieller Puffer" value={stats?.serial_buffer_size ?? 0} unit="B" />
                <StatusRow label="Freier Speicher" value={kb(stats?.free_memory)} unit="KB" />
                <StatusRow label="Reset-Ursache" value={stats?.reset_cause ?? 0} />
                <StatusRow label="Temperatur min / max" value={`${temp(stats?.temp_min)} / ${temp(stats?.temp_max)}`} unit="°C" />
            </StatusCard>

            <StatusCard title="Modem" icon="📶">
                {#if modemInfo}
                    {@const net = modemInfo.network}
                    {@const mem = modemInfo.memory}
                    <StatusRow label="Firmware" value={modemInfo.git_tag || modemInfo.git_hash} />
                    <StatusRow label="Laufzeit" value={uptime(modemInfo.uptime)} />
                    <StatusRow label="Letzter Reset" value={modemInfo.last_reset} />
                    {#if net}
                        <StatusRow label="WLAN-Signal">
                            {#if net.connected}
                                <span class="value">{net.rssi}</span><span class="unit">dBm</span>
                                <Tag type={rssiTag(net.rssi).type} size="sm">{rssiTag(net.rssi).text}</Tag>
                            {:else}
                                <Tag type="red" size="sm">getrennt</Tag>
                            {/if}
                        </StatusRow>
                        <StatusRow label="Kanal / IP" value={`${net.channel} · ${net.ip}`} />
                        <StatusRow label="Sendeleistung" value={net.tx_power.toFixed(1)} unit="dBm" />
                        <StatusRow label="Stromsparmodus" value={net.power_save ? "an" : "aus"} />
                        <StatusRow label="Bluetooth" value={net.bluetooth ? "an" : "aus"} />
                    {/if}
                    {#if mem}
                        <StatusRow label="RAM frei" value={`${kb(mem.heap_free)} / ${kb(mem.heap_total)}`} unit="KB" />
                        <StatusRow label="RAM frei (min.)" value={kb(mem.heap_min_free)} unit="KB" />
                        {#if mem.psram_total > 0}
                            <StatusRow label="PSRAM frei" value={`${kb(mem.psram_free)} / ${kb(mem.psram_total)}`} unit="KB" />
                        {/if}
                        <StatusRow label="Speicher (SPIFFS)" value={`${kb(mem.fs_used)} / ${kb(mem.fs_total)}`} unit="KB" />
                    {/if}
                {:else}
                    <StatusRow label="Modem" value="wird abgefragt …" />
                {/if}
            </StatusCard>

            <SensorEvents summary={$socketStore.sensorSummary} />
        </div>
    {/if}
</div>

<style lang="scss">
    .dashboard-content {
        width: 100%;
        height: 100vh;
        padding: 48px 0 0; /* Carbon-Header */
        margin: 0;
        display: flex;
        flex-direction: column;
        overflow-y: auto;
        box-sizing: border-box;
    }

    .summary {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
        gap: 6px;
        padding: 8px 12px 0;
    }
    :global(.summary-tile) {
        padding: 0.375rem 0.75rem !important;
        min-height: 32px !important;
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 6px;
    }
    :global(.summary-tile .bx--tag) {
        margin: 0;
    }
    .summary-label {
        font-size: 0.75rem;
        color: #525252;
    }
    .summary-value {
        font-size: 0.875rem;
        font-weight: 600;
        font-variant-numeric: tabular-nums;
    }

    .cards {
        display: grid;
        grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
        gap: 6px;
        padding: 6px 12px 12px;
        align-items: stretch;
    }

    .mono {
        font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
        font-size: 0.8125rem;
    }

    .share-bar {
        display: flex;
        width: 90px;
        height: 8px;
        border-radius: 4px;
        overflow: hidden;
        background: #e0e0e0;
    }
    .share.fix { background: #24a148; }
    .share.float { background: #f1c21b; }
    .share.invalid { background: #da1e28; }
    .share-text {
        font-size: 0.75rem;
        color: #525252;
    }
    .value {
        font-weight: 600;
    }
    .unit {
        color: #525252;
        font-size: 0.75rem;
    }
</style>
