#!/bin/bash
#
# Writes the ESP Web Tools manifests for one firmware channel.
#
# Usage: make-flasher-manifest.sh <channel-dir> <version>
#
# The channel directory holds the release assets as published by ci.yml, ie
# prefixed with the platform name: esp32-bootloader.bin, esp32-s3-firmware.bin
# and so on. Two manifests come out of it:
#
#   manifest.json      standard firmware, ESP32 and ESP32-S3
#   manifest-ps4.json  ESP32 with the PS4 controller instead of BLE
#
# A platform whose files are incomplete is left out of its manifest; a manifest
# without any complete platform is not written, and the web installer reports
# that variant as missing for the channel.

set -euo pipefail

dir="${1:?channel directory required}"
version="${2:?version required}"

# Offsets match the partition tables: the ESP32-S3 keeps its bootloader at 0x0,
# the original ESP32 at 0x1000. Everything else is identical.
esp32_offsets="4096 32768 57344 65536"
esp32_s3_offsets="0 32768 57344 65536"
parts="bootloader partitions boot_app0 firmware"

builds=""

add_build() {
  local prefix="$1" family="$2" offsets="$3"
  local part offset entries=""
  local -a offset_list

  read -r -a offset_list <<< "$offsets"

  local index=0
  for part in $parts; do
    local file="$prefix-$part.bin"
    if [ ! -f "$dir/$file" ]; then
      echo "skipping $family: $dir/$file is missing" >&2
      return 0
    fi
    offset="${offset_list[$index]}"
    entries="$entries        { \"path\": \"$file\", \"offset\": $offset },
"
    index=$((index + 1))
  done

  # Drop the trailing comma of the last part.
  entries="${entries%,
}"

  builds="$builds    {
      \"chipFamily\": \"$family\",
      \"parts\": [
$entries
      ]
    },
"
}

write_manifest() {
  local file="$1" name="$2"

  if [ -z "$builds" ]; then
    echo "no complete platform for $file in $dir, not writing it" >&2
    return 0
  fi

  builds="${builds%,
}"

  # new_install_prompt_erase must stay true. MowMate does not implement Improv
  # Serial, so ESP Web Tools treats every flash as a first install. With the
  # flag set it shows an "Erase device" checkbox that starts unchecked, which
  # keeps the spiffs partition - maps, schedules, settings, map CRC - intact.
  # With the flag false or absent it skips the question and erases the whole
  # chip every time.
  cat > "$dir/$file" <<JSON
{
  "name": "$name",
  "version": "$version",
  "new_install_prompt_erase": true,
  "builds": [
$builds
  ]
}
JSON

  echo "wrote $dir/$file ($version)"
}

# The PS4 firmware uses huge_app.csv, whose images sit at the same offsets.
builds=""
add_build esp32 ESP32 "$esp32_offsets"
add_build esp32-s3 ESP32-S3 "$esp32_s3_offsets"
write_manifest manifest.json "MowMate"

builds=""
add_build esp32-ps4 ESP32 "$esp32_offsets"
write_manifest manifest-ps4.json "MowMate PS4"
