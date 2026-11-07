# AOWIS-GUI-Mobile documentation

The documentation distinguishes **implemented behavior**, **agreed product
requirements**, and **designs not yet implemented**. The application is real and runs on Linux and Android. The Map page is the
AOWIS map (Render's `MapBgfxMapView`) with imagery and terrain from the
configured aowis-server-map (awaiting build and device acceptance). A
capability in a related repository is not automatically integrated here.

| Document | What it answers |
| --- | --- |
| [Product](product.md) | What Mobile is for, scope, platform naming and product boundaries |
| [The map](map.md) | Render's map in Qt Quick, map server, gestures, Qt Quick controls, lifecycle |
| [Magnetic compass](compass.md) | Live Android magnetic heading, instrument UI and calibration |
| [Gradienter](gradienter.md) | Two-axis gravity level, bubble vial, pitch/roll and relative zero |
| [Plumb Bob](plumb-bob.md) | Gravity-driven plumb line, upright wall/edge alignment, sensor behavior |
| [Flashlight](flashlight.md) | Android camera LED torch tool, permissions and lifecycle |
| [Display unit profiles](units.md) | Built-in/custom MODEL unit profiles, searchable detailed settings and live GPS formatting |
| [Interface languages](localization.md) | Live EN/FR/AR/DE/ES, persistent language choice, Arabic RTL and catalogs |
| [Device location](location.md) | Foreground GPS/location service, permission and fix semantics |
| [Field workflows](survey-workflows.md) | Observation versus Direct Edit; geometry/GPS capture and review |
| [Architecture](architecture.md) | What exists, dependencies, planned library boundary |
| [Data model](data-model.md) | Generic geometry, observations, domain objects and identities |
| [Offline and sync](offline-sync.md) | Offline guarantees and proposed integration with DVMDB |
| [Android build and device tests](android-renderer-test.md) | Docker/ADB workflow, real-device checks and diagnostics |
| [Roadmap](roadmap.md) | Current priorities and acceptance criteria |

## Present status — 7 November 2026

**Implemented in source (needs build and device acceptance):** the AOWIS map
on the Map page (2D imagery and globe terrain from aowis-server-map, Qt Quick controls,
touch gestures) drawn into Qt Quick's OpenGL / OpenGL ES context; Render's
pinned bgfx replaces the former `bgfx.cmake` test dependency. The 360° photo
experiment and the synthetic GPU test map with its benchmark harness are
removed.

**Previously implemented and exercised:** Qt Quick app with a bgfx underlay
in Qt's EGL context (the pattern the map now uses); QML overlays and input;
Android ARM64 Docker APK; USB and paired Wi-Fi ADB deployment flow; Linux
build; platform-specific app labels and icon. Foreground-only GPS/location
acquisition with Android permission declarations; device accuracy and
permission interactions still require testing.

**Not yet accepted:** map frame pacing and memory on phones; Android
rotation/suspend/context-recreation durability of the map. A recovery watchdog and
repeatable ADB Home/resume runner exist but are not yet device-validated. A single successful phone
launch is not evidence of those cases.

**Not implemented:** the hydraulic network on the mobile map, saved observation capture, feature capture, areas, observations, hydraulic direct edits, durable offline
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

- [Native dependencies and submodules](dependencies.md)
