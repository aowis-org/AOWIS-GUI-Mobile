#!/usr/bin/env bash
set -euo pipefail

# The Linux dependency check names the right packages per distribution and
# detects old Qt/Go, with mocked tools only (no Qt, Go or root needed).
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/core" "$work/full" "$work/project/external/AOWIS-NETWORK/bridge"
printf 'module x\n\ngo 1.23.1\n' > "$work/project/external/AOWIS-NETWORK/bridge/go.mod"
# Only basic utilities: no cmake, ninja, compiler, Go or Qt.
for tool in bash sed sort head printf cat dirname mkdir; do
    path="$(type -P "$tool" || true)"
    [[ -n "$path" ]] && ln -s "$path" "$work/core/$tool"
done

fake_qt() {
    local version="$1" prefix="$work/qt-$1"
    mkdir -p "$prefix/lib/cmake" "$prefix/qml/QtQuick/Controls/Basic" "$prefix/qml/QtQuick/Layouts" \
        "$prefix/qml/QtQuick/Dialogs" "$prefix/qml/QtQuick/Window" "$prefix/qml/QtCore" \
        "$prefix/include/QtGui/$version/QtGui/private"
    for module in Qt6Quick Qt6QuickControls2 Qt6Positioning Qt6Sensors Qt6OpenGL Qt6Network; do
        mkdir -p "$prefix/lib/cmake/$module"
    done
    cat > "$work/full/qtpaths6" <<QT
#!/usr/bin/env bash
case "\$1 \${2:-}" in
    '--qt-version ') echo $version ;;
    '--query QT_INSTALL_LIBS') echo $prefix/lib ;;
    '--query QT_INSTALL_QML') echo $prefix/qml ;;
    '--query QT_INSTALL_HEADERS') echo $prefix/include ;;
esac
QT
    chmod +x "$work/full/qtpaths6"
}
fake_go() {
    printf '#!/usr/bin/env bash\n[[ "$*" == "env GOVERSION" ]] && echo go%s\n' "$1" > "$work/full/go"
    chmod +x "$work/full/go"
}
for tool in cmake ninja c++; do
    printf '#!/usr/bin/env bash\n' > "$work/full/$tool"
    chmod +x "$work/full/$tool"
done
for os in debian ubuntu arch fedora; do
    case "$os" in
        ubuntu) printf 'ID=ubuntu\nID_LIKE=debian\n' > "$work/$os-release" ;;
        *) printf 'ID=%s\n' "$os" > "$work/$os-release" ;;
    esac
done

check() {
    # check <os> <PATH> -> output; status in $status
    status=0
    # AOWIS_QT_TOOL_DIRS: never the host's real /usr/lib/qt6/bin.
    output="$(AOWIS_OS_RELEASE="$work/$1-release" AOWIS_QT_TOOL_DIRS="$work/no-qt" PATH="$2" "$work/core/bash" -c \
        'source "$0" && aowis_linux_preflight "$1"' "$root/tools/linux_build_preflight.sh" "$work/project" 2>&1)" || status=$?
}
fail() { printf 'FAILED: %s\n%s\n' "$1" "$output" >&2; exit 1; }

# Nothing installed: every gap is listed with the distribution's command.
check debian "$work/core"
(( status == 1 )) || fail 'empty Debian host accepted'
for expected in 'missing command: cmake' 'missing command: ninja' 'missing C++ compiler' 'missing Go' \
                'Qt 6 development tools not found' 'sudo apt-get install' 'ninja-build' 'golang-go' \
                'qt6-base-private-dev' 'qt6-declarative-dev-tools' 'qml6-module-qtquick-dialogs'; do
    [[ "$output" == *"$expected"* ]] || fail "Debian message lacks: $expected"
done
check ubuntu "$work/core"
[[ "$output" == *'sudo apt-get install'* ]] || fail 'Ubuntu (ID_LIKE=debian) not treated as Debian'
check arch "$work/core"
[[ "$output" == *'sudo pacman -S --needed'* && "$output" != *'apt-get'* ]] || fail 'Arch command'
check fedora "$work/core"
[[ "$output" == *'sudo dnf install'* ]] || fail 'Fedora command'
printf 'PASS: missing tools listed with the Debian/Ubuntu/Arch/Fedora install command\n'

# Debian 12 / Ubuntu 24.04 era: Qt 6.4 and Go 1.19 are too old.
fake_qt 6.4.2
fake_go 1.19.8
check debian "$work/full:$work/core"
(( status == 1 )) || fail 'old Qt/Go accepted'
[[ "$output" == *'Qt 6.4.2 is too old'* && "$output" == *'Go 1.19.8 is too old'* && "$output" == *'Debian 13'* ]] ||
    fail 'old versions not explained'
printf 'PASS: Qt < 6.5 and Go below go.mod are rejected with the reason\n'

# Debian keeps qtpaths6 off PATH, in /usr/lib/qt6/bin: found there too.
mkdir -p "$work/debian-qt/bin"
fake_qt 6.8.2
mv "$work/full/qtpaths6" "$work/debian-qt/bin/qtpaths6"
fake_go 1.24.4
status=0
output="$(AOWIS_OS_RELEASE="$work/debian-release" AOWIS_QT_TOOL_DIRS="$work/debian-qt/bin" PATH="$work/full:$work/core" \
    "$work/core/bash" -c 'source "$0" && aowis_linux_preflight "$1"' "$root/tools/linux_build_preflight.sh" "$work/project" 2>&1)" || status=$?
(( status == 0 )) || fail 'qtpaths6 outside PATH (Debian layout) not found'
printf 'PASS: Qt tools found outside PATH (Debian /usr/lib/qt6/bin)\n'

# Debian 13 (Qt 6.8, Go 1.24) with everything: accepted.
fake_qt 6.8.2
fake_go 1.24.4
check debian "$work/full:$work/core"
(( status == 0 )) || fail 'complete Debian 13 host rejected'
# A missing QML runtime module or private headers is reported.
rm -r "$work/qt-6.8.2/qml/QtQuick/Dialogs" "$work/qt-6.8.2/include/QtGui/6.8.2/QtGui/private"
check debian "$work/full:$work/core"
[[ "$status" == 1 && "$output" == *'missing QML module: QtQuick.Dialogs'* && "$output" == *'missing Qt GUI private headers'* ]] ||
    fail 'missing QML module / private headers not reported'
printf 'PASS: complete Debian 13 host accepted; missing QML modules and private headers reported\n'
echo 'Linux build preflight tests passed.'
