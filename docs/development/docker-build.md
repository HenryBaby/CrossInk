---
title: Docker Build
parent: Contributing
nav_order: 2
---

# Docker Build

CrossInk can be built and tested in a Docker container so the host only needs
Docker and Git. The image installs PlatformIO Core, CMake/CTest, and the Linux
SDL2 development packages used by the native simulator.

## Build the image

From the repository root:

```sh
docker build -t crossink-builder .
```

## Build firmware (Linux, macOS, or Git Bash)

The default command runs `pio run`, which builds the release environments
listed in `platformio.ini` (`default` for X3/X4):

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
crossink-builder -e default
```

The entrypoint also has explicit container-only test modes. Existing
PlatformIO arguments remain firmware builds for backwards compatibility:

```sh
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/workspace" \
  -v crossink-platformio:/platformio crossink-builder unit-tests
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/workspace" \
  -v crossink-platformio:/platformio crossink-builder simulator
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/workspace" \
  -v crossink-platformio:/platformio crossink-builder sticky-simulator --page-turns 10
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/workspace" \
  -v crossink-platformio:/platformio crossink-builder x4-pro-simulator --theme lyra-carousel
```

Simulator options are passed to the smoke runner; `--no-build` reuses the
matching binary in mounted `.pio/`. Pass CTest selectors after `unit-tests --`,
for example `unit-tests -- -R quick_lock`. The checkout is mounted at
`/workspace`; PlatformIO packages use the named `crossink-platformio` cache,
while `build/test` and `.pio/` persist in the checkout. The smoke runner uses a
temporary isolated `fs_` and does not delete unrelated files or `output/`.

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
  crossink-builder -e default
```

On Windows, Docker Desktop applies ownership for bind-mounted files. On a
Linux container host running as root, `HOST_UID` and `HOST_GID` can instead be
set to chown copied artifacts.

### Troubleshooting output permissions

The builder checks that `/output` is writable before starting PlatformIO. If it
reports `Output directory is not writable`, fix ownership on Linux or macOS
from the host and rerun the command:

```sh
sudo chown -R "$(id -u):$(id -g)" ./output
```

On Windows, verify that Docker Desktop is allowed to share the repository drive
or folder and that your user has permission to write to `output`. The exact
settings depend on the Docker Desktop version and host security policy.

If an earlier run compiled successfully but failed while copying with
`cp ... Permission denied`, the firmware remains under `.pio/build`. After
fixing the output permissions, recover it without rebuilding (replace `default`
with the environment you built):

```sh
cp .pio/build/default/firmware-x3-x4.bin output/
```

Docker builds do not replace hardware validation. After flashing an image,
open an EPUB, turn pages, open Reader Options, toggle a setting, and check
serial logs for allocation or filesystem errors.
