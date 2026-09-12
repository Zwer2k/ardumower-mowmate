import { type Readable, writable } from "svelte/store"

export enum FirmwareUploadStatus {
  clear = 0,
  fileSelected,
  uploading,
  expectReboot,
  error,
  success,
}

export enum FirmwareFlashStatus {
  clear = 0,
  error,
  success,
}

export enum FirmwareUploadType {
  modem = 'modem',
  mower = 'mower'
}

export class FirmwareUploader {
  private _req: XMLHttpRequest | null = null;
  private _file: null | File = null
  private _rebootAwaiter: null | (() => Promise<void>) = null

  private _status = writable<FirmwareUploadStatus>(FirmwareUploadStatus.clear)
  public get status(): Readable<FirmwareUploadStatus> { return this._status }

  private _progress = writable<number>(0)
  public get progress(): Readable<number> { return this._progress }

  private _githubDownloadProgress = writable<number>(0)
  public get githubDownloadProgress(): Readable<number> { return this._githubDownloadProgress }

  private _githubFlashProgress = writable<number>(0)
  public get githubFlashProgress(): Readable<number> { return this._githubFlashProgress }

  private _githubBuffered = writable<boolean>(false)
  public get githubBuffered(): Readable<boolean> { return this._githubBuffered }

  private _error: null | string = null
  public get error(): null | string { return this._error }

  public set file(f: null | File) {
    this._file = f
    const s = f !== null
      ? FirmwareUploadStatus.fileSelected
      : FirmwareUploadStatus.clear
    this._status.set(s)
  }

  public async upload(uploadType: FirmwareUploadType) {
    if (this._file == null) {
      this._status.set(FirmwareUploadStatus.error);
      return
    }
    const data = uploadRequest(this._file)

    this._progress.set(0)
    this._status.set(FirmwareUploadStatus.uploading)

    this._rebootAwaiter = await makeRebootAwaiter()

    this._req = new XMLHttpRequest();
    this._req.open('POST', '/api/modem/ota/upload?type=' + uploadType)
    this._req.upload.addEventListener('progress', this.onUploadProgress.bind(this))
    this._req.addEventListener('load', this.onLoad.bind(this))
    this._req.addEventListener('error', this.onError.bind(this))
    try {
      this._req.send(data)
    } catch (err: any) {
      this._error = `Upload failed (${err.message})`
      this._status.set(FirmwareUploadStatus.error)
    }
  }

  public async installGithubRelease(version: string) {
    this._error = null;
    this._progress.set(0);
    this._githubDownloadProgress.set(0);
    this._githubFlashProgress.set(0);
    this._githubBuffered.set(false);
    this._status.set(FirmwareUploadStatus.uploading);

    try {
      const before = await getModemInfo();
      this._githubBuffered.set(before.firmware_target === "esp32-s3");
      const response = await fetch(`/api/modem/ota/github?version=${encodeURIComponent(version)}`, {
        method: 'POST',
      });
      const result = await response.json();
      if (!response.ok || !result.success) {
        throw new Error(result.error || result.result || `HTTP ${response.status}`);
      }

      await waitForGithubUpdate(
        before,
        5 * 60 * 1000,
        (status) => {
          const downloadProgress = percentage(status.downloadProgress, status.downloadTotal);
          const flashProgress = status.success
            ? 100
            : Math.min(99, percentage(status.flashProgress, status.flashTotal));
          this._githubBuffered.set(status.buffered);
          this._githubDownloadProgress.set(downloadProgress);
          this._githubFlashProgress.set(flashProgress);
          this._progress.set(status.buffered ? downloadProgress : flashProgress);
        },
        () => this._status.set(FirmwareUploadStatus.expectReboot),
      );
      this._githubDownloadProgress.set(100);
      this._githubFlashProgress.set(100);
      this._status.set(FirmwareUploadStatus.success);
    } catch (error) {
      this._error = error instanceof Error ? error.message : String(error);
      this._status.set(FirmwareUploadStatus.error);
    }
  }

  private onUploadProgress(e: ProgressEvent<XMLHttpRequestEventTarget>) {
    const progress = e.loaded / e.total * 100
    this._progress.set(progress)
  }

  private onLoad(_: ProgressEvent<XMLHttpRequestEventTarget>) {
    if (this._req == null) 
      return;

    if (this._req.status >= 400) this.onBadResponseStatus()
    else this.onGoodResponseStatus()
  }

