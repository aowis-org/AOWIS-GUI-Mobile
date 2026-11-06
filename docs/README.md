# AOWIS-GUI-Mobile documentation

The documentation distinguishes **implemented behavior**, **agreed product
requirements**, and **designs not yet implemented**. The application is real and
runs on Linux and Android, but its map currently shows a diagnostic GPU scene.
A capability in a related repository is not automatically integrated here.

| Document | What it answers |
| --- | --- |
| [Product](product.md) | What Mobile is for, scope, platform naming and product boundaries |
| [Magnetic compass](compass.md) | Live Qt Sensors bearing, instrument UI and calibration |
| [Gradienter](gradienter.md) | Two-axis gravity level, bubble vial, pitch/roll and relative zero |
| [Plumb Bob](plumb-bob.md) | Gravity-driven plumb line, upright wall/edge alignment, sensor behavior |
| [Flashlight](flashlight.md) | Android camera LED torch tool, permissions and lifecycle |
| [Interface languages](localization.md) | Live EN/FR/AR/DE/ES, persistent language choice, Arabic RTL and catalogs |
| [Device location](location.md) | Foreground GPS/location service, permission and fix semantics |
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
Foreground-only GPS/location acquisition is now implemented in source, with
Android permission declarations and automatic foreground acquisition; device accuracy
and permission interactions still require testing.

**Not yet accepted:** sustained large-scene GPU/CPU/frame-pacing results; Android
rotation/suspend/context-recreation durability. A recovery watchdog and
repeatable ADB Home/resume runner exist but are not yet device-validated. A single successful phone
launch is not evidence of those cases.

**Not implemented:** real map tiles, geographic coordinate navigation, geographic
GPS map marker or saved observation capture, feature capture, areas, observations, hydraulic direct edits, durable offline
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

## Performance report collection

Run `./benchmark.sh` after recording in-app benchmarks on Linux and Android.
It collects existing CSVs, summarizes them into `benchmarks/`, and recreates
`mobile.zip` with the reports included. See the
[renderer instructions](renderer-lab.md#automatic-collection-for-review).

