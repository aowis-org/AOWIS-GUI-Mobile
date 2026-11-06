#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
build_dir="$(mktemp -d)"
trap 'rm -rf -- "$build_dir"' EXIT
mapfile -d '' sources < <(find "$repo_root/tests/fixtures/torch" -name '*.java' -print0)
javac -d "$build_dir" "${sources[@]}" \
  "$repo_root/android/src/org/aowis/mobile/TorchBridge.java"
java -cp "$build_dir" org.aowis.mobile.TorchBridgeTests
