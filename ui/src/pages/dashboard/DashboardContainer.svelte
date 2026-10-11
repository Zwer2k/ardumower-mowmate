<script lang="ts">
    import { onMount, onDestroy } from 'svelte';
    import { page } from '$app/stores';
    import { browser } from '$app/environment';
    import { socketService } from '../../stores/socket';
    import MainDashboard from './main/MainDashboard.svelte';
    import StatusDashboard from './status/StatusDashboard.svelte';
    import LogDashboard from './log/LogDashboard.svelte';
    import TerminalDashboard from './terminal/TerminalDashboard.svelte';
    import TestsDashboard from './tests/TestsDashboard.svelte';
    import GpsDashboard from './gps/GpsDashboard.svelte';

    const mapEnabled = import.meta.env.VITE_ENABLE_MAP === 'true';
    const liveMapEnabled = import.meta.env.VITE_ENABLE_LIVE_MAP === 'true';
    const gpsDashboardEnabled = import.meta.env.VITE_ENABLE_GPS_DASHBOARD === 'true';

    let currentDashboard = $state('main');

    // Reagiere auf URL-Änderungen (Query-Parameter basiert)
    $effect(() => {
        if (browser && $page.url) {
            const dashboard = $page.url.searchParams.get('dashboard') || 'main';
            const validDashboards = ['main', 'status', 'log', 'terminal', 'tests'];
            if (mapEnabled) validDashboards.push('map');
            if (gpsDashboardEnabled) validDashboards.push('gps');
            if (liveMapEnabled) validDashboards.push('livemap');
            if (validDashboards.includes(dashboard)) {
                currentDashboard = dashboard;
            }
        }
    });

    onMount(() => {
        if (browser) {
            document.body.classList.add('dashboard-mode');
            console.log('DashboardContainer: Dashboard mode activated');
        }
    });

    onDestroy(() => {
        if (browser) {
            document.body.classList.remove('dashboard-mode');
            console.log('DashboardContainer: Cleanup');
        }
    });
</script>

<div class="dashboard-container">
    <!-- Alle Dashboards sind immer geladen und bleiben im Memory -->
    <div class="dashboard-panel" class:active={currentDashboard === 'main'}>
        <MainDashboard />
    </div>
    <div class="dashboard-panel" class:active={currentDashboard === 'status'}>
        <StatusDashboard />
    </div>
    <div class="dashboard-panel" class:active={currentDashboard === 'log'}>
        <LogDashboard />
    </div>
    <div class="dashboard-panel" class:active={currentDashboard === 'terminal'}>
        <TerminalDashboard />
    </div>
    {#if mapEnabled}
    <div class="dashboard-panel" class:active={currentDashboard === 'map'}>
        {#await import('../../map/Map.svelte') then { default: MapComp }}
            <MapComp />
        {/await}
    </div>
    {/if}
    {#if gpsDashboardEnabled}
    <div class="dashboard-panel" class:active={currentDashboard === 'gps'}>
        <GpsDashboard />
    </div>
    {/if}
    {#if liveMapEnabled}
    <div class="dashboard-panel" class:active={currentDashboard === 'livemap'}>
        {#await import('./map/LiveMap.svelte') then { default: LiveMapComp }}
            <LiveMapComp />
        {/await}
    </div>
    {/if}
    <div class="dashboard-panel" class:active={currentDashboard === 'tests'}>
        <TestsDashboard />
    </div>
</div>

<style>
    /* An den sichtbaren Bildschirm gebunden: Mobile Browser richten fixierte
       Elemente mit top/bottom 0 an der tatsächlich sichtbaren Fläche aus, auch
       während die Adressleiste ein- oder ausklappt. Eine berechnete Höhe
       (vh, dvh oder innerHeight) passt auf manchen Geräten nicht genau. */
    .dashboard-container {
        position: fixed;
        top: 0;
        right: 0;
        bottom: 0;
        left: 0;
        overflow: hidden;
    }

    .dashboard-panel {
        position: absolute;
        top: 0;
        left: 0;
        width: 100%;
        height: 100%;
        opacity: 0;
        visibility: hidden;
        transition: opacity 0.2s ease-in-out;
        pointer-events: none;
    }

    .dashboard-panel.active {
        opacity: 1;
        visibility: visible;
        pointer-events: auto;
    }

</style>
