# Magnetic compass

## Current behavior

The hamburger menu has a **Compass** page. Its graduated, animated vector
compass rose is drawn by Qt Quick and **does not use the shared bgfx renderer**,
external image assets, icon fonts, GPS movement heading or map data.

Four fixed index triangles sit at the top, right, bottom and left of the
bezel; the top one reads the heading. The needle index is fixed to the top of the screen; the magnetic compass rose
rotates beneath it as the phone turns. The page shows a continuously smoothed magnetic bearing
in degrees, eight-way cardinal direction, a calibration indicator and live
sensor status. The bearing is **magnetic north**, not true/geographic north;
no declination correction is guessed. A compass measures orientation even
while stationary, unlike the GPS course-over-ground field in Location settings.
The reading is defined relative to the phone's native top edge; the app also
compensates for screen rotation based on the Qt screen orientation.

### Sensor source

The compass reads the Android sensors **directly through the NDK**
(`ASensorManager` on a private `ALooper` thread,
`src/compass_sensor_source.cpp`); it does not use Qt Sensors. Qt's Android
`QCompass` derived azimuth from each raw accelerometer packet plus the
magnetometer: every hand tremor and hand acceleration tilted that solution.
In simulation, 2° tremor with 0.8 m/s² hand acceleration swings it by up to
~9°.

Sources, best first (the status card names the active one and lists which
sensors the phone has):

1. **Android fused rotation vector** (`TYPE_ROTATION_VECTOR`), ~100 Hz.
   If it is listed but silent for 2.5 s, the next source is used.
2. **AOWIS fusion with gyroscope** (`src/compass_fusion.h`): the gyroscope is
   integrated at full rate; the orientation is pulled towards the
   accelerometer/magnetometer solution (TRIAD) with a 2 s time constant,
   slowed further while |a| ≠ g (shaking) or |B| jumps (steel, magnets).
3. **AOWIS fusion without gyroscope**: gravity and field are low-pass
   filtered (200 ms / 150 ms) *before* TRIAD, then the heading filter averages
   further.

Simulated hand-held phone, same tremor and hand acceleration:

| Input | mean error | max error |
| --- | --- | --- |
| Qt-style raw accelerometer + magnetometer | 4.4° | 9.4° |
| AOWIS fusion, no gyroscope | 0.6° | 1.8° |
| AOWIS fusion with gyroscope | 0.26° | 0.56° |

Without any accelerometer or magnetometer the page reports that no compass
sensor is available. Other platforms have no compass backend.

The heading of the **screen-top edge** is computed from the rotation
quaternion (`headingFromRotationVector` in `src/compass_heading.h`). Held flat
it is insensitive to roll and pitch tremor; tilting towards upright hands
over smoothly to the back-camera direction, so the reading stays valid when
the phone is held up like a sighting compass. The screen-orientation
correction is applied per sample.

Calibration is reported from the magnetometer's Android accuracy status;
where the rotation vector reports an estimated heading accuracy, it is shown
as **±N°**.

### Filter

The dial is driven by an **adaptive "One Euro" filter** on the unwrapped
angle (`src/compass_heading_filter.h`). Its cut-off frequency rises with the
turn rate: while still, noise is averaged and the estimate keeps converging
on the true mean heading; while turning, lag is small. A lead term,
fading in with the turn rate and capped at the latest measurement, cancels
the remaining filter and display lag without overshooting when a turn stops.
Two tunings exist: `kFusedSensorTuning` (rotation vector or gyroscope
fusion) and `kRawCompassTuning` (fusion without gyroscope).

The previous observer froze the needle until a departure of ~2.6° was
confirmed. That looked calm, but kept a persistent offset of up to that
size, moved slow turns in steps and lagged turns by ~10°. Simulated with the
same inputs (see `tests/compass_heading_tests.cpp`):

| Scenario | old max error | new max error |
| --- | --- | --- |
| 1.8° change while still (raw, 20 Hz) | 1.80° (never shown) | 0.05° |
| slow turn 2°/s (raw, 20 Hz) | 3.7° | 0.5° |
| turn 90°/s (raw, 20 Hz) | 10.3° | 4.6° |
| turn 90°/s (fused, 100 Hz) | 10.4° | 1.5° |

Only the rendered needle has a deadband of a few hundredths of a degree, so a
stationary phone does not repaint endlessly.

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

All sensors are requested at 100 Hz, but a phone can still supply fewer; the status panel displays the *observed* sensor rate to make
this distinction visible. An animation-clock improvement cannot overcome
hardware sensor latency or degraded magnetic calibration, so phone testing
remains essential. The first valid sample is displayed immediately; stopping,
backgrounding, reopening and loss of readings reset or invalidate the heading.
All filtering parameters are in `src/compass_heading_filter.h`.
Hardware-free regression tests cover the quaternion-to-heading math (flat,
tilted, upright, landscape), convergence while still, small changes while
still, slow and fast turns, turn-and-stop without overshoot, frame-rate
consistency, long sensor gaps and continuous north crossing. These simulations are not a substitute for
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

Poor calibration is indicated rather than presenting false precision. When
comparing with another compass app, check whether it shows **true** north:
the difference is the local magnetic declination (in Germany roughly +3°
to +5°), not an error. Nearby
magnetic fields, steel surfaces or protective cases can degrade accuracy.
The figure-eight movement hint can help Android recalibrate a sensor.

## Build dependencies

The compass needs only the Android NDK's `libandroid` (linked on Android).
Qt Sensors (`qt6-sensors` on Arch; `qtsensors` in Android Docker image
revision 5) is still required by the level and plumb-bob pages. No manual
Android Manifest changes or additional permissions are needed: 100 Hz is
below Android 12's 200 Hz limit for apps without HIGH_SAMPLING_RATE_SENSORS.

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
