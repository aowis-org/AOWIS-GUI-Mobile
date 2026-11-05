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
android_build_tools="${AOWIS_ANDROID_BUILD_TOOLS_VERSION:-36.0.0}"

[[ "$bgfx" == ON || "$bgfx" == OFF ]] || { echo 'Invalid AOWIS_ANDROID_BGFX.' >&2; exit 2; }
[[ "$build_name" =~ ^build-android-[a-zA-Z0-9_-]+$ ]] || { echo 'Invalid build directory name.' >&2; exit 2; }
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid parallel job count.' >&2; exit 2; }
[[ -x "$qt_cmake" && -d "$qt_host" && -d "$ndk" ]] || {
    echo 'Container Android toolchain is incomplete; rebuild using ./prepare_android_docker.sh.' >&2
    exit 1
}
render_cmake_args=()
if [[ "$bgfx" == ON ]]; then
    [[ -f "$project_dir/external/bgfx.cmake/bgfx/include/bgfx/bgfx.h" ]] || {
        echo 'bgfx submodules missing; run ./tools/init_bgfx.sh on the host.' >&2
        exit 1
    }
    [[ -f "${AOWIS_ANDROID_RENDER_SOURCE_DIR:-}/CMakeLists.txt" ]] || {
        echo 'AOWIS-GUI-Render checkout missing from Android container.' >&2
        exit 1
    }
    render_cmake_args=(-DAOWIS_RENDER_SOURCE_DIR="$AOWIS_ANDROID_RENDER_SOURCE_DIR")
fi

# Preserve the Android debug certificate between Docker runs. The host binds
# this directory from its persistent XDG data home. Never fall back to a
# temporary container-only signing key, even if Gradle would generate one.
signing_home="${ANDROID_USER_HOME:-}"
[[ "$signing_home" == /tmp/.android && -d "$signing_home" ]] || {
    echo 'Persistent Android signing directory is not mounted at /tmp/.android.' >&2
    exit 1
}
signing_key="$signing_home/debug.keystore"
"$project_dir/tools/qt-android/prepare_debug_signing.sh"

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
    -DAOWIS_MOBILE_WITH_BGFX="$bgfx" \
    "${render_cmake_args[@]}"
cmake --build "$build_dir" --target aowis-mobile_make_apk --parallel "$jobs"

# Verify the actual produced APK has the same signature as the persistent
# keystore, rather than trusting the host mount or Gradle defaults alone.
apk_dir="$build_dir/android-build/build/outputs/apk/debug"
mapfile -t built_apks < <(find "$apk_dir" -maxdepth 1 -type f -name '*.apk' -print | sort)
(( ${#built_apks[@]} == 1 )) || { echo "Expected one debug APK under $apk_dir." >&2; exit 1; }
apksigner="$ANDROID_SDK_ROOT/build-tools/$android_build_tools/apksigner"
[[ -x "$apksigner" ]] || { echo "Android apksigner unavailable: $apksigner" >&2; exit 1; }
key_sha="$(keytool -list -v -keystore "$signing_key" -storepass android \
    -alias androiddebugkey | awk '/^[[:space:]]*SHA256:/ {print $2; exit}' | tr -d ':' | tr '[:upper:]' '[:lower:]')"
apk_sha="$("$apksigner" verify --print-certs "${built_apks[0]}" \
    | awk '/Signer #1 certificate SHA-256 digest:/ {print $6; exit}' \
    | tr '[:upper:]' '[:lower:]')"
if [[ ! "$key_sha" =~ ^[0-9a-f]{64}$ || "$key_sha" != "$apk_sha" ]]; then
    echo 'APK signature DOES NOT match the persistent debug keystore; refusing deployment.' >&2
    echo "Keystore SHA-256: $key_sha" >&2
    echo "APK SHA-256:      $apk_sha" >&2
    exit 1
fi
echo "=== APK signing certificate verified: $key_sha ==="
