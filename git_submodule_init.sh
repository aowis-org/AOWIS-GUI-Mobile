#!/usr/bin/env bash
set -euo pipefail

# Check out the exact commits recorded by this Mobile repository (including
# bgfx.cmake's nested bgfx/bx/bimg gitlinks). Never follow remote branches.
# Usage: ./git_submodule_init.sh [--render | --bgfx | --model | --help]

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd -- "$project_dir"

usage() {
    cat <<'HELP'
Usage: ./git_submodule_init.sh [--render | --bgfx | --model | --help]

  (no option)  Initialize MODEL, Render and bgfx.cmake, including nested
               submodules, at exactly the recorded Git commits.
  --render     Initialize only AOWIS-GUI-Render and its pinned dependencies.
  --bgfx       Initialize only bgfx.cmake and its pinned bgfx/bx/bimg.
  --model      Initialize only the pinned AOWIS-SERVER-MODEL gitlink.
               For first registration, run ./tools/init_model.sh.
  --help       Show this help.

Run this script after cloning, or to restore submodules to recorded revisions.
This does not advance any dependency to its upstream branch.
HELP
}

die() {
    printf 'AOWIS Mobile submodules: %s\n' "$*" >&2
    exit 1
}

[[ "$(git rev-parse --show-toplevel 2>/dev/null)" == "$project_dir" ]] ||
    die "Expected a Git working-tree root at $project_dir"

render_path='external/AOWIS-GUI-Render'
bgfx_path='external/bgfx.cmake'
model_path='external/AOWIS-SERVER-MODEL'
paths=()

case "${1:-}" in
    '')
        [[ $# -eq 0 ]] || { usage >&2; exit 2; }
        paths=("$model_path" "$render_path" "$bgfx_path")
        ;;
    --render)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        paths=("$render_path")
        ;;
    --bgfx)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        paths=("$bgfx_path")
        ;;
    --model)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        paths=("$model_path")
        ;;
    --help|-h)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

# Git tracks submodules as gitlinks (mode 160000). A .gitmodules entry copied
# by a source ZIP is NOT sufficient: do not claim success when Git would skip
# the missing submodule. Register it using 'git submodule add' in the real repo.
for path in "${paths[@]}"; do
    if [[ "$(git ls-files --stage -- "$path")" != '160000 '* ]]; then
        die "Missing Git submodule gitlink for $path. For MODEL run ./tools/init_model.sh once; otherwise register it with 'git submodule add'."
    fi
done

git submodule sync --recursive
git submodule update --init --recursive --checkout -- "${paths[@]}"
