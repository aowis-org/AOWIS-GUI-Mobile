#!/usr/bin/env python3
"""Static checks for the Mobile app and its Qt Quick map, not a GPU or Qt build test."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
required = [
    "CMakeLists.txt", "src/main.cpp", "src/mobile_map_view.cpp", "src/mobile_map_view.h",
    "src/mobile_map_math.h", "tests/mobile_map_math_tests.cpp",
    "src/map_server_settings.cpp", "src/map_server_settings.h",
    "src/appearance_settings.cpp", "src/appearance_settings.h",
    "src/language_settings.cpp", "src/language_settings.h", "i18n/translations.json",
    "tests/language_settings_tests.cpp",
    "tests/appearance_settings_tests.cpp",
    "src/unit_profile_settings.cpp", "src/unit_profile_settings.h",
    "tools/init_model.sh", "tools/init_geographiclib_dvmdb.sh",
    "docs/dependencies.md", "tests/test_native_dependencies.sh",
    "qml/UnitSettingsPage.qml", "tests/unit_profile_settings_tests.cpp",
    "tests/unit_registry_contract_tests.cpp", "tests/test_unit_settings.py",
    "tests/map_server_settings_tests.cpp",
    "src/lifecycle_watchdog.cpp", "src/lifecycle_watchdog.h",
    "tests/lifecycle_watchdog_tests.cpp", "qml/Main.qml", "docs/map.md",
]
for path in required:
    assert (root / path).is_file(), path
removed = [
    "src/render_lab.cpp", "src/gpu_scene_renderer.cpp", "src/gpu_scene_geometry.cpp",
    "src/panorama_view.cpp", "src/panorama_settings.cpp", "src/panorama_math.h",
    "assets/pachon-hotel-night.jpg", "tools/init_bgfx.sh", "benchmark.sh",
]
for path in removed:
    assert not (root / path).exists(), "removed experiment present again: " + path

cmake = (root / "CMakeLists.txt").read_text()
source = (root / "src/mobile_map_view.cpp").read_text()
header = (root / "src/mobile_map_view.h").read_text()
main = (root / "src/main.cpp").read_text()
qml = (root / "qml/Main.qml").read_text()

checks = [
    ("qml-only baseline still available", "AOWIS_MOBILE_WITH_BGFX" in cmake and
     "#else // !AOWIS_MOBILE_WITH_MAP" in source),
    ("no dependency fetching in CMake", "FetchContent_" not in cmake),
    ("no bgfx.cmake: Render's pinned bgfx", "bgfx.cmake" not in cmake and
     "external/bgfx/include/bgfx/bgfx.h" in cmake),
    ("full Render map stack", all(s in cmake for s in [
        "AOWIS_RENDER_BUILD_MAP ON", "AOWIS_RENDER_BUILD_MAP_REST ON",
        "AOWIS_RENDER_BUILD_GLOBE_TERRAIN ON", "AOWIS::RenderMap", "AOWIS::RenderMapRest"])),
    ("Mobile's GeographicLib for Render", "AOWIS_RENDER_GEOGRAPHICLIB_SOURCE_DIR" in cmake),
    ("Qt Quick scene graph renderers only", '"OPENGLES"' in cmake and '"OPENGL"' in cmake),
    ("basic render loop: map frame on the GUI thread", 'qputenv("QSG_RENDER_LOOP", "basic")' in main),
    ("EGL on X11 desktop", 'qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl")' in main),
    ("host-context map view", "MapBgfxMapView::RenderTarget::HostContext" in source and
     "renderHostFrame(host_context)" in source and "releaseHostContext()" in source),
    ("Qt external GL state fence", "beginExternalCommands" in source and "endExternalCommands" in source),
    ("Qt Quick rendering callback", "beforeRenderPassRecording" in source and
     "Qt::DirectConnection" in source),
    ("borrowed EGL context", "nativeInterface<QNativeInterface::QEGLContext>" in source),
    ("scene-graph release", "sceneGraphInvalidated" in source),
    ("frames requested on demand", "hostFrameRequested" in source and "->update()" in source),
    ("map server from the settings", "serverUrl: mapServerSettings.endpointUrl" in qml and
     "InterfaceServerMapREST(server, parent)" in source and "catalog.refresh()" in source),
    ("map gestures and Qt Quick HUD", "void touchEvent(QTouchEvent *event) override;" in header and
     "mapViewHudEnabled() const override { return false; }" in source and
     "handleHudMouseEvent" not in source and "handleHudWheelEvent" not in source and
     "mapView.setGlobe(!mapView.globe)" in qml and "mapView.zoomBy(1)" in qml and "panByPixelsGlobe" in source and
     "zoomByAt" in source and "orbitViewGlobe" in source),
    ("map QML type", 'qmlRegisterType<MobileMapView>("AOWIS.Map", 1, 0, "MapView")' in main and
     "import AOWIS.Map 1.0" in qml and "MapView {" in qml),
    ("no panorama or synthetic map in QML", "PanoramaView" not in qml and "lab." not in qml and
     "showDemoMap" not in qml and "Canvas {" not in qml),
    ("lifecycle recovery log for the Android test", "events.csv" in source and
     "watchdog.poll" in source and '"recovered"' in source),
    ("real lifecycle ADB test", (root / "tools/test_android_lifecycle.sh").is_file() and
     "KEYCODE_HOME" in (root / "tools/test_android_lifecycle.sh").read_text() and
     "exec-out run-as" in (root / "tools/test_android_lifecycle.sh").read_text()),
    ("window attached before first show", "visible: false" in qml and "window->show()" in main),
    ("scene graph rebuild hints on Android", "setPersistentSceneGraph(false)" in main and
     "setPersistentGraphics(false)" in main),
    ("map diagnostics page", "mapView.status" in qml and "mapView.lifecycle" in qml and
     "mapView.reloadServer()" in qml),
    ("map math test", "aowis-mobile-map-math" in cmake),
    ("Android manifest via Qt", 'QT_ANDROID_PACKAGE_NAME "org.aowis.mobile"' in cmake),
    ("app binary identity", "qt_add_executable(aowis-mobile" in cmake and
     "qt_add_qml_module(aowis-mobile" in cmake),
    ("production QML module", "URI AOWIS.Mobile" in cmake and
     'QStringLiteral("AOWIS.Mobile")' in main),
    ("Android launcher label", 'QT_ANDROID_APP_NAME "AOWIS"' in cmake),
    ("platform-specific application names", '#if defined(Q_OS_ANDROID)' in main and
     'QStringLiteral("AOWIS")' in main and 'QStringLiteral("AOWIS Mobile")' in main and
     'setApplicationName(applicationLabel)' in main and
     'setApplicationDisplayName(applicationLabel)' in main),
    ("minimal hamburger navigation", 'id: hamburgerButton' in qml and
     'id: navigationDrawer' in qml and 'root.openPage("settings")' in qml and
     'root.openPage("diagnostics")' in qml),
    ("right-side mobile navigation", 'id: hamburgerButton' in qml and
     'anchors.right: parent.right' in qml and 'edge: Qt.RightEdge' in qml and
     'closePolicy: Popup.CloseOnReleaseOutside' in qml),
    ("Android Back returns from secondary pages", 'sequences: ["Back"]' in qml and
     'Qt.platform.os === "android"' in qml and
     'navigationDrawer.opened || root.activePage !== "map"' in qml and
     'onActivated: root.navigateBack()' in qml and
     'function navigateBack()' in qml and
     'navigationDrawer.close()' in qml and
     'openPage("map")' in qml),
    ("desktop Escape returns to map", 'sequence: "Escape"' in qml and
     'Qt.platform.os !== "android"' in qml),
    ("persistent map server settings", 'mapServerSettings' in main and
     'QSettings' in (root / "src/map_server_settings.cpp").read_text() and
     'mapServerSettings.save' in qml and 'IntValidator { bottom: 1; top: 65535 }' in qml and
     'Qt.ImhDigitsOnly' in qml),
    ("system dark light theme and explicit field contrast", 'appearanceSettings.mode' in qml and
     'appearanceSettings.dark' in qml and 'appearanceSettings.setMode' in qml and
     'placeholderTextColor: root.uiPlaceholder' in qml and
     'color: root.uiText' in qml and 'palette.buttonText: root.uiText' in qml and
     'QtQuick.Controls.Basic' in qml),
    ("persistent appearance settings and system theme", 'QSettings' in
     (root / "src/appearance_settings.cpp").read_text() and
     'colorSchemeChanged' in (root / "src/appearance_settings.cpp").read_text() and
     'aowis-mobile-appearance-settings-tests' in cmake),
    ("MODEL-backed display unit settings", "AOWIS::Model" in cmake and "unitProfileSettings" in main and
     "aowis-mobile-unit-profile-settings-tests" in cmake and
     "unitProfileSettings.listProfiles" in qml and "UnitSettingsPage {" in qml),
    ("settings regression test", 'aowis-mobile-map-server-settings-tests' in cmake and
     'aowis-mobile-map-server-settings' in cmake),
    ("explicit Android launcher activity and verified process", 'tools/launch_android_app.sh' in
     (root / "compile_android.sh").read_text() and
     'tools/launch_android_app.sh' in (root / "tools/test_android_lifecycle.sh").read_text() and
     'cmd package resolve-activity --brief' in (root / "tools/launch_android_app.sh").read_text() and
     'shell am start -W' in (root / "tools/launch_android_app.sh").read_text() and
     'shell pidof' in (root / "tools/launch_android_app.sh").read_text() and
     'settings put' not in (root / "compile_android.sh").read_text()),
    ("Linux executable", '--target aowis-mobile' in (root / "compile_linux.sh").read_text()),
    ("Linux desktop entry", 'Name=AOWIS Mobile\n' in (root / "packaging/linux/org.aowis.mobile.desktop").read_text() and
     'Exec=aowis-mobile\n' in (root / "packaging/linux/org.aowis.mobile.desktop").read_text()),
    ("Android Docker build tooling exists", (root / "compile_android.sh").is_file() and (root / "prepare_android_docker.sh").is_file() and (root / "tools/qt-android/Dockerfile").is_file()),
    ("style: no auto type inference", all(not re.search(r"\bauto\b", (root / name).read_text()) for name in [
        "src/main.cpp", "src/mobile_map_view.cpp", "src/mobile_map_view.h", "src/mobile_map_math.h",
        "tests/mobile_map_math_tests.cpp",
        "src/map_server_settings.cpp", "src/map_server_settings.h",
        "tests/map_server_settings_tests.cpp",
        "src/appearance_settings.cpp", "src/appearance_settings.h",
        "src/language_settings.cpp", "src/language_settings.h",
        "tests/language_settings_tests.cpp", "tests/appearance_settings_tests.cpp",
        "src/unit_profile_settings.cpp", "src/unit_profile_settings.h",
        "tests/unit_profile_settings_tests.cpp", "tests/unit_registry_contract_tests.cpp",
        "src/lifecycle_watchdog.cpp", "src/lifecycle_watchdog.h",
        "tests/lifecycle_watchdog_tests.cpp",
        "src/compass_service.cpp", "src/compass_service.h",
        "src/compass_heading.h", "src/compass_heading_filter.h",
        "src/compass_sensor_source.cpp", "src/compass_sensor_source.h",
        "src/compass_fusion.h", "tests/compass_heading_tests.cpp",
    ])),
]
for name, ok in checks:
    print(f"{'PASS' if ok else 'FAIL'}: {name}")
if not all(ok for _, ok in checks):
    sys.exit(1)
print("Static checks passed; native Qt/bgfx compilation and a GPU test are still required.")
