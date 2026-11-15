#!/usr/bin/env bash
set -euo pipefail

# Regression: CONFIG registration must write a real Git gitlink, not merely a
# .gitmodules entry, and repeated initialization must not change its pin.
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
repo="$work/a checkout with spaces"
config="$work/upstream-config"
mkdir -p "$repo/tools" "$config/include/aowis/config"
cp "$root/tools/init_config.sh" "$repo/tools/init_config.sh"
cp "$root/git_submodule_init.sh" "$repo/git_submodule_init.sh"
chmod +x "$repo/tools/init_config.sh" "$repo/git_submodule_init.sh"

# Local fake remote: the production URL is rewritten only inside this test.
git -C "$config" init -q
printf 'project(fake_config)\n' > "$config/CMakeLists.txt"
printf '// mock client\n' > "$config/include/aowis/config/wireguard_profiles.h"
git -C "$config" add .
git -C "$config" -c user.name=Test -c user.email=test@example.invalid commit -qm 'Initial config'
expected_commit="$(git -C "$config" rev-parse HEAD)"

git -C "$repo" init -q
printf 'test\n' > "$repo/README.md"
git -C "$repo" add README.md
git -C "$repo" -c user.name=Test -c user.email=test@example.invalid commit -qm 'Initial mobile'
cat > "$work/gitconfig" <<CONFIG
[url "file://$config"]
    insteadOf = https://github.com/aowis-org/AOWIS-CONFIG.git
CONFIG
export GIT_CONFIG_GLOBAL="$work/gitconfig"
export GIT_ALLOW_PROTOCOL=file

(cd "$repo" && ./tools/init_config.sh) >/dev/null
[[ "$(git -C "$repo" ls-files --stage -- external/AOWIS-CONFIG)" == "160000 $expected_commit"* ]]
[[ "$(git -C "$repo" config -f .gitmodules --get submodule.external/AOWIS-CONFIG.url)" == 'https://github.com/aowis-org/AOWIS-CONFIG.git' ]]

# A second run restores the pin even when upstream moved on.
printf 'newer\n' >> "$config/CMakeLists.txt"
git -C "$config" -c user.name=Test -c user.email=test@example.invalid commit -qam 'Newer config'
(cd "$repo" && ./tools/init_config.sh) >/dev/null
[[ "$(git -C "$repo/external/AOWIS-CONFIG" rev-parse HEAD)" == "$expected_commit" ]]

git -C "$repo" -c user.name=Test -c user.email=test@example.invalid commit -qm 'Register CONFIG' -- .gitmodules external/AOWIS-CONFIG
git -C "$repo" submodule deinit -f -- external/AOWIS-CONFIG >/dev/null
(cd "$repo" && ./git_submodule_init.sh --config) >/dev/null
[[ "$(git -C "$repo/external/AOWIS-CONFIG" rev-parse HEAD)" == "$expected_commit" ]]

# Missing gitlink is a clear error.
git -C "$repo" rm -q --cached external/AOWIS-CONFIG
if (cd "$repo" && ./git_submodule_init.sh --config) > "$work/error.txt" 2>&1; then
    echo 'Missing CONFIG gitlink was incorrectly accepted' >&2
    exit 1
fi
grep -Fq 'Missing Git submodule gitlink' "$work/error.txt"
printf 'CONFIG submodule registration, pin reuse, recreation and missing-gitlink checks passed.\n'
