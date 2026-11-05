# AOWIS-GUI-Mobile documentation

The documentation distinguishes **implemented behavior**, **agreed product
requirements**, and **designs not yet implemented**. The application is real and
runs on Linux and Android, but its map currently shows a diagnostic GPU scene.
A capability in a related repository is not automatically integrated here.

| Document | What it answers |
| --- | --- |
| [Product](product.md) | What Mobile is for, scope, platform naming and product boundaries |
| [Field workflows](survey-workflows.md) | Observation versus Direct Edit; geometry/GPS capture and review |
| [Architecture](architecture.md) | What exists, dependencies, planned library boundary |
| [Data model](data-model.md) | Generic geometry, observations, domain objects and identities |
| [Offline and sync](offline-sync.md) | Offline guarantees and proposed integration with DVMDB |
| [Renderer integration](renderer-lab.md) | Actual Qt Quick/bgfx implementation, ownership and metrics |
| [Android build and device tests](android-renderer-test.md) | Docker/ADB workflow, real-device checks and diagnostics |
| [Roadmap](roadmap.md) | Current priorities and acceptance criteria |

## Present status — 5 November 2026

**Implemented and exercised:** Qt Quick app; OpenGL/OpenGL ES bgfx direct
underlay with synthetic GPU lines/triangles; QML overlays and input; Android
ARM64 Docker APK; USB and paired Wi-Fi ADB deployment flow; Linux build;
platform-specific app labels and icon. GPU scene/camera and deterministic stress/percentile tests exist. A
selectable large-network benchmark harness and on-device CSV capture are
implemented but device performance has not yet been characterized.
Real-world Linux and Android visual operation has been reported and shown.

**Not yet accepted:** measured large-scene GPU/CPU/frame-pacing results; Android
rotation/suspend/context-recreation durability. A single successful phone
launch is not evidence of those cases.

**Not implemented:** real map tiles, geographic coordinate navigation, GPS,
feature capture, areas, observations, hydraulic direct edits, durable offline
survey storage, DVMDB synchronization, and OSM publication. iOS is an intended
future platform, not a current build target.

## Documentation rules

- **Implemented:** directly supported by current Mobile code or reported build
  and device results; distinguish source presence from device-tested behavior.
- **Agreed:** what the product must eventually do (e.g., both editing modes).
- **Proposed:** a design boundary or implementation approach that still needs
  work or validation.
- **Future:** outside current build and near-term acceptance priorities.

Keep the docs about **the product and code as they exist**. Do not accumulate
historic patch diaries, past build failures or superseded development plans.
The [roadmap](roadmap.md) is the only forward implementation sequence.
