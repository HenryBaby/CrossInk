---
title: Docker Build
parent: Contributing
nav_order: 2
---

# Docker Build

CrossInk can be built in a Docker container so the host only needs Docker and
Git. The image installs the PlatformIO Core version used by CI and uses the
same `platformio.ini` environments as a local build.

## Build the image

From the repository root:

```sh
docker build -t crossink-builder .
```

## Build firmware (Linux, macOS, or Git Bash)

The default command runs `pio run`, which builds the release environments
listed in `platformio.ini` (`tiny` and `xlarge`):

```sh
mkdir -p output
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$PWD:/workspace" \
  -v crossink-platformio:/platformio \
  -v "$PWD/output:/output" \
crossink-builder
```

Generated `firmware-*.bin` files are copied into `./output/`. To build one
environment, pass normal PlatformIO arguments after the image name:

```sh
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$PWD:/workspace" \
  -v crossink-platformio:/platformio \
  -v "$PWD/output:/output" \
crossink-builder -e tiny
```

The script initializes the `freeink-sdk` submodule when it is missing. The
named PlatformIO volume keeps downloaded packages and toolchains between runs.
On Linux and macOS, `--user` keeps mounted files owned by the host user. Git
Bash on Windows can use these commands, but Docker Desktop's bind-mount
ownership is managed by Docker Desktop rather than by Unix UID/GID mapping.

## Build firmware (PowerShell)

From the repository root, create the output directory and run the container
without the POSIX-only `--user`/`id` options:

```powershell
New-Item -ItemType Directory -Force output | Out-Null
docker run --rm `
  -v "${PWD}:/workspace" `
  -v crossink-platformio:/platformio `
  -v "${PWD}/output:/output" `
  crossink-builder
```

To build one environment in PowerShell:

```powershell
docker run --rm `
  -v "${PWD}:/workspace" `
  -v crossink-platformio:/platformio `
  -v "${PWD}/output:/output" `
  crossink-builder -e tiny
```

On Windows, Docker Desktop applies ownership for bind-mounted files. On a
Linux container host running as root, `HOST_UID` and `HOST_GID` can instead be
set to chown copied artifacts.

Docker builds do not replace hardware validation. After flashing an image,
open an EPUB, turn pages, open Reader Options, toggle a setting, and check
serial logs for allocation or filesystem errors.
