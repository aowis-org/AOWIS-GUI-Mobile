#!/usr/bin/env bash
set -euo pipefail

# Launch the actual Android launcher activity, without injecting input events
# or modifying system settings. Used by deployment and lifecycle tests.
if (( $# != 1 )) || [[ -z "$1" ]]; then
    printf 'Usage: %s ADB_SERIAL\n' "${0##*/}" >&2
    exit 2
fi
serial="$1"
package=org.aowis.mobile

fail() {
    printf 'AOWIS Mobile Android launch: %s\n' "$*" >&2
    exit 1
}

# -p on its own is NOT an activity component. Resolve the installed APK's
# exported MAIN/LAUNCHER component; do not guess a QtActivity class name.
if ! resolved="$(adb -s "$serial" shell cmd package resolve-activity --brief \
    -a android.intent.action.MAIN -c android.intent.category.LAUNCHER \
    -p "$package" 2>&1)"; then
    printf '%s\n' "$resolved" >&2
    fail "Could not resolve the installed launcher activity on $serial."
fi
component=""
while IFS= read -r line; do
    line="${line%$'\r'}"
    if [[ "$line" =~ ^org[.]aowis[.]mobile/[[:alnum:]_.\$]+$ ]]; then
        component="$line"
    fi
done <<< "$resolved"
[[ -n "$component" ]] || {
    printf '%s\n' "$resolved" >&2
    fail "No launcher activity found for $package on $serial."
}

if ! launch_output="$(adb -s "$serial" shell am start -W \
    -n "$component" -a android.intent.action.MAIN \
    -c android.intent.category.LAUNCHER 2>&1)"; then
    printf '%s\n' "$launch_output" >&2
    fail "Android refused to launch $component on $serial."
fi
# Some Android activity-manager errors are printed to stdout with exit code 0.
if [[ "$launch_output" == *'Error:'* ||
      "$launch_output" == *'Status: error'* ||
      "$launch_output" == *'Status: timeout'* ]] ||
   [[ "$launch_output" != *'Status: ok'* &&
      "$launch_output" != *'Warning: Activity not started,'* ]]; then
    printf '%s\n' "$launch_output" >&2
    fail "Activity manager did not confirm launch of $component."
fi

# An unsuccessful launch after `adb install -r` leaves the old app closed.
# Check the process before reporting success, instead of trusting adb's status.
pid="$(adb -s "$serial" shell pidof "$package" 2>/dev/null | tr -d '\r' || true)"
[[ -n "$pid" ]] || {
    printf '%s\n' "$launch_output" >&2
    fail "Android reported a launch, but $package has no running process on $serial. Inspect: adb -s '$serial' logcat -d -t 200"
}
printf 'AOWIS Mobile launched: %s (PID %s)\n' "$component" "$pid"
