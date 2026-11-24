# Native dependency submodules

Mobile uses Git submodules rather than vendored copies. Paths are repository-
relative and independent of the checkout directory name.

| Submodule | Git remote | Current use |
| --- | --- | --- |
| `external/AOWIS-SERVER-MODEL` | `https://github.com/aowis-org/AOWIS-SERVER-MODEL.git` | `AOWIS::Model` linked for units and domain model |
| `external/AOWIS-GUI-Render` | `https://github.com/aowis-org/AOWIS-GUI-Render.git` | The AOWIS map (2D/globe, imagery, terrain, HUD, map server client) and its nested pinned bgfx/bx/bimg |
| `external/GeographicLib` | `https://github.com/geographiclib/geographiclib.git` | Render's geodesy; built by Render from this checkout (`AOWIS_RENDER_GEOGRAPHICLIB_SOURCE_DIR`) |
| `external/AOWIS-SERVER-DVMDB` | `https://github.com/aowis-org/AOWIS-SERVER-DVMDB.git` | Pinned source dependency for future native personal repository; not compiled, linked or started yet |
| `external/AOWIS-SERVER-EPANET` | `https://github.com/aowis-org/AOWIS-SERVER-EPANET.git` | Pinned source dependency for future hydraulic simulation; not compiled or linked yet |
| `external/AOWIS-CONFIG` | `https://github.com/aowis-org/AOWIS-CONFIG.git` | `aowis::config`: configuration storage (owner root `gui-mobile`), currently the WireGuard profiles |
| `external/AOWIS-NETWORK` | `https://github.com/aowis-org/AOWIS-NETWORK.git` | `AOWIS::Network` / `AOWIS::NetworkQt`: embedded userspace WireGuard engine (Go) behind a loopback SOCKS5 proxy |
| `external/AOWIS-SERVER-MAP` | `https://github.com/aowis-org/AOWIS-SERVER-MAP.git` | Required `AOWIS::ServerMap` core library; direct in-process renderer transport |

The GeographicLib path and URL match those used in Desktop AOWIS-GUI. Git
records each dependency's exact commit as a *gitlink*, not as copied source.

## First registration and subsequent checkouts

For a **new** dependency after applying a source patch, in the actual Mobile Git checkout:

```bash
./tools/init_geographiclib_dvmdb.sh
```

This uses `git submodule add`, creating `.gitmodules` entries and real gitlinks.
Do not manually create the `external/` directories first. It is safe to repeat
and does not update existing pins. The existing MODEL registration remains
`./tools/init_model.sh` (once if its gitlink does not exist). EPANET is
registered once with `./tools/init_epanet.sh`, initialized with
`./git_submodule_init.sh --epanet`, and deliberately advanced with
`./git_submodule_update.sh --epanet` (excluded from `--all`, just like DVMDB).
NETWORK is registered once with `./tools/init_network.sh`, initialized with
`./git_submodule_init.sh --network` and advanced deliberately with
`./git_submodule_update.sh --network` (also part of `--all`); CONFIG the same
way with `./tools/init_config.sh` and `--config`.

After the gitlinks are committed, future clones use:

```bash
./git_submodule_init.sh
```

To initialize individual source dependencies, use `--geographiclib`, `--dvmdb` or `--epanet`.
`git_submodule_update.sh --dvmdb` intentionally changes the DVMDB pin to its
remote branch; avoid that unless reviewing/testing an update. GeographicLib
should be advanced deliberately to a tested release, not upstream development
HEAD. Always commit `.gitmodules` and all new/changed gitlinks together.

**Nested dependency note:** DVMDB and EPANET are currently initialized only
at their top-level gitlinks, not recursively. DVMDB has a nested MODEL submodule
whose upstream URL may use a machine-specific Git SSH alias; resolve that
before integrating its native library. Do not blindly run recursive updates
on source-only dependencies.

## Build boundary

Mobile builds its Qt Quick app with `AOWIS::Model`, `aowis::config`, Render's map stack,
GeographicLib (through Render) and AOWIS-NETWORK's WireGuard engine
(`AOWIS_MOBILE_WITH_WIREGUARD`, default ON). The engine is Go code: Linux
builds compile it with the host Go during the CMake build; the Android
Docker build cross-compiles it first (`GOOS=android GOARCH=arm64`, the NDK's
`aarch64-linux-android28-clang`, `-buildmode=c-shared`, 16 KiB pages) with
NETWORK's `tools/build_wireguard_engine.sh` and passes the result as
`AOWIS_NETWORK_WIREGUARD_ARCHIVE`; CMake packages it into the APK through
`QT_ANDROID_EXTRA_LIBS`. A Go c-archive cannot be used on Android: it uses
local-exec TLS, which cannot be linked into the app's shared library. Go
module and build caches persist in `$XDG_CACHE_HOME/aowis-mobile/go`. It does not compile DVMDB or EPANET just because their
sources are present.
Linking DVMDB now would introduce native-store, filesystem and nested MODEL
requirements before the mobile working-copy design is implemented. EPANET's
solver integration is likewise postponed until local simulation is implemented.
Adding these source-only submodules establishes reproducible pins without
premature build or runtime dependencies.

`tools/zip_mobile_project.sh` exports Mobile's own files and dependency metadata,
not source trees owned by Git submodules. The resulting `mobile.zip` is a source
snapshot, **not** a replacement for the real Git gitlinks.

## Required embedded MAP library

`external/AOWIS-SERVER-MAP` is registered with:

```bash
./tools/init_map.sh
```

Commit `.gitmodules` and the actual MAP gitlink. Later checkouts use
`./git_submodule_init.sh --map`; deliberate upgrades use
`./git_submodule_update.sh --map`. MAP reuses Mobile's `aowis::config` target;
its nested CONFIG checkout is not needed. MAP is not advanced by `--all`.

MAP is linked into every Linux and Android build, including the QML-only
baseline. On Arch Linux install the native MAP dependencies (in addition to
the usual Mobile toolchain):

```bash
sudo pacman -S --needed libtiff libpng libjpeg-turbo zstd zlib pkgconf
./compile_linux.sh --build-only --tests
```

The build links `AOWIS::ServerMap`, not the MAP HTTP executable. With
`--tests`, the Linux test suite also runs
`aowis-mobile -platform offscreen --check-embedded-map` in a temporary
storage root. The check exercises MAP's own SettingsRepository, including
provider settings and cascade persistence, and initializes raster and terrain
with remote fetching disabled. It never changes the real app settings.

For Android:

```bash
./compile_android.sh --build-only
```

The existing Docker/NDK toolchain builds ARM64 static PNG, TurboJPEG, Zstd
and TIFF libraries in `build-android-bgfx/map-native/`. Their
release-tag source checkouts are retained, their resolved commit IDs recorded
in `source-revisions.txt`, and all archive members checked for AArch64. NDK
zlib is used; TIFF retains lossless LZW/Deflate support. No host libraries are
used. The first build requires Internet access to the dependency repositories;
subsequent builds reuse the checkouts. The baseline uses the same machinery
in `build-android-baseline/map-native/`.

This stage does **not** change the renderer transport, move the current MAP
endpoint into cascade settings, or add provider editing UI. Those require
wiring the MAP catalog and tile/terrain adapters to the renderer, including
fail-closed WireGuard routing, and then exposing MAP's settings APIs in QML.
