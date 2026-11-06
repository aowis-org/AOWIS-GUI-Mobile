# Device location (GPS-01)

## Implemented behavior

Mobile uses Qt 6 Positioning (`QGeoPositionInfoSource`) and Qt's foreground-only
`QLocationPermission`. The backend may return GNSS, Wi-Fi or cellular location:
**a valid phone position is not necessarily a satellite-only GPS fix.**

Location acquisition **starts automatically whenever AOWIS is in the foreground**,
including on first launch and after app restarts. There is no in-app enable/stop
toggle. Android requests foreground location permission when needed; the user
can grant precise/approximate access or deny it. The operating system remains
in charge of access. The app declares `ACCESS_FINE_LOCATION` and
`ACCESS_COARSE_LOCATION` but **not** background location. If access is denied,
Settings displays the status; the user can grant it later through Android's app
permissions. GPS must not be shown as available without a valid provider fix.

Updates run only while AOWIS is foreground/active. Backgrounding stops the Qt
location provider; resuming rechecks permission and starts new fixes. The UI
marks a previously received position stale until a new update arrives. A
fix older than 15 seconds is stale rather than silently shown as current.

The Settings panel shows the most recent valid latitude, longitude, UTC
measurement timestamp, fix age, reported horizontal/vertical accuracy, altitude,
speed and heading. Unknown metrics are shown as **unavailable**, never as
fabricated zero. Heights are shown as reported by the active provider; no
vertical-datum conversion or guarantee of survey-grade accuracy is made.

A small status pill is visible at the bottom-left of the map while the map
is open, showing acquisition, accuracy or permission/provider errors. Tapping
it opens Settings. The current map is synthetic
and has no geographic camera projection, so this version deliberately **does
not place a potentially misleading location marker on that scene**.

Location coordinates are kept **in memory only**. They are neither saved to
QSettings nor uploaded to the map server, included in benchmark captures, or
written to the Mobile source snapshot. This service does not record GPS tracks
or survey observations yet.

## Build dependencies

Linux needs Qt 6 Positioning (`qt6-positioning` on Arch), in addition to the
existing Qt packages. Android uses a new pinned Docker toolchain revision with
the official Qt Positioning module for both host and Android ARM64. The first
build of that revision provisions the dependency; later builds reuse the
image. The Qt-managed Android Manifest is retained, with precise/coarse
permissions declared via `qt_add_android_permission()`.

## Validation

Run:

```bash
./compile_linux.sh --tests
./compile_android.sh --run
```

On Android, start with permission undetermined and verify AOWIS requests
foreground access at launch (without a separate Enable action). Grant precise
or approximate access; wait outdoors for a fix, check coordinate changes and
reported accuracy. Background/resume the app and verify that it requests fresh
updates rather than representing an old fix as current. Restart AOWIS: location
must acquire automatically again. Revoke permission in Android Settings and
verify a clear status without a crash; returning from Settings should recheck
permissions. If a receiver is unavailable, the app should show a provider
status rather than fake coordinates.

Code/unit tests validate optional measurement semantics and sample validity;
actual satellite/provider performance and Android permission UX require phone
verification.

## Future integration

- After shared geographic map rendering exists, transform valid WGS84
  latitude/longitude into the actual map camera and draw a position/accuracy
  indication, with source timestamp and accuracy.
- Survey points and traces will require explicit user saving into a durable
  observation store; never silently persist continuously collected location.
- Future external GNSS/RTK support will need a documented provenance and
  precision contract. Do not invent it in this initial positioning service.
