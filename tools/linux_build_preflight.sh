# Sourced by compile_linux.sh: checks the host build dependencies before
# CMake runs and names the packages to install for the detected distribution
# (Arch, Debian/Ubuntu and derivatives, Fedora). Every problem is collected
# first, so one install command fixes them all.
#
# Usage: aowis_linux_preflight <project dir>   (returns 1 after printing)
# Tests: AOWIS_OS_RELEASE overrides /etc/os-release.

# Qt 6.5: QPermissions, qt_add_qml_module defaults used by Mobile.
AOWIS_MIN_QT=6.5.0

aowis_distro_family() {
    local file="${AOWIS_OS_RELEASE:-/etc/os-release}" id='' like=''
    if [[ -r "$file" ]]; then
        id="$(sed -nE 's/^ID="?([^"]*)"?$/\1/p' "$file" | head -n1)"
        like="$(sed -nE 's/^ID_LIKE="?([^"]*)"?$/\1/p' "$file" | head -n1)"
    fi
    case " $id $like " in
        *" arch "*|*" archlinux "*|*" manjaro "*|*" endeavouros "*) echo arch ;;
        *" debian "*|*" ubuntu "*) echo debian ;;
        *" fedora "*|*" rhel "*) echo fedora ;;
        *) echo other ;;
    esac
}

aowis_install_hint() {
    case "$1" in
        arch)
            echo 'sudo pacman -S --needed cmake ninja gcc go qt6-base qt6-declarative qt6-positioning qt6-sensors libtiff libpng libjpeg-turbo zstd zlib pkgconf' ;;
        debian)
            # -dev packages for CMake, the private GUI headers Render uses, the
            # Qt build tools (moc, qmlcachegen) and the QML runtime modules
            # (Debian splits each QML import into its own package).
            echo 'sudo apt-get install cmake ninja-build g++ golang-go git python3 libgl-dev libegl-dev libx11-dev qt6-base-dev qt6-base-private-dev qt6-base-dev-tools qt6-declarative-dev qt6-declarative-dev-tools qt6-positioning-dev qt6-sensors-dev libqt6opengl6-dev qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-templates qml6-module-qtquick-window qml6-module-qtquick-dialogs qml6-module-qtqml-workerscript qml6-module-qtcore qml6-module-qt-labs-folderlistmodel libtiff-dev libpng-dev libturbojpeg0-dev libzstd-dev zlib1g-dev pkg-config' ;;
        fedora)
            echo 'sudo dnf install cmake ninja-build gcc-c++ golang qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel qt6-qtpositioning-devel qt6-qtsensors-devel mesa-libGL-devel mesa-libEGL-devel libX11-devel libtiff-devel libpng-devel libjpeg-turbo-devel libzstd-devel zlib-devel pkgconf-pkg-config' ;;
        *)
            echo 'Install CMake, Ninja, a C++20 compiler, Go and Qt 6.5+ (Base incl. private headers, Declarative, Positioning, Sensors) with their development files; also pkg-config, TIFF, PNG, TurboJPEG, Zstandard and zlib development files.' ;;
    esac
}

# True when version $1 >= $2 (dotted numbers).
aowis_version_at_least() {
    [[ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" == "$2" ]]
}

aowis_find_qtpaths() {
    local candidate
    for candidate in qtpaths6 qtpaths-qt6; do
        if command -v "$candidate" >/dev/null 2>&1; then
            command -v "$candidate"
            return 0
        fi
    done
    # Debian/Ubuntu keep the Qt 6 tools off PATH. AOWIS_QT_TOOL_DIRS
    # (space-separated) replaces these directories in tests.
    local dir
    for dir in ${AOWIS_QT_TOOL_DIRS-/usr/lib/qt6/bin /usr/lib/*-linux-gnu/qt6/bin /usr/lib64/qt6/bin}; do
        for candidate in "$dir/qtpaths6" "$dir/qtpaths"; do
            if [[ -x "$candidate" ]]; then
                echo "$candidate"
                return 0
            fi
        done
    done
    return 1
}

aowis_linux_preflight() {
    local project="$1" family problems=() tool go_required go_have qtpaths qt_version qml libs headers module
    family="$(aowis_distro_family)"

    for tool in cmake ninja pkg-config; do
        command -v "$tool" >/dev/null 2>&1 || problems+=("missing command: $tool")
    done
    command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1 ||
        problems+=("missing C++ compiler (g++ or clang++)")

    # MAP is linked by every build. Check native development packages before CMake.
    if command -v pkg-config >/dev/null 2>&1; then
        for module in libtiff-4 libpng libturbojpeg libzstd zlib; do
            pkg-config --exists "$module" || problems+=("missing MAP development files: $module")
        done
    fi

    # The embedded WireGuard engine is Go; its go.mod names the minimum.
    go_required="$(sed -nE 's/^go ([0-9.]+).*/\1/p' "$project/external/AOWIS-NETWORK/bridge/go.mod" 2>/dev/null | head -n1)"
    if ! command -v go >/dev/null 2>&1; then
        problems+=("missing Go (embedded WireGuard engine)")
    elif [[ -n "$go_required" ]]; then
        go_have="$(go env GOVERSION 2>/dev/null | sed -nE 's/^go([0-9.]+).*/\1/p')"
        if [[ -z "$go_have" ]] || ! aowis_version_at_least "$go_have" "$go_required"; then
            problems+=("Go ${go_have:-unknown} is too old: the WireGuard engine needs Go $go_required or newer")
        fi
    fi

    if ! qtpaths="$(aowis_find_qtpaths)"; then
        problems+=("Qt 6 development tools not found (qtpaths6)")
    else
        qt_version="$("$qtpaths" --qt-version 2>/dev/null || true)"
        if [[ -z "$qt_version" ]] || ! aowis_version_at_least "$qt_version" "$AOWIS_MIN_QT"; then
            problems+=("Qt ${qt_version:-unknown} is too old: AOWIS Mobile needs Qt $AOWIS_MIN_QT or newer (Debian 13, Ubuntu 25.04, Fedora 39 or newer)")
        fi
        libs="$("$qtpaths" --query QT_INSTALL_LIBS 2>/dev/null || true)"
        for module in Qt6Quick Qt6QuickControls2 Qt6Positioning Qt6Sensors Qt6OpenGL Qt6Network; do
            [[ -n "$libs" && -d "$libs/cmake/$module" ]] || problems+=("missing Qt development files: $module")
        done
        # The map's Qt surface uses QtGui private headers (separate package on
        # Debian/Fedora; part of qt6-base on Arch).
        headers="$("$qtpaths" --query QT_INSTALL_HEADERS 2>/dev/null || true)"
        [[ -n "$headers" && -n "$qt_version" && -d "$headers/QtGui/$qt_version/QtGui/private" ]] ||
            problems+=("missing Qt GUI private headers")
        qml="$("$qtpaths" --query QT_INSTALL_QML 2>/dev/null || true)"
        for module in QtQuick/Controls/Basic QtQuick/Layouts QtQuick/Dialogs QtQuick/Window QtCore; do
            [[ -n "$qml" && -d "$qml/$module" ]] || problems+=("missing QML module: ${module//\//.}")
        done
    fi

    (( ${#problems[@]} == 0 )) && return 0
    printf 'AOWIS Mobile Linux: the build dependencies are incomplete:\n' >&2
    printf '  - %s\n' "${problems[@]}" >&2
    printf 'Install them with:\n  %s\n' "$(aowis_install_hint "$family")" >&2
    return 1
}
