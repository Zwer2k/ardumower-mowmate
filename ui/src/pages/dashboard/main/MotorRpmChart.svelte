<script lang="ts">
    import type { MotorRpmSample } from '../../../stores/motorRpm';

    interface Props {
        samples: MotorRpmSample[];
        intervalS: number;
    }

    let { samples, intervalS }: Props = $props();

    const WIDTH = 420;
    const PANEL_H = 110;
    const PAD_L = 36;
    const PAD_R = 8;
    const PAD_T = 8;
    const PAD_B = 18;
    const GRAPH_W = WIDTH - PAD_L - PAD_R;
    const GRAPH_H = PANEL_H - PAD_T - PAD_B;

    // One colour per motor, shared by both panels: the same motor keeps its identity
    // whether you look at its speed or its current.
    const MOTORS = [
        { rpm: 'mow' as const, amps: 'ampsMow' as const, label: 'Mäher', color: '#d32f2f' },
        { rpm: 'left' as const, amps: 'ampsLeft' as const, label: 'Links', color: '#1565c0' },
        { rpm: 'right' as const, amps: 'ampsRight' as const, label: 'Rechts', color: '#2e7d32' },
    ];

    let data = $derived(samples.length >= 2 ? samples : []);
    let tMin = $derived(data.length ? data[0].t : 0);
    let tMax = $derived(data.length ? data[data.length - 1].t : 1);

    function peak(keys: (keyof MotorRpmSample)[]): number {
        let max = 0;
        for (const s of data) for (const k of keys) max = Math.max(max, s[k] as number);
        return max > 0 ? max : 1;
    }

    // Both panels start at zero so a standing motor reads as zero instead of filling the
    // panel with noise around its own mean.
    let rpmMax = $derived(peak(['left', 'right', 'mow']));
    let ampsMax = $derived(peak(['ampsLeft', 'ampsRight', 'ampsMow']));

    function mapX(t: number): number {
        const span = tMax - tMin;
        return PAD_L + (span > 0 ? ((t - tMin) / span) * GRAPH_W : 0);
    }

    function mapY(v: number, max: number): number {
        return PAD_T + GRAPH_H - (v / max) * GRAPH_H;
    }

    function path(key: keyof MotorRpmSample, max: number): string {
        if (!data.length) return '';
        return data
            .map((s, i) => `${i === 0 ? 'M' : 'L'}${mapX(s.t).toFixed(1)},${mapY(s[key] as number, max).toFixed(1)}`)
            .join('');
    }

    function niceStep(raw: number): number {
        if (raw <= 0) return 1;
        const mag = Math.pow(10, Math.floor(Math.log10(raw)));
        const norm = raw / mag;
        return (norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 5 ? 5 : 10) * mag;
    }

    function ticks(max: number): number[] {
        const step = niceStep(max / 3);
        const out: number[] = [];
        for (let v = 0; v <= max + step * 0.001; v += step) out.push(v);
        return out;
    }

    let rpmTicks = $derived(ticks(rpmMax));
    let ampsTicks = $derived(ticks(ampsMax));

    /** elapsed time relative to the newest sample, so the axis reads "how long ago" */
    function agoLabel(t: number): string {
        const d = tMax - t;
        if (d >= 3600) return `-${Math.round(d / 3600)}h`;
        if (d >= 60) return `-${Math.round(d / 60)}m`;
        return d === 0 ? 'jetzt' : `-${Math.round(d)}s`;
    }

    let xTicks = $derived.by(() => {
        if (data.length < 2) return [];
        const n = Math.min(4, data.length - 1);
        return Array.from({ length: n + 1 }, (_, i) => tMin + ((tMax - tMin) * i) / n);
    });

    let spanLabel = $derived.by(() => {
        const d = tMax - tMin;
        if (d >= 3600) return `${(d / 3600).toFixed(1)} h`;
        if (d >= 60) return `${Math.round(d / 60)} min`;
        return `${Math.round(d)} s`;
    });
</script>

{#snippet panel(unit: string, max: number, yTicks: number[], decimals: number, key: 'rpm' | 'amps', showX: boolean)}
    <svg viewBox="0 0 {WIDTH} {PANEL_H}" class="rpm-svg" role="img" aria-label="Motoren: {unit} über die Zeit">
        {#each yTicks as tick}
            {@const y = mapY(tick, max)}
            <line x1={PAD_L} y1={y} x2={WIDTH - PAD_R} y2={y} stroke="#eee" stroke-width="0.5" />
            <text x={PAD_L - 4} y={y + 3} fill="#888" font-size="8" font-family="monospace" text-anchor="end">{tick.toFixed(decimals)}</text>
        {/each}

        <text x={4} y={PAD_T + 6} fill="#666" font-size="8" font-family="monospace">{unit}</text>

        {#if showX}
            {#each xTicks as tick}
                <text x={mapX(tick)} y={PANEL_H - 5} fill="#888" font-size="8" font-family="monospace" text-anchor="middle">{agoLabel(tick)}</text>
            {/each}
        {/if}

        <line x1={PAD_L} y1={PAD_T} x2={PAD_L} y2={PANEL_H - PAD_B} stroke="#ccc" stroke-width="0.5" />
        <line x1={PAD_L} y1={PANEL_H - PAD_B} x2={WIDTH - PAD_R} y2={PANEL_H - PAD_B} stroke="#ccc" stroke-width="0.5" />

        {#each MOTORS as m}
            {@const d = path(key === 'rpm' ? m.rpm : m.amps, max)}
            {#if d}
                <path {d} fill="none" stroke={m.color} stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" />
            {/if}
        {/each}
    </svg>
{/snippet}

<div class="rpm-panel">
    {#if data.length < 2}
        <div class="rpm-empty">Noch keine Aufzeichnung — die erste Messreihe entsteht in den nächsten Minuten.</div>
    {:else}
        {@render panel('U/min', rpmMax, rpmTicks, 0, 'rpm', false)}
        {@render panel('A', ampsMax, ampsTicks, 2, 'amps', true)}

        <div class="rpm-legend">
            {#each MOTORS as m}
                <span class="rpm-legend-item">
                    <span class="rpm-legend-line" style="background: {m.color}"></span>{m.label}
                </span>
            {/each}
            <span class="rpm-meta">{data.length} Punkte · {intervalS}s · {spanLabel}</span>
        </div>
    {/if}
</div>

<style lang="scss">
    .rpm-panel {
        padding: 6px 8px 8px;
    }
    .rpm-svg {
        width: 100%;
        display: block;
    }
    .rpm-empty {
        font-size: 0.75em;
        color: #6f6f6f;
        padding: 12px 4px;
        text-align: center;
    }
    .rpm-legend {
        display: flex;
        flex-wrap: wrap;
        align-items: center;
        gap: 10px;
        font-size: 0.7em;
        color: #525252;
        margin-top: 2px;
    }
    .rpm-legend-item {
        display: inline-flex;
        align-items: center;
        gap: 4px;
    }
    .rpm-legend-line {
        width: 12px;
        height: 2px;
        border-radius: 1px;
    }
    .rpm-meta {
        margin-left: auto;
        font-family: monospace;
        opacity: 0.7;
    }
</style>
