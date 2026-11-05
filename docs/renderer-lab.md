# Qt Quick / bgfx rendering laboratory

## Purpose and current status

This repository now contains a **minimal rendering experiment**, not yet the
AOWIS Mobile survey application. The lab deliberately has no AOWIS MODEL, MAP,
DVMDB, sensor or network dependency. Its purpose is to answer whether bgfx can
reliably render a map beneath Qt Quick controls while Qt owns the native window
and presentation lifecycle.

**Status:** the source and build configuration have been prepared. The bgfx
integration is an **unverified experiment**, not a demonstrated working
solution. Linux/Android compilation and device rendering must be checked on
machines with the Qt development SDK, bgfx sources, and a GPU. Results from
this build environment cannot be called a performance benchmark.

This milestone does *not* extract the desktop renderer. Extracting the shared
`AOWIS-RENDER` library remains a subsequent decision after integration evidence.

## Dependency model

A **temporary** `external/bgfx.cmake` Git submodule is provisioned by
`tools/init_bgfx.sh`. It is pinned to upstream release tag
`v1.159.9485-574`, and its own pinned nested submodules provide matching
versions of bgfx, bx and bimg. The script stages `.gitmodules` and the Git
submodule pointer in the Mobile repository. Commit both alongside the source.

The patch ZIP cannot represent a Git gitlink by itself; run the installer once
in your Git checkout after applying the source patch. Never vendor the
submodule contents into `mobile.zip`. CMake will not download dependencies.

The lab uses upstream [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake),
with its examples, tools, tests, video decoding, and bgfx internal renderer
thread disabled to limit the build and keep submission on the Qt render thread.

## Linux commands

Baseline (no dependency download needed):

```bash
cmake -S . -B build-linux-lab -G Ninja -DAOWIS_MOBILE_WITH_BGFX=OFF
cmake --build build-linux-lab -j 8
./build-linux-lab/aowis-mobile-render-lab
```

One-time bgfx dependency provisioning:

```bash
./tools/init_bgfx.sh
```

Experimental bgfx build and run:

```bash
cmake -S . -B build-linux-bgfx -G Ninja -DAOWIS_MOBILE_WITH_BGFX=ON
cmake --build build-linux-bgfx -j 8
./build-linux-bgfx/aowis-mobile-render-lab --bgfx-underlay
```

Linux requires the Qt 6.5+ Core, Gui, Qml, Quick and QuickControls2 development
components, CMake 3.21+, a C++20 compiler and Ninja. The underlay probe
requires an **EGL-backed** Qt OpenGL context (not GLX). On X11, selecting the
EGL Qt xcb integration may be necessary, depending on available Qt plugins;
for example, use `QT_XCB_GL_INTEGRATION=xcb_egl` if installed. On native
Wayland, the Qt implementation must expose an EGL context.

For a build with bgfx linked but **not initialized**, launch without
`--bgfx-underlay`; this is the QML baseline. The experimental mode is selected
at process startup to avoid switching graphics-device ownership mid-frame.

## Android

Android is the first intended device target, using the **Android Qt 6.5+ kit**
and matching CMake/toolchain configuration. Provision the git submodule first,
then configure with the Qt-provided `qt-cmake` (or Qt Creator Android kit),
build, package and deploy using the Qt Android toolchain. `QT_ANDROID_ABIS`,
Android SDK/NDK versions and signing are intentionally taken from the
installed Qt kit, not hardcoded into this repository. The app uses the Qt
package template; there are no permissions or manifest customizations yet.

On Android, builds with `AOWIS_MOBILE_WITH_BGFX=ON` **automatically start
in experimental mode**, because normal Android launchers do not forward custom
CLI arguments. Builds with the option `OFF` run the QML baseline. `--baseline`
can force the QML-only mode when an Android launcher does support custom
arguments.

## What is rendered

- Baseline: a Qt Quick Canvas draws a test grid and polyline.
- Experimental underlay: bgfx attempts to clear the **same** framebuffer to
  dark blue, submit a touch view and render built-in debug text/patterns.
