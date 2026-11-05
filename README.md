# AOWIS-Mobile
Mobile Android and iOS App for the AOWIS System

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
