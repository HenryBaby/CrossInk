---
title: Docker Build
parent: Contributing
nav_order: 2
---

# Docker Build

CrossInk can be built in a Docker container so the host only needs Docker and
Git. The container installs PlatformIO and uses the same `platformio.ini`
environments as a local build.

## Build The Image

From the repository root:

```sh
docker build -t crossink-builder .
```

## Build Release Firmware

The default container command runs plain `pio run`, which builds the release
variants listed in `platformio.ini`: `teensy`, `tiny`, `xlarge`, and
`no_emoji`.

```sh
mkdir -p output
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$PWD:/workspace" \
  -v crossink-platformio:/platformio \
  -v "$PWD/output:/output" \
  crossink-builder
```

The container copies the generated `firmware-*.bin` files into `./output/`.
Artifact names are produced by `scripts/rename_firmware.py`, matching normal
PlatformIO builds.

## Build One Environment

Pass normal PlatformIO arguments after the image name:

```sh
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$PWD:/workspace" \
  -v crossink-platformio:/platformio \
  -v "$PWD/output:/output" \
  crossink-builder -e tiny
```

## Notes

- The container initializes `open-x4-sdk` with `git submodule update --init
  --recursive` if the SDK is missing.
- `--user "$(id -u):$(id -g)"` keeps `.pio/` and copied firmware artifacts
  owned by the host user on Linux.
- `crossink-platformio` is a named Docker volume for PlatformIO packages and
  toolchains so they do not need to be downloaded every run.
- If you run the container as root instead, you can set `HOST_UID` and
  `HOST_GID` to make copied firmware artifacts host-owned.
- Docker builds do not replace hardware validation. After flashing a produced
  firmware image, open an EPUB, turn pages, open Reader Options, toggle one
  setting, back out, and watch serial logs for allocation or filesystem errors.
