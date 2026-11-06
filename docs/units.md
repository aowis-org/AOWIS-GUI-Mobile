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
in degrees. The map still shows synthetic geometry; map scale, hydraulic
inspectors and simulation tables have not yet been integrated into Mobile.

## MODEL dependency boundary

Until a pinned full MODEL checkout is available to both the Linux and Android
Mobile builds, `src/unit_registry/aowis/model/units/` contains the **three
unmodified, header-only unit definitions** from `AOWIS-SERVER-MODEL` bundled
with the Desktop GUI snapshot (Apache-2.0). Their definitions are identical
to those consumed by the desktop `UnitProfileManager`, including preset
inheritance, option lists and canonical identifiers. They must not be
edited independently: replace this snapshot with a direct MODEL dependency
once Mobile's model integration is available.

The local settings location is the Qt per-user AOWIS-GUI-Mobile settings
store. Custom profiles and the selected profile ID live under `units/`.
These settings do not modify MODEL or move between devices automatically.

Regression checks:

```bash
python3 tests/test_unit_settings.py
./compile_linux.sh --tests
```
