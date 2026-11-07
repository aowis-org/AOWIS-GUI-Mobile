# AOWIS-GUI-Mobile

**AOWIS-GUI-Mobile** is the touch-first, offline-first field-survey application in
AOWIS. Its intended workspace is a map where field users capture, classify and
inspect **points, lines and areas**, either as **survey observations** or as
**direct edits** to an authorized AOWIS working copy. General geographic/OSM-like
surveying and hydraulic distribution share the geometry interface; hydraulic
drainage is a future domain.

## What works today

The repository builds and launches a **Qt Quick** application on Linux and
Android ARM64 whose Map page is the **AOWIS map** shared with the desktop GUI
(AOWIS-GUI-Render `MapBgfxMapView`): 2D Web Mercator imagery and the 3D globe
with terrain. Qt Quick provides the view switch, zoom and location controls. bgfx draws the map *under* the QML controls inside Qt
Quick's own OpenGL / OpenGL ES context. One finger pans, two fingers zoom and,
on the globe, turn and tilt; the mouse wheel zooms and the right button
orbits the globe on desktop.

Imagery tiles, terrain and the imagery provider catalog come from the
**aowis-server-map** configured under **Settings → Map server** (HTTP/HTTPS
URL and port); changing it reconnects at once. The top-left map menu controls GPS acquisition and optional auto-pan.
The location button re-centres the map on demand. See [the map](docs/map.md).

The hamburger menu opens **Settings → Appearance** (system/dark/light theme),
**Settings → Language** (English, Français, العربية, Deutsch, Español; live
change without restart), **Settings → Map server**, the instruments (compass,
gradienter, plumb bob, flashlight) and **Diagnostics** (map renderer state,
map server status, lifecycle recovery counters, map server reload).
The Android APK builds in a pinned Docker toolchain on Arch Linux.

**This is not yet a survey editor.** There is no hydraulic network on the
mobile map yet, no GPS-based survey capture, feature classification,
point/line/area editor, offline map package, local observation store or DVMDB
synchronization in this repository yet.

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

For new clones, initialize all pinned submodules, including Render's nested
bgfx/bx/bimg revisions (the same ones the desktop GUI builds):

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

The updater never advances Render's nested third-party repositories beyond
the commits the new Render revision records. `./git_submodule_update.sh --all`
also advances MODEL. `./git_submodule_init.sh --render`, `--model`,
`--geographiclib` and `--dvmdb` permit selective initialization.

A `.gitmodules` entry is not enough: the repository must also track a gitlink
(mode `160000`) for each submodule. The initialization/update scripts reject missing gitlinks
with a diagnostic. New clones should use `git clone --recurse-submodules`,
or run `./git_submodule_init.sh` afterwards.

Linux (installed Qt 6 including Qt Positioning, CMake, Ninja, C++ compiler):

```bash
./compile_linux.sh                # build and launch with the map
./compile_linux.sh --build-only   # build only
./compile_linux.sh --baseline     # separate QML-only build without the map renderer
./compile_linux.sh --tests        # run the map-math, settings and lifecycle tests
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
[the map](docs/map.md) for the map integration.

## What happens next

The hydraulic network on the mobile map (Render's network snapshot from the
working copy), then survey capture and editing.

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
secrets, Git metadata and nested submodule contents. The `external/AOWIS-GUI-Render`
submodule (with its bgfx) and the MODEL dependency are pinned separately by Git gitlinks.

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

The pinned `external/AOWIS-GUI-Render` submodule supplies the whole map
(`AOWIS::RenderMap`, `AOWIS::RenderMapRest` and their dependencies) and its
pinned bgfx. `aowis-mobile-shared-render-geometry` tests Render's geometry
headers directly.

The hamburger menu also provides a live Qt Sensors **magnetic compass** with
an animated compass rose. See [Compass](docs/compass.md). The independent [Gradienter](docs/gradienter.md)
adds a live two-axis gravity spirit level with a bubble vial and optional
relative zero reference.

## Shared native dependencies

MODEL, Render (with its pinned bgfx) and GeographicLib (Render's geodesy) are
compiled into the application. DVMDB is recorded as a pinned source dependency
for upcoming work but is not compiled into Mobile yet. On the first checkout, run
`./tools/init_geographiclib_dvmdb.sh`, then commit `.gitmodules` and the Git
gitlinks. See [dependency setup](docs/dependencies.md).
