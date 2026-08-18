# CrossPoint Reader — Durable Context

Keep this file focused on repo-specific gotchas that are worth reusing in future sessions.

## FreeInk SDK

Refer to https://freeink.org/llms.txt for guidance.

## Simulator

- Sticky is outside this downstream fork's product and validation scope. Do not spend build or test time on Sticky targets unless the user explicitly brings it back into scope.
- Simulator patches belong in the adjacent `crossink-simulator` repo.
- The valid local simulator env in this repo is `simulator`, and `pio run -e simulator` currently builds cleanly.
- The simulator `PNGdec` stub in `crossink-simulator/src/PNGdec.h` needs to mirror the real API shape used by app code, including `hasAlpha()` and `getTransparentColor()`, even though decode still fails intentionally.
- Known simulator limits:
  - No image rendering: `platformio.ini` ignores `hal`, `PNGdec`, and `JPEGDEC`, so image decoders are intentionally absent.
  - JPEGDEC stub always fails; `JPEGDEC fallback: open failed (err=-1)` is expected in simulator.
  - `esp_deep_sleep_start()` is a no-op in simulator.
  - `HalStorage` uses POSIX file access under `./fs_` and allows multiple readers, unlike real hardware.

## Real Hardware / Storage

- SdFat on hardware allows only one open reader per file path at a time. If a fallback needs to reopen the same file, close the first handle before reopening.

## Rendering / Reader Pipeline

- `lib/Epub/Epub/Page.cpp`: images must render only in `GfxRenderer::BW`; grayscale passes are text anti-aliasing passes only.
- Kindle EPUBs may contain paired high-res and old-Kindle fallback images. `ChapterHtmlSlimParser` should skip `<img>` nodes with `data-AmznRemoved-M8` to avoid duplicate stacked images.
- After image/layout pipeline changes that affect cached EPUB output, clear the affected `.crosspoint/epub_<hash>/` cache if behavior looks stale.

## UI Consistency

- Use FreeInkUI SDK components and input routing for list-style screens where possible. Row rendering, touch targets,
  hit testing, and pagination should share the same FreeInkUI list configuration instead of custom touch scaling.

## Heap Baselines (X4 hardware, SD card font)

- A normal resume-into-partial reading session runs at ~85-90KB free / ~49KB maxAlloc by
  the first watermark crossing (Epub metadata + x-locations + resident glyph caches).
  Do not read mid-range heap numbers as session degradation without checking the scenario.
- SD-font section builds cost ~38-50KB at cold start; the 4-style advance-table prewarm
  (~30KB incl. 16KB contiguous scratch) dominates and is skipped below 80KB free.

## Misc Repo Gotchas

- Run all builds, tests, static analysis, formatting checks, and simulator execution inside project-specific Docker containers. Do not install or run development tooling directly on the homelab host or in the persistent code-server control-plane container.
- POSIX TZ signs are inverted from ISO 8601 in `TimeStore::applyTimezone()`: `"UTC-1"` means UTC+1.
- `LyraTheme::drawHeader()` does not call `BaseTheme::drawHeader()`, so header changes in the base theme must be duplicated in Lyra if needed.
- Preserve the complete opening `[!CAUTION]` block in `README.md` verbatim during upstream syncs, README rewrites, and documentation updates unless the user explicitly asks to change it. It identifies this as a personal, fully vibe-coded downstream build and warns that there could be dragons.
- The README section `Changes maintained by this downstream fork` is the canonical persistent footer for published release notes. `scripts/generate_release_notes.py` appends it automatically; update the README section instead of duplicating new wording in the release workflow.
