# Field workflows and interaction requirements

These are **agreed product workflows**, not implemented mobile editors.
The current renderer scene is unrelated to saved observations or hydraulic
network objects.

## Explicit editing intent

| | Survey Observation | Direct Edit |
| --- | --- | --- |
| Working context | Local survey session / evidence layer | Authorized editable project branch/HEAD in personal working DVMDB |
| Goal | Record what was seen/measured, possibly uncertain | Change authoritative typed geographic or hydraulic data |
| Incomplete geometry/classification | May be retained as a clearly marked draft | Must comply with structural MODEL/reference constraints |
| Save | Durable local evidence and attachments | Durable working-copy edit, separate from Commit |
| Result | Observation pending review or linked to known asset | Updated HEAD/working state and history |
| Later integration | Deliberately link/create/update a target object | Commit/publish according to DVMDB workflow |

Never silently convert an observation to a network edit or infer a hydraulic
connection from a geographical overlap. The UI must make the active mode
unmistakable.

## Observation flow

1. Open a survey session, view location and map, select **Point/Line/Area**.
2. Use device location, map crosshair/tap or manual vertex/trace acquisition.
3. Classify the shape: e.g. Valve, Suspected Pipe, Building, Unidentified.
4. Enter optional attributes, field notes, photo and measured quality.
5. Save on-device **before** reporting success; mark as not yet synced.
6. Later review against an authoritative project; link to an existing object
   or explicitly create/update one, retaining observation identity/evidence.

A survey of an unconnected valve or ambiguous pipe must still be recordable.
Repeated capture of the same feature type should require very few actions.

## Direct hydraulic editing flow

1. Open a project that is editable in the designated personal DVMDB.
2. Select an existing hydraulic entity or choose an allowed creation operation.
3. Edit typed properties/position/topology through canonical MODEL rules.
4. Autosave to the local working state, with Undo/Redo and visible saved status.
5. Commit on explicit request; structural data integrity matters, but a
   hydraulically unsolved/non-valid network **must still be committable**.
6. Fetch/publish/reconcile when the applicable DVMDB capabilities are ready.

Direct Edit v1 may support only a safe subset of desktop operations. Ordinary
survey polylines must not be incorrectly persisted as connected pipes.

## Map and touch UI

The intended tool row is **Point · Line · Area · Select**. The map dominates
the screen, while classifications and properties open in touch-sized sheets.
Support panning, pinch zoom, locate-me, layer selection, selection/edit handles
and undo/cancel operations. Make a current mode, GPS accuracy and save/sync
status visible without obscuring the map. Retain recent/favorite classifications.

**Current code only supports** test-scene pan/zoom/recenter and touch/tap
reporting. No geometry edit gestures or selection semantics exist yet.

## Position quality and field provenance

Capture source (`phone GPS`, `external receiver`, `manual`, `trace`, `import`),
UTC timestamp, reported horizontal accuracy and, if available, vertical
accuracy and accuracy method. Distinguish the survey observation coordinate
from a later authoritative network coordinate. Do not claim phone GPS offers
centimeter accuracy by default. Allow incomplete evidence/sketches with a
visible review status; validate authoritative polygons and topology before
promoting them into canonical objects.

Future field enhancements: GPS averaging, external GNSS/RTK, track capture,
photo capture, map snapping, robust trace interruption/resume and accessible
forms including Arabic RTL localization.

## Save and recovery rules

A network outage must never discard already committed local survey evidence.
Deleting or overwriting unsynced work requires explicit user intent. A failed
sync remains a locally visible retryable condition. UI success cannot rely on
an in-memory state or just a matching model hash. Details are in
[offline and synchronization](offline-sync.md).
