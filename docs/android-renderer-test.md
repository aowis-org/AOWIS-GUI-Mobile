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
./git_submodule_init.sh        # Render with its pinned bgfx, GeographicLib, MODEL
./compile_android.sh          # build and deploy if one authorized device exists
./compile_android.sh --build-only
./compile_android.sh --devices
./compile_android.sh --run
./compile_android.sh --baseline
./prepare_android_docker.sh   # explicit image preparation/rebuild
```

### Stable Android debug signing

`compile_android.sh` keeps one persistent Android **debug signing key** on the
host, outside the checkout and build directories:

```text
${XDG_DATA_HOME:-$HOME/.local/share}/aowis-gui-mobile/android-debug-signing/debug.keystore
```

The directory is mounted into Docker as `/tmp/.android`, and Gradle's Android
user-home location is pinned there. The first build creates the key; later
builds verify the existing key and compare its certificate to the APK's actual
signature. Rebuilding Docker, removing CMake build directories or renaming the
checkout does not change the signing identity. Back up this keystore: if it is
lost, future updates to an installed APK cannot preserve the signing identity.
You may override its location with `AOWIS_ANDROID_DEBUG_SIGNING_DIR` (absolute
host path), but should not change it casually.

**One-time transition:** APKs built by earlier disposable containers used an
ephemeral debug key, so Android may reject the first update with
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`. Back up all local app data first, then
explicitly run `adb uninstall org.aowis.mobile` and reinstall. The script
never uninstalls automatically; an uninstall deletes private app data.
This is a development/debug certificate, not an app-store release key.

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

The bgfx underlay in Qt's EGL context (the former synthetic test map) has been
exercised on a physical Android phone and on Linux. The AOWIS map that
replaces it uses the same integration ([the map](map.md)) and still needs
device testing. Do not interpret successful APK compilation as proof of
rotation/resume behavior.

## Android lifecycle recovery testing (harness implemented; device results pending)

After installing the latest debug APK (`./compile_android.sh --run`),
configure a map server, let imagery and terrain load and move the camera
away from the initial position. Then run the non-destructive, repeatable ADB test:

```bash
./tools/test_android_lifecycle.sh --cycles 12
# Or: ./tools/test_android_lifecycle.sh --serial DEVICE_SERIAL --cycles 20
./tools/test_android_lifecycle.sh --pull-only
```

This presses Home and resumes the installed application for each cycle, checks
that its PID has not silently changed, and retrieves:

- `benchmarks/lifecycle/<device>-<UTC>/summary.txt`: device, cycles and process restarts;
- `events.csv`: app state, scene-graph events, first-frame recovery, timeouts,
  renderer reinitialization, resident tiles, vertex bytes and process RSS;
- `memory.csv`: `dumpsys meminfo` total PSS sampled after each cycle, when available;
- `meminfo.txt` and `logcat-filtered.txt`: supporting diagnostics.

The application tracks **first bgfx frame after** resume, surface resize or
active scene-graph invalidation. The on-screen Recovery panel shows successful,
stalled and late recoveries, the slowest observed recovery and any scene-graph
invalidation without a current OpenGL context. It records each event to
private app storage immediately, rather than keeping only in-memory counters.

Explicit GPU resource recreation is not offered by the map; scene-graph
invalidation on backgrounding (persistent scene graph and graphics are off on
Android) exercises the same teardown and re-initialization.

A recovery timeout is recorded after **five seconds while the application is
active and its window exposed**. Screen-off/background intervals are not
counted. The ADB script fails on a process restart, a timeout, or if no recovery
event is recorded during the run. It cannot prove visual correctness, that an EGL context was truly
lost, or that native GPU allocations were completely freed; those remain
real-device checks.

Record phone model, Android version, GPU/driver and app version. Retain
screenshots and logs for any failure. Perform each manual case with the bgfx
scene visible and panned/zoomed away from default:

| Test | Action | Passing behavior |
| --- | --- | --- |
| Rotation | Portrait → landscape → portrait repeatedly | Geometry, coordinate mapping, touch and QML remain intact |
| Home/resume | Background and return repeatedly | Rendering resumes; no stale/black frames persist |
| App switch | Switch between heavy apps | Scene remains usable; counters reflect lifecycle changes |
| Display off/on | Lock and unlock repeatedly | Render thread resumes without crash or frozen overlay |
| Resource reinitialization | Trigger Qt scene-graph invalidation where possible | GPU resources released/recreated once per valid lifecycle |
| Gesture interruption | Pinch/pan immediately before background/rotation | Controls never get stuck or jump uncontrollably |
| Extended run | Pan/zoom for a sustained session | No accumulating GPU/CPU memory or thermal collapse |

The automated test covers Home/resume. In addition, manually repeat rotation,
screen lock/unlock, heavy-app switching and gesture interruption on the phone;
do not assume ADB Home cycles cover those different paths.

Run at least 10 repeated transitions for each ordinary lifecycle path,
then longer mixed-use trials. A Qt persistence hint does not guarantee actual
EGL context destruction; record whether scenegraph or bgfx init counts
changed. **A test that never caused resource recreation cannot prove context
recovery.** Missing functionality or difficult-to-force context loss should
remain an explicit open test.

## Acceptance protocol: map performance (RESULTS PENDING)

With a configured map server, pan and zoom the same route in 2D and on the
globe on Linux and Android, cold and with warm tile caches. Record frame
smoothness against the display refresh, memory (`dumpsys meminfo`), startup
and sustained/thermal behaviour, and the device, refresh rate and resolution.

## Diagnostics

```bash
adb logcat -v time | grep --line-buffered -E 'AOWIS lifecycle:|AOWIS map|bgfx map'
adb devices -l
```

Diagnostics shows the map renderer, the map server status, map frames,
bgfx initializations and scene-graph inits/releases.
Capture full logcat if the app crashes; a filtered log alone may miss EGL,
Android or driver errors.

## Known technical boundaries

Qt Quick owns the OpenGL/EGL window and final presentation; bgfx borrows that
context and uses `BGFX_FRAME_FLUSH`. The app has not established a universal
fallback for devices whose GLES implementation rejects this arrangement.
Android device approval cannot be automated. iOS builds are not yet supported.

## Deployment and phone rotation preference

`compile_android.sh` installs the APK with `adb install -r` and opens its launcher
by resolving the installed MAIN/LAUNCHER activity and starting that **explicit
component** with `adb shell am start -W -n`. Deployment verifies the start
response and the application process before reporting success. The lifecycle
runner uses the same launch helper. Neither
script writes `settings put system accelerometer_rotation`, changes
`user_rotation`, sends rotation test events, or configures the device's global
Auto-rotate switch. Android's own rotation preference remains user-controlled.

The Qt-generated manifest keeps normal activity orientation support; the app
reads current screen orientation to transform compass/level/plumb values.
This *does not* alter the global Android auto-rotate setting. If the phone's
system toggle still changes, inspect device settings or other software: the
AOWIS scripts do not include a command to change it.
