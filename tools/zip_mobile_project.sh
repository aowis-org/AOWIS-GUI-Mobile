#!/usr/bin/env bash
set -euo pipefail

# Export a reviewable source snapshot of AOWIS-Mobile. Include tracked files
# and non-ignored untracked files, but not .git, generated outputs, secrets or
# contents of nested Git submodules (which are separate repositories).
main() {
    local script_dir project_root project_parent project_name output_arg output_dir output_path
    local file_list tmp_dir tmp_archive relative_path
    local -a files=()

    if [[ $# -gt 1 ]]; then
        printf 'Usage: %s [output.zip]\n' "${0##*/}" >&2
        return 2
    fi
    for executable in git zip; do
        command -v "$executable" >/dev/null 2>&1 || {
            printf 'Missing required command: %s\n' "$executable" >&2
            return 1
        }
    done

    script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
    project_root="$(cd -- "$script_dir/.." && pwd -P)"
    project_parent="$(dirname -- "$project_root")"
    project_name="$(basename -- "$project_root")"
    if [[ "$(git -C "$project_root" rev-parse --show-toplevel 2>/dev/null)" != "$project_root" ]]; then
        printf 'Expected the root of a Git worktree: %s\n' "$project_root" >&2
        return 1
    fi

    output_arg="${1:-$project_parent/mobile.zip}"
    output_dir="$(dirname -- "$output_arg")"
    [[ -d "$output_dir" ]] || {
        printf 'Output directory does not exist: %s\n' "$output_dir" >&2
        return 1
    }
    output_path="$(cd -- "$output_dir" && pwd -P)/$(basename -- "$output_arg")"
    [[ "$output_path" == *.zip ]] || {
        printf 'Output filename must end in .zip.\n' >&2
        return 1
    }

    tmp_dir="$(mktemp -d "$output_dir/.aowis-mobile-zip.XXXXXX")"
    trap 'rm -rf -- "$tmp_dir"' RETURN
    file_list="$tmp_dir/paths"
    git -C "$project_root" ls-files -z --cached --others --exclude-standard > "$file_list"

    while IFS= read -r -d '' relative_path; do
        [[ -f "$project_root/$relative_path" && ! -L "$project_root/$relative_path" ]] || continue
        [[ "$relative_path" != *$'\n'* ]] || {
            printf 'Cannot archive a filename containing a newline: %s\n' "$relative_path" >&2
            return 1
        }
        [[ "$project_root/$relative_path" != "$output_path" ]] || continue

        # Exclude build outputs even when .gitignore has not yet learned about
        # the corresponding toolchain, and avoid accidentally sharing secrets.
        case "/$relative_path" in
            */build/*|*/build-*/*|*/cmake-build-*/*|*/CMakeFiles/*|*/Testing/*|*/_deps/*|\
            */.gradle/*|*/.cxx/*|*/DerivedData/*|*/Pods/*|*/.idea/*|*/.vscode/*|\
            */.qtcreator/*|*/.cache/*|*/__pycache__/*|*/.aowis-mobile-zip.*/*) continue ;;
            */.env|*/.env.*|*.pem|*.key|*.p12|*.pfx|*.keystore|*.jks|*.mobileprovision|\
            *.apk|*.aab|*.ipa|*.o|*.obj|*.a|*.so|*.dylib|*.dll|*.wasm|*.pyc|\
            *.log|*.tmp|*.swp|*.swo|*/.DS_Store) continue ;;
        esac
        files+=("$project_name/$relative_path")
    done < "$file_list"

    if (( ${#files[@]} == 0 )); then
        printf 'No source files to archive.\n' >&2
        return 1
    fi

    tmp_archive="$tmp_dir/mobile.zip"
    (
        cd -- "$project_parent"
        printf '%s\n' "${files[@]}" | zip -q "$tmp_archive" -@
    )
    mv -f -- "$tmp_archive" "$output_path"
    printf 'Created %s (%d files)\n' "$output_path" "${#files[@]}"
}

main "$@"
