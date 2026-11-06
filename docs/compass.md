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

The dial uses a **Qt Quick FrameAnimation** synchronized with each rendered frame,
not a new 230-ms rotation animation for each magnetic sensor update. Its
frame-rate-independent angular follower filters small sensor jitter and
quickly tracks deliberate turns, always taking the short route across
359°/0°. It stops requesting frames once it has caught up. The digital bearing
and cardinal direction follow the same smoothed dial position, so the numbers
and rose agree. The follower snaps to the next valid reading when opening or
resuming the Compass page; stale readings are never replayed as animations.

Sensors are started **on entering Compass while the app is active**, and
stopped when leaving the page or backgrounding AOWIS. A new reading is required
after every resume. Values older than four seconds are marked stale. If the
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
status is intelligible. The existing C++ unit tests cover wrap-around, cardinal sector boundaries,
invalid input, calibration ranges and orientation addition. The optional Node
regression test exercises the very same JavaScript heading follower embedded
in QML: time-independent convergence, smoothing noisy inputs, short-path
359°/0° movement, large turns and pause/stall behavior.

The automated build/source checks cannot replace a real-phone sensor test.
