# AOWIS-GUI-Mobile

**AOWIS-GUI-Mobile** is the touch-first, offline-first field-survey application in
AOWIS. Its intended workspace is a map where field users capture, classify and
inspect **points, lines and areas**, either as **survey observations** or as
**direct edits** to an authorized AOWIS working copy. General geographic/OSM-like
surveying and hydraulic distribution share the geometry interface; hydraulic
drainage is a future domain.

## What works today

The repository already builds and launches a **Qt Quick + bgfx** application on
Linux and Android ARM64. bgfx draws real GPU geometry *under* QML controls using
Qt's OpenGL/EGL context. The app has mouse/touch pan, pinch/wheel zoom, a QML
baseline, frame diagnostics, and Android application-lifecycle counters.
The Android APK builds in a pinned Docker toolchain on Arch Linux and has been
launched on a physical Android phone. Linux rendering has also been exercised.

**This is not yet a survey editor.** The visible grid, pipes and junctions are a
synthetic rendering test scene—not actual georeferenced maps or saved network
objects. There is no GPS capture, feature classification, point/line/area editor,
offline map package, local observation store or DVMDB synchronization in this
repository yet. Sustained rendering load and reliable Android context recovery
have not yet passed acceptance tests.

## Names and repository family

| Purpose | Repository | User-facing name |
| --- | --- | --- |
| Desktop application | `AOWIS-GUI` | AOWIS |
| Mobile application (this repo) | `AOWIS-GUI-Mobile` | **AOWIS Mobile** on Linux; **AOWIS** on Android |
| Planned common rendering library | `AOWIS-GUI-Render` | Library, no launcher |

The Mobile executable/CMake target remains `aowis-mobile` and the Android
package ID remains `org.aowis.mobile`. Neither is a user-facing title. The
provided AOWIS icon is used by Linux/Android and has assets for future desktop
platforms. Renaming the repository does **not** imply the renderer has already
been extracted from Desktop GUI.

## Build

Initialize the temporary pinned bgfx submodule for a new checkout:

```bash
./tools/init_bgfx.sh
```

Linux (installed Qt 6, CMake, Ninja, C++ compiler):

```bash
./compile_linux.sh                # build and launch bgfx underlay
./compile_linux.sh --build-only   # build only
./compile_linux.sh --baseline     # separate QML-only comparison build
./compile_linux.sh --tests        # build and run C++ scene/camera tests
```

Android ARM64 (Docker on host; optional Arch `adb` for deployment):

```bash
./compile_android.sh              # Docker build; deploy if one authorized device exists
./compile_android.sh --build-only # APK without ADB
./compile_android.sh --devices    # list USB / Wi-Fi ADB devices
./compile_android.sh --run        # require device, install and launch
./compile_android.sh --wifi       # prefer paired wireless device
./compile_android.sh --wifi-pair PHONE_IP:PAIR_PORT
./compile_android.sh --wifi-connect PHONE_IP:CONNECT_PORT
```

No Qt Online Installer, host Android SDK/NDK or host Java installation is
required for Android builds. ADB authorization/pairing still requires consent
on the phone. The Docker toolchain is pinned; its first build downloads Qt and
Android dependencies. See [Android build and device guide](docs/android-renderer-test.md)
for setup, diagnostics and APK paths, and
[renderer integration](docs/renderer-lab.md) for the current GPU design.

## What happens next

**Next agreed work:** (1) configurable GPU scene sizes and credible Android/Linux
performance measurements; (2) Android orientation, pause/resume, lock/unlock and
EGL/scene-graph recovery tests. After these pass, define and extract the shared
renderer into `AOWIS-GUI-Render` without regressing Desktop GUI/WASM behavior.
The real mapping and survey tools follow that foundation.

Read the [documentation index](docs/README.md) or the
[current roadmap](docs/roadmap.md).

## Patch and source ZIP tooling

The current checked-in tooling accepts `aowis-mobile-<description>.zip` patches,
even though the **canonical repository name** is now `AOWIS-GUI-Mobile`.
The archive's top-level directory **must exactly match the local checkout
directory name** (for example `AOWIS-GUI-Mobile/` after a local rename).
The scripts derive that directory from the checkout; a repository rename does
not by itself rename the accepted patch filename prefix or `../mobile.zip`.
These tooling names are not the application identity.

```bash
./tools/apply_patch.sh ~/Downloads/aowis-mobile-<description>.zip
./tools/zip_mobile_project.sh         # writes ../mobile.zip
```

Patch ZIPs may include `.aowis-patch-delete.txt` under the repository root for
repository-relative file removals. Applying a patch refreshes timestamps and
regenerates `mobile.zip`. The source snapshot excludes build outputs, secrets,
Git metadata and nested submodule contents. The `external/bgfx.cmake` submodule
is pinned separately in Git.

License: Apache-2.0.
