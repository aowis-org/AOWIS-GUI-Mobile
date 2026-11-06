# Plumb Bob — true vertical alignment

**Plumb Bob** is a gravity-driven plumb line accessible from the right-hand
hamburger menu. It measures vertical alignment of an upright phone independently
of GPS, magnetic heading, the map renderer, and survey storage. A continuously
hanging graphical rope and brass weight show the *screen-projected gravity*
direction, with readouts for the deviation of the phone's nearest vertical
screen edge and the deviation of the phone's **face from a vertical plane**.
Both must be within **0.75°** for the green **PLUMB** indication.

The two readouts are different: an upright phone can have a perfectly vertical
screen edge while its face leans toward or away from the user; that is not a
vertical plane. Conversely, a vertical plane may have the phone's screen edges
rotated in the plane. Both components are needed to check alignment.

A `QAccelerometer` requests gravity-only mode and 60 Hz where supported.
The service runs only on the Plumb Bob page while the application is in the
foreground. Qt Sensors' fixed axes are transformed by the current Qt screen
orientation **without changing the Android global auto-rotate setting**.
A nearly face-up/face-down phone cannot provide a reliable plumb direction,
and the UI explicitly says to hold it upright. Missing hardware, no first
sample, a stale sample, and sensor error are never displayed as valid zero.

The C++ `Filter` uses elapsed-time exponential filtering and a small noise
gate with an independent Qt animation-clock display follower. It does not
predict angular velocity or run JavaScript smoothing. The rope and brass bob
are declarative Qt Quick shapes; they only bind to C++ direction properties.
No new Android permission, GPS observation or persistent sensor logging is
required. The measurement is an aid and is not a calibrated survey level.

## Field check

Run `./compile_linux.sh --tests` then `./compile_android.sh --run`. On the phone,
open **☰ → Plumb Bob** and hold the display in a vertical plane. Rotate the
phone slowly in that plane: the rope should indicate gravity while the edge
deviation increases. Tilt its face toward you: the face deviation should
increase even when the rope is aligned with the screen edge. Hold the phone
flat: the instrument must warn that the face is too horizontal. Verify
portrait/landscape (if enabled by your own system setting), background/resume,
Android Back and dark/light themes. A Linux desktop without an accelerometer
should report sensor unavailable.

The C++ unit test covers projections, inverted orientation, screen rotation,
face lean, unsuitable face-up poses, invalid acceleration, noise gating and
sustained movement. Hardware behavior still needs an on-device check.
