# Architecture and repository boundaries

## The current application

The Mobile source contains **one** Qt Quick application. `src/main.cpp`
starts Qt, supplies the platform title/icon, selects bgfx underlay or QML
baseline, attaches a `RenderLab` to the hidden `QQuickWindow` *before* showing
it, and schedules redraws while active. `qml/Main.qml` owns the temporary
UI, gesture handling and diagnostic panel. `src/render_lab.cpp` bridges Qt's
render thread to bgfx; `src/gpu_scene_renderer.cpp` and
`src/gpu_scene_geometry.cpp` supply synthetic geometry, shaders and camera
math. The geometry has a dependency-free C++ unit test.

`external/bgfx.cmake` is a **temporary pinned submodule** with nested
`bgfx`, `bx` and `bimg`. It is not part of a shared AOWIS renderer library yet.

The renderer-independent `LocationService` uses Qt Positioning and runtime
`QLocationPermission` for automatically started foreground positioning; `LocationSnapshot`
validates the fields returned by providers. It never persists or transfers
coordinates. The QML Settings panel and compact status pill consume its
reactive properties.

`compile_linux.sh` builds against installed Linux Qt. `compile_android.sh`
builds an ARM64 APK within a pinned Docker toolchain and optionally deploys
with host ADB. **Android is a working build target**; iOS is not.

## Repository family and intended ownership

| Repository | Responsibility | Present relationship |
| --- | --- | --- |
| `AOWIS-GUI` | Desktop QWidget application, current production-oriented map/editor stack | Separate existing repository; no renderer extraction yet |
| `AOWIS-GUI-Mobile` | Touch UI, platform adapters, survey workflow, mobile deployment | This repository |
| `AOWIS-GUI-Render` | Shared rendering and map-scene engine | **Planned**, not created/integrated as part of this source snapshot |
| `AOWIS-SERVER-MODEL` | Canonical domain structures, typed hydraulic entities, future generic geographic types | Existing elsewhere, not linked into Mobile |
| `AOWIS-SERVER-MAP` | Map/terrain service and caching | Existing elsewhere, not linked into Mobile |
| `AOWIS-SERVER-DVMDB` | Native project/branch/revision storage and working-copy semantics | Existing elsewhere, not linked into Mobile |

Renaming repositories is independent of migrating code. Nothing in the
current Mobile scene depends on MODEL, MAP or DVMDB.

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
intended contracts, and [renderer integration](renderer-lab.md) for current
GPU-thread details.
