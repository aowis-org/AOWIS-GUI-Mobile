#!/usr/bin/env bash
set -euo pipefail

# Executed only by compile_android.sh inside the pinned toolchain image.
project_dir=/project
qt_version="${AOWIS_QT_ANDROID_VERSION:-6.12.0}"
ndk_version="${AOWIS_ANDROID_NDK_VERSION:-27.2.12479018}"
qt_cmake="/opt/qt/$qt_version/android_arm64_v8a/bin/qt-cmake"
qt_host="/opt/qt/$qt_version/gcc_64"
ndk="$ANDROID_SDK_ROOT/ndk/$ndk_version"
bgfx="${AOWIS_ANDROID_BGFX:-ON}"
build_name="${AOWIS_ANDROID_BUILD_NAME:-build-android-bgfx}"
jobs="${AOWIS_ANDROID_JOBS:-8}"

[[ "$bgfx" == ON || "$bgfx" == OFF ]] || { echo 'Invalid AOWIS_ANDROID_BGFX.' >&2; exit 2; }
[[ "$build_name" =~ ^build-android-[a-zA-Z0-9_-]+$ ]] || { echo 'Invalid build directory name.' >&2; exit 2; }
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid parallel job count.' >&2; exit 2; }
[[ -x "$qt_cmake" && -d "$qt_host" && -d "$ndk" ]] || {
    echo 'Container Android toolchain is incomplete; rebuild using ./prepare_android_docker.sh.' >&2
    exit 1
}
if [[ "$bgfx" == ON ]]; then
    [[ -f "$project_dir/external/bgfx.cmake/bgfx/include/bgfx/bgfx.h" ]] || {
        echo 'bgfx submodules missing; run ./tools/init_bgfx.sh on the host.' >&2
        exit 1
    }
fi

# Keep all outputs in the host repository, as with GUI WASM and Standalone.
# Only build the APK target, never the CTest programs, bgfx tools or examples.
build_dir="$project_dir/$build_name"
echo "=== Building AOWIS Mobile Android ARM64 ($bgfx) inside Docker ==="
echo "Qt:    $qt_version"
echo "NDK:   $ndk_version"
echo "Build: $build_dir"
"$qt_cmake" -S "$project_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=OFF \
    -DQT_HOST_PATH="$qt_host" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    -DANDROID_NDK_ROOT="$ndk" \
    -DAOWIS_MOBILE_WITH_BGFX="$bgfx"
cmake --build "$build_dir" --target aowis-mobile-render-lab_make_apk --parallel "$jobs"
