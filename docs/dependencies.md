# Native dependency submodules

Mobile uses Git submodules rather than vendored copies. Paths are repository-
relative and independent of the checkout directory name.

| Submodule | Git remote | Current use |
| --- | --- | --- |
| `external/AOWIS-SERVER-MODEL` | `https://github.com/aowis-org/AOWIS-SERVER-MODEL.git` | `AOWIS::Model` linked for units and domain model |
| `external/AOWIS-GUI-Render` | `https://github.com/aowis-org/AOWIS-GUI-Render.git` | bgfx GPU rendering components |
| `external/bgfx.cmake` | `https://github.com/bkaradzic/bgfx.cmake.git` | existing renderer bootstrap and shaders |
| `external/GeographicLib` | `https://github.com/geographiclib/geographiclib.git` | Pinned source dependency for future geo/coordinate APIs; not compiled or linked yet |
| `external/AOWIS-SERVER-DVMDB` | `https://github.com/aowis-org/AOWIS-SERVER-DVMDB.git` | Pinned source dependency for future native personal repository; not compiled, linked or started yet |

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
`./tools/init_model.sh` (once if its gitlink does not exist).

After the gitlinks are committed, future clones use:

```bash
./git_submodule_init.sh
```

To initialize either new dependency alone, use `--geographiclib` or `--dvmdb`.
`git_submodule_update.sh --dvmdb` intentionally changes the DVMDB pin to its
remote branch; avoid that unless reviewing/testing an update. GeographicLib
should be advanced deliberately to a tested release, not upstream development
HEAD. Always commit `.gitmodules` and all new/changed gitlinks together.

**DVMDB nested dependency note:** The DVMDB repository itself contains a
MODEL submodule whose upstream URL may use a machine-specific Git SSH alias.
Mobile currently only initializes DVMDB's top-level gitlink, not that nested
copy. This is intentional until its native storage library is integrated.
Do not blindly invoke `git submodule update --init --recursive` for DVMDB;
resolve that nested URL explicitly when the embedded DVMDB build is needed.

## Build boundary

Mobile still builds its existing Qt Quick/bgfx app plus `AOWIS::Model`. It does
not compile GeographicLib or DVMDB just because their source is present.
Linking DVMDB now would introduce native-store, filesystem and nested MODEL
requirements before the mobile working-copy design is implemented. Adding the
submodules early establishes a reproducible pin without doing that premature
integration. Runtime map/tile requests also remain unimplemented.

`tools/zip_mobile_project.sh` exports Mobile's own files and dependency metadata,
not source trees owned by Git submodules. The resulting `mobile.zip` is a source
snapshot, **not** a replacement for the real Git gitlinks.
