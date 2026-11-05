#!/usr/bin/env bash
set -euo pipefail

# Fast, network-free contract tests for the host Docker build/deploy workflow.
# A real Android Qt/GLES test still requires building the Docker image + a phone.
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/repo/tools/qt-android" "$work/repo/external/bgfx.cmake/bgfx/include/bgfx" "$work/bin" "$work/cache"
cp "$root/compile_android.sh" "$root/prepare_android_docker.sh" "$work/repo/"
cp "$root/tools/qt-android/"*.sh "$work/repo/tools/qt-android/"
touch "$work/repo/external/bgfx.cmake/bgfx/include/bgfx/bgfx.h"
cat > "$work/bin/docker" <<'MOCK_DOCKER'
#!/usr/bin/env bash
set -euo pipefail
printf 'docker %s\n' "$*" >> "$MOCK_LOG"
case "${1:-}" in
    image)
        [[ "${FAKE_IMAGE_EXISTS:-1}" == 1 ]] ;;
    build)
        [[ "${FAKE_IMAGE_BUILD_OK:-1}" == 1 ]] ;;
    run)
        [[ "${FAKE_RUN_OK:-1}" == 1 ]] || exit 29
        name=build-android-bgfx
        for arg in "$@"; do
            [[ "$arg" == 'AOWIS_ANDROID_BUILD_NAME='* ]] && name="${arg#*=}"
        done
        output="$MOCK_ROOT/$name/android-build/build/outputs/apk/debug"
        mkdir -p "$output"
        printf 'mock apk\n' > "$output/aowis-mobile-debug.apk" ;;
    *) exit 99 ;;
esac
MOCK_DOCKER
cat > "$work/bin/adb" <<'MOCK_ADB'
#!/usr/bin/env bash
set -euo pipefail
printf 'adb %s\n' "$*" >> "$MOCK_LOG"
case "${1:-}" in
    devices)
        printf 'List of devices attached\n'
        if [[ -n "${FAKE_ADB_DEVICES:-}" ]]; then
            printf '%b\n' "$FAKE_ADB_DEVICES"
        else
            case "${FAKE_DEVICE_COUNT:-0}" in
                0) : ;;
                1) printf 'mobile1\tdevice usb:1-2 model:Phone\n' ;;
                2) printf 'mobile1\tdevice usb:1-2\nmobile2\tdevice usb:1-3\n' ;;
            esac
        fi
        if [[ -s "$MOCK_ROOT/wifi-connected" ]]; then
            printf '%s\tdevice product:wifi\n' "$(cat "$MOCK_ROOT/wifi-connected")"
        fi ;;
    mdns)
        [[ "${2:-}" == services ]] || exit 20
        printf '%b\n' "${FAKE_MDNS:-}" ;;
    pair)
        [[ "${FAKE_PAIR_OK:-1}" == 1 ]] || exit 26
        printf 'Successfully paired to %s\n' "$2" ;;
    connect)
        [[ "${FAKE_CONNECT_OK:-1}" == 1 ]] || exit 27
        printf '%s' "$2" > "$MOCK_ROOT/wifi-connected"
        printf 'connected to %s\n' "$2" ;;
    -s)
        if [[ "${3:-}" == get-state ]]; then printf 'device\n'; fi ;;
esac
MOCK_ADB
chmod +x "$work/bin/docker" "$work/bin/adb"
export PATH="$work/bin:$PATH" MOCK_LOG="$work/commands.log" MOCK_ROOT="$work/repo"
export XDG_CACHE_HOME="$work/cache" XDG_DATA_HOME="$work/data"
cd "$work/repo"

fail() { echo "FAILED: $*" >&2; exit 1; }
run_case() {
    : > "$MOCK_LOG"
    rm -f "$MOCK_ROOT/wifi-connected"
    ./compile_android.sh "$@" > "$work/output.log" 2>&1 || { cat "$work/output.log"; fail "compile_android.sh $*"; }
}
assert_log() { grep -Fq -- "$1" "$MOCK_LOG" || fail "missing log entry: $1"; }
assert_not_log() { ! grep -Fq -- "$1" "$MOCK_LOG" || fail "unexpected log entry: $1"; }

# First build automatically builds the image, but does not install without ADB.
export FAKE_IMAGE_EXISTS=0 FAKE_DEVICE_COUNT=0
run_case
assert_log 'docker build '
assert_log 'docker run '
assert_not_log 'adb -s '
# The signing key directory lives OUTSIDE the source tree and is mounted for
# every Docker invocation, regardless of local checkout names or build mode.
[[ -d "$work/data/aowis-gui-mobile/android-debug-signing" ]] || fail 'persistent signing dir not created'
assert_log "$work/data/aowis-gui-mobile/android-debug-signing:/tmp/.android"
assert_log '-e ANDROID_USER_HOME=/tmp/.android'
assert_log '-e AOWIS_ANDROID_BUILD_TOOLS_VERSION=36.0.0'

