# Standalone Android cross-build environment. NO Qt Online Installer, Qt account,
# Android Studio, or host Qt installation is required. This is a BUILD image,
# not an Android emulator.
FROM ubuntu:24.04

ARG QT_VERSION=6.12.0
ARG AQT_GIT_REF=16db45a70b5905ad596941b223469bc86a56901e
ARG ANDROID_API=36
ARG ANDROID_BUILD_TOOLS=36.0.0
ARG ANDROID_NDK_VERSION=27.2.12479018
ARG CMDLINE_TOOLS_ID=15859902
ARG CMDLINE_TOOLS_SHA256=4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583

ENV DEBIAN_FRONTEND=noninteractive \
    JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64 \
    ANDROID_SDK_ROOT=/opt/android-sdk \
    ANDROID_HOME=/opt/android-sdk \
    GRADLE_OPTS=-Dorg.gradle.daemon=false

LABEL org.opencontainers.image.title="AOWIS Mobile Android build toolchain" \
      org.opencontainers.image.description="Qt Quick Android ARM64 / JDK 21 / NDK r27c / SDK 36"

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake curl git ninja-build \
        openjdk-21-jdk-headless pkg-config python3 python3-venv \
        unzip zip p7zip-full xz-utils \
        libdbus-1-3 libegl1 libfontconfig1 libgl1 libopengl0 \
        libx11-6 libx11-xcb1 libxcb1 libxkbcommon0 libxrender1 libxext6 \
    && rm -rf /var/lib/apt/lists/*

# aqtinstall downloads the official *precompiled* open-source Qt packages
# directly. It is a command-line downloader, NOT the Qt GUI/online installer.
# Qt for Android needs a matching Linux HOST Qt for its build-time tools.
# Qt 6.11+ reorganized its online archive metadata; aqtinstall 3.3.0 does
# not understand the new layout. Pin the upstream commit with that support.
# The *archive architecture* is linux_gcc_64 (Qt 6.7+), even though the
# installed host directory is still named gcc_64.
RUN python3 -m venv /opt/aqt-venv \
    && /opt/aqt-venv/bin/pip install --no-cache-dir "aqtinstall @ git+https://github.com/miurahr/aqtinstall.git@${AQT_GIT_REF}" \
    && /opt/aqt-venv/bin/aqt install-qt linux desktop "${QT_VERSION}" linux_gcc_64 -O /opt/qt \
    && /opt/aqt-venv/bin/aqt install-qt all_os android "${QT_VERSION}" android_arm64_v8a -O /opt/qt \
    && test -x "/opt/qt/${QT_VERSION}/android_arm64_v8a/bin/qt-cmake" \
    && test -f "/opt/qt/${QT_VERSION}/gcc_64/lib/cmake/Qt6/Qt6Config.cmake" \
    && test -f "/opt/qt/${QT_VERSION}/android_arm64_v8a/lib/cmake/Qt6QuickControls2/Qt6QuickControls2Config.cmake"

# Google's command-line-only SDK, verified against its published SHA256.
# The SDK/NDK version is deliberately pinned to match Qt's Android binaries.
RUN curl -fL --retry 5 --retry-delay 3 \
        "https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS_ID}_latest.zip" \
        -o /tmp/android-commandline-tools.zip \
    && echo "${CMDLINE_TOOLS_SHA256}  /tmp/android-commandline-tools.zip" | sha256sum -c - \
    && mkdir -p /opt/android-sdk/cmdline-tools /tmp/android-commandline-tools \
    && unzip -q /tmp/android-commandline-tools.zip -d /tmp/android-commandline-tools \
    && mv /tmp/android-commandline-tools/cmdline-tools /opt/android-sdk/cmdline-tools/latest \
    && rm -rf /tmp/android-commandline-tools /tmp/android-commandline-tools.zip

# Explicitly accepted only for this image. Building the image means accepting
# the corresponding Google Android SDK license terms. Does not install an IDE.
RUN yes | /opt/android-sdk/cmdline-tools/latest/bin/sdkmanager \
        --sdk_root=/opt/android-sdk --licenses >/dev/null \
    && /opt/android-sdk/cmdline-tools/latest/bin/sdkmanager \
        --sdk_root=/opt/android-sdk \
        "platform-tools" "platforms;android-${ANDROID_API}" \
        "build-tools;${ANDROID_BUILD_TOOLS}" "ndk;${ANDROID_NDK_VERSION}" \
    && test -x "/opt/android-sdk/ndk/${ANDROID_NDK_VERSION}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"

# Qt 6.12's Linux host tools use GLib (including qmlimportscanner). The
# Ubuntu minimal image must provide this shared library explicitly. Keep this
# layer AFTER Qt/SDK provisioning so Docker can reuse the expensive r2 layers.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libglib2.0-0t64 libsm6 libice6 libxkbcommon-x11-0 \
        libxcb-cursor0 libxcb-render0 libxcb-shape0 libxcb-randr0 \
        libxcb-xfixes0 libxcb-render-util0 \
    && rm -rf /var/lib/apt/lists/*

# TaskTree became a separately packaged Qt module in Qt 6.11. Android
# Qt6QmlAssetDownloaderPrivate requires it even if this lab does not use
# TaskTree directly. Install the matching official host/target archives.
RUN /opt/aqt-venv/bin/aqt install-qt linux desktop "${QT_VERSION}" linux_gcc_64 \
        --modules qttasktree -O /opt/qt \
    && /opt/aqt-venv/bin/aqt install-qt all_os android "${QT_VERSION}" android_arm64_v8a \
        --modules qttasktree -O /opt/qt \
    && test -f "/opt/qt/${QT_VERSION}/gcc_64/lib/cmake/Qt6TaskTree/Qt6TaskTreeConfig.cmake" \
    && test -f "/opt/qt/${QT_VERSION}/android_arm64_v8a/lib/cmake/Qt6TaskTree/Qt6TaskTreeConfig.cmake"

# Fail at image construction rather than at the first app configure when a
# Linux host tool has missing ELF runtime dependencies.
RUN set -eu; \
    for executable in \
        "/opt/qt/${QT_VERSION}/gcc_64/bin/qmake" \
        "/opt/qt/${QT_VERSION}/gcc_64/libexec/qmlimportscanner" \
        "/opt/qt/${QT_VERSION}/gcc_64/libexec/qmlcachegen"; do \
        test -x "$executable"; \
        missing="$(ldd "$executable" | grep 'not found' || true)"; \
        if [ -n "$missing" ]; then \
            echo "Missing host libraries for $executable:" >&2; \
            echo "$missing" >&2; \
            exit 1; \
        fi; \
    done; \
    "/opt/qt/${QT_VERSION}/gcc_64/bin/qmake" -query QT_VERSION

ENV PATH="/opt/android-sdk/platform-tools:/opt/android-sdk/cmdline-tools/latest/bin:${JAVA_HOME}/bin:${PATH}"
WORKDIR /project
