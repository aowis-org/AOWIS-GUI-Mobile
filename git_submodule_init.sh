#!/usr/bin/env bash
set -euo pipefail

# Initialize exactly the Git commits recorded by Mobile. Registration of new
# dependencies is separate: see tools/init_model.sh and
# tools/init_geographiclib_dvmdb.sh. Never follow upstream branches here.
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd -- "$project_dir"

usage() {
    cat <<'HELP'
Usage: ./git_submodule_init.sh [--render | --model | --geographiclib | --dvmdb | --help]

  (no option)      Initialize MODEL, Render, GeographicLib and DVMDB.
  --render         Initialize pinned Render and its nested pinned dependencies
                   (bgfx, bx, bimg: the map's GPU library).
  --model          Initialize pinned MODEL.
  --geographiclib  Initialize pinned GeographicLib.
  --dvmdb          Initialize pinned DVMDB without its nested submodules.
  --help           Show this help.

First-time registration: tools/init_model.sh or tools/init_geographiclib_dvmdb.sh.
DVMDB's nested MODEL is deliberately not initialized until native storage
integration requires it (its upstream .gitmodules may use a private SSH alias).
This command never advances any dependency beyond its recorded Git commit.
HELP
}

die() { printf 'AOWIS Mobile submodules: %s\n' "$*" >&2; exit 1; }
[[ "$(git rev-parse --show-toplevel 2>/dev/null)" == "$project_dir" ]] ||
    die "Expected a Git working-tree root at $project_dir"

render_path='external/AOWIS-GUI-Render'
model_path='external/AOWIS-SERVER-MODEL'
geo_path='external/GeographicLib'
dvmdb_path='external/AOWIS-SERVER-DVMDB'
paths=()
case "${1:-}" in
    '')
        [[ $# -eq 0 ]] || { usage >&2; exit 2; }
        paths=("$model_path" "$render_path" "$geo_path" "$dvmdb_path") ;;
    --render) [[ $# -eq 1 ]] || { usage >&2; exit 2; }; paths=("$render_path") ;;
    --model) [[ $# -eq 1 ]] || { usage >&2; exit 2; }; paths=("$model_path") ;;
    --geographiclib) [[ $# -eq 1 ]] || { usage >&2; exit 2; }; paths=("$geo_path") ;;
    --dvmdb) [[ $# -eq 1 ]] || { usage >&2; exit 2; }; paths=("$dvmdb_path") ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac

# A .gitmodules entry without the index gitlink is not a registered submodule.
for path in "${paths[@]}"; do
    [[ "$(git ls-files --stage -- "$path")" == '160000 '* ]] ||
        die "Missing Git submodule gitlink for $path. Use tools/init_model.sh or tools/init_geographiclib_dvmdb.sh to register new dependencies."
done

# DVMDB may contain a private-address nested MODEL gitlink. Deliberately do
# not recurse into it, as its MODEL copy is not yet needed by Mobile.
git submodule sync -- "${paths[@]}"
git submodule update --init --checkout -- "${paths[@]}"
for path in "${paths[@]}"; do
    case "$path" in
        "$render_path")
            git -C "$path" submodule sync --recursive
            git -C "$path" submodule update --init --recursive --checkout
            ;;
    esac
done
