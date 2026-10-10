<script lang="ts">
    import { page } from '$app/stores';
    import { afterNavigate } from '$app/navigation';
    import { onMount, onDestroy } from 'svelte';
    import { browser } from '$app/environment';
    import { socketStore } from '../../../stores/socket';
    import { gpsStore } from '../../../stores/gpsStore';

    import GpsSkyplot from './GpsSkyplot.svelte';
    import GpsDeviationMap from './GpsDeviationMap.svelte';
    import GpsAltitudeChart from './GpsAltitudeChart.svelte';
    import GpsNavStatus from './GpsNavStatus.svelte';
    import GpsAdvancedConfig from './GpsAdvancedConfig.svelte';
    import GpsFixTimeline from './GpsFixTimeline.svelte';

    function gnssName(gnssId: number): string {
        switch (gnssId) {
            case 0: return "GPS";
            case 1: return "SBAS";
            case 2: return "Galileo";
            case 3: return "BeiDou";
            case 4: return "IMES";
            case 5: return "QZSS";
            case 6: return "GLONASS";
            default: return `GNSS-${gnssId}`;
        }
    }

    // u-blox qualityInd (NAV-SIG/NAV-SAT)
    function qualityName(qi: number): string {
        switch (qi) {
            case 0: return "no signal";
            case 1: return "searching";
            case 2: return "acquired";
            case 3: return "unusable";
            case 4: return "code locked";
            case 5: case 6: case 7: return "carrier locked";
            default: return `q${qi}`;
        }
    }

    // u-blox sigId je GNSS (NAV-SIG) als Bandname
    function signalName(gnssId: number, sigId: number): string {
        const names: Record<number, Record<number, string>> = {
            0: { 0: 'L1', 3: 'L2CL', 4: 'L2CM', 6: 'L5I', 7: 'L5Q' },
            1: { 0: 'L1' },
            2: { 0: 'E1C', 1: 'E1B', 3: 'E5aI', 4: 'E5aQ', 5: 'E5bI', 6: 'E5bQ' },
            3: { 0: 'B1I', 1: 'B1I', 2: 'B2I', 3: 'B2I', 5: 'B1C', 7: 'B2a' },
            5: { 0: 'L1', 1: 'L1S', 4: 'L2CM', 5: 'L2CL', 8: 'L5I', 9: 'L5Q' },
            6: { 0: 'L1', 2: 'L2' },
        };
        return names[gnssId]?.[sigId] ?? `sig ${sigId}`;
    }

    function solutionName(sol: number): string {
        switch (sol) {
            case 0: return "Invalid";
            case 1: return "Float";
            case 2: return "Fixed";
            default: return `Sol-${sol}`;
        }
    }

    // Reagiere auf Sichtbarkeit (Dashboard ist immer im DOM, nur CSS hidden)
    // afterNavigate funktioniert zuverlässiger als $effect für Query-Param-Änderungen.
    // Das Abo beim Modem (auch nach einem Reconnect) verwaltet gpsStore.
    let lastGpsActive = false;
    function syncGpsPolling() {
        if (!browser) return;
        const isGps = $page.url.searchParams.get('dashboard') === 'gps';
        if (isGps && !lastGpsActive) {
            gpsStore.connect();
            lastGpsActive = true;
        } else if (!isGps && lastGpsActive) {
            gpsStore.disconnect();
            lastGpsActive = false;
        }
    }
    onMount(syncGpsPolling);
    afterNavigate(syncGpsPolling);
    onDestroy(() => {
        if (lastGpsActive) {
            gpsStore.disconnect();
            lastGpsActive = false;
        }
    });

    // Alter der Daten, damit veraltete Werte nicht als aktuell erscheinen
    const STALE_MS = 15000;
    let now = $state(Date.now());
    $effect(() => {
        const t = setInterval(() => (now = Date.now()), 1000);
        return () => clearInterval(t);
    });
    let gps = $derived($gpsStore);
    let d = $derived($socketStore.gpsDetails);
    let pvt = $derived(gps.navPvt);
    let lastDataAt = $derived(Math.max(gps.gpsDetailsReceivedAt ?? 0, pvt?.timestamp ?? 0));
    let dataAge = $derived(lastDataAt > 0 ? now - lastDataAt : null);
    let stale = $derived(dataAge !== null && dataAge > STALE_MS);

    function ageText(ms: number | null): string {
        if (ms === null) return '—';
        const s = Math.round(ms / 1000);
        return s < 60 ? `${s} s ago` : `${Math.floor(s / 60)} min ago`;
    }

    // S4 liefert je Signal einen Eintrag (L1 und L2 eines Satelliten getrennt)
    let signals = $derived(d?.satellites ?? []);
    let sortedSignals = $derived([...signals].sort((a, b) => b.cno - a.cno));
    let usedSignals = $derived(signals.filter(s => s.prUsed).length);
    let satCount = $derived(new Set(signals.map(s => `${s.gnssId}:${s.svId}`)).size);

    // Tabelle: eine Zeile je Satellit, Signale als Bänder
    let satelliteRows = $derived.by(() => {
        const rows = new Map<string, { gnssId: number; svId: number; sigs: typeof signals }>();
        for (const sig of signals) {
            const key = `${sig.gnssId}:${sig.svId}`;
            if (!rows.has(key)) rows.set(key, { gnssId: sig.gnssId, svId: sig.svId, sigs: [] });
            rows.get(key)!.sigs.push(sig);
        }
        return [...rows.values()]
            .map(r => ({ ...r, sigs: [...r.sigs].sort((a, b) => a.sigId - b.sigId), maxCno: Math.max(...r.sigs.map(x => x.cno)) }))
            .sort((a, b) => a.gnssId - b.gnssId || b.maxCno - a.maxCno);
    });

    // Status: S4 wenn vorhanden, sonst NAV-PVT
    let solution = $derived(d ? d.solution : pvt ? (pvt.fixOk ? pvt.carrSoln : 0) : 0);
    let hAcc = $derived(d?.hAccuracy ?? pvt?.hAcc ?? null);
    let vAcc = $derived(d?.vAccuracy ?? pvt?.vAcc ?? null);

    // Skyplot nur aus NAV-SAT: S4 (NAV-SIG) enthält keine Elevation/Azimut
    let skyplotSats = $derived(gps.navSat);

    let mode = $state<'simple' | 'advanced'>('simple');
