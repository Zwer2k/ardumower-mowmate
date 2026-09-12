export type FirmwareTarget = "esp32" | "esp32-s3";

export interface GitHubReleaseAsset {
  name: string;
  browser_download_url: string;
  size: number;
}

export interface GitHubRelease {
  tag_name: string;
  name: string;
  html_url: string;
  published_at: string;
  draft: boolean;
  prerelease: boolean;
  assets: GitHubReleaseAsset[];
}

export interface FirmwareRelease {
  version: string;
  name: string;
  publishedAt: string;
  releaseUrl: string;
  asset: GitHubReleaseAsset;
}

const RELEASES_URL = "https://api.github.com/repos/Zwer2k/ardumower-mowmate/releases?per_page=20";
const STABLE_VERSION = /^v?(\d+)\.(\d+)\.(\d+)$/;

const parseVersion = (version: string): [number, number, number] | null => {
  const match = STABLE_VERSION.exec(version.trim());
  return match ? [Number(match[1]), Number(match[2]), Number(match[3])] : null;
};

export const compareVersions = (left: string, right: string): number => {
  const leftVersion = parseVersion(left);
  const rightVersion = parseVersion(right);
  if (!leftVersion || !rightVersion) return 0;

  for (let index = 0; index < leftVersion.length; index++) {
    const difference = leftVersion[index] - rightVersion[index];
    if (difference !== 0) return difference;
  }
  return 0;
};

export const firmwareAssetName = (target: FirmwareTarget): string =>
  `${target}-firmware.bin`;

export const selectFirmwareReleases = (
  releases: GitHubRelease[],
  target: FirmwareTarget,
): FirmwareRelease[] => releases
  .filter((release) => !release.draft && !release.prerelease && parseVersion(release.tag_name))
  .map((release) => {
    const asset = release.assets.find((candidate) => candidate.name === firmwareAssetName(target));
    if (!asset) return null;
    return {
      version: release.tag_name,
      name: release.name || release.tag_name,
      publishedAt: release.published_at,
      releaseUrl: release.html_url,
      asset,
    };
  })
  .filter((release): release is FirmwareRelease => release !== null)
  .sort((left, right) => compareVersions(right.version, left.version));

export const hasFirmwareUpdate = (
  currentVersion: string,
  releases: FirmwareRelease[],
): boolean => parseVersion(currentVersion) !== null
  && releases.length > 0
  && compareVersions(releases[0].version, currentVersion) > 0;

export const fetchFirmwareReleases = async (
  target: FirmwareTarget,
  fetcher: typeof fetch = fetch,
): Promise<FirmwareRelease[]> => {
  const response = await fetcher(RELEASES_URL, {
    headers: { Accept: "application/vnd.github+json" },
  });
  if (!response.ok) throw new Error(`GitHub Releases: ${response.status} ${response.statusText}`);
  return selectFirmwareReleases(await response.json() as GitHubRelease[], target);
};

export const downloadFirmware = async (
  release: FirmwareRelease,
  onProgress: (percent: number) => void,
  fetcher: typeof fetch = fetch,
): Promise<File> => {
  const response = await fetcher(release.asset.browser_download_url);
  if (!response.ok) throw new Error(`Firmware-Download: ${response.status} ${response.statusText}`);

  const reader = response.body?.getReader();
  if (!reader) {
    const blob = await response.blob();
    onProgress(100);
    return new File([blob], release.asset.name, { type: "application/octet-stream" });
  }

  const parts: Blob[] = [];
  let received = 0;
  const total = Number(response.headers.get("content-length")) || release.asset.size;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    const chunk = new Uint8Array(value.byteLength);
    chunk.set(value);
    parts.push(new Blob([chunk.buffer]));
    received += value.length;
    if (total > 0) onProgress(Math.min(100, received / total * 100));
  }
  onProgress(100);
  return new File(parts, release.asset.name, { type: "application/octet-stream" });
};
