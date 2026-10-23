<script lang="ts">
    import { onMount, onDestroy } from 'svelte';
    import { select, pointer } from 'd3-selection';
    import { zoom, zoomIdentity, type ZoomTransform } from 'd3-zoom';
    import { MapStore, drivenTrackStore } from '../../../map/service';
    import { socketStore } from '../../../stores/socket';
    import { mowSettingsStore, mowAreasUpdatingStore } from '../../../map/mow-settings';
    import { filterWaypointsByToggles } from '../../../map/core/waypoint-filter';
    import { splitRouteProgress, routeTransits, mergeTransits } from '../../../map/core/route-progress';
    import { isMowerMapSynced } from '../../../map/services/map-sync';
    import { mowJobStore, isJobOnMower, isJobPreview, jobPartProgress, jobRouteIndex, outlineAsMap } from '../../../stores/mow-job';
    import Obstacles from '../../../map/Obstacles.svelte';
    import JobPreviewLayer from '../../../map/JobPreviewLayer.svelte';
    import { areaMarksStore } from '../../../map/area-marks';
    import MapLoadingOverlay from '../../../map/MapLoadingOverlay.svelte';
    import MapGotoOverlay from '../../../map/overlay/MapGotoOverlay.svelte';
    import GotoControls from '../../../map/overlay/GotoControls.svelte';
    import { createMapSelection, gotoStore } from '../../../map/interactions/map-goto';

    export let mowPointIndex: number = -1;

    // Bei einem Auftrag (Vorschau oder auf dem Mäher) dessen Karte zeichnen: Er
    // gehört nicht zwingend zur geladenen Karte
    $: jobOutline = (jobOnMower || jobPreview) ? $mowJobStore?.geometry : undefined;
    $: map = jobOutline ? (outlineAsMap(jobOutline) as unknown as import('../../../map/model').Map) : $MapStore?.map;
    $: presentation = jobOutline ? undefined : $MapStore?.presentation;
    $: mowerPos = $socketStore?.state?.position ?? null;
    $: track = $drivenTrackStore;
    $: mapRotation = jobOutline ? jobOutline.rotation : ($socketStore?.currentMapMeta?.rotation ?? 0);
    $: waypoints = map?.waypoints?.points ?? [];
    $: completedIndex = mowPointIndex ?? -1;

    $: mx = mowerPos?.x ?? 0;
    $: my = mowerPos != null ? -mowerPos.y : 0;
    $: headingDeg = mowerPos ? 90 - mowerPos.delta * 180 / Math.PI : 0;

    let svgEl: SVGSVGElement | null = null;
    let mapGroup: SVGGElement | null = null;
    let currentTransform: ZoomTransform = zoomIdentity;
    let cleanup: (() => void) | null = null;
    let compassOffset = 0;

    $: hasAnyMapData = (map?.perimeter?.points?.length ?? 0) > 0 ||
                         (map?.waypoints?.points?.length ?? 0) > 0 ||
                         (map?.dockpoints?.points?.length ?? 0) > 0 ||
                         (map?.exclusions ?? []).some((ex) => (ex.points?.length ?? 0) > 0);

    $: storedCrc = $socketStore?.currentMapMeta?.crc ?? 0;
    // Ein Mähauftrag aus mehreren Bereichen ersetzt die Route der geladenen Karte
    $: jobOnMower = isJobOnMower($mowJobStore, $socketStore.state);
    // Vorschau: zusammengesetzt, noch nicht hochgeladen
    $: jobPreview = isJobPreview($mowJobStore, $socketStore.state);
    function jobStatusText(job: typeof $mowJobStore): string {
        if (!job) return '';
        const names = job.parts.map((p) => p.name).join(', ');
        return job.segmentStarts.length > 1
            ? `Auftrag: ${names} · Teil ${job.segment + 1}/${job.segmentStarts.length}`
            : `Auftrag: ${names}`;
    }
    // Bereiche mit zufälligem/dynamischem Winkel: Ihre Route entsteht je Auftrag
    // neu, ihre Fläche wird wie im Zeitplaner farbig hervorgehoben
    $: shownAreaIds = (jobOnMower || jobPreview) && $mowJobStore
        ? $mowJobStore.parts.map((p) => p.id)
        : ($socketStore.currentMapId ? [$socketStore.currentMapId] : []);
    // Geladene Karte: ihre Auswahl liegt schon vor (MapStore), ohne Anfrage beim Modem
    $: knownSelections = !(jobOnMower || jobPreview) && $socketStore.currentMapId && !$socketStore.isLoadingMap
        ? { [$socketStore.currentMapId]: $MapStore?.map?.selection?.points ?? [] }
        : {};
    $: dynamicAreaIds = shownAreaIds.filter((id) => ($socketStore.maps ?? []).some((m) => m.id === id && m.dynamicAngle));
    $: isMapSynced = jobOnMower || isMowerMapSynced($socketStore.state, $socketStore.currentMapId, storedCrc);
    $: uploadOp = ($socketStore?.state as any)?.progressOp;
    $: uploadMsg = $socketStore?.state?.progressMsg ?? '';
    $: uploadPct = $socketStore?.state?.progressPct ?? 0;
    $: isUploading = uploadOp === 'upload' && uploadPct < 100;
    $: mapStatus = isUploading
        ? { text: uploadMsg || 'Hochladen...', synced: false, warn: false }
        : !hasAnyMapData
            ? { text: 'Keine Karte', synced: false, warn: false }
            : jobPreview
                ? { text: `Vorschau: ${($mowJobStore?.parts ?? []).map((p) => p.name).join(', ')} · nicht hochgeladen`, synced: false, warn: true }
            : isMapSynced
                ? { text: jobOnMower ? jobStatusText($mowJobStore) : 'Karte synchron', synced: true, warn: false }
                : { text: 'Karte nicht synchron', synced: false, warn: true };

    $: viewBoxValid = isViewBoxValid(presentation?.viewBox);
    $: derivedViewBox = deriveViewBox(map, presentation?.viewBox);
    $: safeViewBox = (viewBoxValid ? presentation?.viewBox : undefined) ?? derivedViewBox;

    $: transformAttr = viewBoxValid || derivedViewBox !== '0 0 1 1'
        ? `translate(${safeNumber(currentTransform.x).toFixed(4)}, ${safeNumber(currentTransform.y).toFixed(4)}) scale(${safeNumber(currentTransform.k).toFixed(4)})`
        : '';

    $: vb = safeViewBox.split(/\s+/).map(Number);
    $: cx = safeNumber(vb[0] + vb[2] / 2);
    $: cy = safeNumber(vb[1] + vb[3] / 2);
    $: totalRotation = mapRotation + compassOffset;
    $: rotationAttr = (viewBoxValid || derivedViewBox !== '0 0 1 1') && Number.isFinite(totalRotation)
        ? `rotate(${safeNumber(totalRotation).toFixed(4)}, ${cx.toFixed(4)}, ${cy.toFixed(4)})`
        : '';

    function isViewBoxValid(viewBox: string | undefined): boolean {
        if (!viewBox) return false;
        const parts = viewBox.split(/\s+/).map(Number);
        if (parts.length !== 4) return false;
        const max = 1e9;
        const [x, y, w, h] = parts;
        return (
            Number.isFinite(x) && Math.abs(x) < max &&
            Number.isFinite(y) && Math.abs(y) < max &&
            Number.isFinite(w) && Math.abs(w) < max && w > 0 &&
            Number.isFinite(h) && Math.abs(h) < max && h > 0
        );
    }

    function deriveViewBox(map: any, viewBox: string | undefined): string {
        if (isViewBoxValid(viewBox)) return viewBox!;

        const points: { x: number; y: number }[] = [];
        for (const arr of [
            map?.perimeter?.points ?? [],
            map?.waypoints?.points ?? [],
            map?.dockpoints?.points ?? [],
            ...(map?.exclusions ?? []).map((ex: any) => ex.points ?? []),
        ]) {
            for (const p of arr) {
                if (p && Number.isFinite(p.x) && Number.isFinite(p.y)) {
                    points.push({ x: p.x, y: p.y });
                }
            }
        }

        if (points.length === 0) return '0 0 1 1';

        const xs = points.map((p) => p.x);
        const ys = points.map((p) => p.y);
        const minX = Math.min(...xs);
        const maxX = Math.max(...xs);
        const minY = Math.min(...ys);
        const maxY = Math.max(...ys);
        const padding = 1;
        const w = Math.max(maxX - minX, 0.001) + padding * 2;
        const h = Math.max(maxY - minY, 0.001) + padding * 2;
        const x = minX - padding;
        const y = minY - padding;
        return `${x} ${y} ${w} ${h}`;
    }

    function safeNumber(n: number): number {
        return Number.isFinite(n) ? n : 0;
    }


    // ─── Goto (wie im Karteneditor, gemeinsames Ziel) ──────────────────────
    const gotoSelection = createMapSelection();
    const gotoHint = gotoSelection.hint;
    $: hasPerimeter = (map?.perimeter?.points?.length ?? 0) > 0;
    let pointerStart: { id: number; x: number; y: number; time: number } | null = null;

    function onPointerDown(event: PointerEvent) {
        pointerStart = { id: event.pointerId, x: event.clientX, y: event.clientY, time: performance.now() };
    }

    // Kurzer Tipp ohne Verschieben = Klick (wie Canvas.svelte im Editor)
    function onPointerUp(event: PointerEvent) {
        if (!pointerStart || event.pointerId !== pointerStart.id) return;
        const moved = Math.hypot(event.clientX - pointerStart.x, event.clientY - pointerStart.y);
        const dt = performance.now() - pointerStart.time;
        pointerStart = null;
        if (dt >= 300 || moved >= 8 || !mapGroup || !hasPerimeter) return;
        if (!gotoSelection.click()) return;
        const [x, y] = pointer(event, mapGroup);
        gotoStore.setTarget(x, y);
    }

    // Etwas außerhalb der Karte bedient: der nächste Klick wählt nur aus
    function onWindowPointerDown(event: PointerEvent) {
        if (!svgEl || !(event.target instanceof Node) || !svgEl.contains(event.target)) {
            gotoSelection.interact();
        }
    }

    onMount(() => {
        window.addEventListener('pointerdown', onWindowPointerDown, true);
        return () => window.removeEventListener('pointerdown', onWindowPointerDown, true);
    });

    onDestroy(() => gotoSelection.destroy());

    function attachZoom(node: SVGSVGElement) {
        const z = zoom<SVGSVGElement, unknown>()
            .scaleExtent([0.5, 10])
            .on('zoom', ({ transform, sourceEvent }) => {
                if (sourceEvent) gotoSelection.interact();
                currentTransform = transform;
            });
        const sel = select(node);
        sel.call(z);
        return () => sel.on('.zoom', null);
    }

    $: if (svgEl && (viewBoxValid || derivedViewBox !== '0 0 1 1')) {
        cleanup = attachZoom(svgEl);
    } else if (cleanup) {
        cleanup();
        cleanup = null;
        currentTransform = zoomIdentity;
    }

    onDestroy(() => {
        cleanup?.();
    });

    function resetZoom() {
        if (!svgEl) return;
        const z = zoom<SVGSVGElement, unknown>()
            .scaleExtent([0.5, 10])
            .on('zoom', ({ transform }) => {
                currentTransform = transform;
            });
        select(svgEl).transition().duration(250).call(z.transform, zoomIdentity);
    }

    function resetCompass() {
        compassOffset = 0;
    }

    function onCompassDown(event: MouseEvent) {
        event.preventDefault();
        const btn = event.currentTarget as HTMLElement;
        const rect = btn.getBoundingClientRect();
        const centerX = rect.left + rect.width / 2;
        const centerY = rect.top + rect.height / 2;
        const startAngle = Math.atan2(event.clientY - centerY, event.clientX - centerX) * 180 / Math.PI;
        let lastAngle = startAngle;
        let didDrag = false;

        function onMove(e: MouseEvent) {
            didDrag = true;
            const angle = Math.atan2(e.clientY - centerY, e.clientX - centerX) * 180 / Math.PI;
            let delta = angle - lastAngle;
            if (delta > 180) delta -= 360;
            if (delta < -180) delta += 360;
            compassOffset = ((compassOffset + delta) % 360 + 360) % 360;
            lastAngle = angle;
        }

        function onUp() {
            window.removeEventListener('mousemove', onMove);
            window.removeEventListener('mouseup', onUp);
            if (!didDrag) {
                compassOffset = ((compassOffset + 90) % 360 + 360) % 360;
            }
        }

        window.addEventListener('mousemove', onMove);
        window.addEventListener('mouseup', onUp);
    }

    function arrowPoints(x: number, y: number, deg: number): string {
        // Same logic as Editor map: deg 0 = north (up), increasing clockwise
        // Convert to radians with -90° offset (SVG coordinates: 0° = east)
        const rad = ((deg - 90) * Math.PI) / 180;
        const r = 0.25;
        const tipX = x + r * Math.cos(rad);
        const tipY = y + r * Math.sin(rad);  // SVG y increases downward
        const wing1 = rad + Math.PI * 0.8;
        const wing2 = rad - Math.PI * 0.8;
        const w1x = x + r * 0.5 * Math.cos(wing1);
        const w1y = y + r * 0.5 * Math.sin(wing1);
        const w2x = x + r * 0.5 * Math.cos(wing2);
        const w2y = y + r * 0.5 * Math.sin(wing2);
        return `${w1x},${w1y} ${tipX},${tipY} ${w2x},${w2y}`;
    }

