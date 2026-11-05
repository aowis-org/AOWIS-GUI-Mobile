# Qt Quick + bgfx rendering integration

This describes the **implemented rendering foundation** of `AOWIS-GUI-Mobile`.
The source still uses `RenderLab` names for its instrumentation; those are
internal diagnostic class names, not a separate user-facing application.

## Verified baseline

The same synthetic GPU network scene has been visually exercised on Linux
and on a physical Android phone, with QML overlays displayed above it and
interactive pan/zoom working. Android ARM64 APK construction and Linux native
compilation have succeeded in the supplied build logs. This establishes
**initial direct-underlay feasibility**, not full GPU stress or Android EGL
recovery certification.

The visible geometry is in **arbitrary model coordinates**, not geographic
coordinates, and none of the map services or hydraulic MODEL data are used.

## Actual rendering path

1. `src/main.cpp` selects Qt Quick's OpenGL scene graph and creates the QML
   window hidden. It attaches `RenderLab` callbacks before showing the window.
2. QML owns the window, touch/mouse gestures, controls and overlays. Its
   `Canvas` renders a reference grid **only** in `--baseline` mode.
3. On the Qt Quick **render thread**, `beforeRenderPassRecording` calls the
   bgfx adapter. `beginExternalCommands()` / `endExternalCommands()` isolate
   custom commands from Qt's scene-graph rendering state.
4. `RenderLab` borrows the current Qt-owned EGL context. bgfx uses OpenGL on
   desktop or OpenGL ES on Android, with `BGFX_CONFIG_MULTITHREADED=0`.
5. `GpuSceneRenderer` allocates retained GPU vertex buffers and loads matching
   pinned vertex-color shaders (`glsl` / `essl`) via Qt resources. The renderer
   draws major/minor grid lines, pipe triangles and junction disks in the
   small demo. The stress scene generates deterministic pipe/node tiles.
6. Pan/zoom data is passed from QML to render thread via atomics and applied
   in the camera transform, without rebuilding vertex buffers per gesture.
7. `bgfx::frame(BGFX_FRAME_FLUSH)` submits without a bgfx-owned swap; Qt owns
   final presentation. Scene-graph invalidation releases bgfx resources.

The application also records first-frame recovery after app activation, active
scene-graph invalidation and resize. A five-second watchdog checks that a new
bgfx frame actually completes once the window is active and exposed. The log
in `AppDataLocation/lifecycle/events.csv` survives process restarts; the GUI
shows timeout/recovery counts and any invalidations without a current GL
context. The development-only **Recreate GPU resources** button explicitly destroys and
rebuilds the bgfx scene inside Qt's active GL context. It does not simulate
actual context loss. This is **instrumentation awaiting physical-device acceptance**, not
proof that arbitrary lost-EGL recovery is safe.

**Caveat:** Both bgfx and Qt manage GPU state. Rendering inside Qt's active
OpenGL pass is an experimental coupling; context recreation, driver variants,
Qt upgrades and multiple independent map windows remain risks. The current
path is not a generic Qt 3D item and does not work with an arbitrary Vulkan
or Metal QML backend.

## Key files

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | Platform identity, startup, rendering mode, app state and refresh timer |
| `qml/Main.qml` | Synthetic baseline, touch gestures, diagnostic status and overlays |
| `src/render_lab.h/.cpp` | Qt/bgfx lifecycle, context borrowing, frame submission and diagnostic counters |
| `src/gpu_scene_geometry.h/.cpp` | Baseline geometry, camera math, deterministic stress tile geometry and LOD selection |
| `src/gpu_scene_renderer.h/.cpp` | bgfx shaders, bounded tile cache, visibility/LOD, submission and release |
| `src/performance_samples.h/.cpp` | Dependency-free frame-metric percentile calculations |
| `src/lifecycle_watchdog.h/.cpp` | Dependency-free first-frame recovery deadline logic |
| `tools/test_android_lifecycle.sh` | ADB Home/resume loops and private lifecycle CSV retrieval |
| `tests/lifecycle_watchdog_tests.cpp` | Recovery, timeout, hidden-window and late-recovery tests |
| `tests/gpu_scene_stress_tests.cpp` | Deterministic geometry/visibility/cache-budget/percentile tests |
| `tests/gpu_scene_geometry_tests.cpp` | Dependency-free CPU tests for geometry/camera |
| `CMakeLists.txt` | Linux/Android Qt targets, bgfx dependency, shader resources and Qt icons |

## Implemented scalability harness (measurement results pending)

Select **Demo**, **~1k**, **~10k**, **~100k**, or **~1M features** in the QML
header. The largest preset models 577 × 577 nodes and right/down pipes:
**332,929 nodes + 664,704 pipes = 997,633 features**. The counts are *logical*
model features; only geometry inside the camera viewport is uploaded/drawn.
They are not 1 million independently simulated hydraulic objects.

