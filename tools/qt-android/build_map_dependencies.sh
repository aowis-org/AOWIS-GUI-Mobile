#!/usr/bin/env bash
set -euo pipefail

# Build target-only static libraries with the same NDK/API as the APK.
# Cache source checkouts and Ninja outputs in the host build directory.
[[ $# -eq 4 ]] || { echo 'Usage: build_map_dependencies.sh NDK API JOBS BUILD_ROOT' >&2; exit 2; }
ndk="$1"
api="$2"
jobs="$3"
root="$4"
[[ "$api" =~ ^[0-9]+$ && "$jobs" =~ ^[1-9][0-9]*$ && "$root" == /* ]] || exit 2
[[ -f "$ndk/build/cmake/android.toolchain.cmake" ]] || { echo 'NDK toolchain not found.' >&2; exit 1; }
mkdir -p "$root/sources" "$root/build" "$root/prefix"
prefix="$root/prefix"

# Deliberate release versions; retain the resolved Git commits locally and
# never pull or advance existing source checkouts during a build.
checkout() {
    local name="$1" url="$2" version="$3" source="$root/sources/$1-$3"
    if [[ ! -d "$source/.git" ]]; then
        [[ ! -e "$source" ]] || { echo "Unexpected dependency path: $source" >&2; exit 1; }
        git clone --quiet --depth 1 --branch "$version" -- "$url" "$source"
    fi
    [[ "$(git -C "$source" remote get-url origin)" == "$url" ]] || {
        echo "Unexpected origin for $name" >&2; exit 1;
    }
    [[ "$(git -C "$source" rev-parse HEAD)" == "$(git -C "$source" rev-parse "refs/tags/$version^{commit}")" ]] || {
        echo "Unexpected revision for $name; expected $version" >&2; exit 1;
    }
    [[ -z "$(git -C "$source" status --porcelain)" ]] || {
        echo "Modified dependency source: $source" >&2; exit 1;
    }
    printf '%s %s %s\n' "$name" "$version" "$(git -C "$source" rev-parse HEAD)" >> "$root/source-revisions.txt"
}

build() {
    local name="$1" version="$2" subdir="$3"
    shift 3
    local source="$root/sources/$name-$version"
    local output="$root/build/$name-$version"
    echo "=== Building Android ARM64 $name $version ==="
    cmake -S "$source/$subdir" -B "$output" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-$api" \
        -DANDROID_STL=c++_shared -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_FIND_ROOT_PATH="$prefix" -DCMAKE_PREFIX_PATH="$prefix" \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF "$@"
    cmake --build "$output" --parallel "$jobs"
    cmake --install "$output"
}

: > "$root/source-revisions.txt"
checkout libpng https://github.com/pnggroup/libpng.git v1.6.50
build libpng v1.6.50 . -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF
checkout libjpeg-turbo https://github.com/libjpeg-turbo/libjpeg-turbo.git 3.1.2
build libjpeg-turbo 3.1.2 . -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=ON -DWITH_TOOLS=OFF -DWITH_TESTS=OFF
checkout zstd https://github.com/facebook/zstd.git v1.5.7
build zstd v1.5.7 build/cmake -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_STATIC=ON -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_CONTRIB=OFF
checkout libtiff https://gitlab.com/libtiff/libtiff.git v4.7.1
# Terrain uses lossless GeoTIFF (including LZW/Deflate). Keep zlib support via
# the NDK; disable optional codecs and command-line tools for this first build.
build libtiff v4.7.1 . -Dtiff-tools=OFF -Dtiff-tests=OFF -Dtiff-contrib=OFF -Dtiff-docs=OFF \
    -Djpeg=OFF -Dold-jpeg=OFF -Djbig=OFF -Dlzma=OFF -Dwebp=OFF -Dzstd=OFF \
    -Dlerc=OFF -Dlibdeflate=OFF -Dcxx=OFF -Dzlib=ON

readelf="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
for archive in libpng16.a libturbojpeg.a libzstd.a libtiff.a; do
    file="$prefix/lib/$archive"
    [[ -s "$file" ]] || { echo "Missing target archive: $file" >&2; exit 1; }
    # Check every archive member rather than trusting a filename or host library.
    headers="$("$readelf" -h "$file")"
    machines="$(printf '%s\n' "$headers" | sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p' | sort -u)"
    [[ "$machines" == AArch64 ]] || { echo "Not an ARM64-only archive: $file ($machines)" >&2; exit 1; }
done
[[ -f "$prefix/lib/cmake/zstd/zstdConfig.cmake" || -f "$prefix/lib/cmake/zstd/zstd-config.cmake" ]] || {
    echo 'Missing target zstd CMake package' >&2; exit 1;
}
echo "=== Android MAP dependencies ready: $prefix ==="
