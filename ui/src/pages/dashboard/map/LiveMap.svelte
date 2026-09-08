<script lang="ts">
    import { onMount, onDestroy } from 'svelte';
    import { page } from '$app/stores';
    import { afterNavigate } from '$app/navigation';
    import { browser } from '$app/environment';
    import { socketService, socketStore } from '../../../stores/socket';
    import { gpsStore } from '../../../stores/gpsStore';
    import type { PositionSample } from '../../../stores/gpsStore';

    // Settings reference position for converting relative mower coords to absolute
    let settingsPos = $derived($socketStore.settings?.position);

    // Convert relative mower position (meters) to absolute lat/lon
    function relativeToAbsolute(x: number, y: number): [number, number] | null {
        if (!settingsPos || settingsPos.mode !== 'relative') return null;
        const metersPerDegLat = 111320;
        const metersPerDegLon = 111320 * Math.cos(settingsPos.lat * Math.PI / 180);
        const lat = settingsPos.lat + y / metersPerDegLat;
        const lon = settingsPos.lon + x / metersPerDegLon;
        return [lat, lon];
    }

    const TIME_WINDOWS: { label: string; ms: number }[] = [
        { label: '5 min', ms: 5 * 60 * 1000 },
        { label: '15 min', ms: 15 * 60 * 1000 },
        { label: '30 min', ms: 30 * 60 * 1000 },
        { label: '1 h', ms: 60 * 60 * 1000 },
        { label: '2 h', ms: 2 * 60 * 60 * 1000 },
        { label: 'All', ms: Infinity },
    ];

    let selectedWindow = $state(TIME_WINDOWS[3].ms);
    let showAccuracy = $state(true);

    let mapContainer: HTMLDivElement;
    let L: any = null;
    let map: any = null;
    let trackPolyline: any = null;
    let accuracyCircle: any = null;
    let roverMarker: any = null;
    let resizeObserver: ResizeObserver | null = null;
    let initMapPromise: Promise<void> | null = null;
    let gpsState = $derived($gpsStore);

    let filteredHistory = $derived(
        selectedWindow === Infinity
            ? gpsState.positionHistory
            : gpsState.positionHistory.filter(p => p.time >= Date.now() - selectedWindow)
    );

    function hasVisibleSize() {
        return mapContainer && mapContainer.offsetWidth > 0 && mapContainer.offsetHeight > 0;
    }

    async function initMap() {
        if (!browser || !mapContainer || map || initMapPromise) return;

        // Don't initialise Leaflet while the panel is hidden (0x0). It caches
        // the size and will not load tiles even after becoming visible.
        if (!hasVisibleSize()) return;

        initMapPromise = (async () => {
            const leafletModule = await import('leaflet');
            L = leafletModule.default || leafletModule;

            map = L.map(mapContainer, {
                zoomControl: false,
                attributionControl: true,
            }).setView([51.1657, 10.4515], 6);

            L.control.zoom({ position: 'bottomright' }).addTo(map);

            L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
                maxZoom: 19,
                attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
            }).addTo(map);

            // Watch for size changes (e.g. panel becoming visible, window resize)
            if ('ResizeObserver' in window) {
                const RO = (window as any).ResizeObserver;
                resizeObserver = new RO(() => {
                    if (map) {
                        map.invalidateSize();
                    }
                });
                resizeObserver.observe(mapContainer);
            }

            updateLayers();
            // Leaflet may have measured the container while it was still hidden.
            // Force a size recalculation now that the layout is final.
            requestAnimationFrame(() => {
                if (map) map.invalidateSize();
            });
        })();
        await initMapPromise;
        initMapPromise = null;
    }

    function updateLayers() {
        if (!map || !L) return;

        const history = filteredHistory;
        const pvt = gpsState.navPvt;

        // ─── Track Polyline ─────────────────────────────────────────────
        const trackLatLngs = history.map((p: PositionSample) => [p.lat, p.lon]);

        if (trackPolyline) {
            map.removeLayer(trackPolyline);
            trackPolyline = null;
        }

        if (trackLatLngs.length >= 2) {
            trackPolyline = L.polyline(trackLatLngs, {
                color: '#006064',
                weight: 3,
                opacity: 0.85,
                lineCap: 'round',
                lineJoin: 'round',
            }).addTo(map);
        }

        // ─── Rover Marker + Accuracy ────────────────────────────────────
        if (roverMarker) { map.removeLayer(roverMarker); roverMarker = null; }
        if (accuracyCircle) { map.removeLayer(accuracyCircle); accuracyCircle = null; }

        // Determine rover position: prefer UBX NAV-PVT, fallback to mower state position
        let roverPos: [number, number] | null = null;
        let roverHeading = 0;
        let roverAccuracy = 0;

        if (pvt && pvt.fixOk && pvt.fixType >= 2) {
            roverPos = [pvt.lat, pvt.lon];
            roverHeading = pvt.heading;
            roverAccuracy = pvt.hAcc;
        } else {
            // Fallback: use mower state position (relative coordinates) converted to absolute
            const mowerState = $socketStore.state;
            if (mowerState?.position && settingsPos?.mode === 'relative') {
                const abs = relativeToAbsolute(mowerState.position.x, mowerState.position.y);
                if (abs) {
                    roverPos = abs;
                    roverHeading = (mowerState.position.delta * 180 / Math.PI);
                    roverAccuracy = mowerState.position.accuracy;
                }
            }
        }

        if (roverPos) {
            const roverIcon = L.divIcon({
                className: 'rover-marker',
                html: `<div class="rover-marker-inner" style="transform: rotate(${roverHeading}deg)">
                        <div class="rover-arrow"></div>
                        <div class="rover-body"></div>
                       </div>`,
                iconSize: [24, 24],
                iconAnchor: [12, 12],
            });

            roverMarker = L.marker(roverPos, { icon: roverIcon, zIndexOffset: 1000 }).addTo(map);

            if (showAccuracy && roverAccuracy > 0) {
                accuracyCircle = L.circle(roverPos, {
                    radius: roverAccuracy,
                    color: '#006064',
                    fillColor: '#006064',
                    fillOpacity: 0.15,
                    weight: 1,
                    dashArray: '4,4',
                }).addTo(map);
            }
        }

        // ─── Auto-fit or center ─────────────────────────────────────────
        if (trackLatLngs.length >= 2) {
            const bounds = L.latLngBounds(trackLatLngs);
            if (roverPos) bounds.extend(roverPos);
            if (!map._livemap_fitted) {
                map.fitBounds(bounds, { padding: [40, 40], maxZoom: 18 });
                map._livemap_fitted = true;
            }
        } else if (roverPos) {
            map.setView(roverPos, 18);
            map._livemap_fitted = true;
        }
    }

    // React to store changes
    $effect(() => {
        // Track state.position changes to trigger re-render
        const state = $socketStore.state;
        if (map && L) {
            updateLayers();
        }
    });

    // ─── Lifecycle / visibility ───────────────────────────────────────────
    let lastLivemapActive = false;
    function syncLivemapPolling() {
        if (!browser) return;
        const dashboard = $page.url.searchParams.get('dashboard');
        const isLivemap = dashboard === 'livemap';
        if (isLivemap && !lastLivemapActive) {
            socketService.requestGpsDetails();
            gpsStore.connect();
            initMap();
            // If initMap was skipped because the container was still hidden,
            // retry once the browser has painted the active panel.
            requestAnimationFrame(() => {
                if (!map && hasVisibleSize()) initMap();
                if (map) {
                    // Give the layout one more frame so Leaflet sees the final size.
                    requestAnimationFrame(() => {
                        map.invalidateSize();
                    });
                }
            });
            lastLivemapActive = true;
        } else if (!isLivemap && lastLivemapActive) {
            socketService.stopGpsDetails();
            gpsStore.disconnect();
            lastLivemapActive = false;
        }
    }
    onMount(() => {
        syncLivemapPolling();
    });
    afterNavigate(syncLivemapPolling);

    onDestroy(() => {
        if (lastLivemapActive) {
            socketService.stopGpsDetails();
            gpsStore.disconnect();
            lastLivemapActive = false;
        }
        if (resizeObserver) {
            resizeObserver.disconnect();
            resizeObserver = null;
        }
        if (map) {
            map.remove();
            map = null;
        }
    });

    function onWindowChange() {
        if (map && L) {
            map._livemap_fitted = false;
            updateLayers();
        }
    }
