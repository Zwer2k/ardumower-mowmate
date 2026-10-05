# Web installer

MowMate ships a browser-based firmware installer built on
[ESP Web Tools](https://github.com/esphome/esp-web-tools). It is published to
GitHub Pages at <https://zwer2k.github.io/ardumower-mowmate/> and lets users
flash a board over Web Serial without installing PlatformIO, esptool or any
USB tooling beyond the serial driver.

## How it is put together

| Piece | Location |
| --- | --- |
| Page | [`web-flasher/index.html`](../web-flasher/index.html) |
| Manifest generator | [`ci/bin/make-flasher-manifest.sh`](../ci/bin/make-flasher-manifest.sh) |
| Deployment | the `pages` job in [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) |

The `pages` job does not use the artifacts of its own build. It pulls the
binaries back down from the published releases with `gh release download`:
the newest non-prerelease tag becomes the `stable/` channel and the rolling
`nightly` tag becomes `nightly/`. Both channels therefore stay flashable no
matter which event triggered the deployment, and the page itself has no build
step.

Each channel directory gets a `manifest.json` listing one build per chip
family. ESP Web Tools reads the chip ID off the connected board and picks the
matching build, so a single button serves both the ESP32 and the ESP32-S3.
A platform whose binaries are incomplete is omitted from the manifest; if
neither is complete, no manifest is written and the page reports the channel as
empty instead of failing mid-flash.

## Flash offsets

The manifest flashes four images per board. These are also the offsets to use
with `esptool` or Espressif's Flash Download Tool.

| Offset | ESP32 (4 MB, `min_spiffs.csv`) | ESP32-S3 (16 MB, `default_16MB.csv`) |
| --- | --- | --- |
| `0x0` | — | `bootloader.bin` |
| `0x1000` | `bootloader.bin` | — |
| `0x8000` | `partitions.bin` | `partitions.bin` |
| `0xe000` | `boot_app0.bin` | `boot_app0.bin` |
| `0x10000` | `firmware.bin` | `firmware.bin` |

The ESP32-S3 keeps its second-stage bootloader at `0x0`; only the original
ESP32 uses `0x1000`. `boot_app0.bin` comes from the Arduino framework package
and resets `otadata` so the freshly written `app0` partition is the one that
boots. The CI locates it under `~/.platformio/packages` and publishes it as a
release asset alongside the other images.

None of the four images touches the `spiffs` partition, which is where maps,
mowing schedules, settings and the persisted map CRC live. Whether that data
survives therefore comes down to the erase flag.

## The erase flag

The manifest sets `new_install_prompt_erase` to `true`, and that value matters.
MowMate does not implement Improv Serial, so ESP Web Tools has no way to
recognise an already-installed MowMate and treats every flash as a first
install. In that path the flag decides between two very different behaviours:

| `new_install_prompt_erase` | What happens |
| --- | --- |
| `true` | A dialog asks "Erase device?" with the checkbox **unchecked**. Confirming without ticking it writes the firmware and leaves `spiffs` alone. |
| `false` or absent | No question is asked and `eraseFlash()` runs before every write, wiping maps, schedules and WiFi credentials. |

Reading the flag as "do not erase" is the obvious mistake and it is backwards:
it means "do not ask". Users who do want a clean board tick the checkbox.

Writes go out with `flashSize`, `flashMode` and `flashFreq` set to `keep`, so
ESP Web Tools does not rewrite the flash header the way Espressif's Flash
Download Tool does with *DoNotChgBin* disabled. The `qio_opi` memory type the
ESP32-S3 bootloader is built with therefore stays intact.

## Serial type

Builds in the manifest deliberately carry no `serialType`. ESP Web Tools picks
a build by `chipFamily` plus `serialType`, falling back to a build that
specifies no `serialType` at all, so omitting it makes each build serve both
the USB-UART bridge and the ESP32-S3's native USB-Serial/JTAG port. The
`chipFamily` strings must match esptool-js's `CHIP_NAME` exactly: `ESP32` and
`ESP32-S3`.

## Constraints

- **Chromium only.** Web Serial exists in Chrome, Edge and Opera on the
  desktop. Firefox and Safari do not implement it, and neither do mobile
  browsers. The page says so through the `unsupported` slot.
- **HTTPS required.** GitHub Pages provides this; a local `file://` or plain
  HTTP copy will not work. For local development, serve the directory over
  `http://localhost`, which counts as a secure context.
- **One port at a time.** A running serial monitor holds the device and the
  browser cannot open it.
- **ESP32-S3 DevKit:** use the UART bridge port, not the native USB port. The
  firmware is built without `ARDUINO_USB_CDC_ON_BOOT`, so the native port does
  not expose a serial console after boot.

## Repository setup

GitHub Pages must be switched to the Actions source once, under
*Settings → Pages → Build and deployment → Source: GitHub Actions*. Without
that, the `deploy-pages` step fails with a "Pages site not found" error.

## Testing locally

`manifest.json` paths are relative to the manifest, so a channel directory
filled with release assets works unchanged:

```sh
mkdir -p site/stable
cp web-flasher/index.html site/
gh release download v1.5.0 --dir site/stable \
  --pattern 'esp32-*.bin' --pattern 'esp32-s3-*.bin'
ci/bin/make-flasher-manifest.sh site/stable v1.5.0
python3 -m http.server --directory site 8000
```

Then open <http://localhost:8000/>.
