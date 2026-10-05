import { writable, get } from 'svelte/store';
import { browser } from '$app/environment';

export interface MotorRpmSample {
  t: number;      // seconds since mower/modem boot
  left: number;
  right: number;
  mow: number;
  ampsLeft: number;
  ampsRight: number;
  ampsMow: number;
}

export interface MotorRpmState {
  left: number;
  right: number;
  mow: number;
  pwmMow: number;
  ampsLeft: number;
  ampsRight: number;
  ampsMow: number;
  uptime: number;
  /** current spacing of the recorded series; grows as the run gets longer */
  intervalS: number;
  samples: MotorRpmSample[];
  loaded: boolean;
}

const empty: MotorRpmState = {
  left: 0, right: 0, mow: 0, pwmMow: 0, ampsLeft: 0, ampsRight: 0, ampsMow: 0,
  uptime: 0, intervalS: 5, samples: [], loaded: false,
};

function createMotorRpmStore() {
  const { subscribe, update } = writable<MotorRpmState>({ ...empty });

  let timer: ReturnType<typeof setInterval> | null = null;
  let wantSamples = false;
  let inFlight = false;

  async function load(withSamples: boolean) {
    // A slow or stalled request must not pile up behind the interval.
    if (inFlight) return;
    inFlight = true;
    try {
      const url = `/api/robot/motor_rpm${withSamples ? '' : '?samples=0'}`;
      const res = await fetch(url);
      if (!res.ok) {
        console.warn(`[motorRpm] ${url} -> HTTP ${res.status}`);
        return;
      }
      // A modem without this endpoint falls through to the SPA fallback and answers with
      // index.html at status 200. Without this check that lands in the catch below and looks
      // exactly like "no data", which is the one thing it must not look like.
      const type = res.headers.get('content-type') ?? '';
      if (!type.includes('application/json')) {
        console.warn(`[motorRpm] ${url} answered ${type || 'without content-type'} - firmware without /api/robot/motor_rpm?`);
        return;
      }
      const j = await res.json();
      // With MOW_TOGGLE_DIR the mower alternates the mow direction on every start, so rpm and
      // pwm arrive negative on every other run. The sign is direction, not magnitude - charting
      // it would push the curve below a zero-based axis and out of sight.
      update((s) => ({
        left: Math.abs(j.left ?? 0),
        right: Math.abs(j.right ?? 0),
        mow: Math.abs(j.mow ?? 0),
        pwmMow: Math.abs(j.pwm_mow ?? 0),
        ampsLeft: j.amps_left ?? 0,
        ampsRight: j.amps_right ?? 0,
        ampsMow: j.amps_mow ?? 0,
        uptime: j.uptime_s ?? 0,
        intervalS: j.interval_s ?? 5,
        // keep the previous series when this response carried none.
        // currents arrive in 10 mA steps and are scaled back to amps here
        samples: Array.isArray(j.samples)
          ? j.samples.map((r: number[]) => ({
              t: r[0], left: Math.abs(r[1]), right: Math.abs(r[2]), mow: Math.abs(r[3]),
              ampsLeft: Math.abs(r[4] ?? 0) / 100, ampsRight: Math.abs(r[5] ?? 0) / 100, ampsMow: Math.abs(r[6] ?? 0) / 100,
            }))
          : s.samples,
        loaded: true,
      }));
    } catch (e) {
      // transient network errors are normal while the modem reconnects, but staying silent
      // made a missing endpoint indistinguishable from a standing motor
      console.warn('[motorRpm] request failed', e);
    } finally {
      inFlight = false;
    }
  }

  function tick() {
    load(wantSamples);
  }

  return {
    subscribe,

    /** start polling; call once while the dashboard is mounted */
    start() {
      if (!browser || timer) return;
      tick();
      timer = setInterval(tick, 5000);
    },

    stop() {
      if (timer) clearInterval(timer);
      timer = null;
    },

    /** the chart asks for the recorded series; the tile alone does not need it */
    setSamplesWanted(wanted: boolean) {
      if (wantSamples === wanted) return;
      wantSamples = wanted;
      if (wanted) load(true);
    },

    reload() {
      load(wantSamples);
    },
  };
}

export const MotorRpmStore = createMotorRpmStore();
