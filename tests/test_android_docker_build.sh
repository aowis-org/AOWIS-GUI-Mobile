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
        case "${FAKE_DEVICE_COUNT:-0}" in
            0) printf 'List of devices attached\n' ;;
            1) printf 'List of devices attached\nmobile1\tdevice\n' ;;
            2) printf 'List of devices attached\nmobile1\tdevice\nmobile2\tdevice\n' ;;
        esac ;;
    -s)
        if [[ "${3:-}" == get-state ]]; then printf 'device\n'; fi ;;
esac
MOCK_ADB
chmod +x "$work/bin/docker" "$work/bin/adb"
export PATH="$work/bin:$PATH" MOCK_LOG="$work/commands.log" MOCK_ROOT="$work/repo"
export XDG_CACHE_HOME="$work/cache"
cd "$work/repo"

fail() { echo "FAILED: $*" >&2; exit 1; }
run_case() {
    : > "$MOCK_LOG"
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

# Cached image, explicit build-only, with device: must not deploy.
export FAKE_IMAGE_EXISTS=1 FAKE_DEVICE_COUNT=1
run_case --build-only
assert_not_log 'docker build '
assert_log 'docker run '
assert_not_log 'adb '

# One authorized device deploys; baseline produces its own output.
run_case --baseline
assert_log 'AOWIS_ANDROID_BUILD_NAME=build-android-baseline'
assert_log 'adb -s mobile1 install -r'
assert_log 'adb -s mobile1 shell monkey'

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
[[ "$AOWIS_ANDROID_TOOLCHAIN_REVISION" -ge 2 ]] || fail 'image version was not bumped'
grep -Fq -- 'aqtinstall @ git+https://github.com/miurahr/aqtinstall.git@${AQT_GIT_REF}' "$root/tools/qt-android/Dockerfile" || fail 'pinned modern aqt is missing'
grep -Fq -- '"${QT_VERSION}" linux_gcc_64' "$root/tools/qt-android/Dockerfile" || fail 'old Linux Qt archive architecture'
grep -Fq -- '"${QT_VERSION}" android_arm64_v8a' "$root/tools/qt-android/Dockerfile" || fail 'Android Qt architecture changed'
grep -Fq -- 'AQT_GIT_REF=$AOWIS_ANDROID_AQT_GIT_REF' "$root/prepare_android_docker.sh" || fail 'aqt pin not passed to Docker build'

printf 'Android Docker entrypoint: 8 scenarios + Qt repository contract passed\n' 