</script>

<div class="mini-map">
    {#if hasAnyMapData}
        <div class="map-status" class:synced={mapStatus.synced} class:warn={mapStatus.warn}>
            {mapStatus.text}
        </div>
        <button class="compass-btn" on:mousedown={onCompassDown} on:dblclick={resetCompass} title="Karte drehen (ziehen für fein, Doppelklick zurücksetzen)">
            <svg viewBox="-12 -12 24 24" width="20" height="20">
                <g transform="rotate({totalRotation})">
                    <circle cx="0" cy="0" r="10" fill="white" stroke="#999" stroke-width="1.5"/>
                    <polygon points="0,-8 -4,0 0,-2 4,0" fill="#d32f2f"/>
                    <polygon points="0,8 -4,0 0,2 4,0" fill="#999"/>
                </g>
            </svg>
        </button>
        {#if $mowAreasUpdatingStore}
            <span class="updating-hint" aria-live="polite">Updating…</span>
        {/if}
        <button class="zoom-reset" on:click={resetZoom} title="Zoom zurücksetzen">⟲</button>
        <!-- svelte-ignore a11y_no_static_element_interactions -->
        <svg bind:this={svgEl} viewBox={safeViewBox} preserveAspectRatio="xMidYMid meet"
            class:grab={!$gotoHint && !$gotoStore.targetSet}
            on:pointerdown={onPointerDown} on:pointerup={onPointerUp}>
            <g transform={transformAttr}>
                <g transform={rotationAttr} bind:this={mapGroup}>
                    {#if map.perimeter?.points?.length > 0}
                        <polygon
                            points={map.perimeter.points.map((p) => `${p.x},${p.y}`).join(' ')}
                            fill="rgba(36, 161, 72, 0.15)"
                            stroke="red"
                            stroke-width="0.04"
                        />
                    {/if}

                {#each map.exclusions ?? [] as ex}
                    <polygon
                        points={ex.points.map((p) => `${p.x},${p.y}`).join(' ')}
                        fill="rgba(218, 30, 40, 0.15)"
                        stroke="#da1e28"
                        stroke-width="0.03"
                    />
                {/each}

                <Obstacles obstacles={$socketStore.obstacles} />

                {#if $areaMarksStore && dynamicAreaIds.length > 0 && map.perimeter?.points?.length}
                    <JobPreviewLayer areaIds={shownAreaIds} onlyIds={dynamicAreaIds} perimeter={map.perimeter.points} {knownSelections} />
                {/if}

                {#if map.dockpoints?.points?.length}
                    <polyline
                        points={map.dockpoints.points.map((p) => `${p.x},${p.y}`).join(' ')}
                        fill="none"
                        stroke="orange"
                        stroke-width="0.008"
                        stroke-linecap="round"
                        stroke-linejoin="round"
                    />
                {/if}

                {#if map.searchWire?.points?.length > 1}
                    <polyline
                        points={map.searchWire.points.map((p) => `${p.x},${p.y}`).join(' ')}
                        fill="none"
                        stroke="#0f766e"
                        stroke-width="0.012"
                        stroke-dasharray="0.06 0.04"
                        stroke-linecap="round"
                        stroke-linejoin="round"
                    />
                {/if}

                {#if (jobOnMower || jobPreview) && $mowJobStore}
                    <!-- Mähauftrag: jeder Bereich in eigener Farbe, gemäht blass -->
                    {#each jobPartProgress($mowJobStore.points, $mowJobStore.parts, jobRouteIndex($mowJobStore, completedIndex), jobOnMower) as part}
                        {#each part.done as seg}
                            <polyline points={seg.map((p) => `${p.x},${p.y}`).join(' ')} fill="none"
                                stroke="rgba(127, 127, 127, 0.25)" stroke-opacity="0.5" stroke-width="2"
                                vector-effect="non-scaling-stroke" stroke-linecap="round" stroke-linejoin="round" />
                        {/each}
                        {#each part.todo as seg}
                            <polyline points={seg.map((p) => `${p.x},${p.y}`).join(' ')} fill="none"
                                stroke={part.color} stroke-opacity="0.7" stroke-width="2"
                                vector-effect="non-scaling-stroke" stroke-linecap="round" stroke-linejoin="round" />
                        {/each}
                        {#if part.current}
                            <polyline points={part.current.map((p) => `${p.x},${p.y}`).join(' ')} fill="none"
                                stroke="black" stroke-opacity="0.8" stroke-width="2" stroke-dasharray="4 3"
                                vector-effect="non-scaling-stroke" />
                        {/if}
                    {/each}
                {:else if map.waypoints?.points?.length}
                    {@const settings = $mowSettingsStore}
                    {@const visibleWaypoints = mergeTransits(filterWaypointsByToggles(map.waypoints.points, map, settings), $socketStore.currentMapTransits)}
                    {@const progress = splitRouteProgress(visibleWaypoints, completedIndex, isMapSynced)}
                    <!-- wie CaSSAndRA: alle Linien 2 px, offen grün, gemäht blass, aktuelles Teilstück gestrichelt -->
                    {#each progress.done as seg}
                        <polyline
                            points={seg.map((p) => `${p.x},${p.y}`).join(' ')}
                            fill="none"
                            stroke="rgba(127, 127, 127, 0.25)"
                            stroke-opacity="0.5"
                            stroke-width="2"
                            vector-effect="non-scaling-stroke"
                            stroke-linecap="round"
                            stroke-linejoin="round"
                        />
                    {/each}
                    <!-- Lücken, für die noch kein Weg des Modems vorliegt: gestrichelt gerade -->
                    {#each routeTransits(visibleWaypoints) as [a, b]}
                        <line x1={a.x} y1={a.y} x2={b.x} y2={b.y}
                            stroke="#7f7f7f" stroke-opacity="0.7" stroke-width="1"
                            stroke-dasharray="3 3" vector-effect="non-scaling-stroke" />
                    {/each}
                    {#each progress.todo as seg}
                        <polyline
                            points={seg.map((p) => `${p.x},${p.y}`).join(' ')}
                            fill="none"
                            stroke="#7fb249"
                            stroke-opacity="0.7"
                            stroke-width="2"
                            vector-effect="non-scaling-stroke"
                            stroke-linecap="round"
                            stroke-linejoin="round"
                        />
                    {/each}
                    {#if progress.current}
                        <polyline
                            points={progress.current.map((p) => `${p.x},${p.y}`).join(' ')}
                            fill="none"
                            stroke="black"
                            stroke-opacity="0.8"
                            stroke-width="2"
                            stroke-dasharray="4 3"
                            vector-effect="non-scaling-stroke"
                        />
                    {/if}
                {/if}

                </g>
                <g transform={rotationAttr}>
                    {#if track?.points?.length && isMapSynced}
                        <polyline
                            points={track.points.map((p) => `${p.x},${-p.y}`).join(' ')}
                            fill="none"
                            stroke="rgba(0, 200, 255, 0.5)"
                            stroke-width="0.05"
                        />
                    {/if}

                    <MapGotoOverlay
                        targetPos={$gotoStore.targetPos}
                        {mowerPos}
                        showTarget={$gotoStore.targetSet}
                        showLine={$gotoStore.targetSet && !!mowerPos && (mowerPos.x !== 0 || mowerPos.y !== 0)}
                    />

                    {#if mowerPos}
                        {#if (mowerPos.accuracy ?? 0) > 0}
                            <circle
                                cx={mx}
                                cy={my}
                                r={mowerPos.accuracy}
                                fill="rgba(0, 200, 255, 0.08)"
                                stroke="rgba(0, 200, 255, 0.35)"
                                stroke-width="0.02"
                            />
                        {/if}
                        <g transform="translate({mx}, {my})">
                            <circle r="0.22" fill="none" stroke="#00c8ff" stroke-width="0.05" />
                            <circle r="0.14" fill="#00c8ff" />
                            <polygon points={arrowPoints(0, 0, headingDeg)} fill="white" />
                        </g>
                    {/if}
                </g>
            </g>
        </svg>
        <GotoControls hint={$gotoHint} />
    {:else}
        <div class="no-map">No map loaded</div>
    {/if}
    <MapLoadingOverlay />
</div>

<style lang="scss">
    .mini-map {
        width: 100%;
        height: 100%;
        min-height: 200px;
        display: flex;
        align-items: center;
        justify-content: center;
        background: #f4f4f4;
        border-radius: 0;
        overflow: hidden;
        position: relative;
    }

    .map-status {
        position: absolute;
        top: 8px;
        right: 8px;
        padding: 2px 8px;
        border-radius: 12px;
        font-size: 0.625rem;
        font-weight: 600;
        text-transform: uppercase;
        letter-spacing: 0.03em;
        background: #da1e28;
        color: white;
        z-index: 2;
    }

    .map-status.synced {
        background: #24a148;
    }

    .map-status.warn {
        background: #f1c21b;
        color: #161616;
    }

    .zoom-reset,
    .compass-btn {
        position: absolute;
        width: 28px;
        height: 28px;
        border-radius: 50%;
        border: none;
        background: white;
        box-shadow: 0 1px 4px rgba(0,0,0,0.2);
        display: flex;
        align-items: center;
        justify-content: center;
        cursor: pointer;
        z-index: 2;
    }

    .zoom-reset {
        bottom: 8px;
        right: 8px;
        font-size: 1rem;
    }

    .compass-btn {
        top: 8px;
        left: 8px;
        padding: 0;
    }

    /* Mow areas warten auf das Modem: neben dem Kompass statt in der Kachel */
    .updating-hint {
        position: absolute;
        top: 12px;
        left: 44px;
        z-index: 2;
        padding: 0 0.375rem;
        font-size: 0.75rem;
        color: #525252;
        background: rgba(255, 255, 255, 0.85);
        border-radius: 2px;
        pointer-events: none;
    }

    svg {
        width: 100%;
        height: 100%;
        display: block;
    }

    /* Hand-Cursor nur, solange ein Klick kein Goto-Ziel setzt */
    svg.grab {
        cursor: grab;
    }

    svg.grab:active {
        cursor: grabbing;
    }

    .no-map {
        color: #525252;
        font-size: 0.875rem;
    }
</style>
