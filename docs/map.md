# The map

The Map page shows the AOWIS map shared with the desktop GUI: Render's
`MapBgfxMapView` (AOWIS-GUI-Render) with 2D Web Mercator imagery, the 3D globe
with terrain. Mobile has no map code of its own beyond the
Qt Quick host and its gestures.

## How it is drawn

`MobileMapView` (`src/mobile_map_view.*`, QML type `AOWIS.Map 1.0` `MapView`)
is a `QQuickItem` that fills the window underneath all QML controls. It hosts
the view in `RenderTarget::HostContext`:

1. `src/main.cpp` selects Qt Quick's OpenGL scene graph (OpenGL ES on
   Android), the **basic render loop** and, on X11, EGL. The map's frame works
   on the map model, the tile/terrain repositories and their signals, which
   live on the GUI thread; the basic loop renders on that thread too.
2. On `QQuickWindow::beforeRenderPassRecording` the item takes Qt's current
   EGL context and calls `MapBgfxMapView::renderHostFrame()` inside
   `beginExternalCommands()` / `endExternalCommands()`. The first call
   initializes bgfx in that context (no window handle, no swapchain,
   `BGFX_RESET_NONE`).
3. Every Qt Quick frame draws the whole map (Qt cleared the framebuffer);
   frames are executed with `bgfx::frame(BGFX_FRAME_FLUSH)`, Qt presents.
4. The view asks for frames through `hostFrameRequested()` (camera changes,
   arriving tiles, animations); the item calls `QQuickWindow::update()`.
   An idle map renders nothing.
5. On `sceneGraphInvalidated` (Android backgrounding: persistent scene graph
   and graphics are off) the item calls `releaseHostContext()`; the next
   render pass initializes again. CPU tile and terrain caches survive.

bgfx, bx and bimg are Render's pinned submodules (the revisions the desktop
GUI builds), so Render's committed shader binaries match. Linux embeds the
GLSL shaders, Android the ESSL ones.

## Map server

`serverUrl` is bound to `mapServerSettings.endpointUrl` (Settings → Map
server). For a configured server the item creates Render's
`InterfaceServerMapREST` transport, the `MapTileRepository` and
`MapTerrainRepository`, and refreshes the server's imagery provider catalog
(`MapProviderCatalog`, `GET /map/v1/providers`). Changing the server rebuilds
them. Without a server the map shows its background and a banner points to
Settings. Diagnostics → **Reload map server** reconnects.

## Input

| Input | 2D | Globe |
| --- | --- | --- |
| One finger / left mouse drag | Pan | Pan the globe |
| Two-finger pinch / wheel | Zoom at the fingers / pointer | Zoom |
| Two-finger twist | — | Turn (heading) |
| Two fingers up / down | Pan | Tilt |
| Right mouse drag | — | Orbit |

Tap the folded-map icon at the top left to select an enabled imagery
provider, switch between 2D and 3D, or zoom. In 3D the same menu offers
wireframe and hillshade. Drags and
pinches elsewhere go directly to map navigation. Mobile disables the
renderer HUD, including its crosshair, scale bar and device-position marker.

The camera is saved when the app goes to the background and restored on the
next start. Without a saved camera the map opens on Central Europe. Enable
**Keep GPS marker centered** in the map menu to follow fresh fixes at the
current zoom. The location button also re-centres the map on demand.

## Look around

In the Android 3D map menu, **Look around** holds the camera at its current
position and altitude while the phone controls its orientation. Pinch or use the
zoom buttons to change magnification from 0.5× to 8× around the center of the screen. The map
uses the Compass instrument’s orientation sensor. With a location fix,
it adjusts for magnetic declination at the phone’s physical location,
even while viewing a distant part of the globe. The menu shows the active
north reference. Without a correction, the view follows magnetic north.
The separate Compass instrument displays magnetic bearings.

### Camera stabilization

**Camera stabilization** in the map menu (Look around on) uses the back camera
as the gyroscope a cheap phone lacks: `src/look_around_vision.h` (tested in
`tests/look_around_vision_tests.cpp` on synthetic camera images),
`src/look_around_camera.{h,cpp}` (Android NDK Camera2 + AImageReader, no Qt
Multimedia; CAMERA permission asked when switched on).

