#!/usr/bin/env bash
set -euo pipefail

# Deliberate upstream update for the AOWIS-owned renderer. bgfx.cmake stays
# pinned at the release chosen by tools/init_bgfx.sh; never use --remote on it
# or its nested bgfx/bx/bimg dependencies.

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd -- "$project_dir"

usage() {
    cat <<'HELP'
Usage: ./git_submodule_update.sh [--render | --model | --all | --help]

  (no option)  Advance AOWIS-GUI-Render to its tracked remote branch.
  --render     Same as the default; leave bgfx.cmake and MODEL untouched.
  --model      Advance only AOWIS-SERVER-MODEL to its tracked remote branch.
  --all        Advance Render and MODEL, then initialize bgfx.cmake at its pin.
               Never advance bgfx.cmake or its nested third-party submodules.
  --help       Show this help.

Review and commit any changed Render/MODEL gitlinks explicitly.
To change the third-party bgfx.cmake release, use tools/init_bgfx.sh and
validate Linux/Android before committing its gitlink.
HELP
}

die() {
    printf 'AOWIS Mobile submodules: %s\n' "$*" >&2
    exit 1
}

[[ "$(git rev-parse --show-toplevel 2>/dev/null)" == "$project_dir" ]] ||
    die "Expected a Git working-tree root at $project_dir"

include_bgfx=false
update_model=false
update_render=true
case "${1:-}" in
    '')
        [[ $# -eq 0 ]] || { usage >&2; exit 2; }
        ;;
    --render)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        ;;
    --model)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        update_render=false
        update_model=true
        ;;
    --all)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        include_bgfx=true
        update_model=true
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

render_path='external/AOWIS-GUI-Render'
if "$update_render" && [[ "$(git ls-files --stage -- "$render_path")" != '160000 '* ]]; then
    die "Missing Git submodule gitlink for $render_path. First run 'git submodule add https://github.com/aowis-org/AOWIS-GUI-Render.git $render_path' and commit the gitlink and .gitmodules."
fi

if "$include_bgfx"; then
    # Initialize third-party dependencies at their *recorded* commits, not
    # their remote HEADs. This must not advance a tested bgfx.cmake pin.
    "$project_dir/git_submodule_init.sh" --bgfx
fi

git submodule sync --recursive
# Advance only selected first-party submodules; leave third-party commits pinned.
if "$update_render"; then
    git submodule update --init --remote --checkout -- "$render_path"
    git -C "$render_path" submodule update --init --recursive --checkout
    printf 'Updated %s to %s. Review and commit the gitlink.\n' \
        "$render_path" "$(git -C "$render_path" rev-parse --short HEAD)"
fi
if "$update_model"; then
    model_path='external/AOWIS-SERVER-MODEL'
    [[ "$(git ls-files --stage -- "$model_path")" == '160000 '* ]] ||
        die 'MODEL gitlink missing. Run ./tools/init_model.sh once.'
    git submodule update --init --remote --checkout -- "$model_path"
    printf 'Updated %s to %s. Review and commit the gitlink.\n' \
        "$model_path" "$(git -C "$model_path" rev-parse --short HEAD)"
fi
