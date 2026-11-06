#!/usr/bin/env bash
set -euo pipefail

# First-time MODEL Git submodule registration; afterwards, Git stores and pins
# its exact commit as a gitlink (mode 160000) in the Mobile repository.
# Safe to repeat. Never updates a previously registered gitlink to upstream HEAD.
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd -- "$project_dir"
model_path='external/AOWIS-SERVER-MODEL'
model_url='https://github.com/aowis-org/AOWIS-SERVER-MODEL.git'

[[ "$(git rev-parse --show-toplevel 2>/dev/null)" == "$project_dir" ]] || {
    echo 'Run from a Git checkout (not a source-only ZIP).' >&2
    exit 1
}

if [[ "$(git ls-files --stage -- "$model_path")" == '160000 '* ]]; then
    # Restore the recorded version, never --remote here.
    git submodule sync -- "$model_path"
    git submodule update --init --checkout -- "$model_path"
    echo 'MODEL already registered; initialized its pinned commit.'
else
    [[ ! -e "$model_path" && ! -L "$model_path" ]] || {
        echo "Refusing to replace existing path: $model_path" >&2
        exit 1
    }
    # This command creates BOTH .gitmodules metadata and the gitlink.
    git submodule add "$model_url" "$model_path"
    echo 'MODEL registered. Commit the .gitmodules and MODEL gitlink with this change.'
fi
