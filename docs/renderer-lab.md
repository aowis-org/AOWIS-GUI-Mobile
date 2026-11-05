# Qt Quick / bgfx rendering laboratory

## Purpose and verified status

AOWIS-Mobile currently contains an isolated renderer test, **not** a mobile
survey editor. It examines whether Qt Quick can own a native OpenGL window,
while bgfx uses its EGL context to draw under QML. No AOWIS MODEL, MAP, DVMDB,
location, or editor dependencies are involved.

On **5 November 2026**, user-provided Linux screenshots showed the **earlier
ASCII test scene** rendering under QML; dragging and zooming affected the bgfx
output without obscuring the QML controls. Character-grid aliasing made some
lines disappear at particular sample positions. This is **not** an indication
of GPU line-rendering behavior.

**Current implementation:** real GPU geometry has replaced that ASCII probe.
It uses pinned vertex-color shaders, retained vertex buffers, and bgfx draw
submissions. The GPU geometry build and visual composition on real hardware
are **not yet verified**. Do not interpret source tests or frame counters as a
GPU integration pass. No desktop GUI renderer extraction has been undertaken.

## Dependencies and reproducible build

`tools/init_bgfx.sh` pins `external/bgfx.cmake` to `v1.159.9485-574` and its
nested bgfx/bx/bimg revisions. This is a temporary Git submodule, not shipped
inside `mobile.zip`. CMake never fetches source dependencies.

The GPU geometry test uses `vs_cubes` and `fs_cubes`, **precompiled bgfx sample
shaders from that exact pinned bgfx checkout**. `CMakeLists.txt` embeds both
`glsl` (Linux OpenGL) and `essl` (Android OpenGL ES) binary pairs in Qt
resources. It does not compile shaders at runtime, invoke shaderc during the
build, or use shader binaries generated against unrelated bgfx versions.
These are temporary test shaders and **not** the AOWIS-RENDER shader library.

Linux:

```bash
# Provision bgfx only if it is not already initialized.
./tools/init_bgfx.sh
cmake -S . -B build-linux-bgfx -G Ninja -DAOWIS_MOBILE_WITH_BGFX=ON
cmake --build build-linux-bgfx -j 8
ctest --test-dir build-linux-bgfx --output-on-failure
./build-linux-bgfx/aowis-mobile-render-lab --bgfx-underlay
```

For QML without bgfx, run the executable without `--bgfx-underlay`, or build
with `-DAOWIS_MOBILE_WITH_BGFX=OFF` in a separate build directory.

The experiment requires Qt 6.5+ Core/Gui/Qml/Quick/QuickControls2, C++20,
CMake >=3.21, Ninja, a compatible GPU and an **EGL-backed Qt OpenGL context**.
If the Qt xcb backend selects GLX, try `QT_XCB_GL_INTEGRATION=xcb_egl` on Linux
when that Qt integration plugin is installed.

Android uses the Qt-provided Android kit, SDK/NDK and packaging. With bgfx ON,
Android starts the experiment by default; `--baseline` is available on
launchers that support custom arguments. Shader binaries are packaged inside
the executable's Qt resource section, not loaded from the source checkout.
Android compilation and device behavior are **not yet demonstrated**.

## Actual render pipeline

- bgfx clears the map background in the Qt-owned OpenGL/EGL render pass.
- A retained, world-space grid uses actual line primitives, with minor/major
  colors. There is no character-cell sampling and no redraw-by-CPU on pan.
- A small hydraulic-style demonstration network uses colored triangle meshes
  for pipes and circular junction markers. It is synthetic test geometry, not
  a NetworkHydraulic object.
- One model-space geometry set is uploaded to GPU buffers during init. An
  orthographic view matrix moves/scales the scene for all subsequent frames.
  Input pan values are QML logical pixels, scaled by device pixel ratio.
- Shader/program and vertex buffers are cleaned up before `bgfx::shutdown()`
  on Qt Quick scene-graph invalidation.
- QML owns map input, the header, status panel, buttons and app presentation.
  Its baseline Canvas is hidden when bgfx is selected.

Code boundaries: `gpu_scene_geometry` is dependency-free mesh generation and
camera math; `gpu_scene_renderer` owns bgfx resources/draws; `render_lab` owns
Qt Quick integration, cross-thread view values, lifecycle and timings.

## Frame diagnostics

The bottom panel reports:

- **bgfx submit CPU (ms):** elapsed wall-clock time for preparing the draw
  submissions and `bgfx::frame(BGFX_FRAME_FLUSH)` on the render thread.
  This may include waits or GL-driver work. It is **not** total Qt frame time.
- **Frame interval (ms):** measured time between successive bgfx callbacks.
  This is not a measurement of screen presentation latency or QML FPS.
- **bgfx GPU (ms):** the bgfx stats GPU timestamp delta **only if supported and
  positive**; otherwise `N/A`. These GPU counters may lag submitted frames.
- **Draws:** bgfx's reported draw count, which can reflect an earlier frame.

These metrics are preliminary diagnostics, **not** credible power, thermal,
end-to-end FPS, or direct-underlay-versus-texture performance comparisons.

## Rendering integration limitations

Qt Quick chooses the OpenGL graphics API before creating its first window.
`RenderLab` connects to `beforeRenderPassRecording` on the scenegraph rendering
thread, wraps bgfx calls in `beginExternalCommands()` /
`endExternalCommands()`, borrows `QEGLContext`, and submits
`bgfx::frame(BGFX_FRAME_FLUSH)`. bgfx's internal renderer thread remains OFF.
Qt owns the swap, native window, resize, and scenegraph lifecycle.

**Not yet guaranteed:** correct framebuffer binding under Qt redirected
rendering, complete GL state restoration, shader compatibility on all devices,
repeated Android foreground/background cycles, seamless resize, and precise
frame pacing. The native underlay may ultimately be unsuitable. Do **not**
extract the desktop renderer until this basic integration is verified.

## Checks and next experiment

Dependency-free unit and source checks:

```bash
c++ -std=c++20 -Wall -Wextra -Werror \
  tests/gpu_scene_geometry_tests.cpp src/gpu_scene_geometry.cpp \
  -o /tmp/aowis-mobile-gpu-scene-tests
/tmp/aowis-mobile-gpu-scene-tests
python3 tests/verify_sources.py
```

With Qt+bgfx, also run `ctest --test-dir build-linux-bgfx --output-on-failure`.
Unit tests prove only scene/camera math, not GPU rendering.

**User validation procedure:**

1. Launch `--bgfx-underlay`. Look for colored grid lines, turquoise/brown
   pipes and filled circular junctions, all drawn by GPU shaders.
2. Drag, zoom in/out to 0.3x and 3x, recenter and resize repeatedly. Grid
   lines should remain continuous (no ASCII sampling gaps); QML overlays must
   stay opaque, crisp and responsive.
3. Observe any missing vertices, shader errors, flicker, lost rendering,
   black screen, corrupted UI, wrong device-pixel-ratio scaling or crashes.
4. Capture the terminal log, diagnostics and a screenshot for review. Report
   whether the bgfx GPU timing is available.
5. Repeat on real Android hardware, including screen rotation, background,
   foreground and display sleep. Only then benchmark alternative composition
   paths with **equivalent** workloads and energy/frame-pacing measurements.

This repository remains a deliberately disposable integration laboratory.
