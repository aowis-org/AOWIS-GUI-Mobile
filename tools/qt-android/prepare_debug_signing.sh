#!/usr/bin/env bash
set -euo pipefail

# Invoked inside Docker with ANDROID_USER_HOME=/tmp/.android (host bind mount).
# Also independently testable with any temporary signing directory.
signing_home="${ANDROID_USER_HOME:-}"
[[ "$signing_home" == /* && -d "$signing_home" ]] || {
    echo 'ANDROID_USER_HOME must refer to an existing absolute signing directory.' >&2
    exit 1
}
command -v keytool >/dev/null || {
    echo 'JDK keytool is required for Android debug signing.' >&2
    exit 1
}

umask 077
signing_key="$signing_home/debug.keystore"
signing_marker="$signing_home/.aowis-debug-keystore-created"
if [[ -e "$signing_marker" && ! -s "$signing_key" ]]; then
    echo "Android signing key was lost: $signing_key; refusing identity regeneration." >&2
    exit 1
fi
if [[ ! -e "$signing_key" ]]; then
    echo 'Creating persistent AOWIS Android development signing key (first build only).'
    keytool -genkeypair -noprompt -keystore "$signing_key" \
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10950 \
        -storepass android -keypass android \
        -dname 'CN=Android Debug,O=Android,C=US'
fi
[[ -s "$signing_key" ]] || { echo 'Android debug signing key is empty.' >&2; exit 1; }
keytool -list -keystore "$signing_key" -storepass android -alias androiddebugkey >/dev/null || {
    echo 'Android debug signing key is invalid or cannot be read.' >&2
    exit 1
}
chmod 600 "$signing_key"
touch "$signing_marker"
