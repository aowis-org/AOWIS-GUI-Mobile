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

> **Repository status (2026-11-05):** This is a design-and-tooling repository.
> There is no mobile application, Qt build, data schema, GPS integration or
> synchronization implementation here yet. The documentation records the
> agreed product direction and clearly marked proposals for future work.

## Documentation

Start at [Documentation index](docs/README.md).

- [Product scope and capabilities](docs/product.md)
- [Field survey workflows and touch interface](docs/survey-workflows.md)
- [Architecture and AOWIS integration](docs/architecture.md)
- [Geographic features, observations and topology](docs/data-model.md)
- [Offline operation and synchronization](docs/offline-sync.md)
- [Implementation roadmap and acceptance criteria](docs/roadmap.md)

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
