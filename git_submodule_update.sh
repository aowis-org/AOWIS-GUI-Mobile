#!/usr/bin/env bash
set -euo pipefail

# Deliberate upstream update for the AOWIS-owned renderer. Render's nested
# bgfx/bx/bimg follow the commits recorded by that Render revision; never use
# --remote on them.

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd -- "$project_dir"

usage() {
    cat <<'HELP'
Usage: ./git_submodule_update.sh [--render | --model | --dvmdb | --all | --help]

  (no option)  Advance AOWIS-GUI-Render to its tracked remote branch.
  --render     Same as the default; leave MODEL untouched.
  --model      Advance only AOWIS-SERVER-MODEL to its tracked remote branch.
  --dvmdb      Deliberately advance only the DVMDB gitlink, without nested modules.
  --all        Advance Render and MODEL. Render's nested third-party
               submodules stay at the commits that Render revision records.
  --help       Show this help.

Review and commit any changed Render/MODEL/DVMDB gitlinks explicitly.
GeographicLib always stays pinned; choose a tested release before changing it.
bgfx is pinned by Render (the GUI pins the same revisions).
HELP
}

die() {
    printf 'AOWIS Mobile submodules: %s\n' "$*" >&2
    exit 1
}

[[ "$(git rev-parse --show-toplevel 2>/dev/null)" == "$project_dir" ]] ||
    die "Expected a Git working-tree root at $project_dir"

update_model=false
update_dvmdb=false
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
    --dvmdb)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        update_render=false
        update_dvmdb=true
        ;;
    --all)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
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

if "$update_dvmdb"; then
    dvmdb_path='external/AOWIS-SERVER-DVMDB'
    [[ "$(git ls-files --stage -- "$dvmdb_path")" == '160000 '* ]] ||
        die 'DVMDB gitlink missing. Run ./tools/init_geographiclib_dvmdb.sh --dvmdb once.'
    git submodule update --init --remote --checkout -- "$dvmdb_path"
    printf 'Updated %s to %s. Review and commit the gitlink.\n' \
        "$dvmdb_path" "$(git -C "$dvmdb_path" rev-parse --short HEAD)"
fi
