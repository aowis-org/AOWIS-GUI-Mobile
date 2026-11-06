#!/usr/bin/env bash
set -euo pipefail

# Regression: MODEL registration must write a real Git gitlink, not merely a
# .gitmodules entry, and repeated initialization must not change its pin.
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
repo="$work/a checkout with spaces"
model="$work/upstream-model"
mkdir -p "$repo/tools" "$model/include/aowis/model/units"
cp "$root/tools/init_model.sh" "$repo/tools/init_model.sh"
cp "$root/git_submodule_init.sh" "$repo/git_submodule_init.sh"
chmod +x "$repo/tools/init_model.sh" "$repo/git_submodule_init.sh"

# Local fake remote: the production URL is rewritten only inside this test.
git -C "$model" init -q
printf 'project(fake_model)\n' > "$model/CMakeLists.txt"
printf '// mock model registry\n' > "$model/include/aowis/model/units/unit_profile.h"
git -C "$model" add .
git -C "$model" -c user.name=Test -c user.email=test@example.invalid commit -qm 'Initial model'
expected_commit="$(git -C "$model" rev-parse HEAD)"

git -C "$repo" init -q
printf 'test\n' > "$repo/README.md"
git -C "$repo" add README.md
git -C "$repo" -c user.name=Test -c user.email=test@example.invalid commit -qm 'Initial mobile'
# A process-wide config is inherited by submodule clone child processes.
cat > "$work/gitconfig" <<CONFIG
[url "file://$model"]
    insteadOf = https://github.com/aowis-org/AOWIS-SERVER-MODEL.git
CONFIG
export GIT_CONFIG_GLOBAL="$work/gitconfig"
export GIT_ALLOW_PROTOCOL=file

(cd "$repo" && ./tools/init_model.sh)
[[ "$(git -C "$repo" ls-files --stage -- external/AOWIS-SERVER-MODEL)" == "160000 $expected_commit"* ]]
[[ "$(git -C "$repo" config -f .gitmodules --get submodule.external/AOWIS-SERVER-MODEL.url)" == 'https://github.com/aowis-org/AOWIS-SERVER-MODEL.git' ]]
(cd "$repo" && ./tools/init_model.sh)
[[ "$(git -C "$repo/external/AOWIS-SERVER-MODEL" rev-parse HEAD)" == "$expected_commit" ]]

# Pinned initialization works after the worktree is removed and recreated.
git -C "$repo" -c user.name=Test -c user.email=test@example.invalid commit -qm 'Register MODEL' -- .gitmodules external/AOWIS-SERVER-MODEL
git -C "$repo" submodule deinit -f -- external/AOWIS-SERVER-MODEL >/dev/null
(cd "$repo" && ./git_submodule_init.sh --model)
[[ "$(git -C "$repo/external/AOWIS-SERVER-MODEL" rev-parse HEAD)" == "$expected_commit" ]]

# Missing MODEL is a clear error; don't silently treat a .gitmodules entry
# without a gitlink as a valid dependency.
git -C "$repo" rm -q --cached external/AOWIS-SERVER-MODEL
if (cd "$repo" && ./git_submodule_init.sh --model) > "$work/error.txt" 2>&1; then
    echo 'Missing MODEL gitlink was incorrectly accepted' >&2
    exit 1
fi
grep -Fq 'Missing Git submodule gitlink' "$work/error.txt"
printf 'MODEL submodule registration, pin reuse, recreation and missing-gitlink checks passed.\n'
