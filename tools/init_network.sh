#!/usr/bin/env bash
set -euo pipefail

# First-time AOWIS-NETWORK Git submodule registration (embedded userspace
# WireGuard engine). Afterwards Git pins its exact commit as a gitlink (mode
# 160000) in the Mobile repository. Safe to repeat. Never advances a
# registered gitlink to upstream HEAD (use ./git_submodule_update.sh --network).
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd -- "$project_dir"
network_path='external/AOWIS-NETWORK'
network_url='https://github.com/aowis-org/AOWIS-NETWORK.git'

[[ "$(git rev-parse --show-toplevel 2>/dev/null)" == "$project_dir" ]] || {
    echo 'Run from a Git checkout (not a source-only ZIP).' >&2
    exit 1
}

entry="$(git ls-files --stage -- "$network_path")"
if [[ "$entry" == '160000 '* ]]; then
    registered_url="$(git config -f .gitmodules --get "submodule.$network_path.url" || true)"
    [[ "$registered_url" == "$network_url" ]] || {
        echo "$network_path has an unexpected or missing submodule URL: $registered_url" >&2
        exit 1
    }
    # Restore the recorded version, never --remote here.
    git submodule sync -- "$network_path"
    git submodule update --init --checkout -- "$network_path"
    echo 'NETWORK already registered; initialized its pinned commit.'
elif [[ -n "$entry" ]]; then
    echo "$network_path is tracked as ordinary files, not a submodule. Refusing to replace it." >&2
    exit 1
else
    [[ ! -e "$network_path" && ! -L "$network_path" ]] || {
        echo "Refusing to replace existing path: $network_path" >&2
        exit 1
    }
    # Creates BOTH the .gitmodules record and the gitlink.
    git submodule add -- "$network_url" "$network_path"
    echo 'NETWORK registered. Commit the .gitmodules and NETWORK gitlink with this change.'
fi
