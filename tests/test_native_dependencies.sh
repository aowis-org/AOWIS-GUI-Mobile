#!/usr/bin/env bash
set -euo pipefail

# Native deps must be real pinned Git submodules, not copied directories or
# .gitmodules-only entries. Never recurse into DVMDB's private nested MODEL URL.
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
repo="$work/checkout name deliberately unrelated"
geo="$work/fake-geographiclib"
dvmdb="$work/fake-dvmdb"
mkdir -p "$repo/tools" "$geo/include/GeographicLib" "$dvmdb/include/aowis/dvmdb"
cp "$root/tools/init_geographiclib_dvmdb.sh" "$repo/tools/"
cp "$root/git_submodule_init.sh" "$repo/"
chmod +x "$repo/tools/init_geographiclib_dvmdb.sh" "$repo/git_submodule_init.sh"

for source in "$geo" "$dvmdb"; do
    git -C "$source" init -q
    printf 'project(test)\n' > "$source/CMakeLists.txt"
    git -C "$source" add .
    git -C "$source" -c user.name=Test -c user.email=test@example.invalid commit -qm first
 done
printf 'Fake nested MODEL url; must not clone it.\n' > "$dvmdb/README.md"
cat > "$dvmdb/.gitmodules" <<'MODULE'
[submodule "external/AOWIS-SERVER-MODEL"]
    path = external/AOWIS-SERVER-MODEL
    url = git@github-aowis:aowis-org/AOWIS-SERVER-MODEL.git
MODULE
 git -C "$dvmdb" add README.md .gitmodules
 git -C "$dvmdb" -c user.name=Test -c user.email=test@example.invalid commit -qm second

geo_pin="$(git -C "$geo" rev-parse HEAD)"
dvmdb_pin="$(git -C "$dvmdb" rev-parse HEAD)"
git -C "$repo" init -q
printf 'mobile\n' > "$repo/README.md"
git -C "$repo" add README.md
git -C "$repo" -c user.name=Test -c user.email=test@example.invalid commit -qm first

cat > "$work/gitconfig" <<CONFIG
[url "file://$geo"]
    insteadOf = https://github.com/geographiclib/geographiclib.git
[url "file://$dvmdb"]
    insteadOf = https://github.com/aowis-org/AOWIS-SERVER-DVMDB.git
CONFIG
export GIT_CONFIG_GLOBAL="$work/gitconfig"
export GIT_ALLOW_PROTOCOL=file

(cd "$repo" && ./tools/init_geographiclib_dvmdb.sh)
[[ "$(git -C "$repo" ls-files --stage -- external/GeographicLib)" == "160000 $geo_pin"* ]]
[[ "$(git -C "$repo" ls-files --stage -- external/AOWIS-SERVER-DVMDB)" == "160000 $dvmdb_pin"* ]]
[[ "$(git -C "$repo" config -f .gitmodules --get submodule.external/GeographicLib.url)" == https://github.com/geographiclib/geographiclib.git ]]
[[ "$(git -C "$repo" config -f .gitmodules --get submodule.external/AOWIS-SERVER-DVMDB.url)" == https://github.com/aowis-org/AOWIS-SERVER-DVMDB.git ]]
# DVMDB's nested MODEL is not even registered as a gitlink by Mobile.
[[ ! -e "$repo/external/AOWIS-SERVER-DVMDB/external/AOWIS-SERVER-MODEL" ]]

# A second registration must not advance either gitlink or change metadata.
printf 'new upstream version\n' >> "$geo/CMakeLists.txt"
git -C "$geo" -c user.name=Test -c user.email=test@example.invalid commit -qam second
printf 'new upstream version\n' >> "$dvmdb/CMakeLists.txt"
git -C "$dvmdb" -c user.name=Test -c user.email=test@example.invalid commit -qam third
(cd "$repo" && ./tools/init_geographiclib_dvmdb.sh)
[[ "$(git -C "$repo/external/GeographicLib" rev-parse HEAD)" == "$geo_pin" ]]
[[ "$(git -C "$repo/external/AOWIS-SERVER-DVMDB" rev-parse HEAD)" == "$dvmdb_pin" ]]

git -C "$repo" -c user.name=Test -c user.email=test@example.invalid commit -qm 'Pinned deps' -- .gitmodules external/GeographicLib external/AOWIS-SERVER-DVMDB
 git -C "$repo" submodule deinit -f -- external/GeographicLib external/AOWIS-SERVER-DVMDB > /dev/null
(cd "$repo" && ./git_submodule_init.sh --geographiclib)
(cd "$repo" && ./git_submodule_init.sh --dvmdb)
[[ "$(git -C "$repo/external/GeographicLib" rev-parse HEAD)" == "$geo_pin" ]]
[[ "$(git -C "$repo/external/AOWIS-SERVER-DVMDB" rev-parse HEAD)" == "$dvmdb_pin" ]]
[[ ! -e "$repo/external/AOWIS-SERVER-DVMDB/external/AOWIS-SERVER-MODEL" ]]

# A .gitmodules stanza alone must not be accepted as registered submodule.
git -C "$repo" rm -q --cached external/GeographicLib
if (cd "$repo" && ./git_submodule_init.sh --geographiclib) > "$work/error.txt" 2>&1; then
    echo 'Missing GeographicLib gitlink was incorrectly accepted' >&2
    exit 1
fi
grep -Fq 'Missing Git submodule gitlink' "$work/error.txt"
echo 'Native dependency registration, pins, no-recursion, and missing-gitlink tests passed.'
