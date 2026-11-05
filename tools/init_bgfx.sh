#!/usr/bin/env bash
set -euo pipefail

# Install bgfx.cmake as a temporary, pinned Git submodule. The wrapper's own
# submodules provide mutually matching bgfx, bx and bimg source revisions.
main() {
    local root dependency tag
    root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
    dependency="$root/external/bgfx.cmake"
    tag="v1.159.9485-574"

    if [[ "$(git -C "$root" rev-parse --show-toplevel 2>/dev/null)" != "$root" ]]; then
        echo 'Run this tool from an AOWIS-Mobile Git working tree.' >&2
        return 1
    fi
    if [[ ! -e "$dependency/.git" ]]; then
        if [[ -e "$dependency" ]]; then
            echo "The path $dependency already exists and is not an initialized Git submodule." >&2
            return 1
        fi
        git -C "$root" submodule add https://github.com/bkaradzic/bgfx.cmake.git external/bgfx.cmake
    fi
    git -C "$dependency" fetch --tags origin
    git -C "$dependency" checkout --detach "$tag"
    git -C "$dependency" submodule update --init --recursive
    git -C "$root" add .gitmodules external/bgfx.cmake
    printf 'Pinned bgfx.cmake submodule at %s\n' "$(git -C "$dependency" rev-parse HEAD)"
    printf '%s\n' 'Commit the staged .gitmodules + gitlink along with the mobile source patch.'
}
main "$@"
