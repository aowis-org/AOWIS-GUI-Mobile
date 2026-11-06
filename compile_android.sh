#!/usr/bin/env bash
set -euo pipefail

# AOWIS Mobile: reproducible Docker Android build with host-side USB/Wi-Fi ADB.
# No Qt, SDK, NDK, JDK or Android Studio required on the host.
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
# shellcheck source=tools/qt-android/toolchain_versions.sh
source "$project_dir/tools/qt-android/toolchain_versions.sh"

usage() {
    cat <<'HELP'
Usage: ./compile_android.sh [options]

  (default)                     Build APK in Docker; auto-install when one
                                authorized USB (preferred) or Wi-Fi device exists.
  --run                         Require a device and install/launch after building.
  --build-only                  Build APK; do not use ADB.
  --baseline                    Build QML-only baseline without bgfx.
  --devices                     List USB/Wi-Fi ADB devices without building.
  --wifi                        Discover/reconnect paired Wi-Fi devices via mDNS;
                                prefer Wi-Fi for deployment.
  --wifi-pair HOST:PAIR_PORT     One-time Android 11+ wireless ADB pairing.
                                The phone must display its pairing code.
  --wifi-connect HOST:PORT      Connect directly to an already-paired Android
                                wireless-debugging endpoint (NOT pairing port).
  --help                        Display this help.

Requires Docker for builds and host adb (Arch: android-tools) for deployment.
The Android SDK, NDK and Qt are entirely inside the pinned Docker image.
For USB debugging: enable Developer Options / USB debugging on the phone,
connect its USB cable, and accept the phone's RSA authorization prompt.
For Wi-Fi (Android 11+): same LAN, enable Developer Options / Wireless
Debugging, select 'Pair device with pairing code', and run --wifi-pair with
that screen's HOST:PAIR_PORT. Paired devices can reconnect automatically.

  ANDROID_SERIAL            Choose a specific ADB serial if multiple exist.
  AOWIS_ANDROID_IMAGE       Override pinned Docker image.
  AOWIS_ANDROID_JOBS        Parallel jobs (default 8).
  AOWIS_ANDROID_DEBUG_SIGNING_DIR  Persistent host debug keystore directory
                                (default: XDG data home/aowis-gui-mobile/android-debug-signing).
  AOWIS_DOCKER_RUN_NETWORK  Container network (default host).
  AOWIS_DOCKER_BUILD_NETWORK Docker image-build network (default host).

No automatic 'adb tcpip 5555': that would expose debugging on the LAN.
HELP
}

die() { printf 'AOWIS Mobile Android: %s\n' "$*" >&2; exit 1; }

