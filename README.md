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
Selectable synthetic networks (~1k to ~1M total node/pipe features) use
viewport tiles, bounded GPU buffers and explicit LOD sampling. A 15-second
benchmark saves frame-level CSV and frame-pacing percentiles.
The mobile UI now starts with a clean, map-first view. Its hamburger menu
opens **Settings → Appearance** (system/dark/light interface theme),
**Settings → Language** (English, Français, العربية, Deutsch, Español;
live change without restart), and
**Settings → Map server** (persistent HTTP/HTTPS URL and numeric port)
and **Diagnostics** (stress presets, recording, GPU recovery, zoom/recenter).
Settings reports automatically acquired foreground phone position,
provider status, latitude/longitude and reported accuracy. A small status pill
appears on the map. The sample is not persisted or
placed on the synthetic map.
These map-server settings only configure a future map client; they do **not** yet load
map imagery or establish a server connection.
The Android APK builds in a pinned Docker toolchain on Arch Linux and has been
launched on a physical Android phone. Linux rendering has also been exercised.

**This is not yet a survey editor.** The visible grid, pipes and junctions are a
synthetic rendering test scene—not actual georeferenced maps or saved network
objects. There is no GPS-based survey capture, feature classification, point/line/area editor,
offline map package, local observation store or DVMDB synchronization in this
repository yet. The stress harness has initial Linux/Android recordings; further controlled
performance and genuine context-loss testing remain open.

## Names and repository family

| Purpose | Repository | User-facing name |
| --- | --- | --- |
| Desktop application | `AOWIS-GUI` | AOWIS |
| Mobile application (this repo) | `AOWIS-GUI-Mobile` | **AOWIS Mobile** on Linux; **AOWIS** on Android |
| Shared rendering library | `AOWIS-GUI-Render` | Library, no launcher |

The Mobile executable/CMake target remains `aowis-mobile` and the Android
package ID remains `org.aowis.mobile`. Neither is a user-facing title. The
provided AOWIS icon is used by Linux/Android and has assets for future desktop
platforms. The shared library is linked by Mobile for bgfx device lifecycle. The full
production map/terrain renderer has not yet been moved out of Desktop GUI.

## Build

For existing checkouts, register the MODEL dependency **once** (the command
creates both `.gitmodules` metadata and the Git submodule gitlink):

```bash
./tools/init_model.sh
```

For new clones, initialize all pinned submodules, including bgfx.cmake's
nested bgfx/bx/bimg revisions:

```bash
./git_submodule_init.sh
```

To advance only the AOWIS renderer to its tracked upstream branch:

```bash
./git_submodule_update.sh
git diff --submodule=log
git add external/AOWIS-GUI-Render
# Review/test, then commit the new gitlink in Mobile.
```

The updater deliberately does **not** advance third-party bgfx.cmake or its
nested vendor repositories. `./git_submodule_update.sh --all` additionally
initializes the third-party dependencies at their recorded pins, without
advancing them. `./git_submodule_init.sh --render`, `--bgfx` and `--model` permit
selective initialization. `./git_submodule_update.sh --model` deliberately
advances the MODEL gitlink, while the default updater changes only Render. Changing the independently pinned bgfx.cmake release remains
an explicit operation through `./tools/init_bgfx.sh`.

A `.gitmodules` entry is not enough: the repository must also track a gitlink
(mode `160000`) for each submodule. The initialization/update scripts reject missing gitlinks
with a diagnostic. New clones should use `git clone --recurse-submodules`,
or run `./git_submodule_init.sh` afterwards.

Linux (installed Qt 6 including Qt Positioning, CMake, Ninja, C++ compiler):

