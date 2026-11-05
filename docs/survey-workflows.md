# Field workflows and interaction design

## Two editing intents — explicit and visible

**Agreed:** AOWIS Mobile supports **both** of these workflows:

| Direct AOWIS editing | Survey observation |
| --- | --- |
| User deliberately opens an editable project/branch/working copy | User opens a survey session or observation layer |
| Targets existing/new *authoritative* typed model objects | Captures field evidence and tentative classifications |
| Must honor real network relationships and typed attributes | May represent unknown, unconnected or disputed assets |
| Saves an editable **draft**, separate from local Commit | Saves independently; review/integration is a later action |
| On revision/Commit, enters normal DVMDB version history | Observation remains identifiable after it is linked or accepted |

Do not silently infer a direct model edit from a collected observation. Do
not silently turn an unconnected observation into a hydraulic network entity.
A deliberate **Review / Integrate** operation should establish the target
object and record which source observation led to the change. How this is
presented remains to be designed.

### A typical observation

1. Select a survey session or observation layer.
2. Tap **Point**; choose GPS position or place a crosshair on the map.
3. Choose **Hydraulic Distribution → Valve**, or **Unidentified asset** if
   unsure. Attributes may be incomplete.
4. Attach notes, photos and reported positioning accuracy (where available).
5. Save immediately to durable local storage, without requiring a connection.
6. Later inspect the observation and either keep it as evidence, link it to an
   existing valve, or explicitly create/update the authoritative valve.

The same pattern works for a line (suspected pipe or ditch) or area (site,
building outline, possible catchment), with more geometry input.

### A typical direct edit

1. Open an editable distribution network in the personal working repository.
2. Select an existing junction or pipe, or invoke a hydraulic creation tool.
3. Move/reshape geometry or change its typed properties.
4. Save locally as a draft action. The network's node references and other
   MODEL invariants must remain coherent.
5. Undo/redo and eventually Commit using the AOWIS history model. Committing
   does not require an EPANET-valid hydraulic design; it still requires
   structurally valid records and references.
6. Synchronize/publish through an explicit controlled workflow when available.

The mobile implementation may initially allow only a safe *subset* of desktop
hydraulic editing; it should never pretend an unconstrained GPS line is a
fully connected pipe.

## Map interface

**Proposed layout:** an almost full-screen map, compact top bar for project,
layer and connection state, prominent locate/layers controls, and a bottom tool
row for **Point · Line · Area · Select**. Selecting an object opens a touch-
friendly bottom sheet, not a tiny desktop dialog. The sheet presents type,
fields, validation and Save/Cancel; large tables should be avoided in the
field workflow.

The interface must clearly show:

- The active **workspace and editing intent** (Observation / Direct Edit).
- Whether a position is measured or manually placed.
- Whether the current change is saved locally, waiting for sync, or rejected
  by a remote target.
- The selected object's identity, type and geometry, without losing the map.

Searchable type catalogues, recent/favorite feature types and repeated capture
are preferred over navigating a deep hierarchy for each valve or hydrant.
Classification should be independent of geometry: the same point tool may
create a tree observation, an OSM-like POI or a distribution asset.

## Position and geometry capture

**Proposed positioning methods:**

- **GPS fix:** read the device position and quality reported by the platform.
- **GPS average:** take a bounded series of fixes and retain enough quality
  metadata to show that the output is an estimate, not survey-grade truth.
- **Map placement:** tap or move a center crosshair to a known location.
- **Recorded track:** walk a path or boundary, then inspect and edit it as
  a line or closed area.
- **Manual vertex editing:** add/move/remove vertices and close an area;
  snap-to-existing-feature support can come later.

Store capture provenance: timestamp, acquisition source (device GPS, external
receiver, manual, trace, imported), available reported horizontal/vertical
accuracy and optional raw measurement reference. A GPS estimate must not be
presented as centimeter-accurate without supporting equipment and evidence.
External GNSS/RTK support is an extension, not a first-run dependency.

For lines and areas, retain sensible user controls for finishing or cancelling
a trace, correcting vertices, resuming after interruption, and distinguishing
an ordinary line from a closed polygon. Polygon validity checks should reject
self-intersections when editing authoritative area geometry; evidence layers
may retain an incomplete sketch with explicit status.

## Evidence and review

**Proposed:** photos, notes, optional asset identifiers and local capture
metadata belong to a survey observation. Attachments must be durable and
tracked as unsynchronized data; a text record alone is not sufficient. A
review workflow should preserve the observation's original measured location
when the resulting authoritative object is adjusted or snapped to topology.

## Important edge cases

- GPS missing, stale, or low accuracy: offer manual capture and label quality.
- Accidentally walking away mid-trace: preserve a draft, not a fabricated line.
- No network connectivity: do not block local Save or lose photos.
- User switches project or workflow: warn about unfinished geometry; preserve
  already saved observations.
- Two surveyors observe the same valve: keep both independent evidence records;
  do not auto-merge by proximity.
- Object moved in the authoritative network while offline: surface a conflict
  during synchronization, never overwrite silently.
- A known valve may have multiple observations over time. Observations are not
  assumed to be the same entity merely because they share coordinates.
