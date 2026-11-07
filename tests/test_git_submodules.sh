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
vendor_upstream="$tmp_dir/bgfx vendor"
model_upstream="$tmp_dir/model upstream"
mobile_upstream="$tmp_dir/mobile upstream"
mobile_checkout="$tmp_dir/mobile checkout with spaces"
selective_checkout="$tmp_dir/selective checkout"

# Render carries its own pinned bgfx (as AOWIS-GUI-Render does).
git_init "$vendor_upstream"
printf 'bgfx pinned\n' > "$vendor_upstream/bgfx.txt"
git_commit "$vendor_upstream" 'bgfx initial'
git_init "$render_upstream"
printf 'Render initial\n' > "$render_upstream/renderer.txt"
git -C "$render_upstream" submodule add -q "$vendor_upstream" external/bgfx
git_commit "$render_upstream" 'Render initial with pinned bgfx'

git_init "$model_upstream"
printf 'MODEL initial\n' > "$model_upstream/model.txt"
git_commit "$model_upstream" 'MODEL initial'

git_init "$mobile_upstream"
cp "$source_dir/git_submodule_init.sh" "$source_dir/git_submodule_update.sh" "$mobile_upstream/"
git -C "$mobile_upstream" submodule add -q "$render_upstream" external/AOWIS-GUI-Render
git -C "$mobile_upstream" submodule add -q "$model_upstream" external/AOWIS-SERVER-MODEL
git_commit "$mobile_upstream" 'Mobile pinned submodules'

git clone -q "$mobile_upstream" "$mobile_checkout"
(cd -- "$tmp_dir" && "$mobile_checkout/git_submodule_init.sh" --render) >/dev/null
[[ -f "$mobile_checkout/external/AOWIS-GUI-Render/renderer.txt" ]]
[[ -f "$mobile_checkout/external/AOWIS-GUI-Render/external/bgfx/bgfx.txt" ]]
initial_render="$(git -C "$mobile_checkout/external/AOWIS-GUI-Render" rev-parse HEAD)"
initial_bgfx="$(git -C "$mobile_checkout/external/AOWIS-GUI-Render/external/bgfx" rev-parse HEAD)"
printf 'PASS: --render checks out Render and its recursively pinned bgfx\n'

# Advance Render and bgfx upstream. The updater moves Render; bgfx follows
# only what the new Render commit records, never bgfx's own remote branch.
printf 'bgfx newer\n' >> "$vendor_upstream/bgfx.txt"
git_commit "$vendor_upstream" 'bgfx newer'
printf 'Render newer\n' >> "$render_upstream/renderer.txt"
git_commit "$render_upstream" 'Render newer'
new_render="$(git -C "$render_upstream" rev-parse HEAD)"
"$mobile_checkout/git_submodule_update.sh" >/dev/null
[[ "$(git -C "$mobile_checkout/external/AOWIS-GUI-Render" rev-parse HEAD)" == "$new_render" ]]
[[ "$(git -C "$mobile_checkout/external/AOWIS-GUI-Render/external/bgfx" rev-parse HEAD)" == "$initial_bgfx" ]]
[[ "$(git -C "$mobile_checkout" ls-files --stage -- external/AOWIS-GUI-Render)" == "160000 $initial_render "* ]]
printf 'PASS: update advances Render only and keeps its recorded bgfx pin\n'

# Initializing against the superproject must restore its pinned Render commit.
"$mobile_checkout/git_submodule_init.sh" --render >/dev/null
[[ "$(git -C "$mobile_checkout/external/AOWIS-GUI-Render" rev-parse HEAD)" == "$initial_render" ]]
printf 'MODEL newer\n' >> "$model_upstream/model.txt"
git_commit "$model_upstream" 'MODEL newer'
new_model="$(git -C "$model_upstream" rev-parse HEAD)"
"$mobile_checkout/git_submodule_update.sh" --all >/dev/null
[[ "$(git -C "$mobile_checkout/external/AOWIS-GUI-Render" rev-parse HEAD)" == "$new_render" ]]
[[ "$(git -C "$mobile_checkout/external/AOWIS-SERVER-MODEL" rev-parse HEAD)" == "$new_model" ]]
[[ "$(git -C "$mobile_checkout/external/AOWIS-GUI-Render/external/bgfx" rev-parse HEAD)" == "$initial_bgfx" ]]
printf 'PASS: --render restores pinned Render and --all advances Render and MODEL only\n'

# Selective init should not initialize the other direct submodule.
git clone -q "$mobile_upstream" "$selective_checkout"
"$selective_checkout/git_submodule_init.sh" --render >/dev/null
[[ -f "$selective_checkout/external/AOWIS-GUI-Render/renderer.txt" ]]
[[ ! -f "$selective_checkout/external/AOWIS-SERVER-MODEL/model.txt" ]]
"$selective_checkout/git_submodule_init.sh" --model >/dev/null
[[ -f "$selective_checkout/external/AOWIS-SERVER-MODEL/model.txt" ]]
if "$selective_checkout/git_submodule_init.sh" --bgfx >/dev/null 2>&1; then
    printf 'FAIL: the removed bgfx.cmake option is still accepted\n' >&2
    exit 1
fi
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
