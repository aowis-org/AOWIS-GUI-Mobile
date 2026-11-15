#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "$repo_root/tests/java_toolchain.sh"
build_dir="$(mktemp -d)"
trap 'rm -rf -- "$build_dir"' EXIT
mapfile -d '' sources < <(find "$repo_root/tests/fixtures/torch" -name '*.java' -print0)
(( ${#sources[@]} > 0 )) || { echo 'Java test fixtures not found.' >&2; exit 1; }
"$JAVAC" -d "$build_dir" "${sources[@]}" \
  "$repo_root/android/src/org/aowis/mobile/TorchBridge.java"
"$JAVA" -cp "$build_dir" org.aowis.mobile.TorchBridgeTests
