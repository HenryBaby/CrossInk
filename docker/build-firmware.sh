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

# PlatformIO links libraries directly from freeink-sdk. Pulling a new CrossInk
# revision updates the recorded gitlink but does not automatically move an
# existing submodule checkout, which can leave headers and application code on
# incompatible SDK revisions. Synchronize to the exact recorded commit before
# every build. Git refuses the checkout if the submodule has conflicting local
# changes, preserving them instead of silently overwriting them.
git submodule update --init --recursive

mkdir -p "$output_dir"

# Fail before compilation if the bind mount cannot be written by this container user.
write_probe=""
cleanup_write_probe() {
  if [[ -n "$write_probe" ]]; then
    rm -f -- "$write_probe"
  fi
}
trap cleanup_write_probe EXIT

if ! write_probe=$(umask 077; mktemp "$output_dir/.crossink-write-probe.XXXXXX" 2>/dev/null); then
  echo "Output directory is not writable: $output_dir" >&2
  echo 'Linux/macOS: on the host, run: sudo chown -R "$(id -u):$(id -g)" ./output' >&2
  echo "Windows: check Docker Desktop file sharing and folder permissions for the mounted output directory." >&2
  exit 1
fi
rm -f -- "$write_probe"
write_probe=""

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
