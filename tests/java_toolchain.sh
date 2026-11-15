# Sourced by the Java bridge tests. Sets JAVAC and JAVA to one JDK (17 or
# newer): $JAVA_HOME, else javac on PATH, else the newest JDK under
# /usr/lib/jvm (Arch keeps installed JDKs there even when archlinux-java
# selects a JRE as default, so javac is not on PATH).
aowis_find_jdk() {
    local candidate version best='' best_version=0 on_path=''
    if on_path="$(command -v javac 2>/dev/null)"; then
        on_path="$(dirname -- "$(dirname -- "$(readlink -f -- "$on_path")")")"
    fi
    for candidate in "${JAVA_HOME:-}" "$on_path" /usr/lib/jvm/*; do
        [[ -n "$candidate" && -x "$candidate/bin/javac" && -x "$candidate/bin/java" ]] || continue
        version="$("$candidate/bin/javac" -version 2>&1 | sed -nE 's/^javac ([0-9]+).*/\1/p')"
        [[ "$version" =~ ^[0-9]+$ ]] || continue
        if (( version >= 17 && version > best_version )); then
            best="$candidate"
            best_version="$version"
        fi
    done
    if [[ -z "$best" ]]; then
        printf 'No JDK 17+ found (javac). Install one, e.g. Arch: sudo pacman -S jdk21-openjdk, or set JAVA_HOME.\n' >&2
        return 1
    fi
    JAVAC="$best/bin/javac"
    JAVA="$best/bin/java"
}
aowis_find_jdk
