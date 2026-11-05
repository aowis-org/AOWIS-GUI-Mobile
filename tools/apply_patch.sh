#!/usr/bin/env bash
set -euo pipefail

# Patch ZIPs may replace this script during execution. Keep the entire operation
# in a function so Bash parses it before unzip overwrites the on-disk file.
main() {
    local script_dir project_root patch_name patch
    local manifest entry relative component current target

    script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
    project_root="$(cd -- "$script_dir/.." && pwd -P)"
    # An archive namespace is not the local checkout directory name.
    # Keep patches portable across arbitrary checkout locations and names.
    local archive_root="AOWIS-GUI-Mobile"
    manifest="$project_root/.aowis-patch-delete.txt"

    # Disallow symbolic links anywhere along a relative path, including the
    # destination itself. A ZIP or deletion manifest must not escape the repo.
    validate_relative_path() {
        local relative_path="$1"
        local parent_path="$project_root"
        local part
        local -a parts=()
        [[ -n "$relative_path" && "$relative_path" != /* &&
           "$relative_path" != *'\'* && "$relative_path" != *$'\n'* ]] || return 1
        IFS='/' read -r -a parts <<< "$relative_path"
        for part in "${parts[@]}"; do
            [[ -n "$part" && "$part" != . && "$part" != .. &&
               "$part" != .git ]] || return 1
            parent_path="$parent_path/$part"
            [[ ! -L "$parent_path" ]] || return 1
        done
    }

    apply_deletions() {
        [[ ! -L "$manifest" ]] || {
            printf 'Refusing symlinked deletion manifest.\n' >&2
            return 1
        }
        [[ -f "$manifest" ]] || return 0
        local relative_path file directory
        local -a files=()
        # Validate the entire manifest before removing any file.
        while IFS= read -r relative_path || [[ -n "$relative_path" ]]; do
            if ! validate_relative_path "$relative_path" ||
               [[ "$relative_path" == '.aowis-patch-delete.txt' ]]; then
                printf 'Unsafe patch deletion path: %s\n' "$relative_path" >&2
                return 1
            fi
            file="$project_root/$relative_path"
            [[ ! -d "$file" ]] || {
                printf 'Refusing to delete directory: %s\n' "$relative_path" >&2
                return 1
            }
            files+=("$file")
        done < "$manifest"
        for file in "${files[@]}"; do
            rm -f -- "$file"
        done
        # Remove only now-empty parent directories.
        for file in "${files[@]}"; do
            directory="$(dirname -- "$file")"
            while [[ "$directory" != "$project_root" ]]; do
                rmdir -- "$directory" 2>/dev/null || break
                directory="$(dirname -- "$directory")"
            done
        done
        rm -f -- "$manifest"
        printf 'Removed %d obsolete file(s) listed by patch.\n' "${#files[@]}"
    }

    if [[ $# -eq 1 && "$1" == --finish ]]; then
        if [[ ! -f "$manifest" ]]; then
            printf 'No pending patch deletions.\n'
            return 0
        fi
        apply_deletions
        return
    fi

    if [[ $# -ne 1 ]]; then
        printf 'Usage: %s /path/to/aowis-mobile-<description>.zip | --finish\n' "${0##*/}" >&2
        return 2
    fi

    patch_name="${1##*/}"
    case "$patch_name" in
        aowis-mobile-?*.zip) ;;
        *)
            printf 'Refusing patch %s: expected aowis-mobile-<description>.zip\n' "$patch_name" >&2
            return 2
            ;;
    esac

    for entry in unzip zip git realpath touch; do
        command -v "$entry" >/dev/null 2>&1 || {
            printf 'Missing required command: %s\n' "$entry" >&2
            return 1
        }
    done

    patch="$(realpath -- "$1")"
    if ! unzip -tq "$patch" >/dev/null; then
        printf 'Invalid patch ZIP: %s\n' "$patch" >&2
        return 1
    fi

    # Inspect all archive entries before changing the checkout.
    local file_count=0
    while IFS= read -r entry; do
        [[ "$entry" == "$archive_root/" ]] && continue
        if [[ "$entry" != "$archive_root/"* ]]; then
            printf 'Invalid patch path: %s\n' "$entry" >&2
            return 1
        fi
        relative="${entry#"$archive_root/"}"
        [[ "$relative" == */ ]] && relative="${relative%/}"
        if ! validate_relative_path "$relative"; then
            printf 'Invalid or symlinked patch path: %s\n' "$entry" >&2
            return 1
        fi
        if [[ "$entry" != */ ]]; then
            ((file_count+=1))
        fi
    done < <(unzip -Z -1 "$patch")
    if (( file_count == 0 )); then
        printf 'Patch contains no files.\n' >&2
        return 1
    fi

    # Reject symlink entries before extraction: a symlink could redirect later
    # ZIP entries outside even the private staging directory.
    if unzip -Z -l "$patch" | grep -qE "^l[rwx-]{9} "; then
        printf 'Patch contains a symbolic link; refusing to apply.\n' >&2
        return 1
    fi

    # Extract into a private staging directory before updating the checkout.
    local staging
    staging="$(mktemp -d)"
    trap 'rm -rf -- "$staging"' RETURN
    unzip -q -o "$patch" -d "$staging"
    if [[ -n "$(find "$staging" -type l -print -quit)" ]]; then
        printf 'Patch contains a symbolic link; refusing to apply.\n' >&2
        return 1
    fi
    while IFS= read -r entry; do
        [[ "$entry" == "$archive_root/" ]] && continue
        relative="${entry#"$archive_root/"}"
        target="$project_root/$relative"
        if [[ "$entry" == */ ]]; then
            mkdir -p -- "$target"
        else
            mkdir -p -- "$(dirname -- "$target")"
            # Replace files atomically by rename, rather than truncating the
            # existing inode. This matters when a patch updates this script
            # while Bash is still executing the original script file.
            local temporary_target
            temporary_target="$(mktemp "$(dirname -- "$target")/.aowis-patch.XXXXXXXX")"
            cp -p -- "$staging/$entry" "$temporary_target"
            mv -f -- "$temporary_target" "$target"
        fi
        # Refresh both files and directories for make/Ninja dependency checks.
        touch -c -m -- "$target"
    done < <(unzip -Z -1 "$patch")
    trap - RETURN
    rm -rf -- "$staging"

    apply_deletions
    printf 'Applied %s with current file and directory timestamps.\n' "$patch"

    # Export a fresh source snapshot next to the mobile repository.
    printf 'Refreshing mobile.zip...\n'
    if ! bash "$project_root/tools/zip_mobile_project.sh"; then
        printf 'Patch applied, but refreshing mobile.zip failed.\n' >&2
        return 1
    fi
}

main "$@"
