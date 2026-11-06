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

The dial is driven by a **C++ noise-gated circular observer**. Raw magnetic
azimuth values are unwrapped through north; a short least-squares measurement
history estimates angular speed without differentiating consecutive noisy
samples. Two low-pass time scales distinguish sustained turns from stationary
magnetic fluctuations. A hysteretic motion gate holds the dial **completely
still** against small magnetic jitter, and releases after sustained movement.
During a turn, a bounded 90 ms angular-velocity prediction bridges gaps between
sensor reports. An analytically integrated first-order display follower advances
smoothly without the spring ringing and high-frequency vibration of the
previous velocity-error servo. Unlike the old implementation, velocity changes
are **not** injected directly from individual magnetic innovation residuals.
Noise suppression inevitably trades a small amount of sensitivity to extremely
slow turns for a stable instrument when stationary.

The compass rose uses a separate **continuous, unwrapped** C++ angle: its
rotation can pass from 359° through 360° to 361° without snapping back by a
full revolution. The numeric bearing remains normalized into 0–359°.

A persistent, indefinite **C++ `QAbstractAnimation`** invokes the display
follower on Qt's animation clock. With Qt Quick's threaded render loop the
animation clock is synchronized to scene-graph frames, rather than running
on an independent 16 ms `QTimer`. No per-sample animations restart, and
**no JavaScript smoothing is used**. The rose rotates as a cached QML Canvas
texture; its paint code does not rerun for heading changes. The numeric
readout and rose use the same C++ observer, with separate continuous and
normalized representations of heading.

The phone requests 60 Hz sensor readings, but the Android backend can still
supply fewer; the status panel displays the *observed* sensor rate to make
this distinction visible. An animation-clock improvement cannot overcome
hardware sensor latency or degraded magnetic calibration, so phone testing
remains essential. The first valid sample is displayed immediately; stopping,
backgrounding, reopening and loss of readings reset or invalidate the heading.
All filtering and prediction parameters are in `src/compass_heading_filter.h`.
Hardware-free regression tests cover alternating magnetic jitter at 20/60 Hz,
noisy real turns, turn-and-stop behavior, slow motion, frame-rate consistency
and continuous north crossing. These simulations are not a substitute for
real-device visual acceptance.

Sensors are started **on entering Compass while the app is active**, and
stopped when leaving the page or backgrounding AOWIS. A new reading is required
after every resume. Values older than four seconds are marked stale.
If a started sensor delivers no samples for six seconds, the UI reports that
explicitly instead of waiting forever. The Compass status card shows whether
the backend is active, the observed sample rate in Hz, and how many valid magnetic samples it has delivered in
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
