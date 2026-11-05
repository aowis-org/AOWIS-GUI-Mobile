# Android Qt Quick / bgfx device test

**Status:** Android build and device-test tooling implemented; **no Android APK or
hardware run has been verified yet**. Linux direct underlay with real GPU
vertex/shader geometry and visible QML overlays was visually confirmed on
2026-11-05. This procedure tests the same code on Android, where EGL, GPU
backend, device pixel ratio and lifecycle can behave differently.

## Prerequisites

- A physical Android device with USB debugging enabled, and `adb` in `PATH`.
- Installed Qt **Android** arm64-v8a kit (Qt 6.5 or newer, matching your
  desktop Qt environment where possible), Android SDK, matching Android NDK,
  Ninja and CMake. The host Linux kit cannot produce an Android APK.
- `./tools/init_bgfx.sh` already run to initialize the pinned bgfx.cmake,
  bgfx, bx and bimg submodules. Source builds are offline after provisioning.

## Build the GPU experiment

Run from AOWIS-Mobile repository root (change paths to your actual versions):

```bash
export AOWIS_ANDROID_QT_CMAKE="$HOME/Qt/<qt-version>/android_arm64_v8a/bin/qt-cmake"
export ANDROID_SDK_ROOT="$HOME/Android/Sdk"
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/<installed-ndk-version>"

./tools/build_android_lab.sh
./tools/run_android_lab.sh
```

`build_android_lab.sh` configures a separate `build-android-bgfx` directory,
builds the Qt/bgfx native library, and then packages a **debug APK** using
Qt's `apk` CMake target. No manual Android Manifest or Gradle modifications are
required: Qt generates its own Activity and deployment metadata, including the
`org.aowis.mobile.renderlab` package name. The stock Activity leaves
orientation unspecified for rotate testing and retains Qt's configuration
change handling. The application starts bgfx underlay by default in Android
BGFX-enabled builds, because custom CLI arguments are not provided by the
Android launcher.

For a QML-only comparison build, use a **different** directory:

```bash
AOWIS_ANDROID_BGFX=OFF ./tools/build_android_lab.sh
./tools/run_android_lab.sh build-android-baseline
```

Both variants use the same Android package ID, so installing the second
replaces the first; no live backend toggling or GPU reinitialization is needed.
Use `AOWIS_ANDROID_BUILD_DIR` if you prefer an explicit build directory. The
install script accepts either a build directory containing a single APK or an
exact `.apk` path. If it finds multiple APKs, pass a specific file instead.

## Device acceptance checklist

1. **Launch:** grid, turquoise/brown pipe segments and circular junctions
   appear behind the QML header, status panel and controls. Check that the
   Android build selected GLES; no missing shader or bgfx initialization log.
2. **Input:** one finger pans, a two-finger pinch scales and translates the
   network about the gesture center, tapping shows coordinates, and the
   minus/Center/plus buttons work. Desktop mouse drag/wheel continue to work.
3. **High DPI and resize:** inspect grid/pipe sharpness on the device's DPR;
   rotate portrait/landscape repeatedly, including while panned and zoomed.
   Geometry must keep its position and UI must not become inaccessible.
4. **Background/foreground:** press Home, return to the app, open the app
   switcher, turn off/on the display, and repeat at least 10 times. Check
   rendering, QML overlays, touch input and lifecycle counters after each
   return. `scenegraph releases` may be zero if Qt retains the context; if
   resources are released, bgfx must initialize again without a crash.
5. **Stress:** interact continuously for several minutes and inspect for
   intermittent black frames, GL errors, lost drawings, corrupt QML controls
   and unexpectedly increasing memory use. This is not yet a heavy-scene
   performance test.
6. **Baseline:** install the QML-only build and compare responsiveness on
   the same device. Frame counters for bgfx should remain zero in baseline.

Qt translates Android activity lifecycle changes to
`QGuiApplication::applicationStateChanged`; the app pauses its 16 ms redraw
timer while inactive. It sets `QQuickWindow::setPersistentGraphics(false)`
and `setPersistentSceneGraph(false)` on Android, requesting scene-graph and EGL
resource release while hidden. Those are hints, **not guarantees** about
context destruction or EGL_CONTEXT_LOST recovery. Qt owns the context,
surface, renderer-thread scheduling and final buffer swap; bgfx borrows the
EGL context and submits non-presenting `BGFX_FRAME_FLUSH` frames.

## Diagnostic logs

```bash
adb logcat -v time | grep --line-buffered 'AOWIS render lab'
```

Record device model, Android version, Qt version, GPU name, the screen state
and orientation where a failure happened, and the lifecycle counters. If the
app crashes, also capture uncropped `adb logcat` output. `bgfx GPU` time in
the UI is driver-reported (if available); it does not measure end-to-end
presentation time, battery drain or thermal throttling.

## Exit criteria before renderer extraction

- Successful native Android build and on-device start.
- Drawing and overlays remain correct across touch/rotation/suspend/resume.
- No GLES errors, context-recovery crashes or persistent black screens.
- A credible heavier-scene performance comparison between actual underlay
  and a fallback integration, with frame pacing, CPU/GPU time and battery
  observed on real devices.

Passing this narrow lab **does not** prove Qt Quick / bgfx compatibility on
all Android devices or yet justify removing the existing AOWIS desktop
renderer from GUI.
