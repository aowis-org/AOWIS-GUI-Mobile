# Geographic data, observations and topology

This is a **target model contract**, not an implemented Mobile schema. The
existing code contains only synthetic GPU vertices and camera transforms.

## Geometry is not domain identity

- **Point:** position for a map feature/observation; may reference a typed
  asset when a domain adapter explicitly creates that relationship.
- **Line:** ordered vertices for roads, tracks, pipes, watercourses, etc.
  Geometry does **not** itself establish hydraulic network connectivity.
- **Area:** closed polygon boundary, later holes and multipart geometry;
  classification determines building/parcel/catchment meaning.

A shared geometry layer should support stable vertex editing, coordinate
reference transformations where necessary, geometry validation and spatial
queries. Rendering is a consumer of model data, not its owner. Area support
must be added to shared MODEL rather than invented only inside Mobile.

## Separate identities

| Object | Proposed identity | Ownership |
| --- | --- | --- |
| Generic geographic feature | UUIDv7 | Shared geographic MODEL/DVMDB |
| Survey observation | UUIDv7 | Durable evidence store; reviewed/integrated explicitly |
| Typed hydraulic entity | Existing canonical UUIDv7 | Hydraulic distribution MODEL |
| Observation-to-asset link | Stable references to source and target UUIDs | Explicit review transaction |
| OSM object | OSM type/ID/version plus local reference | Future OSM adapter |

Do not replace UUID identities with coordinates or content hashes. Preserve
canonical network references on hydraulic edits. The exact C++ types,
serialization and DVMDB route contracts are **not decided here**.

## Evidence and audit

An observation can retain source, acquisition timestamp, reported positional
accuracy, author/session, original measured geometry, classification and
attachments. Acceptance into a network **does not erase the original evidence**.
An observation can be unknown, uncertain, disputed or superseded without
pretending to be a hydraulically connected asset.

In Direct Edit, immutable revisions, mutable HEAD, branch ancestry, durable
edit journal and operation identity belong to DVMDB, not to an ad hoc mobile
revision mechanism. Commit can preserve a structurally coherent but not
hydraulically solved draft.

## OSM boundary

OSM nodes/ways/relations and tags are not canonical AOWIS MODEL types. A
separate adapter must handle OSM IDs, versions, topology semantics, upload
rules and edit conflicts. General survey classifications can resemble OSM
presets without immediately publishing to OSM.

## Implementation status

No geographic layer, polygon serializer, observation record, asset-link table,
OSM adapter or hydraulic edit transaction is present in this Mobile snapshot.
These are dependencies for the [survey workflow](survey-workflows.md) and
[roadmap](roadmap.md), not current runtime capabilities.
