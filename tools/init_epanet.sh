#!/usr/bin/env bash
set -euo pipefail

# One-time registration of the EPANET source dependency as a real Git submodule.
# A ZIP patch can carry .gitmodules but cannot carry Git's mode-160000 gitlink.
# Repeated calls restore the recorded revision; never advance the remote pin.
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd -- "$project_dir"
epanet_path='external/AOWIS-SERVER-EPANET'
epanet_url='https://github.com/aowis-org/AOWIS-SERVER-EPANET.git'

[[ $# -eq 0 ]] || {
    printf 'Usage: ./tools/init_epanet.sh\n' >&2
    exit 2
}
[[ "$(git rev-parse --show-toplevel 2>/dev/null)" == "$project_dir" ]] || {
    printf 'Run from a Git checkout (not a source-only ZIP).\n' >&2
    exit 1
}

entry="$(git ls-files --stage -- "$epanet_path")"
if [[ "$entry" == '160000 '* ]]; then
    recorded_url="$(git config -f .gitmodules --get "submodule.$epanet_path.url" || true)"
    [[ "$recorded_url" == "$epanet_url" ]] || {
        printf '%s has an unexpected or missing submodule URL: %s\n' "$epanet_path" "$recorded_url" >&2
        exit 1
    }
    git submodule sync -- "$epanet_path"
    git submodule update --init --checkout -- "$epanet_path"
    printf 'EPANET already registered; restored its pinned commit.\n'
elif [[ -n "$entry" ]]; then
    printf '%s is tracked as ordinary files, not a submodule. Refusing to replace it.\n' "$epanet_path" >&2
    exit 1
else
    [[ ! -e "$epanet_path" && ! -L "$epanet_path" ]] || {
        printf 'Refusing to replace existing path: %s\n' "$epanet_path" >&2
        exit 1
    }
    git submodule add -- "$epanet_url" "$epanet_path"
    printf 'EPANET registered. Commit .gitmodules and the EPANET gitlink.\n'
fi
