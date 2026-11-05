#!/usr/bin/env bash
set -euo pipefail

# Local-only Git integration test: no GitHub, Qt, bgfx or Android SDK required.
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
tmp_dir="$(mktemp -d)"
trap 'rm -rf -- "$tmp_dir"' EXIT

# Allow local fixture repositories as submodule URLs (Git disables this by
# default for submodule clones). Do not alter the user's global Git config.
export GIT_ALLOW_PROTOCOL=file
export GIT_TERMINAL_PROMPT=0

git_init() {
    local repo="$1"
    git init -q -b main "$repo"
    git -C "$repo" config user.name 'AOWIS Test'
    git -C "$repo" config user.email 'test@example.invalid'
}

git_commit() {
    local repo="$1"
    local message="$2"
    git -C "$repo" add -A
    git -C "$repo" commit -qm "$message"
}

render_upstream="$tmp_dir/render upstream"
bgfx_upstream="$tmp_dir/bgfx wrapper"
nested_upstream="$tmp_dir/nested vendor"
mobile_upstream="$tmp_dir/mobile upstream"
mobile_checkout="$tmp_dir/mobile checkout with spaces"
selective_checkout="$tmp_dir/selective checkout"

git_init "$render_upstream"
printf 'Render initial\n' > "$render_upstream/renderer.txt"
git_commit "$render_upstream" 'Render initial'

git_init "$nested_upstream"
printf 'Nested pinned\n' > "$nested_upstream/vendor.txt"
git_commit "$nested_upstream" 'Nested vendor initial'

git_init "$bgfx_upstream"
printf 'bgfx wrapper initial\n' > "$bgfx_upstream/wrapper.txt"
git -C "$bgfx_upstream" submodule add -q "$nested_upstream" deps/vendor
git_commit "$bgfx_upstream" 'bgfx wrapper with pinned vendor'

git_init "$mobile_upstream"
cp "$source_dir/git_submodule_init.sh" "$source_dir/git_submodule_update.sh" "$mobile_upstream/"
git -C "$mobile_upstream" submodule add -q "$render_upstream" external/AOWIS-GUI-Render
git -C "$mobile_upstream" submodule add -q "$bgfx_upstream" external/bgfx.cmake
git_commit "$mobile_upstream" 'Mobile pinned submodules'

git clone -q "$mobile_upstream" "$mobile_checkout"
[[ ! -f "$mobile_checkout/external/bgfx.cmake/deps/vendor/vendor.txt" ]]
(cd -- "$tmp_dir" && "$mobile_checkout/git_submodule_init.sh") >/dev/null
[[ -f "$mobile_checkout/external/AOWIS-GUI-Render/renderer.txt" ]]
[[ -f "$mobile_checkout/external/bgfx.cmake/deps/vendor/vendor.txt" ]]
initial_render="$(git -C "$mobile_checkout/external/AOWIS-GUI-Render" rev-parse HEAD)"
initial_bgfx="$(git -C "$mobile_checkout/external/bgfx.cmake" rev-parse HEAD)"
initial_vendor="$(git -C "$mobile_checkout/external/bgfx.cmake/deps/vendor" rev-parse HEAD)"
printf 'PASS: init checks out both direct gitlinks and recursive pinned vendor\n'

# Advance both upstreams. The default updater must move only Render.
printf 'Render newer\n' >> "$render_upstream/renderer.txt"
git_commit "$render_upstream" 'Render newer'
new_render="$(git -C "$render_upstream" rev-parse HEAD)"
printf 'bgfx wrapper newer\n' >> "$bgfx_upstream/wrapper.txt"
git_commit "$bgfx_upstream" 'bgfx wrapper newer'
"$mobile_checkout/git_submodule_update.sh" >/dev/null
[[ "$(git -C "$mobile_checkout/external/AOWIS-GUI-Render" rev-parse HEAD)" == "$new_render" ]]
[[ "$(git -C "$mobile_checkout/external/bgfx.cmake" rev-parse HEAD)" == "$initial_bgfx" ]]
[[ "$(git -C "$mobile_checkout/external/bgfx.cmake/deps/vendor" rev-parse HEAD)" == "$initial_vendor" ]]
[[ "$(git -C "$mobile_checkout" ls-files --stage -- external/AOWIS-GUI-Render)" == "160000 $initial_render "* ]]
printf 'PASS: update advances Render only and preserves bgfx nested pins\n'

# Initializing against the superproject must restore its pinned Render commit.
"$mobile_checkout/git_submodule_init.sh" --render >/dev/null
[[ "$(git -C "$mobile_checkout/external/AOWIS-GUI-Render" rev-parse HEAD)" == "$initial_render" ]]
"$mobile_checkout/git_submodule_update.sh" --all >/dev/null
[[ "$(git -C "$mobile_checkout/external/AOWIS-GUI-Render" rev-parse HEAD)" == "$new_render" ]]
[[ "$(git -C "$mobile_checkout/external/bgfx.cmake" rev-parse HEAD)" == "$initial_bgfx" ]]
printf 'PASS: --render restores pinned Render and --all leaves third parties pinned\n'

# Selective init should not initialize the other direct submodule.
git clone -q "$mobile_upstream" "$selective_checkout"
"$selective_checkout/git_submodule_init.sh" --render >/dev/null
[[ -f "$selective_checkout/external/AOWIS-GUI-Render/renderer.txt" ]]
[[ ! -f "$selective_checkout/external/bgfx.cmake/wrapper.txt" ]]
"$selective_checkout/git_submodule_init.sh" --bgfx >/dev/null
[[ -f "$selective_checkout/external/bgfx.cmake/deps/vendor/vendor.txt" ]]
printf 'PASS: selective initialization affects only the requested submodule\n'

# A .gitmodules entry without a gitlink must not silently succeed.
no_gitlink="$tmp_dir/missing gitlink"
git_init "$no_gitlink"
cp "$source_dir/git_submodule_init.sh" "$source_dir/git_submodule_update.sh" "$no_gitlink/"
cat > "$no_gitlink/.gitmodules" <<'MODULES'
[submodule "external/AOWIS-GUI-Render"]
    path = external/AOWIS-GUI-Render
    url = https://github.com/aowis-org/AOWIS-GUI-Render.git
MODULES
git_commit "$no_gitlink" 'Only module configuration, no gitlink'
if "$no_gitlink/git_submodule_init.sh" --render >"$tmp_dir/missing.out" 2>&1; then
    printf 'FAIL: init accepted a .gitmodules entry without a Git gitlink\n' >&2
    exit 1
fi
grep -q 'Missing Git submodule gitlink' "$tmp_dir/missing.out"
if "$no_gitlink/git_submodule_update.sh" >"$tmp_dir/update-missing.out" 2>&1; then
    printf 'FAIL: update accepted a .gitmodules entry without a Git gitlink\n' >&2
    exit 1
fi
grep -q 'Missing Git submodule gitlink' "$tmp_dir/update-missing.out"
printf 'PASS: incomplete submodule registrations produce actionable errors\n'

"$mobile_checkout/git_submodule_init.sh" --help >/dev/null
"$mobile_checkout/git_submodule_update.sh" --help >/dev/null
if "$mobile_checkout/git_submodule_update.sh" --not-a-real-option > /dev/null 2>&1; then
    printf 'FAIL: update accepted unknown option\n' >&2
    exit 1
fi
printf 'PASS: help and unknown-option handling\n'

printf 'Mobile Git submodule tests passed.\n'