</script>

<div class="dashboard-content">
    <div class="mode-toggle">
        <button class:active={mode === 'simple'} onclick={() => mode = 'simple'}>Simple</button>
        <button class:active={mode === 'advanced'} onclick={() => mode = 'advanced'}>Advanced (UBX)</button>
    </div>

    {#if mode === 'advanced'}
        <GpsAdvancedConfig />
    {/if}

    {#if mode === 'simple'}
    {#if !$socketStore.connected}
        <div class="gps-banner offline">Not connected to the modem — the values below are not updated.</div>
    {:else if stale}
        <div class="gps-banner stale">No GPS data for {ageText(dataAge)}. The receiver or the mower may not be responding.</div>
    {/if}
    {#if d != null || pvt != null}
        <!-- u-Center-style Status Bar -->
        <div class="uc-status-bar">
            <div class="uc-status-item solution-{solution}">
                <div class="uc-status-icon">📡</div>
                <div class="uc-status-label">Fix</div>
                <div class="uc-status-value">{solutionName(solution)}</div>
            </div>
            <div class="uc-status-item">
                <div class="uc-status-icon">🛰️</div>
                <div class="uc-status-label">Satellites</div>
                <div class="uc-status-value">{d ? satCount : (pvt?.numSV ?? 0)}
                    {#if d}<span class="uc-status-sub" title="Signals used for the solution (L1 and L2 count separately)">{d.numSV} signals used</span>{/if}
                </div>
            </div>
            <div class="uc-status-item">
                <div class="uc-status-icon">↔️</div>
                <div class="uc-status-label">H-Accuracy</div>
                <div class="uc-status-value">{hAcc === null ? '—' : hAcc.toFixed(2) + ' m'}</div>
            </div>
            <div class="uc-status-item">
                <div class="uc-status-icon">↕️</div>
                <div class="uc-status-label">V-Accuracy</div>
                <div class="uc-status-value">{vAcc === null ? '—' : vAcc.toFixed(2) + ' m'}</div>
            </div>
            <div class="uc-status-item">
                <div class="uc-status-icon">⏱️</div>
                <div class="uc-status-label">DGPS Age</div>
                <div class="uc-status-value">{d ? `${d.dgpsAge} ms` : '—'}</div>
            </div>
            <div class="uc-status-item">
                <div class="uc-status-icon">📶</div>
                <div class="uc-status-label">DGPS Signals</div>
                <div class="uc-status-value">{d ? d.numSVdgps : '—'}</div>
            </div>
            <div class="uc-status-item" class:stale-item={stale}>
                <div class="uc-status-icon">🕒</div>
                <div class="uc-status-label">Updated</div>
                <div class="uc-status-value">{ageText(dataAge)}</div>
            </div>
        </div>

        <GpsFixTimeline history={gps.fixHistory} />

        <!-- New u-Center Style Visualizations -->
        <div class="uc-visual-grid">
            <div class="uc-visual-col">
                {#if skyplotSats.length > 0}
                    <GpsSkyplot satellites={skyplotSats} />
                {:else}
                    <div class="skyplot-wait">Sky plot: waiting for NAV-SAT from the receiver…</div>
                {/if}
                <GpsDeviationMap positionHistory={gps.positionHistory} refLat={gps.refLat} refLon={gps.refLon} />
            </div>
            <div class="uc-visual-col">
                <GpsNavStatus navPvt={gps.navPvt} navDop={gps.navDop} gpsDetails={gps.gpsDetails} />
                <GpsAltitudeChart history={gps.altitudeHistory} />
            </div>
        </div>

        <!-- CN0 Chart like u-Center -->
        {#if d}
        <div class="uc-cn0-panel">
            <div class="uc-panel-header">
                <span class="uc-panel-title">Signal Strength (C/N₀)</span>
                <span class="uc-panel-sub">{satCount} satellites, {signals.length} signals{signals.length >= 40 ? ' (first 40 reported)' : ''}</span>
            </div>
            <div class="uc-cn0-grid">
                {#each [0,1,2,3,4,5,6] as gnssId}
                    {@const gnssSigs = sortedSignals.filter(s => s.gnssId === gnssId)}
                    {#if gnssSigs.length > 0}
                        <div class="uc-cn0-group">
                            <div class="uc-cn0-group-label">{gnssName(gnssId)}</div>
                            {#each gnssSigs as sat}
                                {@const pct = Math.min(100, (sat.cno / 55) * 100)}
                                {@const barColor = sat.cno >= 40 ? '#00c853' : sat.cno >= 30 ? '#ffab00' : '#ff1744'}
                                <div class="uc-cn0-bar-row">
                                    <div class="uc-cn0-sv">{sat.svId} <span class="uc-cn0-band">{signalName(sat.gnssId, sat.sigId)}</span></div>
                                    <div class="uc-cn0-bar-wrap">
                                        <div class="uc-cn0-bar-bg">
                                            <div class="uc-cn0-bar-fill" style="width: {pct}%; background: {barColor};"></div>
                                        </div>
                                    </div>
                                    <div class="uc-cn0-val">{sat.cno}</div>
                                    <div class="uc-cn0-badges">
                                        {#if sat.prUsed}<span class="uc-badge used" title="Used">U</span>{/if}
                                        {#if sat.crCorrUsed}<span class="uc-badge dgps" title="DGPS">D</span>{/if}
                                    </div>
                                </div>
                            {/each}
                        </div>
                    {/if}
                {/each}
            </div>
        </div>
        {/if}

        <!-- Satellite Table -->
        {#if d}
        <div class="uc-table-panel">
            <div class="uc-panel-header">
                <span class="uc-panel-title">Satellite Details</span>
                <span class="uc-panel-sub">one row per satellite</span>
            </div>
            <div class="sat-table-wrapper">
                <table class="sat-table">
                    <thead>
                        <tr>
                            <th>GNSS</th>
                            <th>SV</th>
                            <th>Signal</th>
                            <th>C/N₀</th>
                            <th>Quality</th>
                            <th>PR Res</th>
                            <th>Used</th>
                            <th>DGPS</th>
                        </tr>
                    </thead>
                    <tbody>
                        {#each satelliteRows as row}
                            {#each row.sigs as sat, i}
                                <tr class:used-row={sat.prUsed} class:dgps-row={sat.crCorrUsed} class:sat-first={i === 0}>
                                    {#if i === 0}
                                        <td rowspan={row.sigs.length}><span class="gnss-tag gnss-{sat.gnssId}">{gnssName(sat.gnssId)}</span></td>
                                        <td rowspan={row.sigs.length}>{sat.svId}</td>
                                    {/if}
                                    <td>{signalName(sat.gnssId, sat.sigId)}</td>
                                    <td class:good={sat.cno >= 40} class:weak={sat.cno < 30}>{sat.cno}</td>
                                    <td>{qualityName(sat.qualityInd)}</td>
                                    <td>{(sat.prRes ?? 0).toFixed(1)} m</td>
                                    <td>{sat.prUsed ? "✓" : "—"}</td>
                                    <td>{sat.crCorrUsed ? "✓" : "—"}</td>
                                </tr>
                            {/each}
                        {/each}
                    </tbody>
                </table>
            </div>
        </div>
        {/if}
    {:else}
        <div class="no-data">
            <div class="no-data-title">No GPS data</div>
            <div class="no-data-text">
                GPS receiver is not responding or has no signal.<br>
                Make sure the rover is powered on and has GPS reception.
            </div>
            <div class="no-data-hint">
                Last known state: {$socketStore.state ? ($socketStore.state.position.solution === 0 ? 'Invalid' : $socketStore.state.position.solution === 1 ? 'Float' : 'Fixed') : 'Unknown'}
            </div>
        </div>
    {/if}
    {/if}
</div>

<style lang="scss">
    .dashboard-content {
        width: 100%;
        height: 100vh;
        padding: 0;
        margin: 0;
        display: flex;
        flex-direction: column;
        overflow-y: auto;
        padding-top: 48px;
        box-sizing: border-box;
    }

    .gps-stats {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
        gap: 8px;
        padding: 8px;
        background: #f8f8f8;
        border-bottom: 1px solid #ddd;
    }

    .stat-card {
        background: white;
        border: 1px solid #ddd;
        border-radius: 4px;
        padding: 8px 12px;
        text-align: center;
    }

    .stat-label {
        font-size: 0.75em;
        color: #666;
        margin-bottom: 4px;
    }

    .stat-value {
        font-size: 1.1em;
        font-weight: bold;
        color: #333;
    }

    .stat-value.fix { color: #1e8e3e; }
    .stat-value.float { color: #b06000; }

    .stat-small {
        font-size: 0.75em;
        font-weight: normal;
        color: #666;
    }

    .cn0-section {
        padding: 12px;
        border-bottom: 1px solid #eee;
    }

    .cn0-bars {
        display: flex;
        flex-direction: column;
        gap: 4px;
    }

    .cn0-row {
        display: grid;
        grid-template-columns: 70px 1fr 80px 24px 24px;
        gap: 6px;
        align-items: center;
        font-size: 0.85em;
    }

    .cn0-label {
        font-family: monospace;
        font-size: 0.85em;
    }

    .cn0-bar-bg {
        height: 14px;
        background: #e8e8e8;
        border-radius: 3px;
        overflow: hidden;
    }

    .cn0-bar-fill {
        height: 100%;
        border-radius: 3px;
        transition: width 0.3s ease;
    }

    .cn0-value {
        font-family: monospace;
        font-size: 0.85em;
        text-align: right;
    }

    .badge {
        font-size: 0.65em;
        padding: 1px 3px;
        border-radius: 3px;
        font-weight: bold;
        text-align: center;
    }

    .badge.used {
        background: #e6f4ea;
        color: #1e8e3e;
    }

    .badge.dgps {
        background: #e0f7fa;
        color: #006064;
    }

    .sat-table-section {
        padding: 12px;
        flex: 1;
        overflow: hidden;
    }

    .sat-table-wrapper {
        overflow-x: auto;
    }

    .sat-table {
        width: 100%;
        border-collapse: collapse;
        font-size: 0.85em;
    }

    .sat-table th {
        text-align: left;
        padding: 6px 8px;
        border-bottom: 2px solid #ddd;
        background: #f4f4f4;
        font-weight: 600;
        white-space: nowrap;
    }

    .sat-table td {
        padding: 5px 8px;
        border-bottom: 1px solid #eee;
        white-space: nowrap;
    }

    .sat-table tbody tr:hover {
        background: #f8f8f8;
    }

    .used-row {
        background: #f0f8f0;
    }

    .dgps-row {
        background: #f0f4f8;
    }

    .good {
        color: #1e8e3e;
        font-weight: bold;
    }

    .weak {
        color: #c5221f;
    }

    .no-data {
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        height: 100%;
        color: #888;
        text-align: center;
        padding: 2rem;
    }

    .no-data-title {
        font-size: 1.3em;
        font-weight: bold;
        color: #555;
        margin-bottom: 0.5rem;
    }

    .no-data-text {
        font-size: 0.95em;
        margin-bottom: 1rem;
        line-height: 1.5;
    }

    .no-data-hint {
        font-size: 0.85em;
        color: #666;
        background: #f4f4f4;
        padding: 0.5rem 1rem;
        border-radius: 4px;
    }

    .mode-toggle {
        display: flex;
        gap: 0;
        padding: 8px;
        background: #f8f8f8;
        border-bottom: 1px solid #ddd;
    }

    .mode-toggle button {
        flex: 1;
        padding: 8px 12px;
        border: 1px solid #ccc;
        background: #f4f4f4;
        cursor: pointer;
        font-size: 0.9em;
    }

    .mode-toggle button:first-child {
        border-radius: 4px 0 0 4px;
    }

    .mode-toggle button:last-child {
        border-radius: 0 4px 4px 0;
    }

    .mode-toggle button.active {
        background: #006064;
        color: white;
        border-color: #006064;
    }

    .ubx-panel {
        padding: 12px;
        background: #fafafa;
        border-bottom: 1px solid #ddd;
    }

    .ubx-categories {
        display: flex;
        flex-wrap: wrap;
        gap: 4px;
        margin-bottom: 10px;
    }

    .ubx-command-row {
        display: flex;
        gap: 8px;
        align-items: center;
        margin-bottom: 6px;
    }

    .cmd-desc {
        font-size: 0.8em;
        color: #666;
        margin-bottom: 8px;
    }

    .parsed-response {
        background: white;
        border: 1px solid #ddd;
        border-radius: 4px;
        padding: 8px;
        margin-bottom: 10px;
    }

    .parsed-table-wrapper {
        overflow-x: auto;
    }

    .parsed-table {
        width: 100%;
        border-collapse: collapse;
        font-size: 0.8em;
    }

    .parsed-kv {
        display: flex;
        flex-direction: column;
        gap: 4px;
    }

    .kv-row {
        display: flex;
        justify-content: space-between;
        padding: 3px 6px;
        background: #f8f8f8;
        border-radius: 3px;
        font-size: 0.85em;
    }

    .kv-key {
        font-weight: 600;
        color: #555;
    }

    .kv-val {
        color: #333;
        font-family: monospace;
    }

    .ubx-history {
        display: flex;
        flex-direction: column;
        gap: 6px;
        max-height: 200px;
        overflow-y: auto;
    }

    .ubx-entry {
        background: white;
        border: 1px solid #eee;
        border-radius: 4px;
        padding: 6px 10px;
        font-family: monospace;
        font-size: 0.8em;
    }

    .frames-header {
        display: flex;
        align-items: center;
        gap: 8px;
        font-size: 0.85em;
        font-weight: 600;
        color: #555;
        margin-bottom: 8px;
        padding: 0 4px;
    }
    .frames-filtered {
        color: #888;
        font-weight: 400;
        font-size: 0.9em;
    }
    .frames-toggle {
        margin-left: auto;
        display: flex;
        align-items: center;
        gap: 4px;
        font-size: 0.85em;
        font-weight: 400;
        cursor: pointer;
    }
    .frame-card {
        background: white;
        border: 1px solid #ddd;
        border-radius: 4px;
        margin-bottom: 10px;
        overflow: hidden;
    }

    .frame-header {
        display: flex;
        align-items: center;
        gap: 8px;
        padding: 6px 10px;
        background: #f4f4f4;
        border-bottom: 1px solid #ddd;
        font-size: 0.85em;
    }

    .frame-badge {
        font-size: 0.8em;
        width: 18px;
        height: 18px;
        display: flex;
        align-items: center;
        justify-content: center;
        border-radius: 50%;
        font-weight: bold;
    }

    .frame-badge.valid {
        background: #e6f4ea;
        color: #1e8e3e;
    }

    .frame-badge.invalid {
        background: #fce8e8;
        color: #c5221f;
    }

    .frame-title {
        flex: 1;
        font-weight: 600;
        color: #333;
        font-family: monospace;
    }

    .frame-meta {
        color: #888;
        font-size: 0.85em;
    }

    .frame-no-parser {
        padding: 8px 10px;
    }

    .frame-no-parser-title {
        font-size: 0.8em;
        color: #888;
        margin-bottom: 4px;
    }

    .hex-dump {
        margin: 0;
        padding: 6px 8px;
        background: #f8f8f8;
        border: 1px solid #eee;
        border-radius: 3px;
        font-family: monospace;
        font-size: 0.75em;
        color: #333;
        line-height: 1.4;
        overflow-x: auto;
    }

    .raw-section {
        padding: 0;
    }

    .raw-toggle {
        width: 100%;
        text-align: left;
        padding: 8px 10px;
        background: #f4f4f4;
        border: none;
        border-radius: 4px;
        cursor: pointer;
        font-size: 0.85em;
        font-family: monospace;
        color: #555;
    }

    .raw-toggle:hover {
        background: #e8e8e8;
    }

    .raw-hex {
        padding: 8px 10px;
        font-family: monospace;
        font-size: 0.75em;
        color: #333;
        word-break: break-all;
        background: #fafafa;
        border-top: 1px solid #eee;
        max-height: 150px;
        overflow-y: auto;
    }

    .ubx-header {
        display: flex;
        align-items: center;
        gap: 8px;
        cursor: pointer;
        padding: 4px 0;
    }

    .ubx-expand {
        font-size: 0.85em;
        color: #888;
        width: 12px;
    }

    .ubx-time {
        color: #888;
        font-size: 0.85em;
        white-space: nowrap;
    }

    .ubx-name {
        flex: 1;
        color: #333;
        font-weight: 600;
    }

    .ubx-bytes {
        color: #888;
        font-size: 0.85em;
    }

    .ubx-body {
        margin-top: 6px;
        padding-top: 6px;
        border-top: 1px solid #eee;
    }

    .ubx-custom {
        color: #006064;
        margin-bottom: 4px;
    }

    .ubx-hex {
        color: #333;
        word-break: break-all;
        max-height: 120px;
        overflow-y: auto;
    }

    /* ─── RTK Card (NAV-RELPOSNED) ─── */
    .rtk-card {
        padding: 10px;
    }
    .rtk-status {
        padding: 8px 12px;
        border-radius: 6px;
        text-align: center;
        margin-bottom: 10px;
    }
    .rtk-status.none {
        background: #f4f4f4;
        color: #888;
    }
    .rtk-status.float {
        background: #fff3e0;
        color: #b06000;
    }
    .rtk-status.fixed {
        background: #e6f4ea;
        color: #1e8e3e;
    }
    .rtk-label {
        font-size: 0.75em;
        text-transform: uppercase;
        letter-spacing: 0.5px;
        margin-bottom: 2px;
    }
    .rtk-value {
        font-size: 1.4em;
        font-weight: bold;
    }
    .rtk-grid {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 10px;
        margin-bottom: 10px;
    }
    .rtk-big {
        background: #f8f8f8;
        border-radius: 4px;
        padding: 10px;
        text-align: center;
    }
    .rtk-big-val {
        font-size: 1.3em;
        font-weight: bold;
        color: #333;
    }
    .rtk-big-label {
        font-size: 0.75em;
        color: #888;
        margin-top: 2px;
    }
    .rtk-coords {
        display: flex;
        flex-direction: column;
        gap: 4px;
        justify-content: center;
    }
    .rtk-coord {
        display: flex;
        align-items: center;
        gap: 6px;
        font-size: 0.85em;
        font-family: monospace;
    }
    .rtk-details {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 6px;
    }
    .rtk-detail {
        display: flex;
        justify-content: space-between;
        padding: 4px 6px;
        background: #fafafa;
        border-radius: 3px;
        font-size: 0.8em;
    }
    .rtk-dk {
        color: #888;
    }
    .rtk-dv {
        font-weight: 600;
        color: #333;
    }

    /* ─── ODO Card (NAV-ODO) ─── */
    .odo-card {
        padding: 10px;
        text-align: center;
    }
    .odo-big {
        margin-bottom: 10px;
    }
    .odo-value {
        font-size: 2em;
        font-weight: bold;
        color: #006064;
    }
    .odo-label {
        font-size: 0.8em;
        color: #888;
    }
    .odo-row {
        display: flex;
        justify-content: center;
        gap: 20px;
    }
    .odo-item {
        text-align: center;
    }
    .odo-ik {
        display: block;
        font-size: 0.75em;
        color: #888;
    }
    .odo-iv {
        font-weight: 600;
        color: #333;
        font-size: 0.95em;
    }

    /* ─── DGPS Card (NAV-DGPS) ─── */
    .dgps-card {
        padding: 10px;
    }
    .dgps-status {
        padding: 8px 12px;
        border-radius: 6px;
        text-align: center;
        font-size: 0.95em;
        font-weight: 600;
    }
    .dgps-status.active {
        background: #e6f4ea;
        color: #1e8e3e;
    }
    .dgps-status.inactive {
        background: #f4f4f4;
        color: #888;
    }
    .dgps-details {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 6px;
        margin-top: 8px;
    }
    .dgps-item {
        display: flex;
        justify-content: space-between;
        padding: 4px 6px;
        background: #fafafa;
        border-radius: 3px;
        font-size: 0.8em;
    }
    /* ─── Status Card (NAV-STATUS) ─── */
    .status-card {
        padding: 10px;
    }
    .status-badge {
        padding: 8px 12px;
        border-radius: 6px;
        text-align: center;
        font-size: 1.1em;
        font-weight: bold;
        margin-bottom: 10px;
        color: white;
    }
    .status-badge.fix3d {
        background: #1e8e3e;
    }
    .status-badge.fix2d {
        background: #b06000;
    }
    .status-badge.dr {
        background: #555;
    }
    .status-badge.nofix {
        background: #c5221f;
    }
    .status-grid {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 6px;
        margin-bottom: 8px;
    }
    .status-item {
        padding: 4px 6px;
        background: #fafafa;
        border-radius: 3px;
        font-size: 0.8em;
        color: #666;
    }
    .status-item.ok {
        color: #1e8e3e;
        font-weight: 600;
    }
    .status-times {
        display: flex;
        justify-content: space-between;
        font-size: 0.8em;
        color: #888;
        padding: 4px 6px;
    }

    /* ─── RF Card (MON-RF) ─── */
    .rf-card {
        padding: 10px;
    }
    .rf-block {
        padding: 6px 8px;
        background: #f8f8f8;
        border-radius: 4px;
        margin-bottom: 6px;
    }
    .rf-title {
        font-size: 0.75em;
        color: #888;
        margin-bottom: 2px;
    }
    .rf-val {
        font-size: 0.85em;
        color: #333;
    }
    .rf-count {
        font-size: 0.8em;
        color: #888;
        text-align: right;
    }

    /* ─── VER Card (MON-VER) ─── */
    .ver-card {
        padding: 10px;
    }
    .ver-row {
        display: flex;
        align-items: center;
        gap: 10px;
        margin-bottom: 8px;
    }
    .ver-icon {
        font-size: 1.2em;
    }
    .ver-label {
        font-size: 0.75em;
        color: #888;
    }
    .ver-value {
        font-size: 0.95em;
        font-weight: 600;
        color: #333;
        font-family: monospace;
    }
    .ver-ext {
        font-size: 0.8em;
        color: #666;
        background: #f4f4f4;
        padding: 6px 8px;
        border-radius: 3px;
        margin-top: 4px;
    }

    /* ─── u-Center Style Dashboard ─── */
    .uc-status-bar {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
        gap: 8px;
        padding: 10px;
        background: linear-gradient(180deg, #f0f0f0 0%, #e8e8e8 100%);
        border-bottom: 2px solid #ccc;
    }
    .uc-status-item {
        background: white;
        border-radius: 6px;
        padding: 10px 8px;
        text-align: center;
        box-shadow: 0 1px 3px rgba(0,0,0,0.1);
        border-left: 4px solid #006064;
    }
    .uc-status-item.solution-0 { border-left-color: #c5221f; }
    .uc-status-item.solution-1 { border-left-color: #ffab00; }
    .uc-status-item.solution-2 { border-left-color: #00c853; }
    .uc-status-icon {
        font-size: 1.3em;
        margin-bottom: 2px;
    }
    .uc-status-label {
        font-size: 0.7em;
        color: #888;
        text-transform: uppercase;
        letter-spacing: 0.5px;
    }
    .uc-status-value {
        font-size: 1.1em;
        font-weight: bold;
        color: #333;
    }
    .uc-status-sub {
        font-size: 0.75em;
        font-weight: normal;
        color: #888;
    }

    .gps-banner {
        padding: 8px 12px;
        font-size: 0.85em;
        border-bottom: 1px solid #ddd;
    }

    .gps-banner.offline {
        background: #ffebee;
        color: #b71c1c;
    }

    .gps-banner.stale {
        background: #fff8e1;
        color: #8d6e00;
    }

    .stale-item .uc-status-value {
        color: #e65100;
    }

    .skyplot-wait {
        padding: 24px 12px;
        text-align: center;
        color: #888;
        font-size: 0.85em;
        background: white;
        border-bottom: 1px solid #ddd;
    }

    .uc-cn0-band {
        color: #888;
        font-size: 0.85em;
    }

    .uc-panel-header {
        display: flex;
        justify-content: space-between;
        align-items: center;
        padding: 8px 12px;
        background: #f4f4f4;
        border-bottom: 1px solid #ddd;
        font-size: 0.9em;
    }
    .uc-panel-title {
        font-weight: 600;
        color: #333;
    }
    .uc-panel-sub {
        font-size: 0.8em;
        color: #888;
    }

    .uc-cn0-panel {
        background: white;
        border-bottom: 1px solid #ddd;
        padding-bottom: 10px;
    }
    .uc-cn0-grid {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
        gap: 12px;
        padding: 10px 12px;
    }
    .uc-cn0-group {
        background: #fafafa;
        border-radius: 6px;
        padding: 8px 10px;
        border: 1px solid #eee;
    }
    .uc-cn0-group-label {
        font-size: 0.8em;
        font-weight: 600;
        color: #555;
        margin-bottom: 6px;
        text-transform: uppercase;
        letter-spacing: 0.3px;
    }
    .uc-cn0-bar-row {
        display: grid;
        grid-template-columns: 28px 1fr 32px 40px;
        gap: 6px;
        align-items: center;
        margin-bottom: 4px;
        font-size: 0.8em;
    }
    .uc-cn0-sv {
        font-family: monospace;
        color: #555;
        text-align: right;
    }
    .uc-cn0-bar-wrap {
        display: flex;
        align-items: center;
    }
    .uc-cn0-bar-bg {
        width: 100%;
        height: 12px;
        background: #e0e0e0;
        border-radius: 6px;
        overflow: hidden;
    }
    .uc-cn0-bar-fill {
        height: 100%;
        border-radius: 6px;
        transition: width 0.4s ease;
    }
    .uc-cn0-val {
        font-family: monospace;
        text-align: right;
        color: #333;
    }
    .uc-cn0-badges {
        display: flex;
        gap: 3px;
    }
    .uc-badge {
        font-size: 0.65em;
        padding: 1px 4px;
        border-radius: 3px;
        font-weight: bold;
    }
    .uc-badge.used {
        background: #e6f4ea;
        color: #1e8e3e;
    }
    .uc-badge.dgps {
        background: #e0f7fa;
        color: #006064;
    }

    .uc-table-panel {
        background: white;
        flex: 1;
        overflow: hidden;
        display: flex;
        flex-direction: column;
    }

    .gnss-tag {
        display: inline-block;
        padding: 2px 6px;
        border-radius: 3px;
        font-size: 0.75em;
        font-weight: 600;
    }
    .gnss-tag.gnss-0 { background: #e3f2fd; color: #1565c0; } /* GPS */
    .gnss-tag.gnss-1 { background: #fff3e0; color: #e65100; } /* SBAS */
    .gnss-tag.gnss-2 { background: #e8f5e9; color: #2e7d32; } /* Galileo */
    .gnss-tag.gnss-3 { background: #fce4ec; color: #c2185b; } /* BeiDou */
    .gnss-tag.gnss-4 { background: #f3e5f5; color: #7b1fa2; } /* IMES */
    .gnss-tag.gnss-5 { background: #e0f2f1; color: #00695c; } /* QZSS */
    .gnss-tag.gnss-6 { background: #e8eaf6; color: #283593; } /* GLONASS */

    /* ─── Visual Grid for new components ─── */
    .uc-visual-grid {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
        gap: 8px;
        padding: 8px;
        background: #f0f0f0;
    }
    .uc-visual-col {
        display: flex;
        flex-direction: column;
        gap: 8px;
    }
</style>
