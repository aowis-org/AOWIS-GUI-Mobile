#!/usr/bin/env bash
set -euo pipefail

# One-time registration of the embedded MAP dependency as a real Git submodule.
# A ZIP patch can carry .gitmodules but cannot carry Git's mode-160000 gitlink.
# Repeated calls restore the recorded revision; never advance the remote pin.
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd -- "$project_dir"
map_path='external/AOWIS-SERVER-MAP'
map_url='https://github.com/aowis-org/AOWIS-SERVER-MAP.git'

[[ $# -eq 0 ]] || {
    printf 'Usage: ./tools/init_map.sh\n' >&2
    exit 2
}
[[ "$(git rev-parse --show-toplevel 2>/dev/null)" == "$project_dir" ]] || {
    printf 'Run from a Git checkout (not a source-only ZIP).\n' >&2
    exit 1
}

entry="$(git ls-files --stage -- "$map_path")"
if [[ "$entry" == '160000 '* ]]; then
    recorded_url="$(git config -f .gitmodules --get "submodule.$map_path.url" || true)"
    [[ "$recorded_url" == "$map_url" ]] || {
        printf '%s has an unexpected or missing submodule URL: %s\n' "$map_path" "$recorded_url" >&2
        exit 1
    }
    git submodule sync -- "$map_path"
    git submodule update --init --checkout -- "$map_path"
    printf 'MAP already registered; restored its pinned commit.\n'
elif [[ -n "$entry" ]]; then
    printf '%s is tracked as ordinary files, not a submodule. Refusing to replace it.\n' "$map_path" >&2
    exit 1
else
    [[ ! -e "$map_path" && ! -L "$map_path" ]] || {
        printf 'Refusing to replace existing path: %s\n' "$map_path" >&2
        exit 1
    }
    git submodule add -- "$map_url" "$map_path"
    printf 'MAP registered. Commit .gitmodules and the MAP gitlink.\n'
fi