install_mode=auto
bgfx=ON
list_devices=false
wifi_preferred=false
wifi_pair=
wifi_connect=
while (( $# > 0 )); do
    case "$1" in
        --build-only)
            [[ "$install_mode" != run ]] || die 'Do not combine --run and --build-only.'
            install_mode=build-only ;;
        --run)
            [[ "$install_mode" != build-only ]] || die 'Do not combine --run and --build-only.'
            install_mode=run ;;
        --baseline) bgfx=OFF ;;
        --devices) list_devices=true ;;
        --wifi) wifi_preferred=true ;;
        --wifi-pair|--wifi-connect)
            option="$1"
            shift
            (( $# > 0 )) || die "$option requires an IP:PORT or HOST:PORT argument."
            [[ "$1" =~ ^[A-Za-z0-9._-]+:[0-9]{1,5}$ ]] || die "Invalid $option endpoint: $1"
            if [[ "$option" == --wifi-pair ]]; then wifi_pair="$1"; else wifi_connect="$1"; fi ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
    shift
done

if [[ "$install_mode" == build-only ]] && { [[ -n "$wifi_pair$wifi_connect" ]] || "$wifi_preferred" || "$list_devices"; }; then
    die '--build-only cannot be combined with device options.'
fi

# Device discovery and pairing are intentionally on the host. Docker never
# needs USB passthrough or ADB port forwarding.
require_adb() {
    command -v adb >/dev/null 2>&1 || die 'Host adb is missing (Arch: sudo pacman -S android-tools).'
}

# Android 11+ wireless debugging advertises _adb-tls-connect._tcp through
# mDNS. Only use advertised endpoints, never a blind network/port scan.
connect_discovered_wifi() {
    local services line endpoint output
    services="$(adb mdns services 2>/dev/null || true)"
    while IFS= read -r line; do
        [[ "$line" == *'_adb-tls-connect._tcp'* ]] || continue
        # adb mdns services uses address:port (usually IPv4); use explicit
        # --wifi-connect for IPv6 hosts or environments without mDNS.
        if [[ "$line" =~ ([0-9]{1,3}\.){3}[0-9]{1,3}:[0-9]{1,5} ]]; then
            endpoint="${BASH_REMATCH[0]}"
            output="$(adb connect "$endpoint" 2>&1 || true)"
            if [[ "$output" == *'connected to'* || "$output" == *'already connected'* ]]; then
                echo "Wi-Fi ADB: $output"
            fi
        fi
    done <<< "$services"
}

# Four arrays preserve enough details to diagnose unauthorized USB devices
# without ever treating them as candidates for deployment.
declare -a device_serials=() device_states=() device_kinds=() device_details=()
read_devices() {
    device_serials=(); device_states=(); device_kinds=(); device_details=()
    local output line serial state details kind
    output="$(adb devices -l)" || die 'adb devices failed.'
    while IFS= read -r line; do
        [[ "$line" == 'List of devices attached'* || -z "$line" || "$line" == \** ]] && continue
        read -r serial state details <<< "$line"
        [[ -n "${serial:-}" && -n "${state:-}" ]] || continue
        kind=usb
        # TCPIP and modern mDNS serials identify remote devices.
        if [[ "$serial" == *:* || "$serial" == *'._adb-tls-connect._tcp'* ]]; then
            kind=wifi
        fi
        device_serials+=("$serial")
        device_states+=("$state")
        device_kinds+=("$kind")
        device_details+=("${details:-}")
    done <<< "$output"
}

show_devices() {
    local index
    if (( ${#device_serials[@]} == 0 )); then
        echo 'ADB: no devices discovered.'
    else
        echo 'ADB devices (USB and already-connected Wi-Fi):'
        for index in "${!device_serials[@]}"; do
            printf '  %-25s %-14s %-5s %s\n' "${device_serials[index]}" "${device_states[index]}" "${device_kinds[index]}" "${device_details[index]}"
        done
    fi
}

show_device_hints() {
    echo 'No suitable authorized Android device. For USB: enable USB debugging,' >&2
    echo 'reconnect the cable, unlock the phone and accept its RSA prompt.' >&2
    echo 'For Wi-Fi: enable Wireless debugging and use --wifi-pair HOST:PORT.' >&2
    echo 'Arch USB permission problems: install android-udev, then reconnect.' >&2
    echo 'Inspect with: ./compile_android.sh --devices' >&2
}

# Device selection prefers a single USB device in normal mode, even when the
# same phone is also available through Wi-Fi. Never guess among multiple
# devices: ANDROID_SERIAL provides an explicit override.
select_device() {
    local index serial="" count=0 kind
    local -a candidates=()
    if [[ -n "${ANDROID_SERIAL:-}" ]]; then
        for index in "${!device_serials[@]}"; do
            if [[ "${device_serials[index]}" == "$ANDROID_SERIAL" && "${device_states[index]}" == device ]]; then
                printf '%s\n' "$ANDROID_SERIAL"
                return 0
            fi
        done
        return 1
    fi
    kind=usb
    "$wifi_preferred" && kind=wifi
    for index in "${!device_serials[@]}"; do
        if [[ "${device_states[index]}" == device && "${device_kinds[index]}" == "$kind" ]]; then
            candidates+=("${device_serials[index]}")
        fi
    done
    # Automatic mode can use an already authorized Wi-Fi device if no USB is
    # present; --wifi intentionally avoids falling back to USB silently.
    if (( ${#candidates[@]} == 0 )) && ! "$wifi_preferred"; then
        for index in "${!device_serials[@]}"; do
            if [[ "${device_states[index]}" == device && "${device_kinds[index]}" == wifi ]]; then
                candidates+=("${device_serials[index]}")
            fi
        done
    fi
    (( ${#candidates[@]} == 1 )) || return 1
    printf '%s\n' "${candidates[0]}"
}

if [[ -n "$wifi_pair$wifi_connect" ]] || "$list_devices" || "$wifi_preferred"; then
    require_adb
fi
if [[ -n "$wifi_pair" ]]; then
    echo "Pairing Android wireless debugging at $wifi_pair (enter the phone's one-time code when prompted)."
    adb pair "$wifi_pair" || die 'Wireless ADB pairing failed.'
    wifi_preferred=true
fi
if [[ -n "$wifi_connect" ]]; then
    adb connect "$wifi_connect" || die "Wi-Fi connection failed: $wifi_connect"
    wifi_preferred=true
fi
if "$list_devices"; then
    # --devices must work without Docker, Qt, or initialized bgfx submodules.
    connect_discovered_wifi
    read_devices
    show_devices
    exit 0
fi

command -v docker >/dev/null 2>&1 || die 'Docker is missing (Arch: sudo pacman -S docker).'
if [[ "$bgfx" == ON && ! -f "$project_dir/external/bgfx.cmake/bgfx/include/bgfx/bgfx.h" ]]; then
    die 'bgfx is not initialized. Run ./git_submodule_init.sh first.'
fi
# Bind the pinned Render submodule read-only into Docker. Only an explicit
# AOWIS_RENDER_SOURCE_DIR override may point to an alternate checkout.
renderer_mount=()
if [[ "$bgfx" == ON ]]; then
    renderer_host="${AOWIS_RENDER_SOURCE_DIR:-$project_dir/external/AOWIS-GUI-Render}"
    [[ -f "$renderer_host/CMakeLists.txt" ]] ||
        die 'AOWIS-GUI-Render missing: run ./git_submodule_init.sh --render.'
    renderer_host="$(realpath -- "$renderer_host")"
    renderer_mount=(-v "$renderer_host:/aowis-render:ro")
fi
jobs="${AOWIS_ANDROID_JOBS:-8}"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || die 'AOWIS_ANDROID_JOBS must be a positive integer.'

build_name=build-android-bgfx
[[ "$bgfx" == ON ]] || build_name=build-android-baseline

if ! docker image inspect "$AOWIS_ANDROID_IMAGE" >/dev/null 2>&1; then
    echo '=== Android Docker toolchain missing; preparing image ==='
    "$project_dir/prepare_android_docker.sh"
fi

cache_root="${XDG_CACHE_HOME:-$HOME/.cache}/aowis-mobile/gradle"
# Android's debug APK is signed by Gradle with debug.keystore. A disposable
# Docker HOME must never produce a new signing identity on every build.
# Persist that keystore OUTSIDE the repository/build tree so cleans, renames,
# snapshots and Docker image rebuilds leave it intact.
data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
signing_dir="${AOWIS_ANDROID_DEBUG_SIGNING_DIR:-$data_home/aowis-gui-mobile/android-debug-signing}"
[[ "$signing_dir" == /* ]] || die 'AOWIS_ANDROID_DEBUG_SIGNING_DIR must be an absolute path.'
umask 077
mkdir -p -- "$cache_root" "$signing_dir"
chmod 700 -- "$signing_dir"
if [[ -e "$signing_dir/.aowis-debug-keystore-created" && ! -s "$signing_dir/debug.keystore" ]]; then
    die "Previously provisioned Android debug keystore is missing: $signing_dir/debug.keystore. Restore it from backup; refusing to silently change the APK signing identity."
fi
echo "Android debug signing identity: $signing_dir/debug.keystore"
echo "=== Compiling Android APK with Docker: $AOWIS_ANDROID_IMAGE ==="
docker run --rm --network="${AOWIS_DOCKER_RUN_NETWORK:-host}" \
    --user "$(id -u):$(id -g)" \
    -v "$project_dir:/project" -w /project \
    "${renderer_mount[@]}" \
    -v "$cache_root:/gradle-cache" \
    -v "$signing_dir:/tmp/.android" \
    -e HOME=/tmp \
    -e ANDROID_USER_HOME=/tmp/.android \
    -e GRADLE_USER_HOME=/gradle-cache \
    -e "AOWIS_QT_ANDROID_VERSION=$AOWIS_QT_ANDROID_VERSION" \
    -e "AOWIS_ANDROID_NDK_VERSION=$AOWIS_ANDROID_NDK_VERSION" \
    -e "AOWIS_ANDROID_BUILD_TOOLS_VERSION=$AOWIS_ANDROID_BUILD_TOOLS_VERSION" \
    -e "AOWIS_ANDROID_BGFX=$bgfx" \
    -e AOWIS_ANDROID_RENDER_SOURCE_DIR=/aowis-render \
    -e "AOWIS_ANDROID_BUILD_NAME=$build_name" \
    -e "AOWIS_ANDROID_JOBS=$jobs" \
    "$AOWIS_ANDROID_IMAGE" \
    /bin/bash /project/tools/qt-android/build_android_inside_container.sh \
    || die 'Docker APK build failed; no APK was deployed.'

search_dir="$project_dir/$build_name/android-build/build/outputs/apk/debug"
[[ -d "$search_dir" ]] || die "No debug APK directory after successful build: $search_dir"
mapfile -t apks < <(find "$search_dir" -maxdepth 1 -type f -name '*.apk' -print | sort)
(( ${#apks[@]} == 1 )) || die "Expected exactly one debug APK; found ${#apks[@]} in $search_dir"
apk="${apks[0]}"
[[ -s "$apk" ]] || die "APK missing or empty: $apk"
echo "=== APK ready: $apk ==="

[[ "$install_mode" != build-only ]] || exit 0
if ! command -v adb >/dev/null 2>&1; then
    [[ "$install_mode" != run ]] || die 'Host adb is missing (Arch: sudo pacman -S android-tools).'
    echo 'adb unavailable; skipping device deployment.'
    exit 0
fi

# Refresh both USB and Wi-Fi state after the potentially long Docker build.
# mDNS discovery is safe: it only reconnects devices advertising debugging,
# and still requires prior pairing/authorization on the phone.
read_devices
# If no authorized USB device is present, try paired Wi-Fi mDNS endpoints
# even if ADB lists an unauthorized/offline USB entry.
usb_authorized=false
for index in "${!device_serials[@]}"; do
    if [[ "${device_states[index]}" == device && "${device_kinds[index]}" == usb ]]; then
        usb_authorized=true
        break
    fi
done
if "$wifi_preferred" || ! "$usb_authorized"; then
    connect_discovered_wifi
    read_devices
fi
serial="$(select_device || true)"
if [[ -z "$serial" ]]; then
    show_devices
    show_device_hints
    [[ "$install_mode" != run ]] || die 'No unambiguous authorized device; set ANDROID_SERIAL to select one.'
    echo 'Skipping deployment; APK remains available on disk.'
    exit 0
fi

echo "=== Installing and launching on $serial ==="
if ! install_output="$(adb -s "$serial" install -r "$apk" 2>&1)"; then
    printf '%s\n' "$install_output" >&2
    if [[ "$install_output" == *INSTALL_FAILED_UPDATE_INCOMPATIBLE* ]]; then
        echo 'The installed AOWIS package was signed with an older, different debug key.' >&2
        echo 'The new key is persistent. To retain local app data, back it up before uninstalling.' >&2
        echo "When ready: adb -s '$serial' uninstall org.aowis.mobile" >&2
        echo 'Then rerun ./compile_android.sh --run. No automatic uninstall is performed.' >&2
    fi
    die "APK installation failed on $serial"
fi
printf '%s\n' "$install_output"
# Start the exported launcher activity directly. `monkey` is an input-fuzzing
# tool; deployment must not inject random UI, key, touch or rotation events.
# In particular, do not modify Android's system auto-rotate setting.
adb -s "$serial" shell am start -W -a android.intent.action.MAIN \
    -c android.intent.category.LAUNCHER -p org.aowis.mobile \
    || die "Launching AOWIS Mobile failed on $serial"
echo '=== AOWIS Mobile launched ==='
