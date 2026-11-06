#!/usr/bin/env bash
# Android toolchain versions used by both image provisioning and APK builds.
# Keep these values aligned with Qt's supported-platforms documentation.
AOWIS_QT_ANDROID_VERSION=6.12.0
AOWIS_ANDROID_NDK_VERSION=27.2.12479018
AOWIS_ANDROID_SDK_API=36
AOWIS_ANDROID_BUILD_TOOLS_VERSION=36.0.0
# Pinned upstream aqtinstall commit with Qt 6.11+ repository-layout support.
AOWIS_ANDROID_AQT_GIT_REF=16db45a70b5905ad596941b223469bc86a56901e
AOWIS_ANDROID_CMDLINE_TOOLS_ID=15859902
# Official checksum listed by https://developer.android.com/studio .
AOWIS_ANDROID_CMDLINE_TOOLS_SHA256=4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583
AOWIS_ANDROID_TOOLCHAIN_REVISION=4
AOWIS_ANDROID_IMAGE="${AOWIS_ANDROID_IMAGE:-aowis-mobile-android:qt${AOWIS_QT_ANDROID_VERSION}-r${AOWIS_ANDROID_TOOLCHAIN_REVISION}}"
