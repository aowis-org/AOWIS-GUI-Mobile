#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/bin" "$work/repo/tools" "$work/repo/fixtures"
cp "$root/tools/test_android_lifecycle.sh" "$work/repo/tools/"
printf 'utc,event,duration_us\n2026-11-05T16:00:00Z,recovered,23000\n' > "$work/repo/fixtures/events.csv"
cat > "$work/bin/adb" <<'MOCK_ADB'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$MOCK_LOG"
if [[ "$1" == devices ]]; then
    printf 'List of devices attached\n'
    [[ "${FAKE_NO_DEVICE:-0}" == 0 ]] && printf 'usb123\tdevice usb:1-2\n'
    exit 0
fi
[[ "$1" == -s ]] || exit 41
shift 2
case "$1" in
    get-state) printf 'device\n' ;;
    shell)
        shift
        case "$1" in
            pm) printf 'package:/data/app/org.aowis.mobile/base.apk\n' ;;
            getprop) printf 'Test Handset\n' ;;
            monkey) : ;;
            input) : ;;
            pidof)
                if [[ "${FAKE_RESTART:-0}" == 1 ]]; then
                    count=0
                    [[ -f "$MOCK_ROOT/count" ]] && count="$(cat "$MOCK_ROOT/count")"
                    count=$((count + 1))
                    printf '%s\n' "$count" > "$MOCK_ROOT/count"
                    printf '%s\n' "$((1000 + count))"
                else
                    printf '1000\n'
                fi ;;
            dumpsys) printf 'TOTAL PSS: 120000\n' ;;
            *) exit 45 ;;
        esac ;;
    exec-out)
        [[ "$2" == run-as && "$3" == org.aowis.mobile && "$4" == tar ]] || exit 46
        if [[ "${FAKE_NO_RECOVERY:-0}" == 0 ]]; then
            printf '%s,recovered,23000\n' "$(date -u +%Y-%m-%dT%H:%M:%S).000Z" > "$MOCK_ROOT/fixtures/events.csv.tmp"
            head -1 "$MOCK_ROOT/fixtures/events.csv" > "$MOCK_ROOT/fixtures/events.csv.new"
            cat "$MOCK_ROOT/fixtures/events.csv.tmp" >> "$MOCK_ROOT/fixtures/events.csv.new"
            mv "$MOCK_ROOT/fixtures/events.csv.new" "$MOCK_ROOT/fixtures/events.csv"
        fi
        if [[ "${FAKE_TIMEOUT:-0}" == 1 ]]; then
            printf '9999-01-01T00:00:00.000Z,timeout,-1\n' >> "$MOCK_ROOT/fixtures/events.csv"
        fi
        tar -C "$MOCK_ROOT/fixtures" -cf - events.csv ;;
    logcat) printf 'AOWIS lifecycle: recovered\n' ;;
    *) exit 44 ;;
esac
MOCK_ADB
cat > "$work/bin/sleep" <<'MOCK_SLEEP'
#!/usr/bin/env bash
exit 0
MOCK_SLEEP
chmod +x "$work/bin/adb" "$work/bin/sleep" "$work/repo/tools/test_android_lifecycle.sh"
export MOCK_ROOT="$work/repo" MOCK_LOG="$work/commands.log" PATH="$work/bin:$PATH"
cd "$work/repo"
./tools/test_android_lifecycle.sh --cycles 2 > "$work/result.log" 2>&1
find benchmarks/lifecycle -name 'events.csv' | grep -q .
grep -q 'Errors: 0' "$work/result.log"
grep -q 'shell input keyevent KEYCODE_HOME' "$MOCK_LOG"
grep -q 'exec-out run-as org.aowis.mobile tar' "$MOCK_LOG"
./tools/test_android_lifecycle.sh --pull-only > "$work/result.log" 2>&1
! tail -15 "$MOCK_LOG" | grep -q 'shell input keyevent KEYCODE_HOME'
export FAKE_NO_DEVICE=1
if ./tools/test_android_lifecycle.sh --cycles 1 > "$work/result.log" 2>&1; then
    printf 'Expected no-device failure.\n' >&2
    exit 1
fi
unset FAKE_NO_DEVICE
export FAKE_RESTART=1
if ./tools/test_android_lifecycle.sh --cycles 2 > "$work/result.log" 2>&1; then
    printf 'Expected app-restart failure.\n' >&2
    exit 1
fi
grep -q 'PROCESS RESTARTED' "$work/result.log"
unset FAKE_RESTART
export FAKE_TIMEOUT=1
if ./tools/test_android_lifecycle.sh --cycles 1 > "$work/result.log" 2>&1; then
    printf 'Expected watchdog timeout failure.\n' >&2
    exit 1
fi
grep -q 'watchdog timeout' "$work/result.log"
printf 'Android lifecycle ADB smoke tests passed\n'