  private onError(e: ProgressEvent<XMLHttpRequestEventTarget>) {
    this._error = `Upload request failed`
    this._status.set(FirmwareUploadStatus.error)
  }

  private async onGoodResponseStatus() {
    if (this._req == null || this._rebootAwaiter == null) 
      return;

    try {
      const res: FirmwareUploadResponse = JSON.parse(this._req.responseText)
      if (!res.success) {
        this._error = res.result
        this._status.set(FirmwareUploadStatus.error)
        return
      }

      // For mower uploads, "flash_file" means the file was uploaded and flashing starts
      // For modem uploads, we expect reboot after successful upload
      if (res.result === "flash_file") {
        this._status.set(FirmwareUploadStatus.success) // Indicate upload completed, flashing will start
        return
      }

      this._status.set(FirmwareUploadStatus.expectReboot)
      await this._rebootAwaiter()
      this._status.set(FirmwareUploadStatus.success)
    } catch (err: any) {
      this._error = err.message
      this._status.set(FirmwareUploadStatus.error)
    }
  }

  private onBadResponseStatus() {
    if (this._req == null) 
      return;

    this._error = `bad response result: ${this._req.status}`
    this._status.set(FirmwareUploadStatus.error)
  }
};

interface FirmwareUploadResponse {
  result: string
  success: boolean
  md5?: string
}

const uploadRequest = (file: File): FormData => {
  const result = new FormData()
  result.append('upload', file)
  return result
}

const makeRebootAwaiter = async (timeout: number = 30000): Promise<() => Promise<void>> => {
  const before = await getModemInfo()

  const isAfter = (i: ApiModemInfoResponse): boolean => i.uptime < before.uptime

  return async (): Promise<void> => {
    const limit: number = millis() + timeout

    const isTimeout = (): boolean => millis() > limit

    while (!isTimeout()) {
      try {
        const now = await getModemInfo(2000)
        if (isAfter(now)) return
      } catch (_) {}

      await delay(500)
    }
    throw new Error('timeout waiting for reboot')
  }
}

export const getModemInfo = async (timeout: number = 5000): Promise<ApiModemInfoResponse> => {
  const controller = new AbortController();
  const id = setTimeout(() => controller.abort(), timeout);
  const res = await fetch('/api/modem/info', {
    signal: controller.signal,
  })
  clearTimeout(id)
  if (res.status !== 200) throw new Error(`bad response status: ${res.statusText}`)

  return await res.json()
}

const millis = (): number => new Date().getTime()

const delay = async (ms: number) => new Promise(resolve => setTimeout(resolve, ms))

interface GithubUpdateStatus {
  active: boolean;
  success: boolean;
  buffered: boolean;
  downloadProgress: number;
  downloadTotal: number;
  flashProgress: number;
  flashTotal: number;
  error?: string;
}

const percentage = (progress: number, total: number): number =>
  total > 0 ? Math.min(100, progress / total * 100) : 0;

const waitForGithubUpdate = async (
  before: ApiModemInfoResponse,
  timeout: number,
  onProgress: (status: GithubUpdateStatus) => void,
  onExpectReboot: () => void,
): Promise<void> => {
  const limit = millis() + timeout;
  let updateSucceeded = false;

  while (millis() <= limit) {
    if (!updateSucceeded) {
      try {
        const response = await fetch('/api/modem/ota/github/status');
        if (response.ok) {
          const status = await response.json() as GithubUpdateStatus;
          if (status.error) throw new Error(status.error);
          onProgress(status);
          if (status.success) {
            updateSucceeded = true;
            onExpectReboot();
          }
        }
      } catch (error) {
        if (error instanceof Error && !error.message.includes('fetch')) throw error;
      }
    }

    if (updateSucceeded) {
      try {
        const now = await getModemInfo(2000);
        if (now.uptime < before.uptime) return;
      } catch (_) {}
    }
    await delay(500);
  }
  throw new Error(updateSucceeded
    ? 'timeout waiting for modem restart'
    : 'timeout waiting for firmware update');
}

export interface ApiModemInfoResponse {
  git_hash: string
  git_time: string
  git_tag: string
  build_time: string
  uptime: number
  terminal_available?: boolean;
  firmware_target: "esp32" | "esp32-s3";
}
