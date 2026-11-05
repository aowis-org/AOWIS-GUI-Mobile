#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=tools/qt-android/toolchain_versions.sh
source "$project_dir/tools/qt-android/toolchain_versions.sh"

command -v docker >/dev/null 2>&1 || {
    echo 'Docker is required (on Arch: sudo pacman -S docker; enable/start docker.service).' >&2
    exit 1
}

# Follow the GUI WASM Docker convention: host network avoids broken DNS on
# systems with a local stub resolver, and --pull keeps Ubuntu base current.
network="${AOWIS_DOCKER_BUILD_NETWORK:-host}"
echo '=== Preparing AOWIS Mobile Android Docker toolchain ==='
echo "Qt:         $AOWIS_QT_ANDROID_VERSION"
echo "Android SDK: $AOWIS_ANDROID_SDK_API (build-tools $AOWIS_ANDROID_BUILD_TOOLS_VERSION)"
echo "Android NDK: $AOWIS_ANDROID_NDK_VERSION"
echo "Image:      $AOWIS_ANDROID_IMAGE"
echo "Docker network: $network"

docker build --network="$network" --pull \
    --build-arg "QT_VERSION=$AOWIS_QT_ANDROID_VERSION" \
    --build-arg "AQT_VERSION=$AOWIS_ANDROID_AQT_VERSION" \
    --build-arg "ANDROID_API=$AOWIS_ANDROID_SDK_API" \
    --build-arg "ANDROID_BUILD_TOOLS=$AOWIS_ANDROID_BUILD_TOOLS_VERSION" \
    --build-arg "ANDROID_NDK_VERSION=$AOWIS_ANDROID_NDK_VERSION" \
    --build-arg "CMDLINE_TOOLS_ID=$AOWIS_ANDROID_CMDLINE_TOOLS_ID" \
    --build-arg "CMDLINE_TOOLS_SHA256=$AOWIS_ANDROID_CMDLINE_TOOLS_SHA256" \
    -f "$project_dir/tools/qt-android/Dockerfile" \
    -t "$AOWIS_ANDROID_IMAGE" \
    "$project_dir/tools/qt-android"

echo '=== AOWIS Mobile Android Docker toolchain ready ==='
