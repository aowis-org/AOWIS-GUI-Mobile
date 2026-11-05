#!/usr/bin/env bash
set -euo pipefail

# Installs and starts a locally built, debug Qt for Android APK on a device.
main() {
    local root source apk
    command -v adb >/dev/null || { echo 'adb is required.' >&2; return 2; }
    root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
    source="${1:-$root/build-android-bgfx}"
    if [[ -f "$source" && "$source" == *.apk ]]; then
        apk="$source"
    elif [[ -d "$source" ]]; then
        local -a apks=()
        mapfile -t apks < <(find "$source" -type f -name '*.apk' -print | sort)
        if [[ ${#apks[@]} -ne 1 ]]; then
            echo "Expected exactly one APK in $source; found ${#apks[@]}. Pass the exact APK path." >&2
            return 2
        fi
        apk="${apks[0]}"
    else
        echo "APK or build directory not found: $source" >&2
        return 2
    fi
    adb devices
    adb install -r "$apk"
    adb shell monkey -p org.aowis.mobile.renderlab -c android.intent.category.LAUNCHER 1
    echo 'For graphics/lifecycle diagnostics: adb logcat -v time | grep --line-buffered "AOWIS render lab"'
}
main "$@"
