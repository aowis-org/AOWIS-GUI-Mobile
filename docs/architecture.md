# Application architecture and AOWIS integration

## Current repository state

**Implemented here:** repository README, Apache-2.0 LICENSE, `.gitignore`,
`tools/apply_patch.sh`, `tools/zip_mobile_project.sh`, and documentation.

**Not implemented here:** Qt/CMake project, Android or iOS packaging, app UI,
map renderer, GNSS integration, local persistence, new geographic MODEL types,
OSM support, or DVMDB/mobile transport. These are target components.

## Proposed software structure

```text
AOWIS Mobile (Android first; iOS-compatible architecture)
  Qt Quick / QML map and touch UI
    Survey tools: point / line / area / select
    Attribute sheets, observation review, direct-edit workflow
    GPS / location adapter, photos, offline state
                |
      Reusable C++ application services
        geometry edit and validation
        survey observations / provenance
        hydraulic-domain adapter
        OSM adapter (future)
        local persistence + sync coordination
                |
  +-------------+------------------+-----------------+
  |             |                  |                 |
AOWIS MODEL   DVMDB              AOWIS MAP      Mobile platform
geometry &    versioned edits,     basemap /       GPS, camera,
typed data    local repository,    terrain,        files, lifecycle
              transfers           offline packs
```

The boxes above describe proposed ownership, **not** established mobile APIs.
Keep UI-independent editing and persistence logic in C++, so Android, iOS
and desktop can share behavior without sharing QWidget screens.

## Integration boundaries

### AOWIS-SERVER-GUI (existing elsewhere)

The desktop GUI has a shared map workspace, bgfx presentation and distribution
monitor/editor behavior. Reuse the appropriate domain logic and renderer
components where separation permits. Do **not** embed desktop QWidget docks,
inspection panels or desktop pointer handling in the mobile UI. Mobile input,
overlays, forms and lifecycle are separate concerns.

**Proposed rendering experiment:** demonstrate a responsive 2D map in Qt
Quick on an Android device, using bgfx if surface integration and frame
ownership are practical. A separate mobile 2D renderer is acceptable if it
shares data and geometry semantics. 3D is not necessary for the survey MVP.

### AOWIS-SERVER-MAP (existing elsewhere)

The current service supplies raster imagery, elevation data and caching.
Mobile needs an explicit offline map-region/package contract rather than
assuming a cached remote HTTP tile service is an offline package manager.
The geographic view should be usable without terrain availability. Vector
basemaps can be introduced when the provider/packaging plan is settled.

### AOWIS-SERVER-MODEL (existing elsewhere)

Hydraulic distribution already has typed structures and canonical
serialization. Shared geographic point/line/polygon primitives and survey
objects are **proposed additions**. They must not weaken hydraulic entity
identity or connectivity. Shared classes should avoid hidden Qt UI or Android
assumptions; mobile-only capture metadata can live in a suitable domain
component until the permanent ownership boundary is decided.

AOWIS development conventions apply to new C++: do not use `auto`; use
`this->` for class-member data access. Platform-dependent services should be
behind narrow interfaces to make them testable without a device.

### AOWIS-SERVER-DVMDB (existing elsewhere)

DVMDB provides repository/project/branch/revision identities, private working
copies, a durable edit journal, conditional writes and revision bundles. A
private personal repository may be embedded or reached over HTTP in other
AOWIS deployments. The mobile app must not assume that all desktop GUI
checkout, Publish or Fetch flows are complete.

Mobile must distinguish:

1. **Survey storage:** observations and attached evidence, potentially before
   any matching network entity exists.
2. **Writable network draft:** normal DVMDB private-repository editing with
   typed MODEL records and history.
3. **Shared/published source:** remote versioned source with distinct committed
   HEAD, never modified as a side effect of a local Save.

Choosing the exact on-device storage engine is still open (see
[offline/sync](offline-sync.md)); a network-only DVMDB draft cannot replace an
observation/evidence model by itself.

### OSM integration (future)

An OSM adapter should translate between OSM nodes/ways/relations/tags and
AOWIS's typed geographic model, preserving external identity and provenance.
It should also manage upload/download semantics and OSM-specific conflicts.
Do not make OSM tags or relation IDs the canonical identities of AOWIS assets.
OSM data sources and basemap providers each require proper attribution and
license-aware handling.

## Cross-cutting requirements

- **Identity:** UUIDv7 for AOWIS entities; external IDs are explicit mappings.
- **Persistence:** local Save must survive process termination and device restart.
- **Performance:** area queries, spatial indexing and viewport-based loading
  rather than whole-project redownloads on each edit/Undo/navigation.
- **Correctness:** separate map geometry, hydraulic connectivity, observations
  and provenance; do not conflate a displayed shape with a model entity.
- **Locality:** field data acquisition, geometry interactions and queued work
  operate without constant server connectivity.
- **Testing:** logic-unit tests on desktop, API/integration tests against
  DVMDB, and real-device tests for GPS/lifecycle/rendering/touch handling.

Authentication, mobile device security and offline data protection require a
separate implementation design before deployments with sensitive field data;
repository role labels alone do not authorize callers.
