# AOWIS Mobile

AOWIS Mobile is the planned **offline-first geographic field survey and mapping
application** for the AOWIS ecosystem. The map is the main workspace: a surveyor
can capture or edit points, lines and areas, identify what each feature
represents, and work with both general geographic information and AOWIS
hydraulic-distribution assets. Hydraulic drainage is a later domain.

Two complementary workflows are in scope:

- **Direct editing:** make deliberate edits to actual objects in an editable
  AOWIS working copy, following the typed model and its connectivity rules.
- **Survey observations:** collect measured positions, classifications, photos
  and notes without immediately changing authoritative geographic/hydraulic
  objects; observations can subsequently be reviewed and integrated.

These workflows share map navigation, geometry tools, GPS input, feature
selection and attribute editing, but they **do not share the same persistence
semantics**.

> **Current implementation (2026-11-05):** The runnable **AOWIS** application
> uses Qt Quick with bgfx direct-underlay rendering on Linux and Android.
> The GPU scene and QML interaction have been visually verified on both platforms,
> including a real Android phone. The current on-screen map-like grid, hydraulic
> lines and controls remain **development diagnostics**, not survey or hydraulic
> editing functionality. GPS, OSM, field observations, areas, persistent editing
> and synchronization have not yet been implemented.

## Build and run AOWIS

On Arch Linux, the **native desktop build** uses the installed Qt 6
(`qt6-base`, `qt6-declarative`), CMake and Ninja. With the temporary pinned bgfx
submodule already initialized, build and launch the GPU underlay using:

```bash
./compile_linux.sh               # optimized bgfx underlay build + launch
./compile_linux.sh --build-only  # build without opening the application
./compile_linux.sh --baseline    # QML-only comparison (separate build dir)
./compile_linux.sh --tests       # additionally run the CPU geometry test
```

For a new checkout, run `./tools/init_bgfx.sh` once before the default bgfx
build. It is not needed for `--baseline`. The script builds only the mobile
application target and its required dependencies by default; tests are opt-in.
Build type defaults to `RelWithDebInfo` (override using `AOWIS_BUILD_TYPE=Debug`),
and `AOWIS_LINUX_JOBS` controls parallel jobs. Builds stay in
`build-linux-bgfx` or `build-linux-lab`, independently of Android's Docker build.

For Android ARM64, use the **Docker toolchain** (the same build workflow
pattern as GUI WebAssembly and Standalone Windows):

```bash
./tools/init_bgfx.sh               # once: temporary pinned bgfx source submodule
./compile_android.sh               # prepare Docker image if missing; build APK
./compile_android.sh --build-only  # build without installing on a phone
./compile_android.sh --baseline    # compare QML-only build
./compile_android.sh --run         # require one authorized USB or paired Wi-Fi device
./compile_android.sh --devices     # show USB/Wi-Fi ADB devices without building
./compile_android.sh --wifi        # prefer/reconnect paired wireless debugging
./compile_android.sh --wifi-pair PHONE_IP:PAIR_PORT  # pair once, then build/deploy
```

The supplied AOWIS gear/drop artwork is the application icon for Android and
all supported desktop targets. Its master and platform assets live under
`assets/icons/`; Android launcher icons live under `android/res/`.

No Qt installer, Qt account, Android Studio, Android SDK, NDK or Java installation
on the host is necessary. The image downloads the official open-source Qt
Android and matching host toolchains using `aqtinstall`, and Google's Android
command-line SDK. **Docker** is required on the host; `adb` from Arch's
`android-tools` is optional for device deployment. The first image build needs
network access and disk space; subsequent compilations reuse the image and a
persistent Gradle cache. Run `./prepare_android_docker.sh` to rebuild the
image explicitly. See the [Android build and device-test guide](docs/android-renderer-test.md).

The installed Android application is named **AOWIS** with package ID
`org.aowis.mobile`; the Linux executable and window title are also **AOWIS**.
This supersedes the former `org.aowis.mobile.renderlab` package ID, so the
previously installed probe may remain as a separate app until uninstalled.
See [Rendering diagnostics and remaining integration tests](docs/renderer-lab.md).
The diagnostic map is **not** a working mobile field editor yet.

## Documentation

Start at [Documentation index](docs/README.md).

- [Product scope and capabilities](docs/product.md)
- [Field survey workflows and touch interface](docs/survey-workflows.md)
- [Architecture and AOWIS integration](docs/architecture.md)
- [Geographic features, observations and topology](docs/data-model.md)
- [Offline operation and synchronization](docs/offline-sync.md)
- [Implementation roadmap and acceptance criteria](docs/roadmap.md)
- [Qt Quick / bgfx renderer laboratory](docs/renderer-lab.md)
- [Android renderer build and device validation](docs/android-renderer-test.md)

## Patch and source ZIP tooling

Apply a mobile-only patch from the repository root:

```bash
./tools/apply_patch.sh ~/Downloads/aowis-mobile-<description>.zip
```

Patch ZIPs must contain files under `AOWIS-Mobile/` and use the
`aowis-mobile-*.zip` filename prefix. Patches may contain an optional
`AOWIS-Mobile/.aowis-patch-delete.txt` manifest with one repository-relative
file path per line for removing obsolete files. The manifest is deleted after
application. If a patch was applied by an older script, run
`./tools/apply_patch.sh --finish` to process a pending manifest.

A successful patch automatically refreshes `../mobile.zip`. To create the
snapshot explicitly, or write to a different destination:

```bash
./tools/zip_mobile_project.sh
./tools/zip_mobile_project.sh /path/to/mobile-source.zip
```

Snapshots include tracked and non-ignored untracked source files, without Git
metadata, generated build outputs, credentials, or nested submodule contents.
Submodules are maintained and archived as separate repositories.
