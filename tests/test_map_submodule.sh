#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
repo="$work/mobile"
remote="$work/map"
mkdir -p "$repo/tools" "$remote"
cp "$root/tools/init_map.sh" "$repo/tools/"
cp "$root/git_submodule_init.sh" "$root/git_submodule_update.sh" "$root/.gitmodules" "$repo/"
for path in "$repo" "$remote"; do
    git -C "$path" init -q -b main
    git -C "$path" config user.name Test
    git -C "$path" config user.email test@example.invalid
    printf 'test\n' > "$path/README.md"
    git -C "$path" add .
    git -C "$path" commit -qm initial
done
# The nested CONFIG remote must not be contacted.
printf '[submodule "external/AOWIS-CONFIG"]\n\tpath = external/AOWIS-CONFIG\n\turl = https://invalid.example/config.git\n' > "$remote/.gitmodules"
git -C "$remote" update-index --add --cacheinfo "160000,$(git -C "$remote" rev-parse HEAD),external/AOWIS-CONFIG"
git -C "$remote" add .gitmodules
git -C "$remote" commit -qm metadata
pin="$(git -C "$remote" rev-parse HEAD)"
cat > "$work/gitconfig" <<CONFIG
[url "file://$remote"]
    insteadOf = https://github.com/aowis-org/AOWIS-SERVER-MAP.git
CONFIG
export GIT_CONFIG_GLOBAL="$work/gitconfig" GIT_CONFIG_NOSYSTEM=1 GIT_ALLOW_PROTOCOL=file
cd "$repo"
if ./git_submodule_init.sh --map > "$work/missing.log" 2>&1; then
    echo 'Missing MAP gitlink was accepted' >&2; exit 1
fi
grep -Fq 'Missing Git submodule gitlink' "$work/missing.log"
./tools/init_map.sh
[[ "$(git ls-files --stage -- external/AOWIS-SERVER-MAP)" == "160000 $pin"* ]]
[[ ! -e external/AOWIS-SERVER-MAP/external/AOWIS-CONFIG/.git ]]
git commit -qm 'Register MAP' -- .gitmodules external/AOWIS-SERVER-MAP
printf 'next\n' >> "$remote/README.md"
git -C "$remote" commit -qam next
new_pin="$(git -C "$remote" rev-parse HEAD)"
./tools/init_map.sh
[[ "$(git -C external/AOWIS-SERVER-MAP rev-parse HEAD)" == "$pin" ]]
git submodule deinit -f -- external/AOWIS-SERVER-MAP
./git_submodule_init.sh --map
[[ "$(git -C external/AOWIS-SERVER-MAP rev-parse HEAD)" == "$pin" ]]
./git_submodule_update.sh --map
[[ "$(git -C external/AOWIS-SERVER-MAP rev-parse HEAD)" == "$new_pin" ]]
[[ ! -e external/AOWIS-SERVER-MAP/external/AOWIS-CONFIG/.git ]]
echo 'MAP registration, pinned initialization and explicit update passed.'