```bash
./compile_linux.sh                # build and launch bgfx underlay
./compile_linux.sh --build-only   # build only
./compile_linux.sh --baseline     # separate QML-only comparison build
./compile_linux.sh --tests        # run scene/camera and stress/percentile tests
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
Android dependencies. See [interface languages](docs/localization.md) for live translations,
[device location](docs/location.md) for GPS/permissions and
[Android build and device guide](docs/android-renderer-test.md)
for setup, diagnostics and APK paths, and
[renderer integration](docs/renderer-lab.md) for the current GPU design.

## Collecting benchmark results (Linux + Android)

Record **15-second** captures using the app's **Record 15s** control on
Linux and Android. Then run one command from the repository root:

```bash
./benchmark.sh
```

This discovers Linux reports and every authorized USB/Wi-Fi Android device,
reads Android's debug-app private CSVs via `adb run-as`, and copies everything
into `benchmarks/`. It also retrieves available lifecycle event logs, creates
`benchmarks/summary.csv` and `benchmarks/summary.md`, and automatically updates
`../mobile.zip`. **Just upload that refreshed ZIP** for analysis—no ADB
extraction or manual copying required. Missing phones produce a warning rather
than preventing Linux results from being collected. Use `--strict` to require
all requested sources, or `--desktop-only`/`--android-only` as needed.

The collection script does not create new frame measurements: initiate captures
in the app first. Optionally, `./benchmark.sh --run-lifecycle` performs the
existing Android background/resume test before collecting logs. Details:
[renderer benchmark procedure](docs/renderer-lab.md).

## What happens next

**Next agreed work:** move the production map/terrain/HUD rendering code into
`AOWIS-GUI-Render`, retaining Desktop GUI, Android, and WebAssembly rendering
behavior. The shared device layer is already linked by Mobile; the Mobile
scene still uses its existing stress-test renderer. The real mapping and
survey tools follow that foundation.

Read the [documentation index](docs/README.md) or the
[current roadmap](docs/roadmap.md).

## Patch and source ZIP tooling

Tooling is **independent of the local checkout directory name**. The checkout
may be located anywhere and called anything; the scripts locate the Git project
root relative to themselves. Neither the patch validator nor source ZIP
creation compares archive paths with the checkout folder name.

Patches retain the `aowis-mobile-<description>.zip` filename convention and
contain repository-relative paths underneath the **fixed logical archive
namespace** `AOWIS-GUI-Mobile/`. That namespace identifies the repository in
the archive; it is not a required local directory name.

```bash
./tools/apply_patch.sh ~/Downloads/aowis-mobile-<description>.zip
./tools/zip_mobile_project.sh     # writes ../mobile.zip
```

The generated `mobile.zip` uses the same stable logical archive namespace,
regardless of what the checkout directory is called. Apply scripts copy files
relative to their own repository root, not to a derived directory name.

**One-time upgrade from the old patch script:** if the installed script still
validates patches against the checkout's folder name, extract the replacement
script first, then run the ordinary patch command. This also avoids a bug in
the older script when it overwrites its own executable during execution:

```bash
PATCH=~/Downloads/aowis-mobile-checkout-independent-patch-tooling.zip
unzip -p "$PATCH" AOWIS-GUI-Mobile/tools/apply_patch.sh > tools/apply_patch.sh.new
chmod +x tools/apply_patch.sh.new
mv -f tools/apply_patch.sh.new tools/apply_patch.sh
./tools/apply_patch.sh "$PATCH"
```

This upgrade procedure does not depend on the local checkout directory name.

Patch ZIPs may include `AOWIS-GUI-Mobile/.aowis-patch-delete.txt` containing
repository-relative file removals. Applying a patch refreshes file timestamps
and regenerates `mobile.zip`. The source snapshot excludes build outputs,
secrets, Git metadata and nested submodule contents. The `external/bgfx.cmake` and `external/AOWIS-GUI-Render`
submodules and the MODEL dependency are pinned separately by Git gitlinks.

License: Apache-2.0.

Android debug builds reuse a persistent signing key outside the checkout; see [Android build documentation](docs/android-renderer-test.md).

Android lifecycle validation: `./tools/test_android_lifecycle.sh --cycles 12`
checks background/resume and downloads private recovery logs; see
[Android device tests](docs/android-renderer-test.md).

## Shared production map geometry

The pinned `external/AOWIS-SERVER-MODEL` submodule exports `AOWIS::Model`,
which supplies the canonical quantity and unit-profile definitions used in
Settings. Register MODEL once with `./tools/init_model.sh` after applying the
MODEL integration patch; future clones automatically restore its gitlink.

The pinned `external/AOWIS-GUI-Render` submodule exports
`AOWIS::RenderGeometry` (continuous Mercator camera, globe icon visibility,
terrain-height cache and imagery fallback UV transforms) without Qt Widgets
or GUI network-model dependencies. `aowis-mobile-shared-render-geometry`
tests this public API alongside the existing Mobile lab camera/geometry tests.
The Mobile renderer still shows its established Qt Quick/bgfx stress scene;
this patch does not replace that scene or modify the Android EGL lifecycle.


The hamburger menu also provides a live Qt Sensors **magnetic compass** with
an animated compass rose. See [Compass](docs/compass.md). The independent [Gradienter](docs/gradienter.md)
adds a live two-axis gravity spirit level with a bubble vial and optional
relative zero reference.

## Shared native dependencies

MODEL, Render and bgfx are already used by the application. GeographicLib and
DVMDB are recorded as pinned source dependencies for upcoming work but are not
compiled into Mobile yet. On the first checkout, run
`./tools/init_geographiclib_dvmdb.sh`, then commit `.gitmodules` and the Git
gitlinks. See [dependency setup](docs/dependencies.md).
