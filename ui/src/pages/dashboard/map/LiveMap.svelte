<script lang="ts">
    import { onMount, onDestroy } from 'svelte';
    import { page } from '$app/stores';
    import { afterNavigate } from '$app/navigation';
    import { browser } from '$app/environment';
    import { socketStore } from '../../../stores/socket';
    import { gpsStore } from '../../../stores/gpsStore';
    import type { PositionSample } from '../../../stores/gpsStore';
    import { MapStore } from '../../../map/service';
    import { mowSettingsStore } from '../../../map/mow-settings';
    import { filterWaypointsByToggles } from '../../../map/core/waypoint-filter';
    import { relativeToAbsolute, type GeoJsonReference } from '../../../map/core/map-formats';
    import { BackendSettings } from '../../../stores/backend';

    let settingsPos = $derived($BackendSettings?.position);
    let mowerMap = $derived($MapStore?.map);

    // Bezugspunkt der Kartenkoordinaten (Meter) auf der Erde:
    // - Position mode "Absolute": die eingestellte Referenz, wie beim
    //   CaSSAndRA-Import und -Export.
    // - "Relative": Sunray rechnet relativ zur RTK-Basis, deren Lage das Modem
    //   nicht kennt. Sie ergibt sich aber aus der GPS-Position des Mähers
    //   (NAV-PVT) minus seiner Kartenposition; gemittelt über mehrere Proben.
    const REFERENCE_SAMPLES = 20;
    let derivedSamples: GeoJsonReference[] = [];
    let derivedReference = $state<GeoJsonReference | null>(null);

    function configuredReference(): GeoJsonReference | null {
        if (settingsPos?.mode !== 'absolute') return null;
        const { lon, lat } = settingsPos;
        if (!Number.isFinite(lon) || !Number.isFinite(lat) || (lon === 0 && lat === 0)) return null;
        return { lon, lat };
    }

    let reference = $derived(configuredReference() ?? derivedReference);
    let referenceSource = $derived(configuredReference() ? 'settings' : derivedReference ? 'gps' : 'none');

    function median(values: number[]): number {
        const sorted = [...values].sort((a, b) => a - b);
        return sorted[Math.floor(sorted.length / 2)];
    }

    // Kartenpunkt im Editorformat (y nach unten) in [lat, lon] für Leaflet
    function mapToLatLng(p: { x: number; y: number }, ref: GeoJsonReference): [number, number] {
        const [lon, lat] = relativeToAbsolute(p, ref);
        return [lat, lon];
    }

    // Mäherposition aus dem Status (y nach Norden) in [lat, lon]
    function mowerToLatLng(x: number, y: number, ref: GeoJsonReference): [number, number] {
        return mapToLatLng({ x, y: -y }, ref);
    }

    function updateDerivedReference() {
        if (configuredReference()) return;
        const pvt = gpsState.navPvt;
        const pos = $socketStore.state?.position;
        if (!pvt || !pvt.fixOk || pvt.carrSoln !== 2 || !pos) return;
        if (Date.now() - pvt.timestamp > 2000) return;
        // Umkehrung von relativeToAbsolute: Referenz = GPS-Position - Kartenposition
        const lat = pvt.lat - pos.y / 111111;
        const lon = pvt.lon - pos.x / (111111 * Math.cos(lat * Math.PI / 180));
        derivedSamples.push({ lon, lat });
        if (derivedSamples.length > REFERENCE_SAMPLES) derivedSamples.shift();
        if (derivedSamples.length >= 3) {
            derivedReference = {
                lon: median(derivedSamples.map(r => r.lon)),
                lat: median(derivedSamples.map(r => r.lat)),
            };
        }
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
    let showMowerMap = $state(true);
    let showRoute = $state(true);

    let mapContainer: HTMLDivElement;
    let L: any = null;
    let map: any = null;
    let trackPolyline: any = null;
    let accuracyCircle: any = null;
    let roverMarker: any = null;
    let mapLayer: any = null;
    let mapLayerKey = '';
    let mapBounds = $state.raw<any>(null);
    let roverPosition = $state.raw<[number, number] | null>(null);
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
                preferCanvas: true,
                maxZoom: 22,
            }).setView([51.1657, 10.4515], 6);

            L.control.zoom({ position: 'bottomright' }).addTo(map);

            // OSM liefert Kacheln bis Zoom 19; darüber werden sie vergrößert,
            // damit auch ein kleiner Garten bildschirmfüllend dargestellt wird.
            L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
                maxNativeZoom: 19,
                maxZoom: 22,
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

    // Mähkarte als eigene Ebene. Neu gezeichnet wird sie nur, wenn sich Karte,
    // Bezugspunkt oder Anzeige ändern, nicht bei jedem Statusupdate.
    function updateMowerMapLayer() {
        if (!map || !L) return;
        const ref = reference;
        const m = mowerMap;
        const settings = $mowSettingsStore;
        const key = !ref || !m || !showMowerMap ? '' : JSON.stringify([
            ref.lon, ref.lat, showRoute, m.perimeter.points.length, m.waypoints.points.length,
            m.exclusions.length, m.perimeter.points[0], settings,
        ]);
        if (key === mapLayerKey && (key === '' || mapLayer)) return;
        mapLayerKey = key;
        if (mapLayer) { map.removeLayer(mapLayer); mapLayer = null; }
        mapBounds = null;
        if (!key || !ref || !m || m.perimeter.points.length < 3) return;

        const toLL = (p: { x: number; y: number }) => mapToLatLng(p, ref);
        const perimeter = m.perimeter.points.map(toLL);
        const exclusions = m.exclusions.filter(e => e.points.length >= 3).map(e => e.points.map(toLL));
        mapLayer = L.layerGroup();
        L.polygon([perimeter, ...exclusions], {
            color: '#2e7d32', weight: 2, fillColor: '#66bb6a', fillOpacity: 0.12,
        }).addTo(mapLayer);
        for (const ex of exclusions) {
            L.polygon(ex, { color: '#c62828', weight: 1.5, fillColor: '#e57373', fillOpacity: 0.25 }).addTo(mapLayer);
        }
        if (showRoute && m.waypoints.points.length >= 2) {
            const route = settings ? filterWaypointsByToggles(m.waypoints.points, m, settings) : m.waypoints.points;
            if (route.length >= 2) {
                L.polyline(route.map(toLL), { color: '#455a64', weight: 1, opacity: 0.6 }).addTo(mapLayer);
            }
        }
        if (m.dockpoints.points.length >= 2) {
            L.polyline(m.dockpoints.points.map(toLL), { color: '#1565c0', weight: 3, opacity: 0.9 }).addTo(mapLayer);
        }
        mapLayer.addTo(map);
        mapBounds = L.latLngBounds(perimeter);
    }

    function updateLayers() {
        if (!map || !L) return;

        updateMowerMapLayer();

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
            // Ersatz: Kartenposition aus dem Status über den Bezugspunkt umrechnen
            const mowerState = $socketStore.state;
            if (mowerState?.position && reference) {
                roverPos = mowerToLatLng(mowerState.position.x, mowerState.position.y, reference);
                // delta: Sunray-Winkel gegen den Uhrzeigersinn ab Osten -> Kompass
                roverHeading = 90 - mowerState.position.delta * 180 / Math.PI;
                roverAccuracy = mowerState.position.accuracy;
            }
        }
        roverPosition = roverPos;

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
        if (mapBounds && !map._livemap_fitted) {
            map.fitBounds(mapBounds, { padding: [30, 30], maxZoom: 22 });
            map._livemap_fitted = true;
        } else if (trackLatLngs.length >= 2) {
            const bounds = L.latLngBounds(trackLatLngs);
            if (roverPos) bounds.extend(roverPos);
            if (!map._livemap_fitted) {
                map.fitBounds(bounds, { padding: [40, 40], maxZoom: 18 });
                map._livemap_fitted = true;
            }
        } else if (roverPos && !map._livemap_fitted) {
            map.setView(roverPos, 20);
            map._livemap_fitted = true;
        }
    }

    function showMower() {
        if (map && roverPosition) map.setView(roverPosition, Math.max(map.getZoom(), 20));
    }

    function showMap() {
        if (map && mapBounds) map.fitBounds(mapBounds, { padding: [30, 30], maxZoom: 22 });
    }

    // React to store changes
    $effect(() => {
        // Track state.position changes to trigger re-render
        const state = $socketStore.state;
        updateDerivedReference();
        if (map && L) {
            updateLayers();
        }
    });

    // Karte, Bezugspunkt oder Mäh-Einstellungen geändert
    $effect(() => {
        const deps = [mowerMap, reference, $mowSettingsStore];
        if (deps && map && L) updateLayers();
    });

    // ─── Lifecycle / visibility ───────────────────────────────────────────
    let lastLivemapActive = false;
    function syncLivemapPolling() {
        if (!browser) return;
        const dashboard = $page.url.searchParams.get('dashboard');
        const isLivemap = dashboard === 'livemap';
        if (isLivemap && !lastLivemapActive) {
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
            <label class="livemap-check">
                <input type="checkbox" bind:checked={showMowerMap} onchange={updateLayers} />
                Map
            </label>
            <label class="livemap-check">
                <input type="checkbox" bind:checked={showRoute} onchange={updateLayers} disabled={!showMowerMap} />
                Route
            </label>
            <div class="livemap-buttons">
                <button type="button" onclick={showMower} disabled={!roverPosition}
                    title="Center the view on the mower">Mower position</button>
                <button type="button" onclick={showMap} disabled={!mapBounds || !showMowerMap}
                    title="Show the whole mowing map">Map position</button>
            </div>

            <div class="livemap-stats">
                {#if gpsState.navPvt}
                    {@const p = gpsState.navPvt}
                    <span class="stat-item">{(p.lat ?? 0).toFixed(6)}°, {(p.lon ?? 0).toFixed(6)}°</span>
                    <span class="stat-item">±{(p.hAcc ?? 0).toFixed(2)} m</span>
                    <span class="stat-item">{(p.gSpeed ?? 0).toFixed(2)} m/s</span>
                {:else if $socketStore.state?.position && reference}
                    {@const mowerPos = $socketStore.state.position}
                    {@const abs = mowerToLatLng(mowerPos.x, mowerPos.y, reference)}
                    <span class="stat-item">{abs[0].toFixed(6)}°, {abs[1].toFixed(6)}°</span>
                    <span class="stat-item">±{(mowerPos.accuracy ?? 0).toFixed(2)} m</span>
                {:else}
                    <span class="stat-item stat-wait">Waiting for GPS…</span>
                {/if}
                {#if showMowerMap && mowerMap && referenceSource === 'none'}
                    <span class="stat-item stat-wait"
                        title="Set Position mode Absolute with the reference longitude/latitude, or wait for an RTK fix so the reference can be derived from GPS">
                        Map: no reference yet
                    </span>
                {:else if showMowerMap && referenceSource === 'gps'}
                    <span class="stat-item" title="Reference derived from the mower's GPS position (RTK fix) and its map position">
                        Map ref: GPS
                    </span>
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

    .livemap-buttons {
        display: flex;
        gap: 6px;
    }

    .livemap-buttons button {
        padding: 4px 10px;
        font-size: 0.8em;
        border: 1px solid #006064;
        border-radius: 4px;
        background: white;
        color: #006064;
        cursor: pointer;
    }

    .livemap-buttons button:hover:not(:disabled) {
        background: #e0f2f1;
    }

    .livemap-buttons button:disabled {
        border-color: #ccc;
        color: #aaa;
        cursor: default;
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