- Both: real QML header, touchable map area, bottom status sheet, plus/minus
  zoom and recenter buttons; tapping/dragging updates QML state.
- Status sheet: reports actual C++ bgfx frame calls and initialization state.
  A positive frame count does **not** mean the presented image is correct.

These two visual scenes are intentionally *not* identical and must not be used
for GPU workload or energy comparisons. First prove correct graphics ownership;
then implement a common scene and realistic frame pacing for benchmarks.

## How the direct-underlay experiment works

1. Select Qt Quick's OpenGL graphics API before the first QQuickWindow.
2. Connect to `beforeRenderPassRecording` with `Qt::DirectConnection` so
   all GL calls happen on Qt Quick's scenegraph render thread while Qt's main
   render pass is active.
3. Wrap calls in `beginExternalCommands()` / `endExternalCommands()` to isolate
   Qt and external OpenGL state.
4. Get the current Qt-owned EGL context from `QOpenGLContext` and pass it as
   bgfx `Init.platformData.context`. Reject GLX and non-OpenGL configurations.
5. Compile bgfx with `BGFX_CONFIG_MULTITHREADED=OFF`. Submit a view and
   `bgfx::frame(BGFX_FRAME_FLUSH)` in that same callback. The pinned bgfx API
   documents this flag as executing drawing without presenting the backbuffer.
   Shutdown on scenegraph invalidation.

**Central unresolved risk:** although `BGFX_FRAME_FLUSH` requests no bgfx
buffer presentation, bgfx still owns GPU state and manages render-target
bindings inside a Qt-owned scenegraph render pass. Qt Quick also owns surface
presentation. The mode can still exhibit missing map output, missing QML
overlays, flicker, timing problems or backend-specific failures. The Qt
external-command state fence does **not** guarantee that bgfx's framebuffer
and viewport are the ones Qt Quick expects. bgfx must not be patched or forked
before measuring alternatives.

### Pinned bgfx API compatibility

The pinned bgfx revision `9b636df330c81e11c84595651a291b6c59fb7396`
uses `Init::swapChain.width/height` and `Init::reset` rather than the older
`Init::resolution` fields. Its `bgfx::reset` accepts global flags and an
optional `SwapChain*`, not separate width/height arguments.

Qt owns the EGL drawable and its dimensions. The lab therefore uses
`bgfx::reset(BGFX_RESET_NONE)` on Qt window-size changes, updates the bgfx view
rectangle, and intentionally does **not** pass a window swapchain description:
`Init::swapChain.nwh` is unset because Qt owns the surface. Actual resize
correctness is part of the visual/device validation, not yet demonstrated.

A second unresolved issue is correct framebuffer ownership when the Qt scene
graph renders through a redirect, offscreen buffer or Android-specific surface.
Do not infer correct rendering merely from successful initialization.

Qt documentation: [QQuickWindow custom rendering](https://doc.qt.io/qt-6/qquickwindow.html).
bgfx source: [EGL context and swap handling](https://github.com/bkaradzic/bgfx/blob/master/src/glcontext_egl.cpp).

## Test procedure and decision criteria

1. **Baseline:** launch, resize, tap, drag, zoom and recenter. Record startup,
   QML behavior and GPU backend.
2. **Experimental:** verify a bgfx blue map background/debug text is visible,
   and that every QML header, status and control is visible **above it**.
3. Check continuous animation for missing frames, flicker, incorrect layers,
   wrong colors or tear artifacts; also test dragging/rapid resizing.
4. On Android, background/foreground, screen off/on, rotate, keyboard/dialog
   overlays and repeated launches. Record crashes and graphics warnings.
5. Compare correctness before performance. If the Qt-owned surface is not
   reliably composited, classify same-target underlay as **unproven/unsuitable**.
6. Only after correctness: add equivalent render workloads, steady-state GPU
   timings, 99th percentile frame pacing, thermal/battery measurements, and a
   zero-copy texture or separate-surface adapter for A/B comparison.

Do not prematurely choose the future shared renderer's platform API based on
this one experimental path.
