#!/usr/bin/env bash
set -euo pipefail

# Test the Linux build entry point without requiring Qt, bgfx or a GPU.
REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf -- "$TMP_DIR"' EXIT
mkdir -p "$TMP_DIR/repo" "$TMP_DIR/mockbin"
cp "$REPO_DIR/compile_linux.sh" "$TMP_DIR/repo/compile_linux.sh"
chmod +x "$TMP_DIR/repo/compile_linux.sh"

cat > "$TMP_DIR/mockbin/cmake" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
printf 'cmake %s\n' "$*" >> "$AOWIS_LINUX_TEST_LOG"
if [[ "${1:-}" == '--build' ]]; then
    build_dir="$2"
    mkdir -p "$build_dir"
    cat > "$build_dir/aowis-mobile" <<'PROGRAM'
#!/usr/bin/env bash
printf 'run %s\n' "$*" >> "$AOWIS_LINUX_TEST_LOG"
PROGRAM
    chmod +x "$build_dir/aowis-mobile"
fi
STUB
cat > "$TMP_DIR/mockbin/ctest" <<'STUB'
#!/usr/bin/env bash
printf 'ctest %s\n' "$*" >> "$AOWIS_LINUX_TEST_LOG"
STUB
chmod +x "$TMP_DIR/mockbin/cmake" "$TMP_DIR/mockbin/ctest"
export PATH="$TMP_DIR/mockbin:$PATH"
export AOWIS_LINUX_TEST_LOG="$TMP_DIR/build.log"
export AOWIS_LINUX_JOBS=3

assert_contains() {
    if ! grep -F -- "$1" "$AOWIS_LINUX_TEST_LOG" >/dev/null; then
        printf 'Missing expected command: %s\n' "$1" >&2
        cat "$AOWIS_LINUX_TEST_LOG" >&2
        exit 1
    fi
}
assert_not_contains() {
    if grep -F -- "$1" "$AOWIS_LINUX_TEST_LOG" >/dev/null; then
        printf 'Unexpected command: %s\n' "$1" >&2
        cat "$AOWIS_LINUX_TEST_LOG" >&2
        exit 1
    fi
}

# Baseline must work without any bgfx sources.
: > "$AOWIS_LINUX_TEST_LOG"
"$TMP_DIR/repo/compile_linux.sh" --baseline --build-only
assert_contains '-DAOWIS_MOBILE_WITH_BGFX=OFF'
assert_contains '-DBUILD_TESTING=OFF'
assert_contains 'build-linux-lab'
assert_contains '--target aowis-mobile --parallel 3'
assert_not_contains 'run '
printf 'PASS: baseline build only\n'

# Missing bgfx should fail early, without configuring CMake.
: > "$AOWIS_LINUX_TEST_LOG"
if "$TMP_DIR/repo/compile_linux.sh" --build-only >"$TMP_DIR/error.log" 2>&1; then
    echo 'Expected failure for missing bgfx checkout' >&2; exit 1
fi
grep -F './git_submodule_init.sh' "$TMP_DIR/error.log" >/dev/null
[[ ! -s "$AOWIS_LINUX_TEST_LOG" ]]
printf 'PASS: missing pinned bgfx diagnostics\n'

mkdir -p "$TMP_DIR/repo/external/bgfx.cmake/bgfx/include/bgfx" \
    "$TMP_DIR/repo/external/bgfx.cmake/bx/include/bx" \
    "$TMP_DIR/repo/external/bgfx.cmake/bimg/include/bimg"
touch "$TMP_DIR/repo/external/bgfx.cmake/CMakeLists.txt" \
      "$TMP_DIR/repo/external/bgfx.cmake/bgfx/include/bgfx/bgfx.h" \
      "$TMP_DIR/repo/external/bgfx.cmake/bx/include/bx/bx.h" \
      "$TMP_DIR/repo/external/bgfx.cmake/bimg/include/bimg/bimg.h"

: > "$AOWIS_LINUX_TEST_LOG"
"$TMP_DIR/repo/compile_linux.sh"
assert_contains '-DAOWIS_MOBILE_WITH_BGFX=ON'
assert_contains '-DCMAKE_BUILD_TYPE=RelWithDebInfo'
assert_contains '-DBUILD_TESTING=OFF'
assert_contains 'build-linux-bgfx'
assert_contains 'run --bgfx-underlay'
assert_not_contains 'ctest '
printf 'PASS: targeted optimized bgfx build and launch\n'

: > "$AOWIS_LINUX_TEST_LOG"
AOWIS_BUILD_TYPE=Debug "$TMP_DIR/repo/compile_linux.sh" --build-only --tests
assert_contains '-DCMAKE_BUILD_TYPE=Debug'
assert_contains '-DBUILD_TESTING=ON'
assert_contains '--target aowis-mobile-gpu-scene-tests aowis-mobile-gpu-stress-tests aowis-mobile-lifecycle-tests --parallel 3'
assert_contains '^aowis-mobile-(gpu-(scene|stress)|lifecycle)$'
assert_contains 'ctest --test-dir '
assert_not_contains 'run '
printf 'PASS: optional geometry, stress and lifecycle tests and build type override\n'

: > "$AOWIS_LINUX_TEST_LOG"
"$TMP_DIR/repo/compile_linux.sh" --baseline
assert_contains 'run --baseline'
assert_not_contains 'run --bgfx-underlay'
printf 'PASS: baseline launch\n'

if AOWIS_LINUX_JOBS=xyz "$TMP_DIR/repo/compile_linux.sh" --build-only >"$TMP_DIR/error.log" 2>&1; then
    echo 'Expected rejection of invalid AOWIS_LINUX_JOBS' >&2; exit 1
fi
if "$TMP_DIR/repo/compile_linux.sh" --does-not-exist >"$TMP_DIR/error.log" 2>&1; then
    echo 'Expected rejection of unknown option' >&2; exit 1
fi
printf 'PASS: invalid argument handling\n'
echo 'Linux compile script tests passed.'
