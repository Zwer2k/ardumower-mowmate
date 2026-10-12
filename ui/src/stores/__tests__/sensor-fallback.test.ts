import { describe, it, expect } from "vitest";
import { get } from "svelte/store";
import { socketStore } from "../socket";
import { sensorFallbackStore, sonarRecentlyTriggered, SONAR_RECENT_MS } from "../sensor-fallback";

function setSonarCount(n: number) {
  socketStore.update((s) => ({ ...s, stats: { ...(s.stats ?? {}), sonar_triggered: n } as any }));
}

describe("sensor fallback (Sunray without AT+S3)", () => {
  it("treats the first counter value as baseline, an increase as trigger, a reset not", () => {
    setSonarCount(5);
    expect(get(sensorFallbackStore).sonarCount).toBe(5);
    expect(get(sensorFallbackStore).sonarTriggeredAt).toBeNull();

    setSonarCount(6);
    const at = get(sensorFallbackStore).sonarTriggeredAt;
    expect(at).not.toBeNull();
    expect(sonarRecentlyTriggered(get(sensorFallbackStore), at! + 1000)).toBe(true);
    expect(sonarRecentlyTriggered(get(sensorFallbackStore), at! + SONAR_RECENT_MS + 1)).toBe(false);

    setSonarCount(0); // statistics cleared
    expect(get(sensorFallbackStore).sonarCount).toBe(0);
    expect(get(sensorFallbackStore).sonarTriggeredAt).toBe(at);
  });
});
