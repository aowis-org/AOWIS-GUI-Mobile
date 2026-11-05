# Android build, deployment and device validation

## Supported build today

Android ARM64 debug APK via **Docker** on Linux/Arch, using Qt 6.12.0 Android
and host tools, Android SDK API 36, NDK 27.2.12479018, JDK 21 and Ninja.
Versions are defined in `tools/qt-android/toolchain_versions.sh`. The container
provisions official prebuilt Qt archives using pinned CLI `aqtinstall` and the
Google SDK command-line tools. **Qt Online Installer and a host Android SDK
are not needed.** A first image build requires internet access.

Host requirements: Docker, Git with initialized submodules, and `adb`
(`android-tools` on Arch) only if deploying to a device. `android-udev` may
help with USB permissions. The Android debug package is `org.aowis.mobile`
and its visible launcher label is **AOWIS**.

```bash
./tools/init_bgfx.sh           # first checkout only
./compile_android.sh          # build and deploy if one authorized device exists
./compile_android.sh --build-only
./compile_android.sh --devices
./compile_android.sh --run
./compile_android.sh --baseline
./prepare_android_docker.sh   # explicit image preparation/rebuild
```

The APK is produced at:

```text
build-android-bgfx/android-build/build/outputs/apk/debug/android-build-debug.apk
```

`--baseline` uses a separate Android build directory. The container runs as
the host UID/GID, preserves Gradle downloads between builds and does not need
USB access. The host runs `adb` after the APK build. Use `ANDROID_SERIAL` to
choose one device if multiple authorized devices are connected.

## USB and Wi-Fi ADB

**USB:** On the phone, enable Developer Options and USB debugging, use a
data-capable cable/port, unlock the phone and approve the RSA prompt. Test
with `adb devices -l`; state must be `device`, not `unauthorized` or `offline`.
If `lsusb` doesn't show the phone, try a different USB port/cable before
changing udev or Docker settings.

**Wi-Fi (Android 11+):** On the same LAN, enable Wireless debugging and choose
*Pair device with pairing code*. Use its one-time pair port:

```bash
./compile_android.sh --wifi-pair PHONE_IP:PAIR_PORT
./compile_android.sh --wifi
```

When mDNS discovery fails, use the separate debugging **connection** port:

```bash
./compile_android.sh --wifi-connect PHONE_IP:CONNECT_PORT
```

Pairing and USB authorization require action on the phone. The build scripts
intentionally do not enable legacy unauthenticated `adb tcpip 5555`.

## Current device-tested behavior

The synthetic bgfx grid, colored network lines and junctions appear under QML
on a physical Android phone, matching the Linux scene. Basic map gestures
have been reported working. **No comprehensive lifecycle or stress results
have been recorded yet.** Do not interpret successful APK compilation as
proof of rotation/resume behavior.

## Acceptance protocol: lifecycle and recovery (NEXT)

Record phone model, Android version, GPU/driver and app version. Retain
screenshots and logs for any failure. Perform each case with the bgfx scene
visible and panned/zoomed away from default:

| Test | Action | Passing behavior |
| --- | --- | --- |
| Rotation | Portrait → landscape → portrait repeatedly | Geometry, coordinate mapping, touch and QML remain intact |
| Home/resume | Background and return repeatedly | Rendering resumes; no stale/black frames persist |
| App switch | Switch between heavy apps | Scene remains usable; counters reflect lifecycle changes |
| Display off/on | Lock and unlock repeatedly | Render thread resumes without crash or frozen overlay |
| Resource reinitialization | Trigger Qt scene-graph invalidation where possible | GPU resources released/recreated once per valid lifecycle |
| Gesture interruption | Pinch/pan immediately before background/rotation | Controls never get stuck or jump uncontrollably |
| Extended run | Pan/zoom for a sustained session | No accumulating GPU/CPU memory or thermal collapse |

Run at least 10 repeated transitions for each ordinary lifecycle path,
then longer mixed-use trials. A Qt persistence hint does not guarantee actual
EGL context destruction; record whether scenegraph or bgfx init counts
changed. **A test that never caused resource recreation cannot prove context
recovery.** Missing functionality or difficult-to-force context loss should
remain an explicit open test.

## Acceptance protocol: performance (HARNESS READY; RESULTS PENDING)

Select Demo/~1k/~10k/~100k/~1M in the top bar. Wait until tile uploads settle
then tap **Record 15s** for a per-frame CSV and p50/p95/p99 frame intervals.
Presets describe total model features (nodes + pipes), whereas the UI reports
currently visible sampled nodes/pipes, LOD and tile count. The private CSV
location appears in the results and logcat. Use `adb shell run-as org.aowis.mobile`
for debug-package retrieval where supported; no extra Android permission is
needed.

Run the same geometry size and script of
pan/zoom interactions on Linux and Android. Record frame interval median,
p95/p99 and inferred frame misses relative to the device refresh period; app CPU, bgfx GPU timing if valid, RSS, GPU
resource sizes, draw calls, startup and sustained/thermal behavior. Compare a
cold run and a warmed-up run. State visible-object count and whether culling
is active. Record platform display refresh rate and resolution.

Do not claim that the current QML-only Canvas is a fair performance baseline
for a large vertex-buffer network. Compare rendering strategies only with
similar geometry, visual output and input workloads.

## Diagnostics

```bash
adb logcat -v time | grep --line-buffered -E 'AOWIS render lab|AOWIS Mobile benchmark'
adb devices -l
```

The log message prefix retains `render lab` as an **internal diagnostic
identifier**. It is not the app name. Look at `bgfx frames`, `frame interval`,
`bgfx inits`, and `Scenegraph inits/releases` in the on-screen status panel.
Capture full logcat if the app crashes; a filtered log alone may miss EGL,
Android or driver errors.

## Known technical boundaries

Qt Quick owns the OpenGL/EGL window and final presentation; bgfx borrows that
context and uses `BGFX_FRAME_FLUSH`. The app has not established a universal
fallback for devices whose GLES implementation rejects this arrangement.
Android device approval cannot be automated. iOS builds are not yet supported.
