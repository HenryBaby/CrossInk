#!/usr/bin/env bash
set -euo pipefail

project_dir="${PROJECT_DIR:-/workspace}"
output_dir="${OUTPUT_DIR:-/output}"

cd "$project_dir"

if [[ ! -f platformio.ini ]]; then
  echo "platformio.ini not found in $project_dir" >&2
  exit 1
fi

git config --global --add safe.directory "$project_dir"

# The current PlatformIO configuration links libraries from freeink-sdk.
if [[ ! -d freeink-sdk/libs ]]; then
  git submodule update --init --recursive
fi

mkdir -p "$output_dir"

if [[ -d .pio/build ]]; then
  find .pio/build -maxdepth 2 -type f -name 'firmware-*.bin' -delete
fi
find "$output_dir" -maxdepth 1 -type f -name 'firmware-*.bin' -delete

if [[ "$#" -eq 0 ]]; then
  pio run
else
  pio run "$@"
fi

mapfile -t artifacts < <(find .pio/build -maxdepth 2 -type f -name 'firmware-*.bin' | sort)
if [[ "${#artifacts[@]}" -eq 0 ]]; then
  echo "No firmware-*.bin artifacts found under .pio/build" >&2
  exit 1
fi

cp -f "${artifacts[@]}" "$output_dir"/

if [[ -n "${HOST_UID:-}" && -n "${HOST_GID:-}" ]]; then
  chown "${HOST_UID}:${HOST_GID}" "$output_dir"/firmware-*.bin
fi

echo "Firmware artifacts copied to $output_dir:"
find "$output_dir" -maxdepth 1 -type f -name 'firmware-*.bin' -printf '  %f\n' | sort
