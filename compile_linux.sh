#!/usr/bin/env bash
set -euo pipefail

# AOWIS desktop Qt Quick/bgfx app: targeted native Linux build.
# Use Arch's installed Qt 6, never Android's Docker Qt cross-toolchain.
PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"

usage() {
    cat <<'HELP'
Usage: ./compile_linux.sh [options]

  (default)       Build optimized Qt Quick + bgfx renderer and launch it.
  --build-only    Build without launching the application.
  --baseline      Build and launch the QML-only baseline (no bgfx).
  --tests         Also build and run the targeted C++ regression tests.
  --help          Show this help.

Environment:
  AOWIS_BUILD_TYPE  CMake build type (default RelWithDebInfo).
  AOWIS_LINUX_JOBS Parallel build jobs (default number of available CPUs).

Requires CMake, Ninja, and host Qt 6 (Core, Gui, Qml, Quick, QuickControls2, Positioning, Sensors).
On Arch: sudo pacman -S --needed cmake ninja qt6-base qt6-declarative qt6-positioning qt6-sensors
For bgfx and the shared renderer: ./git_submodule_init.sh.
For MODEL registration (once): ./tools/init_model.sh.
HELP
}

build_only=false
baseline=false
run_tests=false
while (( $# > 0 )); do
    case "$1" in
        --build-only) build_only=true ;;
        --baseline) baseline=true ;;
        --tests) run_tests=true ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
    shift
done

for tool in cmake ninja; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'AOWIS Mobile Linux: missing required command: %s\n' "$tool" >&2
        exit 1
    }
done

BUILD_TYPE="${AOWIS_BUILD_TYPE:-RelWithDebInfo}"
JOBS="${AOWIS_LINUX_JOBS:-$(nproc)}"
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || {
    printf 'AOWIS Mobile Linux: AOWIS_LINUX_JOBS must be a positive integer.\n' >&2
    exit 1
}

if [[ ! -f "$PROJECT_DIR/external/AOWIS-SERVER-MODEL/CMakeLists.txt" ||
      ! -f "$PROJECT_DIR/external/AOWIS-SERVER-MODEL/include/aowis/model/units/unit_profile.h" ]]; then
    printf 'AOWIS Mobile Linux: MODEL submodule missing. Run ./tools/init_model.sh once.\n' >&2
    exit 1
fi

BUILD_DIR="$PROJECT_DIR/build-linux-bgfx"
BGFX=ON
if "$baseline"; then
    BUILD_DIR="$PROJECT_DIR/build-linux-lab"
    BGFX=OFF
else
    for file in \
        external/bgfx.cmake/CMakeLists.txt \
        external/bgfx.cmake/bgfx/include/bgfx/bgfx.h \
        external/bgfx.cmake/bx/include/bx/bx.h \
        external/bgfx.cmake/bimg/include/bimg/bimg.h; do
        if [[ ! -f "$PROJECT_DIR/$file" ]]; then
            printf 'AOWIS Mobile Linux: bgfx submodules missing (%s).\n' "$file" >&2
            printf 'Run ./git_submodule_init.sh once, then rerun ./compile_linux.sh.\n' >&2
            exit 1
        fi
    done
fi

TESTING=OFF
"$run_tests" && TESTING=ON
printf '=== Building AOWIS Mobile Linux (%s, %s) ===\n' "$BUILD_TYPE" "$BGFX"
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DAOWIS_MOBILE_WITH_BGFX="$BGFX" \
    -DBUILD_TESTING="$TESTING"
# Build only the application and its actual dependencies, not every test/tool.
cmake --build "$BUILD_DIR" --target aowis-mobile --parallel "$JOBS"

if "$run_tests"; then
    cmake --build "$BUILD_DIR" --target aowis-mobile-gpu-scene-tests aowis-mobile-gpu-stress-tests aowis-mobile-lifecycle-tests aowis-mobile-map-server-settings-tests aowis-mobile-appearance-settings-tests aowis-mobile-language-settings-tests aowis-mobile-unit-profile-settings-tests aowis-mobile-unit-registry-contract-tests aowis-mobile-location-snapshot-tests aowis-mobile-compass-heading-tests aowis-mobile-gradienter-math-tests aowis-mobile-plumb-bob-math-tests aowis-mobile-flashlight-tests --parallel "$JOBS"
    ctest --test-dir "$BUILD_DIR" --output-on-failure -R '^aowis-mobile-(gpu-(scene|stress)|lifecycle|map-server-settings|appearance-settings|language-settings|unit-profile-settings|unit-registry-contract|location-snapshot|compass-heading|gradienter-math|plumb-bob-math|flashlight)$'
fi

if "$build_only"; then
    printf '=== Linux application ready: %s/aowis-mobile ===\n' "$BUILD_DIR"
    exit 0
fi

if "$baseline"; then
    exec "$BUILD_DIR/aowis-mobile" --baseline
fi
exec "$BUILD_DIR/aowis-mobile" --bgfx-underlay
