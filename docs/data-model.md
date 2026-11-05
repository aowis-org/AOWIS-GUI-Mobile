# Geographic features, observations and domain topology

## Design principles

**Agreed product behavior:** One set of geometry tools is used across general
mapping, OSM-like surveys, distribution and future drainage. A point/line/area
is a *geometric shape*, not its full domain identity. Selecting a feature type
must determine the applicable attributes, relationships and validation.

**Proposed implementation:** Introduce reusable geographic feature and
observation types rather than re-encoding a water network as generic OSM ways
or duplicating hydraulic entities inside a parallel mobile network schema.

There is no mobile schema in the repository yet. The following conceptual
records deliberately specify **meaning**, not final C++ class names, binary
formats or HTTP fields.

## Conceptual identities

| Concept | Purpose | Identity |
| --- | --- | --- |
| Geographic feature | A typed, classifiable point/line/area in an AOWIS geographic layer | AOWIS UUIDv7 |
| Observation | Immutable-identifiable field evidence and measurements with an editable review state | AOWIS UUIDv7 |
| Domain asset | Existing MODEL hydraulic object or future drainage object | Its own stable AOWIS UUIDv7 |
| Feature-to-asset link | Explicitly associates a geographic feature/observation with a typed asset | Referenced UUIDs, not location matching |
| External OSM reference | Maps an AOWIS feature to a known OSM object and version | Explicit OSM type, ID and version |
| Survey session | Groups observations, capture context and outstanding review work | AOWIS UUIDv7 |

Avoid silently assigning the same identity to an observation and its eventual
hydraulic asset. One asset can have many observations; an observation may be
unmatched, rejected or linked to one or more related targets after review.

## Geometry types

| Geometry | Minimum behavior | Later extensions |
| --- | --- | --- |
| Point | One geographic coordinate | Multiple measured samples and uncertainty |
| LineString | Ordered vertices, open or closed geometry by intent | Snapping, shared topology, track simplification |
| Polygon | Exterior closed ring, validity checks | Holes and multiple components |
| MultiPolygon | Multiple polygon components, optionally holes | More complex relation editing and clipping |

**Agreed:** The editing UI needs *areas* from the outset, although existing
AOWIS hydraulic MODEL editing does not yet have a general area-feature layer.

**Proposed data-model target:** Support Polygon with interior rings and
MultiPolygon without retrofitting incompatible area semantics later. This
need not mean every advanced polygon editing gesture ships in the first UI.
The precise geometry storage encoding and topology model remain open.

Coordinate reference: store/transport a canonical, explicit geodetic
representation (proposed WGS84 longitude/latitude), with map projections and
display units treated as view concerns. Latitude/longitude ordering and
altitude reference must be explicit in any future serialized format. Heights
must not silently mix GPS ellipsoidal altitude with orthometric or terrain
height. For distribution objects, retain the existing model's elevation
semantics instead of creating a conflicting mobile-only hydraulic elevation.

## Classification and attributes

A feature needs a named classification context rather than a single global
type string, e.g.:

- `general` → tree, path, building, waterbody, parcel, survey zone;
- `osm` → an OSM-compatible preset and tags;
- `distribution` → hydrant, valve, pipe, junction, reservoir, etc.;
- `drainage` → future manhole, inlet, culvert, channel, catchment, etc.

These are **illustrative categories, not registered identifiers**. The final
classification registry and custom-form schema remain to be designed. The
classification must determine required/optional attributes, units, UI forms,
validation, iconography and whether topology must be edited through a domain
adapter. Avoid forcing arbitrary dynamic tags into canonical hydraulic
physical-property fields.

## Observations and provenance

A survey observation should be able to retain:

- Observation UUID and survey-session reference.
- Point/line/area geometry or incomplete measurement sketch, with capture
  method and time.
- Reported position accuracy and associated measurement source (if provided).
- Selected/suspected classification, attributes and free-text notes.
- Durable attachment references (photos, future documents/audio), with
  attachment content checksums and upload status.
- Optional reference to a believed matching AOWIS asset or external feature.
- Review state and traceability to an eventual direct asset creation/update.

These fields are a **proposed minimum**, not an implemented schema. Review
state should not delete the raw observation's original provenance; a corrected
network location and the original field reading serve different purposes.

## Hydraulic distribution topology

A distribution line drawn on the map is a candidate visual path until the
hydraulic editor has created/selected its endpoints and validated the MODEL
relationship. A pipe's vertices, end-node references, diameter, material,
status, and other typed attributes must use the canonical AOWIS model.

A mobile tool may offer convenient assisted creation (e.g. draw a pipe and
select two junctions), but it may not introduce dangling references or create
an unowned second topology. Some hydraulically incomplete networks can be
committed; structural reference integrity remains required. Operations such as
moving a valve, changing a pipe segment and splitting a pipe are distinct,
meaningful transactions rather than raw edits to unrelated map features.

## OSM object mapping

OSM nodes, ways and relations do not map one-to-one to AOWIS features in every
case. An OSM way may be an area, a relation may group components, and shared
OSM nodes may express topology different from the hydraulic MODEL.

**Future adapter requirements:**

1. Track external OSM type/ID/version and provenance separately from UUIDv7.
2. Preserve distinctions between OSM geometry and AOWIS domain topology.
3. Translate tags through explicit, versioned presets/attribute mappings.
4. Reconcile edits with newer OSM versions rather than silently replacing
   community data.
5. Respect licensing, attribution and upload/review policies.

The first survey app may display OSM-derived basemaps or use OSM-style
classification without implementing OSM data download/upload editing.

## Open design details

The following require design before implementation: native geometry encoding,
spatial indexes, ownership of generic geographic layers relative to projects
and revisions, feature catalogues/forms, attachment content addressing, how
observations are finalized/accepted, and whether an area is simply a generic
feature or a domain-specific geometry plus relationships.