The test is deterministic: grid topology, colors, positions and tile keys are
functions of the preset and integer coordinates. The full scene is *never*
materialized as a million-object CPU vector. GPU geometry is generated in
32 × 32-node tiles, with **at most 64 visible tiles** and a **96-tile resident
cache**. When a large/zoomed-out viewport needs more tiles, an explicit
sampling stride (LOD 1:2, 1:4, etc.) reduces submitted detail. The UI exposes
this stride and **visible versus total** node/pipe counts. This is a
large-project *viewport navigation* benchmark, not an all-million-at-once
rasterization test; culling and LOD are always active in stress presets.

The display shows bgfx CPU submission duration, actual inter-frame interval,
optional bgfx GPU time, draw counts, tile uploads, estimated retained GPU
**vertex payload** bytes, and Linux/Android process RSS when `/proc/self/status`
is accessible. GPU payload estimates are not complete VRAM totals.

Press **Record 15s** after letting geometry upload/warm up. A 15-second
capture saves individual frame observations as a CSV file under the app's
private `benchmarks/` data directory and prints summary p50/p95/p99 frame
interval and p95 CPU/GPU timings. Pause/background or scene-graph invalidation
discards the interrupted capture. The report includes logical workload,
visible/drawn geometry, LOD, draw calls, upload counts, CPU RSS endpoints and
vertex-buffer estimates. The screen and console display its location.

**Do not interpret an unrun harness as passing the benchmark.** No phone
thermal, power, large-scene frame percentile or memory stability numbers have
yet been measured and independently confirmed. bgfx GPU timing can be
unavailable; it does not encompass Qt composition or display scanout.

## Reproducible benchmark procedure

1. Run the same build mode, resolution, display-refresh setting and device
   orientation on both platforms. Record phone/CPU/GPU model, OS and Qt build.
2. Choose a workload, wait for **uploads/frame: 0** and stable RSS. Note the
   visible nodes/pipes and LOD stride. Pan/zoom to a reproducible view.
3. Press **Record 15s**; perform a consistent pan/zoom sequence (or leave the
   scene stationary for a steady-state measurement). Do not change presets.
4. Record p50/p95/p99 frame interval, p95 CPU/GPU, RSS and cache usage. Repeat
   on each preset. Do a second run after warm-up and compare initial uploads
   against steady-state capture.
5. Check real-device thermal, power consumption and sustained performance
   separately; these are not provided by the app's current metrics.

### Automatic collection for review

After recording the scenes on both devices, run **one command** from the
repository root:

```bash
./benchmark.sh
```

The script automatically finds Linux Qt `AppDataLocation` benchmark CSVs,
collects from **all authorized USB or paired Wi-Fi Android devices** using
`adb exec-out run-as org.aowis.mobile`, and gathers available lifecycle event
logs. It never requires root, accesses other apps, or gives Docker USB access.
It saves reports in `benchmarks/desktop/`, `benchmarks/android/<device-key>/`,
and `benchmarks/lifecycle/<device-key>/`. Existing CSVs are preserved;
identical reports are skipped, and filename collisions never overwrite data.
A stable device key avoids exposing raw ADB serial numbers in the report names.

`benchmarks/summary.csv` and `benchmarks/summary.md` include p50/p95/p99,
CPU/GPU timing when available, logical workload size, and peak visible
features. The summary is an aggregation of **recorded** frame samples; it
cannot tell whether a capture was stationary or interactive. Keep test
conditions comparable and identify capture type manually when reviewing.

The runner regenerates `mobile.zip` automatically. Captures are Git-ignored
but **explicitly included** in source ZIP snapshots, along with the summary
and lifecycle reports. Upload the resulting `mobile.zip` to share everything.

```bash
./benchmark.sh --desktop-only    # Linux captures only
./benchmark.sh --android-only    # Pull reports from all authorized phones
./benchmark.sh --serial SERIAL   # Select an Android phone explicitly
./benchmark.sh --strict          # Error if a requested platform is missing
./benchmark.sh --run-lifecycle   # First run 12 Android Home/resume cycles
./benchmark.sh --help
```

`--desktop-dir PATH` permits overriding an unusual Qt data directory;
`--no-zip` skips snapshot refresh. Automatic collection is not an automated
benchmark capture: continue using **Record 15s** to produce measurements.

## Limitations and next acceptance

The benchmark currently measures a deterministic, relatively uniform network
with simple triangles, not real terrain imagery, mixed object types, complex
labels or survey transactions. Its adaptive LOD intentionally omits individual
pipes/nodes when zoomed out. Pixel correctness, power and thermal performance
require tests on real hardware. The QML-only Canvas is not an equivalent
large-scene GPU baseline because it draws different geometry.

**Next agreed priority:** Android lifecycle/recovery under load: rotation,
background/resume, screen lock and induced scene-graph/EGL recreation. Verify
that tile buffers and shaders are released/rebuilt without persistent black
frames, stale geometry or accumulation of RSS. See the
[Android device checklist](android-renderer-test.md). Only then consider
extraction into `AOWIS-GUI-Render`, preserving Desktop GUI and WASM behavior.
