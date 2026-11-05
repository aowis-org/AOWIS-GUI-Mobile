# Product: field mapping and survey

## Goal

**Agreed:** AOWIS Mobile is a map-centered field application for capturing,
classifying, inspecting and editing geographic objects. Its first purpose is
*surveying*, not recreating the desktop GUI or running a hydraulic simulation
on a phone.

The product should make this interaction short and repeatable:

1. Open a map of the survey area, online or offline.
2. Locate a feature using GPS, a map tap, manual geometry or a track.
3. Choose a **point**, **line**, or **area** and identify what it represents.
4. Supply relevant attributes and optional supporting evidence.
5. Save locally, continue surveying, and synchronize when possible.

Surveyors must also be able to select and update existing mapped objects.

## Domains

| Domain | Initial intent | Important distinction |
| --- | --- | --- |
| General geographic mapping | Points, lines, areas and configurable feature types | Independent survey or map features, not hydraulic assets by default |
| OSM | OSM-like tagging/classification and, later, a capable OSM editor | External OSM identity, data model, policies and publication must be mediated by an adapter |
| AOWIS Hydraulic Distribution | Locate and record water assets; edit real distribution objects when appropriate | Typed hydraulic entities, network references, real connectivity and engineering properties |
| AOWIS Hydraulic Drainage | Future survey and editing of drainage objects | Will require its own domain model and topology |

Examples include valves, hydrants, junctions, pipes, tanks, reservoirs, roads,
waterways, buildings, parcels, catchments, manholes and culverts. These examples
are a product direction, **not** a claim that all corresponding MODEL entity
types or mobile classification forms already exist.

## Shared core interactions

**Agreed** as part of the survey tool:

- Full-screen or near-full-screen **2D map**, with layer/feature selection.
- **Point**, **line**, **area**, and **select/edit** tools.
- Choose the object's identity/type after capture, with an efficient way to
  capture many objects of the same type.
- GPS-based positioning and manual map placement.
- View, inspect, edit and save classified objects.
- **Both** direct AOWIS object editing and separate survey observations.
- **Offline-first** field work, with durable unsynchronized changes.
- A design that can grow into OSM editing and drainage, not special cases
  embedded in a single water-network screen.

**Proposed early enhancements:** location accuracy display, repeated GPS
sampling, GPS-track line/area capture, searchable type presets, photos,
field notes, visible unsynced state and optional external GNSS/RTK receivers.
See [field workflows](survey-workflows.md) for more detail.

## Product boundaries

- A geographic **polyline** is not automatically an AOWIS hydraulic pipe.
  A real pipe must connect appropriate network entities.
- An **observation** can describe a suspected or newly discovered asset even
  when its network topology is unknown; direct hydraulic edits must maintain
  the canonical model's reference rules.
- A **map area** (polygon) is not automatically a hydraulic catchment, a
  reservoir footprint, an OSM multipolygon or a drainage subcatchment.
  Classification determines its domain semantics.
- OSM editing does not mean storing AOWIS hydraulic engineering objects as
  OSM-native entities.
- Simulation/monitoring, complete desktop editing tools and full OSM upload
  workflows are **not prerequisites** for the first useful field-survey app.

## Platforms and UI

**Proposed:** Android first, with iOS kept viable architecturally. Implement a
touch-first application rather than port the existing desktop QWidget panels.
Qt Quick/QML is the preferred UI direction, with shared AOWIS C++ logic where
practical. The rendering choice remains subject to a real-device prototype.

The initial editor should be fast to operate with one hand, support landscape
and tablet layouts, work in poor connectivity, and make the active workflow
(Observation versus Direct Edit) unmistakable.

## Scope of first useful release

The first releasable survey experience should cover map/GPS positioning, point
and line capture, area geometry, type selection, observation collection,
reliable local persistence and offline basemaps. Direct distribution editing
and server synchronization are high-priority follow-ons, with their contracts
defined early enough not to require redesign of captured features.

A full general OSM editor, drainage editor, sophisticated GNSS processing,
collaborative task dispatch, 3D terrain rendering, and iOS release are later
extensions. Ordering and acceptance criteria are in the [roadmap](roadmap.md).
