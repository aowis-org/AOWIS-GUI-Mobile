# Architecture and repository boundaries

## The current application

The Mobile source contains **one** Qt Quick application. `src/main.cpp`
starts Qt (basic render loop, OpenGL / OpenGL ES scene graph, EGL on X11),
supplies the platform title/icon and registers the `AOWIS.Map` `MapView` QML
type. `qml/Main.qml` owns the pages, the navigation drawer and the map page's
QML controls. `src/mobile_map_view.cpp` (`MobileMapView`, a `QQuickItem`) is
the Qt Quick host of Render's `MapBgfxMapView` in host-context mode: it draws
the map from `beforeRenderPassRecording` into Qt Quick's EGL context, releases
it on `sceneGraphInvalidated`, handles touch and mouse gestures, and owns the map
model, the provider catalog and the tile/terrain repositories connected to
the configured aowis-server-map. See [the map](map.md).

Render's nested `external/bgfx`, `external/bx` and `external/bimg` (the
revisions the desktop GUI builds) are the only bgfx sources.

The renderer-independent `LocationService` uses Qt Positioning and runtime
`QLocationPermission` for user-controlled foreground positioning; `LocationSnapshot`
validates the fields returned by providers. It never persists or transfers
coordinates. The QML Settings panel and map menu consume its
reactive properties.

`UnitProfileSettings` is another renderer-independent Qt adapter. It uses the
canonical registry, built-in profiles and inheritance rules directly from the
pinned `external/AOWIS-SERVER-MODEL` submodule (`AOWIS::Model`). The active unit profile
and custom profile overrides are per-device display settings. Profile changes
notify Qt Quick controls immediately; the GPS accuracy/altitude/speed
readouts use those profiles. See [display units](units.md).

`compile_linux.sh` builds against installed Linux Qt. `compile_android.sh`
builds an ARM64 APK within a pinned Docker toolchain and optionally deploys
with host ADB. **Android is a working build target**; iOS is not.

## Repository family and intended ownership

| Repository | Responsibility | Present relationship |
| --- | --- | --- |
| `AOWIS-GUI` | Desktop QWidget application, map editor | Separate repository; hosts the same Render map view in Qt Widgets |
| `AOWIS-GUI-Mobile` | Touch UI, platform adapters, survey workflow, mobile deployment | This repository |
| `AOWIS-GUI-Render` | Shared map engine (2D/globe, imagery, terrain, HUD, map server client) | Pinned submodule; the Mobile map |
| `AOWIS-SERVER-MODEL` | Canonical domain structures and unit registry | Pinned Git submodule; `AOWIS::Model` linked into Mobile |
| `AOWIS-SERVER-MAP` | Map/terrain service and caching | Reached over HTTP(S) at the URL from Settings |
| `AOWIS-SERVER-DVMDB` | Native project/branch/revision storage and working-copy semantics | Pinned source submodule; not yet compiled or linked |

GeographicLib at `external/GeographicLib` is Render's geodesy library, built
by Render from Mobile's checkout. See [dependency setup](dependencies.md).

The Mobile map does not yet show MAP/DVMDB network data; it links MODEL for
its canonical domain types and unit profiles.

## Planned shared renderer boundary

```text
AOWIS-GUI (Qt Widgets)          AOWIS-GUI-Mobile (Qt Quick)
         |                                |
  QWidget presentation             QML/native surface adapter
         +---------------+----------------+
                         |
               AOWIS-GUI-Render (planned)
        camera / scenes / picking / terrain / bgfx
                         |
             typed MODEL data and map sources
```

The **engine** should own scene data preparation, GPU resources, cameras,
render passes and picking. The **applications** should own QWidget/QML
surfaces, user input, forms, persistence workflows and platform lifecycle.
The existing Qt Quick underlay adapter should not force Qt Quick into the
shared engine or impose a single visible surface on all platforms.

Avoid moving `AOWIS-GUI` code until the Mobile prototype passes stress and
lifecycle tests. Extraction must preserve desktop native and WebAssembly
behavior, existing 2D/3D features, terrain, symbols and hydraulic overlays.
Do not introduce duplicate rendering engines or a Graphviz dependency.

## Survey and persistence services (proposed)

The future C++ services should separate map navigation and geometry capture
from typed object editing and durable evidence storage:

```text
Touch QML / GPS / camera
       |
Survey tool controller -- observation store and attachment ledger
       |
Geographic features and classifications (MODEL additions)
       +---- distribution domain adapter --> canonical MODEL working copy
       +---- OSM adapter (later) ----------> external OSM identities
       |
Local durable storage --> explicit synchronization --> personal DVMDB
       |
Map tile/offline package provider --> AOWIS-SERVER-MAP / licensed source
```

The specific on-device storage engine, generic feature schema, map package
format and sync protocol remain design work. Do not imply they are already
implemented. A personal working DVMDB is the agreed editing destination;
projects on other servers must first be checked out to it for direct edits.

See [data model](data-model.md) and [offline/sync](offline-sync.md) for
intended contracts, and [the map](map.md) for current
GPU-thread details.
