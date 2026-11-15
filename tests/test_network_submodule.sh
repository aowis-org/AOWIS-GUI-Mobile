#!/usr/bin/env bash
set -euo pipefail

# Regression: NETWORK registration must write a real Git gitlink, not merely a
# .gitmodules entry, and repeated initialization must not change its pin.
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
repo="$work/a checkout with spaces"
network="$work/upstream-network"
mkdir -p "$repo/tools" "$network/include/aowis/network"
cp "$root/tools/init_network.sh" "$repo/tools/init_network.sh"
cp "$root/git_submodule_init.sh" "$repo/git_submodule_init.sh"
chmod +x "$repo/tools/init_network.sh" "$repo/git_submodule_init.sh"

# Local fake remote: the production URL is rewritten only inside this test.
git -C "$network" init -q
printf 'project(fake_network)\n' > "$network/CMakeLists.txt"
printf '// mock client\n' > "$network/include/aowis/network/wireguard_client.h"
git -C "$network" add .
git -C "$network" -c user.name=Test -c user.email=test@example.invalid commit -qm 'Initial network'
expected_commit="$(git -C "$network" rev-parse HEAD)"

git -C "$repo" init -q
printf 'test\n' > "$repo/README.md"
git -C "$repo" add README.md
git -C "$repo" -c user.name=Test -c user.email=test@example.invalid commit -qm 'Initial mobile'
cat > "$work/gitconfig" <<CONFIG
[url "file://$network"]
    insteadOf = https://github.com/aowis-org/AOWIS-NETWORK.git
CONFIG
export GIT_CONFIG_GLOBAL="$work/gitconfig"
export GIT_ALLOW_PROTOCOL=file

(cd "$repo" && ./tools/init_network.sh) >/dev/null
[[ "$(git -C "$repo" ls-files --stage -- external/AOWIS-NETWORK)" == "160000 $expected_commit"* ]]
[[ "$(git -C "$repo" config -f .gitmodules --get submodule.external/AOWIS-NETWORK.url)" == 'https://github.com/aowis-org/AOWIS-NETWORK.git' ]]

# A second run restores the pin even when upstream moved on.
printf 'newer\n' >> "$network/CMakeLists.txt"
git -C "$network" -c user.name=Test -c user.email=test@example.invalid commit -qam 'Newer network'
(cd "$repo" && ./tools/init_network.sh) >/dev/null
[[ "$(git -C "$repo/external/AOWIS-NETWORK" rev-parse HEAD)" == "$expected_commit" ]]

git -C "$repo" -c user.name=Test -c user.email=test@example.invalid commit -qm 'Register NETWORK' -- .gitmodules external/AOWIS-NETWORK
git -C "$repo" submodule deinit -f -- external/AOWIS-NETWORK >/dev/null
(cd "$repo" && ./git_submodule_init.sh --network) >/dev/null
[[ "$(git -C "$repo/external/AOWIS-NETWORK" rev-parse HEAD)" == "$expected_commit" ]]

# Missing gitlink is a clear error.
git -C "$repo" rm -q --cached external/AOWIS-NETWORK
if (cd "$repo" && ./git_submodule_init.sh --network) > "$work/error.txt" 2>&1; then
    echo 'Missing NETWORK gitlink was incorrectly accepted' >&2
    exit 1
fi
grep -Fq 'Missing Git submodule gitlink' "$work/error.txt"
printf 'NETWORK submodule registration, pin reuse, recreation and missing-gitlink checks passed.\n'
