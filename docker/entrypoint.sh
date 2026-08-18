#!/usr/bin/env bash
set -euo pipefail

project_dir="${PROJECT_DIR:-/workspace}"

usage() {
  cat <<'EOF'
Usage:
  crossink [PlatformIO arguments...]             Build firmware (backwards compatible)
  crossink unit-tests [CTest arguments...]       Build and run host unit tests
  crossink simulator [smoke options...]          Run simulator smoke test
  crossink sticky-simulator [smoke options...]    Run Sticky simulator smoke test
  crossink x4-pro-simulator [smoke options...]   Run X4 Pro simulator smoke test

Simulator options are passed to scripts/run_simulator_smoke_test.py. Use
--no-build to reuse an existing .pio binary, or --help for all options.
EOF
}

run_unit_tests() {
  cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
  cmake --build build/test
  ctest_args=()
  if [[ "${1:-}" == "--" ]]; then
    shift
    ctest_args=("$@")
  elif [[ "$#" -gt 0 ]]; then
    ctest_args=("$@")
  fi
  ctest --test-dir build/test --output-on-failure "${ctest_args[@]}"
}

cd "$project_dir"
if [[ ! -f platformio.ini ]]; then
  echo "platformio.ini not found in $project_dir" >&2
  exit 1
fi

# The mounted checkout and its gitlink may be owned by the host user. Mark both
# paths safe without changing repository contents, then synchronize exact
# recorded submodule revisions before any PlatformIO/CMake operation.
git config --global --add safe.directory "$project_dir"
git config --global --add safe.directory "$project_dir/freeink-sdk"
git submodule update --init --recursive

mode="${1:-firmware}"
if [[ "$#" -gt 0 ]]; then
  shift
fi

case "$mode" in
  help|-h|--help)
    usage
    ;;
  firmware)
    exec /usr/local/bin/build-firmware "$@"
    ;;
  unit-tests|unit_tests)
    run_unit_tests "$@"
    ;;
  simulator|sticky-simulator|x4-pro-simulator)
    exec python scripts/run_simulator_smoke_test.py --env "$mode" "$@"
    ;;
  -*)
    # Existing invocations such as `image -e default` remain firmware builds.
    exec /usr/local/bin/build-firmware "$mode" "$@"
    ;;
  *)
    echo "Unknown mode: $mode" >&2
    usage >&2
    exit 2
    ;;
esac
