#!/usr/bin/env bash
set -euo pipefail

# Fetch Look-around recordings from the connected phone and replay them.
#
# The app records every Look-around session (raw sensors: sensors-<utc>.csv,
# shown frames: frames-<utc>.csv) to its private files/lookaround/ when Look
# around ends or the app goes to the background. This script copies them,
# plus the AOWIS sensor/camera logcat lines and the phone model, into
# benchmarks/lookaround/<model>-<utc>/, deletes them on the phone, and runs
# tools/replay_look_around.sh on them. The results stay in the project, so
# they travel with it.
#
# Usage: tools/pull_look_around.sh [--serial SERIAL] [--keep]
#   --keep   leave the recordings on the phone as well
# Requires host adb and a debuggable installed org.aowis.mobile build.
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package=org.aowis.mobile
serial=""
keep=0
while (( $# > 0 )); do
    case "$1" in
        --serial) serial="${2:-}"; shift ;;
        --keep) keep=1 ;;
        -h|--help) sed -n '4,18p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done
command -v adb >/dev/null 2>&1 || { printf 'Android adb is required.\n' >&2; exit 1; }
if [[ -z "$serial" ]]; then
    mapfile -t devices < <(adb devices -l | awk 'NR>1 && $2=="device" {print $1}')
    if (( ${#devices[@]} != 1 )); then
        printf 'Expected exactly one authorized Android device, found %d. Use --serial SERIAL.\n' "${#devices[@]}" >&2
        adb devices -l >&2
        exit 1
    fi
    serial="${devices[0]}"
fi
adb_cmd=(adb -s "$serial")
[[ "$("${adb_cmd[@]}" get-state | tr -d '\r')" == device ]] || {
    printf 'Selected Android device is not authorized: %s\n' "$serial" >&2
    exit 1
}

model="$("${adb_cmd[@]}" shell getprop ro.product.model | tr -d '\r')"
safe_model="${model//[^a-zA-Z0-9._-]/_}"
output="$root/benchmarks/lookaround/${safe_model:-phone}-$(date -u +%Y%m%d-%H%M%S)"

listing="$("${adb_cmd[@]}" shell run-as "$package" ls files/lookaround 2>/dev/null | tr -d '\r' || true)"
if ! grep -q '\.csv$' <<< "$listing"; then
    printf 'No Look-around recordings on the phone yet.\n'
    printf 'Use Look around (with or without Camera stabilization), then turn it off\n'
    printf '(or switch the app to the background) and run this script again.\n'
    exit 1
fi
mkdir -p -- "$output"
"${adb_cmd[@]}" exec-out run-as "$package" tar -C files/lookaround -cf - . | tar -xf - -C "$output"
{
    printf 'Device: %s\n' "$serial"
    printf 'Model: %s\n' "$model"
    printf 'Android release: %s\n' "$("${adb_cmd[@]}" shell getprop ro.build.version.release | tr -d '\r')"
    printf 'Pulled (UTC): %s\n' "$(date -u +%Y-%m-%dT%H:%M:%S)"
} > "$output/device.txt"
"${adb_cmd[@]}" logcat -d -v threadtime 2>/dev/null | grep -E 'AOWIS (compass sensor|look around)' > "$output/logcat.txt" || true
if (( keep == 0 )); then
    "${adb_cmd[@]}" shell run-as "$package" rm -rf files/lookaround
fi
printf 'Pulled %d recording file(s) to %s\n' "$(find "$output" -name '*.csv' | wc -l)" "${output#"$root"/}"
"$root/tools/replay_look_around.sh" "$output"
