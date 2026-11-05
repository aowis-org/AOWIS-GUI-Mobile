#!/usr/bin/env bash
set -euo pipefail

# Real JDK integration test, independent of Qt, Android SDK, Docker or GPU.
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
command -v keytool >/dev/null || { echo 'SKIP: keytool not found'; exit 0; }
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/signing"
export ANDROID_USER_HOME="$work/signing"
script="$root/tools/qt-android/prepare_debug_signing.sh"
"$script" > "$work/output.log" 2>&1
key="$ANDROID_USER_HOME/debug.keystore"
[[ -s "$key" && -e "$ANDROID_USER_HOME/.aowis-debug-keystore-created" ]] || {
    echo 'Debug signing key not provisioned.' >&2; exit 1;
}
[[ "$(stat -c '%a' "$key")" == 600 ]] || { echo 'Unsafe debug signing key permissions.' >&2; exit 1; }
first="$(sha256sum "$key" | cut -d ' ' -f 1)"
"$script" > "$work/output.log" 2>&1
second="$(sha256sum "$key" | cut -d ' ' -f 1)"
[[ "$first" == "$second" ]] || { echo 'Existing debug signing key changed.' >&2; exit 1; }

# Never replace a lost identity with a new one.
rm "$key"
if "$script" > "$work/output.log" 2>&1; then
    echo 'Lost keystore silently regenerated.' >&2; exit 1
fi
[[ ! -f "$key" ]] || { echo 'Lost keystore was recreated.' >&2; exit 1; }

# Never overwrite an existing but corrupt identity.
rm "$ANDROID_USER_HOME/.aowis-debug-keystore-created"
printf 'not a keystore' > "$key"
if "$script" > "$work/output.log" 2>&1; then
    echo 'Corrupt keystore accepted.' >&2; exit 1
fi
[[ "$(cat "$key")" == 'not a keystore' ]] || { echo 'Corrupt keystore overwritten.' >&2; exit 1; }
echo 'Android debug signing: persistent identity, permissions, missing/corrupt recovery checks passed'
