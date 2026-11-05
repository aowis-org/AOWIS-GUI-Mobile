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
   draws major/minor grid lines, pipe triangles and junction disks.
6. Pan/zoom data is passed from QML to render thread via atomics and applied
   in the camera transform, without rebuilding vertex buffers per gesture.
7. `bgfx::frame(BGFX_FRAME_FLUSH)` submits without a bgfx-owned swap; Qt owns
   final presentation. Scene-graph invalidation releases bgfx resources.

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
| `src/gpu_scene_geometry.h/.cpp` | Static line/triangle geometry and camera calculations |
| `src/gpu_scene_renderer.h/.cpp` | bgfx shaders, buffers, draw submission and resource release |
| `tests/gpu_scene_geometry_tests.cpp` | Dependency-free CPU tests for geometry/camera |
| `CMakeLists.txt` | Linux/Android Qt targets, bgfx dependency, shader resources and Qt icons |

## Current instrumentation

The UI shows bgfx frame count, submission CPU duration, frame interval,
optional bgfx GPU time, draw calls and lifecycle counters (app pause/resume,
scene-graph starts/stops, bgfx initializations). A 16 ms timer requests updates
while the app is active; Android uses non-persistent graphics/scene-graph
settings to *request* resource release while hidden.

These numbers are **not** a full performance profile:

- Submission duration is CPU time spent on rendering submission, not complete
  application CPU utilization.
- Driver-reported GPU timing may be unavailable or incomplete; it does not
  include Qt composition, display scan-out or battery consumption.
- Frame interval is not equivalent to end-to-end input-to-photon latency.
- The scene uses very few static vertices/draws, so current FPS is not
  predictive for real city-scale networks or terrain maps.

Do not claim context-loss recovery merely because the application resumed once.
The [Android device checklist](android-renderer-test.md) and
[roadmap](roadmap.md) specify the next acceptance work.

## Near-term validation

**First:** add deterministic scene-size presets (1k, 10k, 100k, up to 1M
objects when feasible) plus logs of frame pacing percentiles, CPU/GPU
rendering, memory and thermal behavior. Keep the same scene and controls on
Linux and Android. Ensure build-time geometry creation does not contaminate
steady-state frame measurements; report draw calls, visible objects and
whether culling/LOD is enabled. A QML-only Canvas is *not* a like-for-like
large-scene GPU comparison unless it draws equivalent geometry.

**Second:** exercise real Android rotation, Home/resume, screen lock, app
switching and repeated resource invalidation/recreation. Count, log and
assert live recovery rather than relying on counters alone. Test on-device
GPU state integrity and absence of retained resource leaks.

Only after this evidence should the Qt Quick adapter be promoted to the
planned `AOWIS-GUI-Render` boundary. Desktop `AOWIS-GUI` and its WASM
integration must continue to work unchanged during extraction.
