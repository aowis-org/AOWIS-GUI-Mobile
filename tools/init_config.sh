#!/usr/bin/env bash
set -euo pipefail

# First-time AOWIS-CONFIG Git submodule registration (configuration storage:
# WireGuard profiles and more). Afterwards Git pins its exact commit as a gitlink (mode
# 160000) in the Mobile repository. Safe to repeat. Never advances a
# registered gitlink to upstream HEAD (use ./git_submodule_update.sh --config).
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd -- "$project_dir"
config_path='external/AOWIS-CONFIG'
config_url='https://github.com/aowis-org/AOWIS-CONFIG.git'

[[ "$(git rev-parse --show-toplevel 2>/dev/null)" == "$project_dir" ]] || {
    echo 'Run from a Git checkout (not a source-only ZIP).' >&2
    exit 1
}

entry="$(git ls-files --stage -- "$config_path")"
if [[ "$entry" == '160000 '* ]]; then
    registered_url="$(git config -f .gitmodules --get "submodule.$config_path.url" || true)"
    [[ "$registered_url" == "$config_url" ]] || {
        echo "$config_path has an unexpected or missing submodule URL: $registered_url" >&2
        exit 1
    }
    # Restore the recorded version, never --remote here.
    git submodule sync -- "$config_path"
    git submodule update --init --checkout -- "$config_path"
    echo 'CONFIG already registered; initialized its pinned commit.'
elif [[ -n "$entry" ]]; then
    echo "$config_path is tracked as ordinary files, not a submodule. Refusing to replace it." >&2
    exit 1
else
    [[ ! -e "$config_path" && ! -L "$config_path" ]] || {
        echo "Refusing to replace existing path: $config_path" >&2
        exit 1
    }
    # Creates BOTH the .gitmodules record and the gitlink.
    git submodule add -- "$config_url" "$config_path"
    echo 'CONFIG registered. Commit the .gitmodules and CONFIG gitlink with this change.'
fi
