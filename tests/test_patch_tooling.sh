#!/usr/bin/env bash
set -euo pipefail

# Patch/snapshot filenames must not depend on the directory containing the
# checkout. The same logical archive is valid for *any* local Git checkout name.
source_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
tmp="$(mktemp -d)"
trap 'rm -rf -- "$tmp"' EXIT

make_repo() {
    local dest="$1"
    mkdir -p "$dest/tools" "$dest/docs"
    cp -p "$source_root/tools/apply_patch.sh" "$dest/tools/"
    cp -p "$source_root/tools/zip_mobile_project.sh" "$dest/tools/"
    printf 'Original\n' > "$dest/docs/survey.txt"
    printf 'Delete me\n' > "$dest/docs/obsolete.txt"
    git -C "$dest" init -q
    git -C "$dest" -c user.name=Test -c user.email=test@example.com add .
    git -C "$dest" -c user.name=Test -c user.email=test@example.com commit -qm initial
}

# Neither name resembles the repository's logical archive namespace.
repo1="$tmp/completely arbitrary checkout"
repo2="$tmp/second-location/other-name"
make_repo "$repo1"
make_repo "$repo2"

"$repo1/tools/zip_mobile_project.sh" "$tmp/first.zip"
"$repo2/tools/zip_mobile_project.sh" "$tmp/second.zip"
for archive in "$tmp/first.zip" "$tmp/second.zip"; do
    unzip -Z -1 "$archive" | grep -qx 'AOWIS-GUI-Mobile/docs/survey.txt'
    if unzip -Z -1 "$archive" | grep -q 'completely arbitrary\|other-name'; then
        printf 'Snapshot leaked the checkout name: %s\n' "$archive" >&2
        exit 1
    fi
done
printf 'PASS: snapshots have a stable archive root in arbitrary checkout paths\n'

python3 - "$tmp/aowis-mobile-test-portable.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w') as z:
    z.writestr('AOWIS-GUI-Mobile/docs/survey.txt', 'Updated\n')
    z.writestr('AOWIS-GUI-Mobile/.aowis-patch-delete.txt', 'docs/obsolete.txt\n')
PY
for repo in "$repo1" "$repo2"; do
    "$repo/tools/apply_patch.sh" "$tmp/aowis-mobile-test-portable.zip"
    [[ "$(cat "$repo/docs/survey.txt")" == Updated ]]
    [[ ! -e "$repo/docs/obsolete.txt" ]]
    [[ ! -e "$repo/.aowis-patch-delete.txt" ]]
    snapshot="$(dirname "$repo")/mobile.zip"
    unzip -Z -1 "$snapshot" | grep -qx 'AOWIS-GUI-Mobile/docs/survey.txt'
    [[ "$(unzip -p "$snapshot" AOWIS-GUI-Mobile/docs/survey.txt)" == Updated ]]
done
printf 'PASS: same patch applies to unrelated checkout names and refreshes snapshots\n'

# A patch can update the currently running apply script without truncating the
# inode Bash is reading (a failure of the former copy-in-place implementation).
python3 - "$tmp/aowis-mobile-test-self-replace.zip" "$source_root/tools/apply_patch.sh" <<'PYSELF'
import pathlib, sys, zipfile
content = pathlib.Path(sys.argv[2]).read_bytes() + b"\n# self replacement test marker\n"
with zipfile.ZipFile(sys.argv[1], 'w') as z:
    z.writestr('AOWIS-GUI-Mobile/tools/apply_patch.sh', content)
PYSELF
"$repo1/tools/apply_patch.sh" "$tmp/aowis-mobile-test-self-replace.zip"
grep -F '# self replacement test marker' "$repo1/tools/apply_patch.sh" >/dev/null
printf 'PASS: active patch script replaces itself safely\n'

python3 - "$tmp/aowis-mobile-test-traversal.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w') as z:
    z.writestr('AOWIS-GUI-Mobile/../outside.txt', 'malicious')
PY
if "$repo1/tools/apply_patch.sh" "$tmp/aowis-mobile-test-traversal.zip" >/dev/null 2>&1; then
    printf 'Traversal patch incorrectly accepted\n' >&2
    exit 1
fi
[[ ! -e "$tmp/outside.txt" ]]
printf 'PASS: unsafe patch path rejected\n'

python3 - "$tmp/aowis-mobile-test-wrong-namespace.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w') as z:
    z.writestr('wrong-project/docs/survey.txt', 'bad')
PY
if "$repo1/tools/apply_patch.sh" "$tmp/aowis-mobile-test-wrong-namespace.zip" >/dev/null 2>&1; then
    printf 'Wrong repository archive incorrectly accepted\n' >&2
    exit 1
fi
printf 'PASS: mismatched project archive rejected without using checkout directory name\n'

echo 'Portable patch/snapshot tooling tests passed.'