- **Tracking** (`VisionTracker`, on the camera's callback thread): luma only,
  box-downscaled to about 320 px wide, 3-level pyramid. About 48 Shi-Tomasi
  corners (best per cell of an 8x6 grid) are followed by Lucas-Kanade with a
  forward-backward check (0.7 px). The rotation comes from the points'
  directions: RANSAC over point pairs (1.5 px), refined on all inliers
  (Horn's quaternion method). Moving people or cars and near-object parallax
  are outliers. A frame counts as reliable with at least 12 inliers and at
  least half the tracked points. Dark, blank, blurred or sky-only views are
  not reliable.
- **Fusion** (`VisionFusion`): while the camera tracks, it turns the
  heading (about Up), the axis the magnetometer gets wrong; up/down and roll
  always follow gravity through the field orientation, which is fresher than
  the camera. The first unreliable frame hands the heading back to the field
  orientation's own motion (the compass fallback). The engine runs the slow-north mode (the compass
  corrects north, it does not move the view) and never hands the view to the
  raw compass.
- **Time**: camera frames are applied as rotations between frames when they
  arrive; output to the engine runs on the motion sensors' time line, and
  "camera active" is judged by arrival. Camera timestamps are never compared
  with sensor timestamps (a shared time line let a differing camera clock
  block all turning on the test phone). Field readings are dated back by
  their 60 ms filter delay.
- **Plausibility**: seen from close up (a table, a wall at arm's length) the
  phone's own small movements look like large camera rotations. A frame
  whose tilt rate exceeds twice gravity's plus 4 °/s (low-passed over
  0.15 s) is ignored; the compass fallback holds the view.
- **Image orientation**: how the raw camera image is turned against the
  phone (0/90/180/270°) is found by comparing camera rotations with the field
  orientation's over the same arrival intervals. The camera is used only
  after that decision ("learning image orientation - move the phone").

Simulated (65° dip, hand acceleration, magnetometer noise; 30 fps camera,
frames 60 ms late):

| | Compass fallback | With camera |
|---|---|---|
| Up/down sweeps, sideways shake facing N/S | 0.24° | 0.1° |
| Up/down sweeps, sideways shake facing E/W | 1.0° | 0.3° |
| Sideways sweeps, turn followed within | 0.7° | 0.8° |
| Phone on a shaken table, worst frame step | 0.025° | 0.020° (camera ignored) |
| Camera clock off by +0.4 s or -7 s | – | identical results |
| Camera blind for 1 s mid-sweep | – | no jump |

Rotation per frame is measured to about 0.01°. The status line shows frame
rate, the share of reliable frames and the state. While the camera runs, the
flashlight cannot (the camera owns the flash).

### Without a gyroscope: own field orientation

Phones without a real gyroscope (the Armor X13 test phone) get every heading
from the magnetometer, also inside an emulated game rotation vector. The
Earth's field dips about 65° here; if the "down" used for the heading is off
by e about the north-south axis, the heading is off by about e·tan(dip), twice
e. A fusion that filters gravity slower than the field is off exactly while
the phone tilts: up/down then shakes sideways facing east or west, not facing
north or south (seen in AOWIS and in Stellarium on that phone).

Camera stabilization uses `src/look_around_field.h` as its fallback (no
reliable camera) and for up/down (there is no separate menu switch):

- `FieldOrientation` (sensor thread) builds the orientation from the raw
  accelerometer and magnetometer, the accelerometer interpolated to each
  magnetometer time and both vectors through the **same** low-pass (60 ms).
  They lag equally, so there is no tilt-to-heading error from a mismatch.
- `LookAroundFieldSource` (GUI thread) passes heading changes only while the
  phone turns rather than tilts, and gives the orientation to the engine as
  the compass (turn-gated heading: followed while turning, held while
  tilting, settled at rest). The magnetometer corrects what motion shows; it
  does not move the view while tilting.
- The field is checked against the one expected at the GPS fix (strength and
  dip from Android `GeomagneticField`, `LocationService.inclinationDegrees` /
  `fieldStrengthMicroTesla`), else against its own 3 s average (re-learned
  after 5 s at a new place). While disturbed (steel, cars) the heading is held.

Simulated (tests/look_around_field_tests.cpp: 65° dip, hand turning about the
wrist, 0.3 µT noise), worst sideways shake during ±40° up/down sweeps:

| Facing | Phone-style fusion | Own field orientation |
|---|---|---|
| North / South | 0.8° / 1.0° | 1.1° / 1.1° |
| East / West | 18.1° / 18.3° | 1.7° / 1.8° |

What remains is the hand's own acceleration and sensor noise, which no
accelerometer + magnetometer orientation can separate from real motion.
Sideways sweeps are followed about as well as by the phone's fusion. The
recording's `field_motion` / `field_trusted` columns show the mode and when
the field was judged disturbed.

**Heading guard (camera stabilization off).** Recorded on the Armor X13
(`benchmarks/lookaround/Armor_X13-20261109-175728/`, facing north, tilting
slowly up and down): the phone's magnetometer lags the tilt and then catches
up in jumps of 10–25°. Its dip, true 66°, swings between about 20° and 89°;
near 90° the horizontal field vanishes and the heading flipped by up to 180°
although the phone only tilted. No filter can repair such readings, so the
own fusion without gyroscope (`compass_fusion.h`, `MagneticHeadingGuard`,
used by the phone path) splits each update:

- tilt always as measured (accelerometer);
- heading follows the measurement's own changes only while its dip is
  within 8° of the reference (none beyond 16°), and at most 360°/s;
- the offset built up while held (or by a jump) is caught up smoothly: at
  rest (gravity and field steady 0.3 s) in 0.4 s, while moving after the dip
  looked right for 0.3 s in 0.4 s, but never an offset near 180° while
  moving (the signature of the vanished horizontal field);
- the reference dip is learned at rest. Gravity and field also share one
  low-pass (0.2 s); the field used to be filtered faster.

Replay of the six recordings (`tools/replay_look_around.sh`, phone path):

| Recording | jitter moving before / after | worst frame step before / after |
|---|---|---|
| 175717 (up/down facing north) | 8.75° / 0.61° | 129.6° / 4.0° |
| 174951 (turns, laid down) | 3.17° / 1.21° | 47.1° / 13.4° |
| 174813, 174857, 175005, 175638 | 0.45–4.68° / 0.42–4.23° | 4.2–7.8° / 2.9–5.7° |

In 175717 the shown heading stays within 8° rms of north (before: 44°,
flips to 178°). Turns are followed as before, at most about 10° behind
while the field looks odd. `tests/compass_heading_tests.cpp` simulates the
lagging field (±55° tilt, field 0.4 s late): the old fusion flipped by 180°,
the guarded one stays within 1.5°. Recordings also hold the uncalibrated
magnetometer (`U`: raw reading and the OS hard-iron estimate) if the phone
has one, to tell the OS calibration from the sensor.

### Motion engine

`src/look_around_engine.h` (tested in `tests/look_around_engine_tests.cpp`) is
independent of every input and output rate:

- **Inputs keep their sensor timestamps:** the gyroscope pose (Android game
  rotation vector) and the compass pose (OS rotation vector), at any rate and
  in any batches.
- **The core advances in fixed 2 ms steps of sensor time**, lazily: whenever
  input arrives it runs every step up to the newest gyroscope timestamp. No
  timer or thread; the same inputs always give the same motion, and the
  smoothing is identical at 100 and 400 Hz sensor rates.
- **Each frame asks for its own time.** The core history is interpolated at a
  moment shortly in the past (the largest delivery latency of the last 0.5 s
  plus one sample interval, adapting slowly), so frames advance evenly however
  the samples were batched. Latency is measured as arrival minus sensor time,
  so any offset between the clocks cancels.
- **A forward lead** along the smoothed trajectory compensates 60 % of that
  delay. The core runs ahead of the shown moment, so a stop is seen before it
  reaches the screen and the lead eases out (40 ms) instead of overshooting.
  No lead below 2–4 °/s, where it would follow hand tremor.

**Frame clock.** The screen shows frames exactly one refresh apart, but the
moment the app prepares a frame jitters by several milliseconds: on the
Android test phone 11 to 21 ms between 60 Hz frames, in short/long pairs.
Sampling the motion at that raw moment made every shown step jitter by 9 to
19 % rms, single frames up to 2× off: the stutter of the first engine
version. `LookAroundFrameClock` (same header) is a phase-locked loop on the
frame stream: one refresh period per frame, phase and period gently corrected
toward the wall clock. It starts from the total time over the first 30
intervals, follows the panel's true rate (58.9 Hz on a nominal 60 Hz device)
and absorbs a dropped frame within a few seconds without a hitch. Replaying
the phone's eight recorded frame-time sequences: 0.5–1 % rms step jitter
(3.6 % in the one recording with real frame drops). The renderer's
`MapFramePacer` is not used for this: it rounds each interval to whole
periods, which turns a late frame followed by an early one into "two periods,
then none".

**Panorama test.** "Panorama test (no map)" (shown while Look around is on)
draws a 360-degree test photo (`assets/panorama/look-around-test.jpg`,
equirectangular, image centre facing north) instead of the map, through the
same sensors, motion engine, frame clock, renderer frame and presentation;
only the map's rendering work (terrain, imagery, network, overlays, HUD) is
left out (`MapBgfxMapView::setPanorama()` in AOWIS-GUI-Render). If Look
around is smooth over the panorama but not over the map, the map's frames
are too slow; if it stutters over both, the motion or frame timing is at
fault. The frame recording marks panorama frames (`panorama` column), so
both modes can be compared from one session. The photo has about 7 pixels
per degree: soft at 8×, which does not matter for judging motion.

**Raw sensor recording and replay.** Every Look-around session also records
every raw sensor event (accelerometer, magnetometer, uncalibrated
magnetometer, gyroscope, rotation vectors) and every camera tracking result with sensor and arrival time
(`src/look_around_recorder.h`, `sensors-<utc>.csv` next to the frame
recording). With the phone connected:

    tools/pull_look_around.sh

fetches all recordings and the AOWIS sensor/camera logcat lines into
`benchmarks/lookaround/<model>-<utc>/`, clears them on the phone and replays
them. `tools/replay_look_around.sh [dir|file]` (no phone, no Qt) replays
recordings through the current code (`tools/look_around_replay.cpp`): phone
sensors, compass fallback and camera stabilization, frames at 60 Hz on the
arrival clock, and prints per path the frame-to-frame jitter while still and
while moving, the worst single frame step and the up/down lag behind the
phone path
(`<recording>-replay.csv` per frame, `summary.txt` per directory).

**Recording.** Each Look-around session records its first two minutes of
frames (frame and paced times, core lag, display delay, shown bearing and
pitch, measured heading noise and applied correction, driving source, gyro
samples per frame) to `AppDataLocation/lookaround/frames-<utc>.csv` when Look
around ends or the app goes to the background. Pull them from a debug build:

    adb exec-out run-as org.aowis.mobile tar -C files/lookaround -cf - . | tar -xf -

**Heading from the compass while turning, tilt from the gyroscope (default).**
Each compass reading is compared with the gyroscope pose interpolated to the
reading's own timestamp; the rotation about Up between them (the offset) is
kept per reading and interpolated in sensor time like the poses. The view is
the gyroscope pose turned by it: up/down and roll are exactly the gyroscope's
(identical to the slow-north mode below, tested to 0.01°). The offset follows
the compass with a time constant set by the gyroscope's own rates: 0.04 s
while turning sideways fast (the heading is then the compass's), 1 s at rest,
and frozen while only tilting. Moving the phone up/down disturbs the compass's
gravity estimate and wobbles its heading; following it there shook up/down
(simulated 3° wobble: 3.6° sideways shake when always following, 0.12° now). No slow alignment and no creep,
and a hand-over to the compass and back leaves the heading untouched. Every
covered compass reading is used, however the samples are batched. Both
headings pass the noise tracker (transparent on clean sensors). Simulated with
a game rotation vector that turns only 85 % sideways and a compass with
0.4° tilt noise: worst heading error 1.15° (slow-north: 5.3°), no compass tilt
noise in the view. Background: on the test phone up/down was good from the
game rotation vector, sideways good from the rotation vector (as in a sky
app). `setHeadingFromCompass(false)` restores the slow-north mode.

**Slow-north mode: motion first, compass second.** Gyroscope samples move the view; the compass
never sets the pace, and a slow or stalled compass neither slows nor stops it.
Each compass reading is compared with the gyroscope pose interpolated to the
reading's own timestamp, and steers a heading offset about Up through a 2.5 s
low-pass. The applied offset follows at a limited rate that rides on your own
motion: 0.3 °/s at rest (divided by the magnification, so a barely visible
creep at 8×) plus 5 % of the turn rate, so north is fixed mostly while you
pan. A jump of the gyroscope frame (an OS reset) is carried over into the
offset, keeping the view continuous. Without a game rotation vector, or after a
gyroscope dropout of 0.1 s, the compass pose drives the view; the hand-over
in either direction is absorbed by the smoothing.

**Gyroscope bursts.** Some phones deliver the game rotation vector in bursts
longer than the 0.1 s gyroscope timeout, while the compass rotation vector
arrives promptly. The compass then covers every gap and hands the view back
with the next burst. The hand-back aligns north from a compass reading and a
gyroscope pose of the *same* sensor time (one interpolated to the other).
Before, it compared the newest of each, up to a burst apart: while turning
sideways every hand-back jumped the heading by turn rate × burst length, while
up/down stayed smooth (only the heading is aligned). Simulated with 120 ms
bursts and ±40° sweeps: frame-to-frame step change 4.7° sideways vs 0.8° up/down
before, 0.8° both now. The recording's `mode_switches` column counts
hand-overs; rising steadily during a session means the phone bursts.

**Heading noise.** On a phone without a gyroscope (the Armor X13 test phone
is listed without one) tilt still comes cleanly from gravity, but the rotation
about Up can only come from the magnetometer, also inside a game rotation
vector the vendor emulates. That heading jitters by tenths of a degree to
degrees per sample: up/down feels smooth, sideways yanky. The engine measures
the sample-to-sample heading noise of the driving input (second differences,
minus a 0.05° floor that real gyroscopes and hand acceleration stay below) and,
only when there is noise, passes the heading through a constant-velocity
(alpha-beta) tracker whose gains follow it (hand acceleration 200 °/s²). Tilt
is never touched; with a real gyroscope the tracker is transparent. Simulated
with 0.5° rms heading noise, 100 Hz, 60 Hz frames:

| | Before | Now |
|---|---|---|
| Frame step at rest | 0.21° rms | 0.07° rms |
| Frame step jitter, 60 °/s pan | 0.82° rms | 0.24° rms |
| Lag, 60 °/s pan | 1.03° | 0.90° |
| Overshoot, 150 ms stop from 60 °/s | 0.70° | 1.27° |

The logcat lines `AOWIS compass sensor ...` (written when the sensors start)
name the rotation vector, game rotation vector, gyroscope and magnetometer the
phone reports. The tracker cannot make a magnetometer as good as a gyroscope:
a slow heading wander of a few tenths of a degree remains.

**Smoothing** is a speed-adaptive ("One Euro") filter on rotations. At rest the
estimate converges on the exact pose (no dead band); while turning the cut-off
rises with the on-screen angular speed. The rest cut-off is 4 Hz at 1× and
falls with magnification (0.5 Hz at 8×). Roll (about the view axis) is smoothed
at 0.35× the pointing cut-off; it does not move the point under the crosshair.

Simulated with sensor batches of 1–3 events arriving 6–21 ms late, 100 Hz
sensor and 60 Hz frames (the previous per-sample stabilizer for comparison):

| | Previous | Engine |
|---|---|---|
| Frame step jitter, steady 20 °/s pan | 116–118 %, 30 backward frames in 2 s | 0.3 % |
| Lag at 20 / 60 °/s, 1× | 0.59 / 1.29° mean, very uneven | 0.83 / 1.92° |
| Overshoot, natural 150 ms stop from 20 °/s | 0.00–0.03° | 0.02–0.07° |
| Overshoot, abrupt stop from 20 °/s | 0.13–0.24° | 0.16–0.20° |

The lead fraction (`kDefaultLeadFraction`, or `setLeadFraction()`) trades lag
against stop overshoot: 1.0 removes most of the lag of a steady pan (0.33° at
20 °/s) but overshoots an abrupt stop by up to 0.8°.

## Lifecycle diagnostics

The item writes `AppDataLocation/lifecycle/events.csv` (app active/inactive,
scene graph released, bgfx initialized, `watch_<reason>`, `recovered`,
`late_recovery`, `timeout`) and arms a five-second first-frame watchdog after
resume, scene-graph recreation and resize. `tools/test_android_lifecycle.sh`
reads that log via `adb run-as`. Diagnostics shows the renderer, server and
recovery state.

## Limits

- No hydraulic network on the mobile map yet (`setDistributionNetworkVisible(false)`).
- Linux and Android only (EGL context borrowing).
- Gesture directions on the globe are first guesses and need device tuning.
