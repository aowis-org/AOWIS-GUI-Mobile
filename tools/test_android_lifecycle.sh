#!/usr/bin/env bash
set -euo pipefail

# Exercises Android's actual task background/resume behavior without granting
# Docker USB access, clearing logcat, or modifying global Android settings.
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
package=org.aowis.mobile
cycles=12
pause_seconds=2
resume_seconds=3
serial="${ANDROID_SERIAL:-}"
pull_only=false

usage() {
    cat <<'HELP'
Usage: ./tools/test_android_lifecycle.sh [--cycles N] [--serial SERIAL] [--pull-only]

Requires a debuggable, installed org.aowis.mobile build and host adb.
Each cycle presses HOME, waits 2 seconds, reopens AOWIS, then waits 3 seconds.
The tool checks that Android does not restart the application process, and
pulls private app lifecycle CSV via adb run-as into benchmarks/lifecycle/.

Options:
  --cycles N         Number of background/resume cycles (default 12; 1..100)
  --serial SERIAL    Select an authorized USB or Wi-Fi device
  --pull-only        Only retrieve existing lifecycle logs, no transitions
  --help             Show help

Manually test device rotation and screen lock after this automated test.
HELP
}

while (( $# > 0 )); do
    case "$1" in
        --cycles) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; cycles="$2"; shift ;;
        --serial) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; serial="$2"; shift ;;
        --pull-only) pull_only=true ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
    shift
done
[[ "$cycles" =~ ^[1-9][0-9]*$ ]] && (( 10#$cycles <= 100 )) || {
    printf 'Cycles must be an integer from 1 to 100.\n' >&2
    exit 2
}
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
installed_path="$("${adb_cmd[@]}" shell pm path "$package" | tr -d '\r')"
if [[ "$installed_path" != package:* ]]; then
    printf '%s is not installed. Run ./compile_android.sh --run first.\n' "$package" >&2
    exit 1
fi

safe_serial="${serial//[^a-zA-Z0-9._-]/_}"
output="$root/benchmarks/lifecycle/${safe_serial}-$(date -u +%Y%m%d-%H%M%S)"
mkdir -p -- "$output"
printf 'Device: %s\n' "$serial" | tee "$output/summary.txt"
printf 'Model: %s\n' "$("${adb_cmd[@]}" shell getprop ro.product.model | tr -d '\r')" | tee -a "$output/summary.txt"
printf 'Android release: %s\n' "$("${adb_cmd[@]}" shell getprop ro.build.version.release | tr -d '\r')" | tee -a "$output/summary.txt"
printf 'Cycles requested: %s\n' "$cycles" | tee -a "$output/summary.txt"
run_start_utc="$(date -u +%Y-%m-%dT%H:%M:%S)"
printf 'Started (UTC): %s\n' "$run_start_utc" | tee -a "$output/summary.txt"
printf 'cycle,pss_kb\n' > "$output/memory.csv"
record_memory() {
    local cycle="$1" meminfo pss
    meminfo="$("${adb_cmd[@]}" shell dumpsys meminfo "$package" 2>/dev/null || true)"
    pss="$(printf '%s\n' "$meminfo" | awk '/TOTAL PSS:/ {print $3; exit} /^[[:space:]]*TOTAL[[:space:]]+[0-9]+/ {print $2; exit}')"
    printf '%s,%s\n' "$cycle" "${pss:-NA}" >> "$output/memory.csv"
}
record_memory 0

app_pid() {
    "${adb_cmd[@]}" shell pidof "$package" 2>/dev/null | tr -d '\r' | awk '{print $1}' || true
}

errors=0
if ! "$pull_only"; then
    # Launch once, then establish the original PID. A changed PID indicates a
    # process restart even when the screenshot looks correct afterwards.
    "$root/tools/launch_android_app.sh" "$serial" >/dev/null
    sleep "$resume_seconds"
    initial_pid="$(app_pid)"
    if [[ -z "$initial_pid" ]]; then
        printf 'App never started; see device logcat.\n' | tee -a "$output/summary.txt" >&2
        errors=1
    else
        printf 'Initial PID: %s\n' "$initial_pid" | tee -a "$output/summary.txt"
        for ((i=1; i<=cycles; i++)); do
            "${adb_cmd[@]}" shell input keyevent KEYCODE_HOME
            sleep "$pause_seconds"
            "$root/tools/launch_android_app.sh" "$serial" >/dev/null
            sleep "$resume_seconds"
            current_pid="$(app_pid)"
            if [[ -z "$current_pid" ]]; then
                printf 'Cycle %d: APP NOT RUNNING\n' "$i" | tee -a "$output/summary.txt" >&2
                errors=$((errors + 1))
            elif [[ "$current_pid" != "$initial_pid" ]]; then
                printf 'Cycle %d: PROCESS RESTARTED %s -> %s\n' "$i" "$initial_pid" "$current_pid" | tee -a "$output/summary.txt" >&2
                errors=$((errors + 1))
                initial_pid="$current_pid"
            else
                printf 'Cycle %d: same PID %s\n' "$i" "$current_pid" | tee -a "$output/summary.txt"
            fi
            record_memory "$i"
        done
    fi
fi

# Keep the device untouched; no root, no global setting changes, no log clearing.
# run-as only works on a debug APK, which is intentional for this development test.
if "${adb_cmd[@]}" exec-out run-as "$package" tar -C files/lifecycle -cf - . \
    | tar -xf - -C "$output"; then
    if [[ -f "$output/events.csv" ]]; then
        cp "$output/events.csv" "$output/lifecycle-events.csv"
        printf 'Events: %s\n' "$(($(wc -l < "$output/events.csv") - 1))" | tee -a "$output/summary.txt"
        if ! "$pull_only"; then
            if ! awk -F, -v start="$run_start_utc" '
                NR>1 && substr($1,1,19)>=start && ($2=="recovered" || $2=="late_recovery") {found=1}
                END {exit(found ? 0 : 1)}' "$output/events.csv"; then
                printf 'No successful recovery events recorded during this run.\n' | tee -a "$output/summary.txt" >&2
                errors=$((errors + 1))
            fi
            if awk -F, -v start="$run_start_utc" '
                NR>1 && substr($1,1,19)>=start && $2=="timeout" {found=1}
                END {exit(found ? 0 : 1)}' "$output/events.csv"; then
                printf 'A recovery watchdog timeout occurred during this run.\n' | tee -a "$output/summary.txt" >&2
                errors=$((errors + 1))
            fi
        fi
    else
        printf 'No lifecycle events.csv was retrieved.\n' | tee -a "$output/summary.txt" >&2
        errors=$((errors + 1))
    fi
else
    printf 'Could not extract lifecycle CSV using adb run-as.\n' | tee -a "$output/summary.txt" >&2
    errors=$((errors + 1))
fi
"${adb_cmd[@]}" shell dumpsys meminfo "$package" > "$output/meminfo.txt" 2>/dev/null || true
"${adb_cmd[@]}" logcat -d -v time 2>/dev/null | grep -E 'AOWIS lifecycle:|AOWIS map:|FATAL EXCEPTION|Fatal signal' \
    > "$output/logcat-filtered.txt" || true
printf 'Errors: %d\nLogs: %s\n' "$errors" "$output" | tee -a "$output/summary.txt"
(( errors == 0 ))
