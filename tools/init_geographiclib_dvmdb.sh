#!/usr/bin/env bash
set -euo pipefail

# Register GeographicLib and the native DVMDB as real, pinned Git submodules.
# A source ZIP cannot carry mode-160000 gitlinks; Git must create those in the
# actual Mobile working tree. Repeat runs restore recorded pins, not remote HEAD.
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd -- "$project_dir"

usage() {
    cat <<'HELP'
Usage: ./tools/init_geographiclib_dvmdb.sh [--geographiclib | --dvmdb | --help]

  (no option)    Register/restore both dependencies.
  --geographiclib Register/restore GeographicLib only.
  --dvmdb        Register/restore DVMDB only.
  --help         Show this help.

Creates Git gitlinks and .gitmodules records. Commit them with your changes.
Does not build either library, start a database, or recursively initialize
DVMDB's nested submodules. Existing pins are never advanced automatically.
HELP
}

die() {
    printf 'Mobile dependencies: %s\n' "$*" >&2
    exit 1
}

[[ "$(git rev-parse --show-toplevel 2>/dev/null)" == "$project_dir" ]] ||
    die 'Expected a Git working-tree root (a source ZIP is not a Git checkout).'

geo_path='external/GeographicLib'
geo_url='https://github.com/geographiclib/geographiclib.git'
dvmdb_path='external/AOWIS-SERVER-DVMDB'
dvmdb_url='https://github.com/aowis-org/AOWIS-SERVER-DVMDB.git'
paths=()
urls=()
case "${1:-}" in
    '')
        [[ $# -eq 0 ]] || { usage >&2; exit 2; }
        paths=("$geo_path" "$dvmdb_path")
        urls=("$geo_url" "$dvmdb_url")
        ;;
    --geographiclib)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        paths=("$geo_path")
        urls=("$geo_url")
        ;;
    --dvmdb)
        [[ $# -eq 1 ]] || { usage >&2; exit 2; }
        paths=("$dvmdb_path")
        urls=("$dvmdb_url")
        ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
esac

for index in "${!paths[@]}"; do
    path="${paths[index]}"
    url="${urls[index]}"
    entry="$(git ls-files --stage -- "$path")"
    if [[ "$entry" == '160000 '* ]]; then
        # The index controls identity: do not change the recorded commit.
        registered_url="$(git config -f .gitmodules --get "submodule.$path.url" || true)"
        [[ "$registered_url" == "$url" ]] ||
            die "$path has an unexpected or missing submodule URL: $registered_url"
        git submodule sync -- "$path"
        git submodule update --init --checkout -- "$path"
        printf 'Restored pinned dependency %s\n' "$path"
    elif [[ -n "$entry" ]]; then
        die "$path is tracked as ordinary files, not a submodule. Refusing to replace it."
    else
        [[ ! -e "$path" && ! -L "$path" ]] ||
            die "Existing path blocks Git submodule registration: $path"
        git submodule add -- "$url" "$path"
        printf 'Registered %s; commit .gitmodules and its Git gitlink.\n' "$path"
    fi
done
