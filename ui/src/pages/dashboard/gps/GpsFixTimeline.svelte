<script lang="ts">
    import { FIX_HISTORY_MS, type FixSample } from '../../../stores/gpsStore';

    interface Props {
        history: FixSample[];
    }

    let { history }: Props = $props();

    // Lücken über diesem Abstand gelten als "keine Daten" (Dashboard war zu)
    const MAX_GAP_MS = 15000;

    interface Segment { start: number; end: number; carrSoln: number; }

    let now = $state(Date.now());
    $effect(() => {
        const t = setInterval(() => (now = Date.now()), 5000);
        return () => clearInterval(t);
    });

    let windowStart = $derived(now - FIX_HISTORY_MS);

    let segments = $derived.by(() => {
        const segs: Segment[] = [];
        for (let i = 0; i < history.length; i++) {
            const s = history[i];
            const next = history[i + 1];
            const end = next && next.time - s.time <= MAX_GAP_MS ? next.time : Math.min(s.time + 3000, now);
            const last = segs[segs.length - 1];
            if (last && last.carrSoln === s.carrSoln && last.end === s.time) last.end = end;
            else segs.push({ start: s.time, end, carrSoln: s.carrSoln });
        }
        return segs.filter(seg => seg.end > windowStart);
    });

    let stats = $derived.by(() => {
        let fixed = 0, total = 0, losses = 0;
        for (const seg of segments) {
            const d = seg.end - Math.max(seg.start, windowStart);
            total += d;
            if (seg.carrSoln === 2) fixed += d;
        }
        for (let i = 1; i < history.length; i++) {
            if (history[i - 1].carrSoln === 2 && history[i].carrSoln !== 2) losses++;
        }
        const current = history.length ? history[history.length - 1] : null;
        let since: number | null = null;
        if (current) {
            since = current.time;
            for (let i = history.length - 2; i >= 0 && history[i].carrSoln === current.carrSoln; i--) since = history[i].time;
        }
        return { fixedPct: total > 0 ? (fixed / total) * 100 : null, losses, current, since };
    });

    function pct(t: number): number {
        return Math.max(0, Math.min(100, ((t - windowStart) / FIX_HISTORY_MS) * 100));
    }

    function solName(c: number): string {
        return c === 2 ? 'Fixed' : c === 1 ? 'Float' : 'No RTK';
    }

    function duration(ms: number): string {
        const s = Math.round(ms / 1000);
        if (s < 60) return `${s} s`;
        const m = Math.floor(s / 60);
        return m < 60 ? `${m} min ${s % 60} s` : `${Math.floor(m / 60)} h ${m % 60} min`;
    }
</script>

<div class="fix-panel">
    <div class="uc-panel-header">
        <span class="uc-panel-title">RTK history (30 min)</span>
        <span class="uc-panel-sub">
            {#if stats.current}
                {solName(stats.current.carrSoln)} for {duration(now - (stats.since ?? now))}
            {:else}
                waiting for NAV-PVT…
            {/if}
        </span>
    </div>
    <div class="fix-body">
        <div class="fix-bar">
            {#each segments as seg}
                <div class="fix-seg sol-{seg.carrSoln}"
                     style="left: {pct(seg.start)}%; width: {Math.max(0.3, pct(seg.end) - pct(seg.start))}%"
                     title="{solName(seg.carrSoln)}: {duration(seg.end - seg.start)}"></div>
            {/each}
        </div>
        <div class="fix-axis"><span>-30 min</span><span>-15 min</span><span>now</span></div>
        <div class="fix-stats">
            <span class="fix-stat"><span class="fix-dot sol-2"></span>Fixed {stats.fixedPct === null ? '—' : stats.fixedPct.toFixed(0) + ' %'}</span>
            <span class="fix-stat">Fix losses {stats.losses}</span>
            <span class="fix-legend"><span class="fix-dot sol-1"></span>Float <span class="fix-dot sol-0"></span>No RTK <span class="fix-dot none"></span>no data</span>
        </div>
    </div>
</div>

<style lang="scss">
    .fix-panel {
        background: white;
        border-bottom: 1px solid #ddd;
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

    .fix-body {
        padding: 10px 12px 12px;
    }

    .fix-bar {
        position: relative;
        height: 18px;
        background: #eceff1;
        border-radius: 3px;
        overflow: hidden;
    }

    .fix-seg {
        position: absolute;
        top: 0;
        bottom: 0;
    }

    .sol-2 { background: #00c853; }
    .sol-1 { background: #ffab00; }
    .sol-0 { background: #ff1744; }
    .none { background: #eceff1; border: 1px solid #cfd8dc; }

    .fix-axis {
        display: flex;
        justify-content: space-between;
        font-size: 0.7em;
        color: #888;
        margin-top: 2px;
    }

    .fix-stats {
        display: flex;
        flex-wrap: wrap;
        gap: 12px;
        margin-top: 6px;
        font-size: 0.8em;
        color: #444;
    }

    .fix-stat, .fix-legend {
        display: inline-flex;
        align-items: center;
        gap: 4px;
    }

    .fix-legend {
        margin-left: auto;
        color: #777;
    }

    .fix-dot {
        display: inline-block;
        width: 10px;
        height: 10px;
        border-radius: 2px;
    }
</style>