# Cached image, explicit build-only, with device: must not deploy.
export FAKE_IMAGE_EXISTS=1 FAKE_DEVICE_COUNT=1
run_case --build-only
assert_not_log 'docker build '
assert_log 'docker run '
assert_not_log 'adb '

# One authorized device deploys; baseline produces its own output.
run_case --baseline
assert_log "$work/data/aowis-gui-mobile/android-debug-signing:/tmp/.android"
assert_log 'AOWIS_ANDROID_BUILD_NAME=build-android-baseline'
assert_log 'adb -s mobile1 install -r'
assert_log 'adb -s mobile1 shell monkey -p org.aowis.mobile -c android.intent.category.LAUNCHER 1'

# Multiple devices cannot be chosen implicitly, and explicit --run requires one.
export FAKE_DEVICE_COUNT=2
run_case
assert_not_log 'adb -s '
if ./compile_android.sh --run > "$work/output.log" 2>&1; then fail '--run accepted multiple devices'; fi

# Selecting a serial explicitly handles several devices.
export ANDROID_SERIAL=mobile2
run_case --run
assert_log 'adb -s mobile2 install -r'
unset ANDROID_SERIAL

# On a build failure the host must never install an older APK.
export FAKE_RUN_OK=0 FAKE_DEVICE_COUNT=1
: > "$MOCK_LOG"
if ./compile_android.sh > "$work/output.log" 2>&1; then fail 'Docker build failure accepted'; fi
assert_not_log 'adb -s '
unset FAKE_RUN_OK

# Normal auto-selection must prefer USB when the same phone is paired on Wi-Fi.
export FAKE_ADB_DEVICES='usb01\tdevice usb:2-2 model:Handset\n192.168.10.7:38821\tdevice product:wifi'
unset FAKE_DEVICE_COUNT
run_case --run
assert_log 'adb -s usb01 install -r'
assert_not_log 'adb -s 192.168.10.7:38821 install'
run_case --run --wifi
assert_log 'adb -s 192.168.10.7:38821 install -r'

# An unauthorized USB device must never be chosen. Paired Wi-Fi remains usable.
export FAKE_ADB_DEVICES='usb01\tunauthorized usb:2-2\n192.168.10.7:38821\tdevice product:wifi'
run_case --run
assert_log 'adb -s 192.168.10.7:38821 install -r'

# --devices reports state without Docker or requiring bgfx initialization.
: > "$MOCK_LOG"
./compile_android.sh --devices > "$work/output.log" 2>&1 || fail '--devices failed'
assert_not_log 'docker '
grep -Fq 'unauthorized' "$work/output.log" || fail 'unauthorized state not displayed'

# Android 11+ mDNS reconnect for a previously paired wireless device.
export FAKE_ADB_DEVICES='' FAKE_DEVICE_COUNT=0
export FAKE_MDNS='adb-test._adb-tls-connect._tcp. 192.168.10.7:38821'
run_case --run --wifi
assert_log 'adb mdns services'
assert_log 'adb connect 192.168.10.7:38821'
assert_log 'adb -s 192.168.10.7:38821 install -r'

# One-time pairing is user-authorized and uses the separate pairing endpoint.
run_case --run --wifi-pair 192.168.10.7:35999
assert_log 'adb pair 192.168.10.7:35999'
assert_log 'adb connect 192.168.10.7:38821'

# --wifi-connect works when mDNS is blocked on the local network.
export FAKE_MDNS=''
run_case --run --wifi-connect 192.168.10.7:38821
assert_log 'adb connect 192.168.10.7:38821'
assert_log 'adb -s 192.168.10.7:38821 install -r'

# A failed pair must stop before building. Never bypass phone authorization.
export FAKE_PAIR_OK=0
: > "$MOCK_LOG"
if ./compile_android.sh --wifi-pair 192.168.10.7:35999 > "$work/output.log" 2>&1; then
    fail 'failed pairing accepted'
fi
assert_not_log 'docker run '
unset FAKE_PAIR_OK FAKE_MDNS FAKE_ADB_DEVICES
rm -f "$MOCK_ROOT/wifi-connected"
export FAKE_DEVICE_COUNT=1

