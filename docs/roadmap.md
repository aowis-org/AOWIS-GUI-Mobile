# Implementation roadmap

The active application is **AOWIS-GUI-Mobile**: Qt Quick/QML, a working
bgfx OpenGL/EGL underlay on Linux and Android, an instrumented synthetic
network scene, and Docker Android builds. **No geographic survey feature is
implemented yet.** The following milestones describe future work and
acceptance, not already shipped capabilities.

## Immediate priority 1 — GPU scalability and performance

**Harness implemented; first Linux/Android recordings available, full acceptance pending.** The app now
provides deterministic ~1k/~10k/~100k/~1M node+pipe feature presets, visible
tile geometry, bounded GPU cache, adaptive LOD, frame statistics and 15-second
per-frame CSV captures (p50/p95/p99, CPU/GPU time when available, RSS and GPU
vertex payload estimates). Pure C++ tests cover counts, deterministic tile
meshes, selection bounds and percentile calculations. The full million-feature
scene is not allocated or rendered all at once; document visibility/LOD.

**Initial measurements (2026-11-05):** Desktop and Android 15-second CSVs
are available for ~1k/~10k/~100k/~1M presets. Android p95 frame intervals
are approximately 19.6–20.7 ms, desktop approximately 17.0–17.8 ms. The ~1M
runs differ greatly in visible geometry, and one Android run contains a p99
spike near 34.9 ms with tile uploads. Android GPU timings are unavailable and
RSS reports zero; those measurements are not yet meaningful.

**Data collection implemented:** `./benchmark.sh` gathers desktop/Android CSVs and lifecycle logs into Git-ignored `benchmarks/`, summarizes them and automatically includes them in `mobile.zip`. It does not initiate in-app recordings.

**Remaining:** repeat controlled stationary and scripted pan/zoom workloads on
Linux and Android, including cold versus warmed-up scenes, multiple screen
sizes, sustained operation, memory stability and thermal behavior. Store resulting CSV reports and record device specs, refresh rates,
visible items, driver and build configuration.

**Accept when:** repeatable measurements identify stable operation or bounded,
actionable performance limits, without unexplained geometry loss, GPU errors
or runaway memory. Do not claim accepted solely because the synthetic renderer
builds and runs.

## Immediate priority 2 — Android lifecycle and GPU recovery

**Recovery instrumentation and an ADB Home/resume runner are implemented;
real-device lifecycle acceptance remains pending.** Background/foreground,
rotation, screen lock, task switching, window resize, interrupted gestures and
scene-graph invalidation need on-device testing. The GUI records first-frame
recovery/timeout and scene-graph ownership counters, persists lifecycle events,
and `tools/test_android_lifecycle.sh` tests repeated Home/resume, detects
process restarts and retrieves logs/memory samples. Pure C++ tests validate
the watchdog deadline and suspended-window semantics. Verify with hardware
that Qt-owned EGL contexts and bgfx buffers are released/rebuilt correctly.

**Accept when:** repeated real-device transitions leave both bgfx graphics
and QML usable; no persistent black frame, GPU state corruption, crash, leak
or lost input remains. Explicitly note cases where actual context loss could
not be induced. See [Android acceptance procedure](android-renderer-test.md).

## Next — Extract `AOWIS-GUI-Render`

After priorities 1 and 2 pass, create the shared rendering repository.
Separate renderer/core/scenes from `AOWIS-GUI`'s QWidget implementation and
`AOWIS-GUI-Mobile`'s Qt Quick surface adapter. Preserve desktop performance,
2D/3D terrain/network visualization, shader behavior and WebAssembly builds.
Keep a single graphics engine, not duplicated mobile/desktop renderers.

**Accept when:** desktop native, desktop WASM and Android/Linux Mobile builds
link the shared implementation, and relevant regression/performance tests
remain green. Do not move the current desktop renderer before this validation.

## Mobile map server settings (implemented configuration, integration pending)

A minimal hamburger menu preserves the full-screen map and keeps renderer
instrumentation in Diagnostics. Settings stores a validated HTTP(S) URL and
separate TCP port via QSettings. Mobile does **not** yet make map server
requests; that is part of connecting the actual map renderer and tile pipeline.

## Map and survey foundation

Replace synthetic geometry with real AOWIS-MAP/source imagery and geographic
coordinates; the automatic foreground Qt Positioning GPS service is implemented, but its
position cannot yet be plotted on the synthetic map. Add map-position projection,
selection, point/line/area tools and a typed
classification interface. Introduce generic geographic geometry (including
areas) into the appropriate shared MODEL layer.

**Accept when:** a field user can navigate a real map, capture/select
georeferenced shapes and classify them. No false hydraulic connectivity.

## Durable observations and offline workflow

Implement an actual local survey store, stable UUIDv7 identities, provenance,
local save/restart recovery, photos/notes and offline map regions from licensed
sources. Keep unsynced state explicit.

**Accept when:** an offline field session survives application/device restart
with geometry and attachments intact; no lost/duplicated evidence.

## Direct hydraulic editing and DVMDB sync

Implement a safe subset of canonical distribution edits in a personal working
DVMDB. Support durable autosave, undo/redo, commit of structurally coherent
networks even when hydraulically invalid, checkout of remote projects and
sparse/idempotent synchronization. Distinguish survey observations from
accepted authoritative edits.

**Accept when:** a user can create/edit/commit/reopen a working network,
reconcile conflicts deliberately, and recover from interrupted operations
without discarding local changes. No per-gesture full-network re-downloads.

## Later product extensions

Full OSM editing/upload adapters, Hydraulic Drainage, advanced polygon
operations and catchments, external GNSS/RTK, 3D terrain, richer survey
assignments, multilingual/RTL field forms, tablet-specific layout, and iOS.

## Open decisions

- Where canonical generic geometry/areas and observation types belong in
  MODEL/DVMDB and how they are serialized/indexed.
- Whether on-device DVMDB is practical or evidence needs another durable
  store with an explicit synchronization adapter.
- Licensed offline basemap provider/package format and download limits.
- Supported Direct Edit operations and exact conflict-resolution semantics.
- Real multi-view render-device ownership once `AOWIS-GUI-Render` is shared.

## Release discipline

Test durable writes, crash/restart, process kill, invalid geometry, offline
transitions, failed/duplicate sync operations and destructive operations.
Benchmark actual payload sizes and real hardware. Keep the architecture
pre-first-release clean: no historic migration framework, obsolete patch-stage
roadmaps, or unused compatibility layers. Update docs to reflect *current*
product behavior as each milestone is implemented.
