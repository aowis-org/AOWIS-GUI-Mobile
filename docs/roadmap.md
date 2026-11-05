# Implementation roadmap

## Baseline

As of **2026-11-05**, the **AOWIS** application has a working Qt Quick
shell and a bgfx direct-underlay diagnostic map on Linux and Android (verified
on an Android phone). Its identity is `org.aowis.mobile` on Android and
`aowis-mobile` on Linux. The QML-only comparison path remains available.
This is a rendering foundation, **not yet a survey editor**: GPS, generic
feature capture, local survey data, offline basemaps and synchronization are
not implemented. See [rendering diagnostics](renderer-lab.md) for outstanding
lifecycle, scalability and performance validation. The priorities below are
future product deliverables, not claims of completed field functionality.

The scope is intentionally a field-survey product. The complete desktop editor
and community OSM editor are not requirements for the first usable APK.

## Priority 1 — Runnable map shell

Build on the existing Android/Linux Qt Quick shell and verified bgfx underlay.
Finish the real-device lifecycle, orientation, resize and scalability tests;
replace synthetic test geometry with a proper map viewport and initial map
source. Add a visible position indicator and map-coordinate selection. Keep
mobile adapters iOS-compatible where practical. Extract the shared renderer
only after lifecycle and architecture tests, not merely because the probe draws.

**Accept when:** the application launches reliably, pans/zooms smoothly,
recovers after background/resume and exposes selected map coordinates.

## Priority 2 — Generic survey capture and durable observation storage

Implement point/line/polygon drawing, selection, geometry correction and
basic classification. Add a persistent local observation/session store with
UUIDv7 identities, source/time/accuracy metadata, basic fields and map
review. Integrate platform GPS plus manual placement. Support tentative
classifications, including unknown assets. Prefer forms driven by type
metadata rather than hardcoded per-object screens.

**Accept when:** a surveyor can record, classify, review, edit and retain
points, lines and areas across process/device restarts without connectivity.

## Priority 3 — Practical field workflow and offline basemaps

Add saved survey areas, downloadable offline basemaps from an authorized source,
package status, a repeat-capture workflow, photo attachments, notes, and
local save/unsynchronized indicators. Address location permission behavior,
poor-accuracy warnings, interrupted traces, storage pressure and large data.

**Accept when:** a field session can be prepared online, performed entirely
offline and reopened with all images, measurements and map data intact.

## Priority 4 — AOWIS hydraulic distribution direct editing

Reuse canonical MODEL entity types and suitable editor services. Add a
clear **Direct Edit** workspace for private working copies. Begin with safe,
well-defined object selection, attributes, moving positions and a deliberately
small set of creation/edit actions. Ensure pipe endpoints/topology and typed
references remain valid; integrate meaningful action history and local Commit
without requiring hydraulic solver validity.

**Accept when:** a device can edit supported distribution objects through a
private draft, Undo/Redo meaningful steps, commit a valid canonical snapshot,
and restart without losing the saved state.

## Priority 5 — DVMDB synchronization and observation integration

Settle the mobile local repository architecture, implement sparse and
recoverable transfer of survey data/attachments, and connect the established
private/shared DVMDB lifecycle (checkout, draft, local Commit, Fetch, Publish
and explicit reconciliation as server capabilities mature). Provide a review
workflow to attach or integrate observations into existing/new typed objects
without mutating the observation evidence.

**Accept when:** disconnected changes sync after reconnect without duplicate
operations; remote divergence causes an explicit conflict; matching hashes
verify known states but do not conceal missing changes; all objects and photos
retain identity and provenance.

## Later capabilities

- Expanded OSM editor: classification/tag presets, OSM object/version mapping,
  review and compliant upload behavior.
- Hydraulic Drainage survey and direct editor when its domain MODEL exists.
- Advanced area functionality: polygon holes/multipolygons, snapping, topology
  editing, catchment/site geometry and validation.
- External high-accuracy GNSS/RTK acquisition and richer accuracy metadata.
- 3D map/terrain mode, advanced survey forms, multi-device coordination,
  survey assignment tools, tablets and full iOS release.
- Localization consistent with AOWIS GUI language work, including RTL layouts
  for Arabic where practical.

## Architectural decisions to settle during development

These are **open choices**, not missing requirements from the user:

1. Can on-device DVMDB provide acceptable durability, packaging and size on
   Android/iOS, or is a separate embedded store required for survey evidence?
2. Where do generic geographic features, areas and observation records live
   within MODEL/DVMDB projects and revision history?
3. Which offline basemap format/provider can AOWIS distribute and store with
   the required license terms and memory/disk limits?
4. What is the minimum typed hydraulic operation set for Direct Edit v1?
5. How are an observation, its original measured geometry, the target
   authoritative geometry and any OSM feature mapped without identity loss?
6. Which conflict/merge paths are truly ready in DVMDB when mobile sync ships?

Do not block the first Android map/GPS/observation implementation on the full
answer to every later sync or OSM question, but preserve the conceptual
boundaries so those features can be added without breaking the data model.

## Quality gates

For every milestone, add a unit/integration test target and a reproducible
real-device check where hardware behavior matters. Test destructive paths,
process kills, offline/online transitions, schema and UUID invariants,
geometry validity, coordinate round trips and stale server state. Profile
map performance and payload sizes; do not accept full-network downloads as
the default for fine-grained edits or History navigation.

Keep documentation current with *implemented product behavior*. Once code
ships, replace proposed wording with verified contracts and move remaining
work here; do not accumulate superseded patch-stage roadmaps.

The Android GPU-underlay validation instructions are maintained in
[Android renderer test](android-renderer-test.md). Basic real-device drawing
and interaction are verified; repeated background/foreground, rotation,
large-scene performance and recovery acceptance remain open.
