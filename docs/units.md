# Display units — Mobile

AOWIS Mobile uses the same **AOWIS MODEL unit-profile definitions** as Desktop
Settings > Units: one canonical profile plus CMH, LPS, LPM, MLD, CMD, CMS,
CFS, GPM, MGD, IMGD and AFD. These are **display preferences**, not network
units and not EPANET import/export settings.

In **Settings**, Language appears first, followed immediately by **Units**:

- **Unit profile** selects one of the predefined or saved custom profiles.
  Selection changes apply immediately and are saved across application
  restarts.
- **Detailed unit settings…** opens the complete searchable, grouped list of
  currently presented hydraulic, geometric, quality and electrical quantities,
  matching the Desktop Units settings field-selection rules.
- Built-in profiles are read-only. **New custom profile** copies the currently
  selected profile; a custom profile may then be renamed, deleted or have
  individual quantities changed. Units are chosen from the MODEL-defined
  allowed choices, with canonical unit shown alongside each field.
- Android Back and desktop Escape return from detailed Units to Settings;
  from Settings they return to the map. Appearance/Theme is the last section.

`UnitProfileSettings` is a renderer-independent Qt adapter. It notifies all
unit-aware Qt Quick bindings when the selected profile or an individual choice
changes; no restart, scene reload, hydraulic edit, commit or DB write occurs.
**Current live unit-aware values** are GPS accuracy (distance), GPS altitude
(altitude) and GPS reported speed (velocity), as available. Coordinates are
angular degrees and do not change. Compass and inclinometer angles stay
in degrees. The map scale bar is currently always metric; hydraulic
inspectors and simulation tables have not yet been integrated into Mobile.

## MODEL dependency boundary

The `external/AOWIS-SERVER-MODEL` Git submodule is the **single authoritative
source** for canonical quantities, allowed units, predefined profiles and
inheritance rules. Mobile links `AOWIS::Model` in both Linux and Android builds;
there are no copied MODEL unit headers. Mobile owns only the QSettings / QML
presentation adapter and its per-device customizations. The C++ unit contract
test uses the real MODEL target, not a duplicated fixture.

To register MODEL once in an existing checkout:

```bash
./tools/init_model.sh
```

Future clones use `./git_submodule_init.sh` (or `--model`) to restore the pinned
commit. To intentionally advance MODEL later, use
`./git_submodule_update.sh --model` and commit the updated gitlink after testing.

The local settings location is the Qt per-user AOWIS-GUI-Mobile settings
store. Custom profiles and the selected profile ID live under `units/`.
These settings do not modify MODEL or move between devices automatically.

Regression checks:

```bash
python3 tests/test_unit_settings.py
./compile_linux.sh --tests
```
