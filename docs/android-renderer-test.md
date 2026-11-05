# Android Qt Quick / bgfx device test

**Status:** Android build and device-test tooling implemented; **no Android APK or
hardware run has been verified yet**. Linux direct underlay with real GPU
vertex/shader geometry and visible QML overlays was visually confirmed on
2026-11-05. This procedure tests the same code on Android, where EGL, GPU
backend, device pixel ratio and lifecycle can behave differently.

## Building on Arch Linux (Docker)

**No Qt Online Installer.** Like the existing AOWIS GUI WASM and Standalone
Windows cross-builds, the Android toolchain lives entirely inside a Docker
image. The host needs Docker, Git and the initialized bgfx source submodule.
For USB deployment only, install Arch's `android-tools` (ADB); `android-udev`
may be useful for USB device permissions.

```bash
sudo pacman -S --needed docker android-tools
sudo systemctl enable --now docker
# Log in again after adding yourself to the docker group, if needed.
# Alternatively run Docker with appropriate privileges; avoid sudo-ing the
# project script because that would create root-owned build files.
./tools/init_bgfx.sh
./compile_android.sh
```

`./compile_android.sh` automatically invokes `./prepare_android_docker.sh`
when the pinned image does not exist. You can prepare/rebuild it explicitly:

```bash
./prepare_android_docker.sh
```

The image uses **Ubuntu 24.04**, Qt **6.12.0** for Linux host tools and
Android ARM64, JDK **21**, Android SDK API **36**, Build Tools **36.0.0**,
and Android NDK **27.2.12479018**. Versions are declared in
`tools/qt-android/toolchain_versions.sh`. Provisioning uses the open-source
`aqtinstall` command-line downloader for official Qt archives, not the Qt GUI
installer or a Qt account. Google's command-line-tools archive is SHA-256
verified against the published checksum. The build process accepts Android
SDK license terms when provisioning; please review those terms before building.

The image build requires an internet connection for the downloads and enough
storage for Qt, SDK, NDK and Docker layers. First APK build also downloads
Gradle dependencies into `~/.cache/aowis-mobile/gradle`; subsequent builds
reuse that cache. No Android Qt, Java, CMake, Android SDK or NDK installation
is needed on the Arch host. `AOWIS_DOCKER_BUILD_NETWORK` and
`AOWIS_DOCKER_RUN_NETWORK` (default `host` on Linux) can be overridden.

## Build and deploy the rendering lab

```bash
./compile_android.sh               # build; install/launch on one authorized device
./compile_android.sh --build-only  # build without deployment
./compile_android.sh --baseline    # QML-only comparison build
./compile_android.sh --run         # fail if no authorized Android device
```

The script builds only `aowis-mobile-render-lab_make_apk`, with native tests,
bgfx examples and unrelated tools disabled. Output is written under
`build-android-bgfx/` or `build-android-baseline/`, with the debug APK under
`android-build/build/outputs/apk/debug/`. Docker runs using the invoking host
UID/GID, so output files remain writable by the developer. The ADB install
and launch happen **on the host**; USB devices are not mounted into Docker.
Set `ANDROID_SERIAL` to choose one of multiple authorized devices.

Both lab variants have the same package ID (`org.aowis.mobile.renderlab`),
so installing one replaces the other. On Android the bgfx underlay is selected
automatically in the GPU build. If `--run` is specified, deployment must
succeed; by default, a missing device just leaves the APK on disk.

The host's Qt installation and default Java selection are irrelevant. In
particular, an Arch default of Java 8 will **not** affect the Docker build.
Run `./compile_android.sh --help` for command-line options.

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
