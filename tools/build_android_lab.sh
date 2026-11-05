#!/usr/bin/env bash
set -euo pipefail

# Reproducible, offline-source Android build using the user's installed Qt kit.
# No SDK/NDK/Qt acquisition is performed here.
main() {
    local root qt_cmake sdk ndk mode suffix build_dir jobs
    root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
    qt_cmake="${AOWIS_ANDROID_QT_CMAKE:-}"
    sdk="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
    ndk="${ANDROID_NDK_ROOT:-}"
    mode="${AOWIS_ANDROID_BGFX:-ON}"
    jobs="${AOWIS_ANDROID_JOBS:-8}"

    if [[ "$mode" != ON && "$mode" != OFF ]]; then
        echo 'AOWIS_ANDROID_BGFX must be ON or OFF.' >&2
        return 2
    fi
    if [[ -z "$qt_cmake" || ! -x "$qt_cmake" ]]; then
        cat >&2 <<'MESSAGE'
Set AOWIS_ANDROID_QT_CMAKE to the qt-cmake executable in an installed Qt
Android kit (for example ~/Qt/6.8.3/android_arm64_v8a/bin/qt-cmake).
Do not use the host Linux Qt kit.
MESSAGE
        return 2
    fi
    if [[ -z "$sdk" || ! -d "$sdk" ]]; then
        echo 'Set ANDROID_SDK_ROOT to an installed Android SDK directory.' >&2
        return 2
    fi
    if [[ -z "$ndk" || ! -d "$ndk" ]]; then
        echo 'Set ANDROID_NDK_ROOT to an installed, Qt-compatible NDK directory.' >&2
        return 2
    fi
    if [[ "$mode" == ON && ! -f "$root/external/bgfx.cmake/bgfx/include/bgfx/bgfx.h" ]]; then
        echo 'Initialize pinned bgfx first: ./tools/init_bgfx.sh' >&2
        return 2
    fi

    suffix="bgfx"
    if [[ "$mode" == OFF ]]; then suffix="baseline"; fi
    build_dir="${AOWIS_ANDROID_BUILD_DIR:-$root/build-android-$suffix}"
    "$qt_cmake" -S "$root" -B "$build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DANDROID_SDK_ROOT="$sdk" \
        -DANDROID_NDK_ROOT="$ndk" \
        -DAOWIS_MOBILE_WITH_BGFX="$mode"
    cmake --build "$build_dir" --parallel "$jobs"
    cmake --build "$build_dir" --target apk --parallel "$jobs"
    echo "Android $mode build completed; APK candidates:"
    find "$build_dir" -type f -name '*.apk' -print
    echo 'Install via ./tools/run_android_lab.sh [build-directory-or-apk]'
}
main "$@"