</script>

<div class="livemap-container">
    <!-- Toolbar -->
    <div class="livemap-toolbar">
        <div class="livemap-title">🗺️ Live Map</div>
        <div class="livemap-controls">
            <label class="livemap-label">
                Track
                <select bind:value={selectedWindow} onchange={onWindowChange}>
                    {#each TIME_WINDOWS as w}
                        <option value={w.ms}>{w.label}</option>
                    {/each}
                </select>
            </label>
            <label class="livemap-check">
                <input type="checkbox" bind:checked={showAccuracy} onchange={updateLayers} />
                Accuracy
            </label>

            <div class="livemap-stats">
                {#if gpsState.navPvt}
                    {@const p = gpsState.navPvt}
                    <span class="stat-item">{(p.lat ?? 0).toFixed(6)}°, {(p.lon ?? 0).toFixed(6)}°</span>
                    <span class="stat-item">±{(p.hAcc ?? 0).toFixed(2)} m</span>
                    <span class="stat-item">{(p.gSpeed ?? 0).toFixed(2)} m/s</span>
                {:else if $socketStore.state?.position && settingsPos?.mode === 'relative'}
                    {@const mowerPos = $socketStore.state.position}
                    {@const abs = relativeToAbsolute(mowerPos.x, mowerPos.y)}
                    {#if abs}
                        <span class="stat-item">{abs[0].toFixed(6)}°, {abs[1].toFixed(6)}°</span>
                        <span class="stat-item">±{(mowerPos.accuracy ?? 0).toFixed(2)} m</span>
                    {/if}
                {:else}
                    <span class="stat-item stat-wait">Waiting for GPS…</span>
                {/if}
            </div>
        </div>
    </div>

    <!-- Map container -->
    <div bind:this={mapContainer} class="livemap-map"></div>
</div>

<svelte:head>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"
        integrity="sha256-p4NxAoJBhIIN+hmNHrzRCf9tD/miZyoHS5obTRR9BMY="
        crossorigin="" />
</svelte:head>

<style lang="scss">
    .livemap-container {
        display: flex;
        flex-direction: column;
        width: 100%;
        height: 100vh;
        padding-top: 48px;
        box-sizing: border-box;
    }

    .livemap-toolbar {
        display: flex;
        flex-wrap: wrap;
        align-items: center;
        gap: 12px;
        padding: 8px 12px;
        background: white;
        border-bottom: 1px solid #ddd;
        z-index: 10;
    }

    .livemap-title {
        font-weight: 600;
        font-size: 0.95em;
        color: #333;
    }

    .livemap-controls {
        display: flex;
        flex-wrap: wrap;
        align-items: center;
        gap: 12px;
        flex: 1;
    }

    .livemap-label {
        display: flex;
        align-items: center;
        gap: 6px;
        font-size: 0.8em;
        color: #555;
    }

    .livemap-label select {
        padding: 4px 8px;
        border: 1px solid #ccc;
        border-radius: 4px;
        font-size: 0.9em;
    }

    .livemap-check {
        display: flex;
        align-items: center;
        gap: 4px;
        font-size: 0.8em;
        color: #555;
        cursor: pointer;
    }

    .livemap-stats {
        display: flex;
        flex-wrap: wrap;
        gap: 8px;
        margin-left: auto;
    }

    .stat-item {
        font-size: 0.75em;
        padding: 3px 8px;
        background: #f4f4f4;
        border-radius: 3px;
        font-family: monospace;
        color: #333;
    }

    .stat-wait {
        color: #888;
        font-style: italic;
    }

    .livemap-map {
        flex: 1;
        width: 100%;
        min-height: 0;
    }

    :global(.rover-marker) {
        background: transparent !important;
        border: none !important;
    }

    :global(.rover-marker-inner) {
        width: 24px;
        height: 24px;
        position: relative;
    }

    :global(.rover-body) {
        width: 12px;
        height: 12px;
        background: #006064;
        border: 2px solid white;
        border-radius: 50%;
        position: absolute;
        top: 6px;
        left: 6px;
        box-shadow: 0 1px 3px rgba(0,0,0,0.3);
    }

    :global(.rover-arrow) {
        width: 0;
        height: 0;
        border-left: 5px solid transparent;
        border-right: 5px solid transparent;
        border-bottom: 10px solid #006064;
        position: absolute;
        top: -2px;
        left: 7px;
    }

    :global(.leaflet-container) {
        font-family: inherit;
    }
</style>
