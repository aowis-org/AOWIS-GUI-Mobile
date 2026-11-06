# Magnetic compass

## Current behavior

The hamburger menu has a **Compass** page. Its graduated, animated vector
compass rose is drawn by Qt Quick and **does not use the shared bgfx renderer**,
external image assets, icon fonts, GPS movement heading or map data.

The needle index is fixed to the top of the screen; the magnetic compass rose
rotates beneath it as the phone turns. The page shows a continuously smoothed magnetic bearing
in degrees, eight-way cardinal direction, a calibration indicator and live
sensor status. The bearing is **magnetic north**, not true/geographic north;
no declination correction is guessed. A compass measures orientation even
while stationary, unlike the GPS course-over-ground field in Location settings.
The reading is defined relative to the phone's native top edge; the app also
compensates for screen rotation based on the Qt screen orientation.

The dial is driven by a **C++ circular One Euro filter**, adapted from
Casiez, Roussel and Vogel (CHI 2012, DOI 10.1145/2207676.2208639;
https://gery.casiez.net/publications/CHI2012-casiez.pdf). This filter uses a
low cutoff at rest to reject magnetic jitter and raises the cutoff as angular
velocity increases to minimize lag. Shortest-arc angular differences handle
359° → 1° and long continuous rotations without discontinuities. Actual
monotonic sensor sample times (not an assumed fixed sampling period) control
the filter.

An independent **16-ms C++ display timer** follows the filtered magnetic
bearing using a time-based, shortest-arc response. This provides intermediate
frames even on phones that report magnetometer readings around 20 Hz. The
numeric readout and rose use the *same displayed heading*. QML only binds to
the C++ property; **no JavaScript smoothing and no per-sample Qt Quick
RotationAnimation** exist. The first sample appears immediately, and the
filter resets on every pause/resume to avoid carrying a stale heading into a
new sensor session. A stopped sensor or expired fix is never extrapolated into
a fake direction. Filter tuning constants are in
`src/compass_heading_filter.h` and need real-hardware acceptance.

Sensors are started **on entering Compass while the app is active**, and
stopped when leaving the page or backgrounding AOWIS. A new reading is required
after every resume. Values older than four seconds are marked stale.
If a started sensor delivers no samples for six seconds, the UI reports that
explicitly instead of waiting forever. The Compass status card shows whether
the backend is active and how many valid magnetic samples it has delivered in
the current visit. These numbers distinguish missing sensor updates from any
visual dial animation problem. If the
phone has no supported compass sensor, the UI explicitly reports this and does
not display an invented bearing. There is no separate runtime permission for
Android's magnetic compass. No compass readings are transmitted, persisted or
added to benchmark CSV files.

Poor calibration is indicated rather than presenting false precision. Nearby
magnetic fields, steel surfaces or protective cases can degrade accuracy.
The figure-eight movement hint can help Android recalibrate a sensor.

## Build dependencies

Linux requires Qt Sensors (`qt6-sensors` on Arch), alongside existing Qt
Positioning. Android Docker image revision 5 installs the matching Qt 6.12.0
`qtsensors` module for both Linux host tools and Android ARM64. No manual
Android Manifest changes or additional permissions are needed.

## Testing

```bash
./compile_linux.sh --tests
./compile_android.sh --run
```

On an Android phone, open **☰ → Compass**, turn the phone slowly through
north/east/south/west, and verify smooth movement and matching bearings.
Check screen rotation, background/resume, and Android Back navigation. Verify
there is no fake heading on devices lacking the sensor, and that calibration
status is intelligible. The C++ unit tests cover wrap-around, cardinal sector boundaries,
invalid input, calibration ranges, stationary noise, slow and fast turns,
variable sample rates, 60/120Hz display pacing, and lifecycle reset. Source
regression checks prevent a return to per-sample animations and frame-gated
JavaScript smoothing.

The automated build/source checks cannot replace a real-phone sensor test.
