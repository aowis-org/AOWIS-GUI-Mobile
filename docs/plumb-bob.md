# Plumb Bob — edge vertical alignment

**Plumb Bob** is a graphical gravity-based plumb line accessible through the
right-hand hamburger menu. Hold the phone upright with a long screen edge beside
the edge you want to check. A hanging rope and brass weight show gravity's
projection onto the screen, while one numeric reading shows the edge's angular
deviation from vertical. The **PLUMB** indication appears when that deviation
is within **0.75°**. The instrument deliberately has no separate phone-face
lean measurement; inclination measurements belong to the Gradienter tool.

A `QAccelerometer` requests gravity-only mode and 60 Hz where supported. The
sensor runs only while the page is open and the app is foregrounded. Qt Sensors'
fixed axes are transformed for the current screen orientation without changing
Android's system auto-rotate setting. With a phone nearly face-up or face-down,
the in-screen gravity projection is unreliable, so the page asks you to hold it
upright. The value is not shown as valid while the sensor is unavailable, stale,
or waiting for its first measurement.

The C++ `Filter` applies time-based filtering and a small noise gate, with an
independent Qt animation-clock display follower. QML only draws the direction;
there are no QML heading tweens or JavaScript smoothing. No new Android
permissions, storage, or renderer integration are needed. This is a practical
visual alignment aid, not a calibrated survey instrument.

## Field check

Run `./compile_linux.sh --tests` then `./compile_android.sh --run`. On Android,
open **☰ → Plumb Bob** and hold the phone upright. Rotate it gently in the
screen plane: the rope and single edge-deviation reading should respond,
showing **PLUMB** when the edge is close to vertical. Turning the face toward or
away from you must not reveal a second lean measurement. Holding the phone
flat should produce the upright-position warning. Verify screen orientation,
background/resume, Android Back, and dark/light themes.

The native tests cover gravity projection, inverted position, screen rotation,
edge-only threshold behavior, unsuitable flat poses, invalid samples, noise
gating, and sustained movement. Actual phone operation still requires checking.
