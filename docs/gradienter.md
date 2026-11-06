# Gradienter — two-axis spirit level

The **Gradienter** is a renderer-independent spirit-level and inclinometer,
accessible from the right-side hamburger menu. The circular vial and the
numerical **roll**, **pitch** and **combined tilt** readouts use actual device
gravity measurements; no GPS course, magnetic compass or map tiles are
involved. The measured tilt is with respect to gravity and the phone's screen
plane, not the terrain beneath the phone.

Qt Sensors' `QAccelerometer` requests gravity-only acceleration at 60 Hz,
where supported. On devices/backends that cannot separate gravity, Qt may
continue providing combined acceleration; brief movement then affects the
reading. The sensor is active only while the Gradienter page is visible and
the application is in the foreground. After background/resume a fresh sample
is mandatory. Six seconds without an initial sample, missing hardware,
sensor failure, and readings stale for three seconds all have explicit status.

Native C++ `src/gradienter_math.h` converts acceleration into two signed
gravitational inclination components. `src/gradienter_service.cpp` compensates
for screen orientation using the Qt screen's native-vs-current orientation,
with rotation handled separately from filtering. This permits portrait and
landscape use without misinterpreting the screen's axes. Both face-up and
face-down horizontal phone positions read as level. Hardware-specific case
angles and sensor calibration can still cause offsets.

An exact time-dependent exponential low-pass filter in C++ attenuates
accelerometer noise; another continuous filter advances on the Qt animation
clock via `QAbstractAnimation`. It does **not** use QML JavaScript filters,
per-sample restarted animations, velocity prediction or a competing fixed
16 ms refresh timer. In particular, the map-renderer lab's timer is suspended
on both Compass and Gradienter pages. The decorative circular vial and
bubble are expressed as lightweight declarative Qt Quick rectangles, and
support both light and dark appearance.

A level is indicated when the combined tilt is within **0.5°**. The circular
bubble covers approximately ±12° along either axis, clamping beyond that
range; the digital readouts retain the actual angle. As with any spirit
level, avoid moving the phone while making a precision reading.

**Zero reference** captures the current tilt as a *relative* baseline (for
comparing a second surface with the first); it does **not** claim the
reference surface is truly horizontal. **Reset zero** returns to gravity's
horizontal plane. The UI identifies when a relative reference is active.
The reference lives only in application memory; no new private location or
sensor history is written to disk or transmitted.

## Test

```bash
./compile_linux.sh --tests
./compile_android.sh --run
```

On Android, select **☰ → Gradienter** and lay the phone on a level surface.
Check that the bubble is near the center and the readouts near 0°. Raise
left/right and top/bottom edges separately. Flip the phone face-down and
repeat. Rotate the app portrait/landscape and check the display axes.
Press **Zero reference** on a tilted surface, then **Reset zero**; verify
that the absolute tilt returns. Background/reopen, verify fresh readings,
and use Android Back to return to Map. A desktop computer without an
accelerometer should show an unavailable status, not fake measurements.

The pure C++ math tests validate gravity conversion, screen-axis rotation,
invalid readings, noise damping, response to a sustained tilt and reset.
Actual precision and device smoothness require an Android hardware test.
