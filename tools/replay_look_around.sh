#!/usr/bin/env bash
set -euo pipefail

# Replay Look-around raw sensor recordings through the CURRENT motion code
# (tools/look_around_replay.cpp: phone sensors, compass fallback, camera
# stabilization) and summarize jitter and lag per path.
#
# Usage: tools/replay_look_around.sh [DIRECTORY|sensors-<utc>.csv ...]
#   Without arguments: every recording under benchmarks/lookaround/.
# Writes <recording>-replay.csv next to each recording and summary.txt into
# each directory. Needs only a C++17 compiler (no Qt, no phone).
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiler="${CXX:-c++}"
command -v "$compiler" >/dev/null 2>&1 || { printf 'A C++17 compiler is required (set CXX).\n' >&2; exit 1; }
build="$root/build/look_around_replay"
mkdir -p -- "$(dirname "$build")"
"$compiler" -std=c++17 -O2 -I"$root/src" "$root/tools/look_around_replay.cpp" -o "$build"

targets=("$@")
(( ${#targets[@]} > 0 )) || targets=("$root/benchmarks/lookaround")
mapfile -t recordings < <(for target in "${targets[@]}"; do
    if [[ -d "$target" ]]; then find "$target" -name 'sensors-*.csv' ! -name '*-replay.csv'; else printf '%s\n' "$target"; fi
done | sort)
(( ${#recordings[@]} > 0 )) || { printf 'No sensors-*.csv recordings found.\n' >&2; exit 1; }
for recording in "${recordings[@]}"; do
    directory="$(dirname "$recording")"
    "$build" "$recording" | tee -a "$directory/summary.txt"
done
