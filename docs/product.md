# Product: geographic field survey

## Purpose

**AOWIS-GUI-Mobile** is a map-first field tool for capturing, describing and
editing geographic features. Its main job is to make surveying fast and
reliable in the field, including without connectivity. It is **not** a smaller
copy of desktop AOWIS-GUI or a hydraulic solver.

The intended short workflow is: open an area → position or draw a feature →
classify it → attach details/evidence → save durably → synchronize when possible.
Existing features can be selected and inspected as well.

## Domains

| Domain | Intended use | Domain constraint |
| --- | --- | --- |
| General mapping / OSM-like surveying | Points of interest, roads, waterways, buildings, parcels and other features | OSM tags/IDs are external semantics; full OSM editing/upload is later |
| Hydraulic Distribution | Survey and eventually directly edit pipes, valves, junctions, hydrants, tanks, reservoirs | Typed AOWIS MODEL objects and real node connectivity, not arbitrary polylines |
| Hydraulic Drainage | Survey and later directly edit manholes, drains, culverts, channels and catchments | Future domain model and topology |

One geometry toolbar is shared across domains: **Point**, **Line**, **Area**,
**Select/Edit**. A selected object's classification determines attributes,
validation and any domain-specific connections. A polygon alone is not a
catchment; a polyline alone is not a hydraulic pipe.

## Two explicitly separate workflows

**Agreed:** Mobile supports both **Survey Observations** (field evidence that
may be incomplete or uncertain) and **Direct Edit** (authorized changes to real
AOWIS objects in a working copy). Observations must never silently mutate
an authoritative hydraulic network. The review/integration step explicitly
links evidence to a target object; the original observation remains traceable.
See [field workflows](survey-workflows.md).

## Intended user experience

- A near-full-screen 2D map, compact status/layer controls, large touch targets,
  searchable/recent object classifications and attribute bottom sheets.
- Capture from device GPS, manual placement and eventually walks/traces or
  external GNSS. Show reported accuracy rather than implying survey-grade
  precision from a normal phone.
- Clear distinction among **locally saved**, **not synchronized**, **synced**
  and **conflict** states; keep all unsynced field data after a network failure.
- Photos, notes and asset identifiers as evidence, with repeat-capture flows
  for many objects of the same type.
- Offline basemaps from licensed sources, prepared before leaving coverage.

## Platform identity

| Environment | Visible name | Internal identity |
| --- | --- | --- |
| Android | **AOWIS** | `org.aowis.mobile` |
| Linux desktop build of Mobile | **AOWIS Mobile** | `aowis-mobile` |
| Future iOS | **AOWIS** | To be defined |

The supplied water/gear icon is common across current targets. Touch UI is
Qt Quick/QML, not a QWidget desktop port.

## Implemented navigation and server configuration

The map fills the app window with a small hamburger button in the **top-right**;
the navigation drawer opens from the **right**. The previous permanent metrics
and workload controls have moved to **Diagnostics** in the menu. The same
diagnostic tools remain accessible for Android/Linux testing. Menu, selector,
and zoom icons are drawn as Qt Quick strokes rather than Unicode font glyphs,
so they remain visible on Android even when a system font lacks those symbols.

On Android, **Back** closes an open drawer first; otherwise it returns from
Settings or Diagnostics to the map. From the map, Back retains Android's
normal behavior (sending the application to the background). Desktop Escape
closes the drawer or returns to the map as well. Unsaved changes to the map
server fields are not applied: use **Save map server** before leaving.

**Settings → Appearance** provides a persistent **Follow system / Dark / Light**
selector. Switching takes effect immediately; the system choice follows the
Android/desktop color scheme when Qt reports a change. The settings screens,
drawer, text fields, buttons, prompts and diagnostics use an explicit shared
palette so text remains legible in both themes. The bgfx map renderer keeps its
own graphics palette; changing the interface theme does not recolor hydraulic
network geometry.

**Settings → Map server** provides two fields: HTTP(S) server URL (without a
port) and a numeric port (1–65535). Both values are validated in C++, saved
through Qt's per-user settings, and restored on restart. A full endpoint URL
is formed from those two entries. This is a *configuration-only* feature:
no map-server HTTP client, tile requests, health check or connectivity status
is implemented in Mobile yet. Saving does not imply that the server is online.

## Current implementation boundary

The **existing** grid, colored segments and junctions are synthetic graphics
for GPU integration tests. None represent captured sites or hydraulic assets.
A magnetic Compass page is available from the hamburger menu. It shows a
vector compass rose, magnetic bearing and calibration status using Qt Sensors;
it does not rely on GPS course-over-ground or the shared map renderer.

Foreground-only GPS/location permissions and a live position readout are
implemented; no position-to-map projection, survey forms, offline DB, maps or
sync exist yet.
The rendering foundation has been exercised on Linux and Android. See
[renderer integration](renderer-lab.md) and [roadmap](roadmap.md).

A first useful survey release should allow GPS/manual points, lines and areas,
classification, reliable local observation saving and offline basemaps. Direct
hydraulic edits, synchronization and richer OSM editing follow as the relevant
shared dependencies become ready. Advanced 3D, RTK and drainage can come later.

## Gradienter

The hamburger menu includes a two-axis gravity spirit level with a live
bubble, pitch/roll and a relative zero reference. See
[Gradienter](gradienter.md) for the implemented behavior.