# Never regenerate a previously provisioned but missing keystore. This check
# must fail on the host BEFORE starting a disposable Docker container.
signing_dir="$work/data/aowis-gui-mobile/android-debug-signing"
touch "$signing_dir/.aowis-debug-keystore-created"
: > "$MOCK_LOG"
if ./compile_android.sh --build-only > "$work/output.log" 2>&1; then fail 'lost debug key was accepted'; fi
assert_not_log 'docker run '
rm -f "$signing_dir/.aowis-debug-keystore-created"

# No bgfx sources must fail before trying Docker (but baseline can work).
rm "$work/repo/external/bgfx.cmake/bgfx/include/bgfx/bgfx.h"
: > "$MOCK_LOG"
if ./compile_android.sh --build-only > "$work/output.log" 2>&1; then fail 'missing bgfx accepted'; fi
assert_not_log 'docker run '
run_case --baseline --build-only
assert_log 'docker run '

# Prevent regression to obsolete aqtinstall release or Qt archive architecture.
source "$root/tools/qt-android/toolchain_versions.sh"
[[ "$AOWIS_ANDROID_AQT_GIT_REF" =~ ^[a-f0-9]{40}$ ]] || fail 'aqt ref is not a 40-character commit'
[[ "$AOWIS_ANDROID_TOOLCHAIN_REVISION" -ge 3 ]] || fail 'image version was not bumped'
grep -Fq -- 'aqtinstall @ git+https://github.com/miurahr/aqtinstall.git@${AQT_GIT_REF}' "$root/tools/qt-android/Dockerfile" || fail 'pinned modern aqt is missing'
grep -Fq -- '"${QT_VERSION}" linux_gcc_64' "$root/tools/qt-android/Dockerfile" || fail 'old Linux Qt archive architecture'
grep -Fq -- '"${QT_VERSION}" android_arm64_v8a' "$root/tools/qt-android/Dockerfile" || fail 'Android Qt architecture changed'
grep -Fq -- 'AQT_GIT_REF=$AOWIS_ANDROID_AQT_GIT_REF' "$root/prepare_android_docker.sh" || fail 'aqt pin not passed to Docker build'

# The real device build depends on Qt's host-side tools being executable,
# Android's shared-library install destination, and Qt 6.12 TaskTree addon.
grep -Fq 'libglib2.0-0t64' "$root/tools/qt-android/Dockerfile" || fail 'GLib host runtime missing'
grep -Fq 'libexec/qmlimportscanner' "$root/tools/qt-android/Dockerfile" || fail 'qmlimportscanner missing from preflight'
grep -Fq -- '--modules qttasktree' "$root/tools/qt-android/Dockerfile" || fail 'TaskTree not requested'
grep -Fq 'android_arm64_v8a/lib/cmake/Qt6TaskTree/Qt6TaskTreeConfig.cmake' "$root/tools/qt-android/Dockerfile" || fail 'TaskTree Android validation missing'
grep -Eq '^[[:space:]]+LIBRARY DESTINATION lib$' "$root/CMakeLists.txt" || fail 'Android library install destination missing'
# The pinned bgfx EGL backend uses ANativeWindow_setBuffersGeometry. The
# Android NDK exports it from libandroid.so; GLES/EGL alone cannot resolve it.
grep -Fq 'target_link_libraries(aowis-mobile PRIVATE android)' "$root/CMakeLists.txt" || fail 'bgfx Android native-window library link missing'
grep -Fq -- '--target aowis-mobile_make_apk' "$root/tools/qt-android/build_android_inside_container.sh" || fail 'Android APK build still uses old test target'
grep -Fq 'QT_ANDROID_APP_NAME "AOWIS"' "$root/CMakeLists.txt" || fail 'Android launcher label changed'
grep -Fq 'QT_ANDROID_PACKAGE_NAME "org.aowis.mobile"' "$root/CMakeLists.txt" || fail 'Android package identity changed'

grep -Fq 'keytool -genkeypair -noprompt' "$root/tools/qt-android/prepare_debug_signing.sh" || fail 'persistent debug certificate creation missing'
grep -Fq 'APK signature DOES NOT match' "$root/tools/qt-android/build_android_inside_container.sh" || fail 'APK certificate comparison missing'
grep -Fq 'ANDROID_USER_HOME=/tmp/.android' "$root/compile_android.sh" || fail 'persistent Gradle signing location missing'
grep -Fq 'prepare_debug_signing.sh' "$root/tools/qt-android/build_android_inside_container.sh" || fail 'signing provisioning script not invoked'

printf 'Android Docker entrypoint: 15+ scenarios + build dependency contracts passed\n'

