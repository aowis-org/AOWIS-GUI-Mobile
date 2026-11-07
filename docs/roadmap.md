# Implementation roadmap

The active application is **AOWIS-GUI-Mobile**: Qt Quick/QML with the shared
AOWIS map (AOWIS-GUI-Render `MapBgfxMapView`) drawn in Qt Quick's OpenGL /
OpenGL ES context, imagery and terrain from the configured aowis-server-map,
and Docker Android builds. **No geographic survey feature is implemented
yet.** The following milestones describe future work and acceptance, not
already shipped capabilities.

## Immediate priority 1 — the map on devices

**Implemented in source; build and device acceptance pending.** See
[the map](map.md). Verify on Linux and Android GLES hardware: imagery and
terrain from a real aowis-server-map, 2D and globe navigation with touch
(directions, sensitivity), Qt Quick map controls, frame pacing and memory while
panning and zooming, Home/resume and rotation (scene-graph recreation),
switching the map server in Settings, and QML overlays in all five
languages.

**Accept when:** the map shows the server's imagery and terrain, navigates
smoothly and recovers from repeated background/resume and rotation without
black frames, GPU errors, crashes, leaks or lost input.

## Immediate priority 2 — Android lifecycle and GPU recovery

**Recovery instrumentation and an ADB Home/resume runner are implemented;
real-device lifecycle acceptance remains pending.** The map item records
first-frame recovery/timeout and scene-graph counters in
`lifecycle/events.csv`; `tools/test_android_lifecycle.sh` tests repeated
Home/resume, detects process restarts and retrieves logs/memory samples.

**Accept when:** repeated real-device transitions leave both the map and QML
usable. Explicitly note cases where actual context loss could not be induced.
See [Android acceptance procedure](android-renderer-test.md).

## Next — the hydraulic network on the mobile map

Feed the map view a `NetworkRenderSnapshot` and symbology from the working
copy (as the desktop GUI does), with picking for inspection.

## Map and survey foundation

Add map-position projection of survey geometry, selection, point/line/area
tools and a typed classification interface. Introduce generic geographic
geometry (including areas) into the appropriate shared MODEL layer.

**Accept when:** a field user can navigate the real map, capture/select
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
