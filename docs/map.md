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
