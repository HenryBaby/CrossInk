> **This is a personal fork of [CrossInk](https://github.com/uxjulia/CrossInk), which is itself a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).**
>
> This fork aims to keep CrossInk intact while adding practical workflow improvements. It includes docker-based firmware builds for hosts that should not run a full local PlatformIO setup. See [Docker Build Instructions](./docs/contributing/docker-build.md) on how to build it. Primarily this will only include stuff tailored to my specific organizational quirks.
>
> For any other CrossInk-related documentation please refer to their repo, [CrossInk](https://github.com/uxjulia/CrossInk).

## What's different in this fork

- OPDS organization:
  - Per-server download folders with optional author subfolders.
  - Finished books keep the same author-folder organization when moved to the Read folder.
