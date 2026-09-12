import { describe, expect, it } from "vitest";
import {
  compareVersions,
  firmwareAssetName,
  hasFirmwareUpdate,
  selectFirmwareReleases,
  type GitHubRelease,
} from "./github-releases";

const release = (
  tag: string,
  assets: string[],
  options: Partial<GitHubRelease> = {},
): GitHubRelease => ({
  tag_name: tag,
  name: tag,
  html_url: `https://example.test/${tag}`,
  published_at: "2026-01-01T00:00:00Z",
  draft: false,
  prerelease: false,
  assets: assets.map((name) => ({ name, size: 123, browser_download_url: `https://example.test/${name}` })),
  ...options,
});

describe("GitHub firmware releases", () => {
  it("sorts stable semantic versions newest first", () => {
    expect(compareVersions("v1.10.0", "v1.9.9")).toBeGreaterThan(0);
    expect(compareVersions("1.2.0", "v1.2.0")).toBe(0);
  });

  it("selects only stable releases with the exact target asset", () => {
    const releases = selectFirmwareReleases([
      release("v1.9.0", ["esp32-s3-firmware.bin"]),
      release("v1.10.0", ["esp32-s3-firmware.bin"]),
      release("v2.0.0-beta.1", ["esp32-s3-firmware.bin"], { prerelease: true }),
      release("v2.0.0", ["esp32-firmware.bin"]),
    ], "esp32-s3");

    expect(releases.map((item) => item.version)).toEqual(["v1.10.0", "v1.9.0"]);
    expect(releases.every((item) => item.asset.name === firmwareAssetName("esp32-s3"))).toBe(true);
  });

  it("reports an update only when the latest release is newer", () => {
    const releases = selectFirmwareReleases([
      release("v1.3.0", ["esp32-s3-firmware.bin"]),
      release("v1.2.0", ["esp32-s3-firmware.bin"]),
    ], "esp32-s3");

    expect(hasFirmwareUpdate("v1.2.0", releases)).toBe(true);
    expect(hasFirmwareUpdate("v1.3.0", releases)).toBe(false);
    expect(hasFirmwareUpdate("v1.4.0", releases)).toBe(false);
    expect(hasFirmwareUpdate("local-build", releases)).toBe(false);
  });
});
