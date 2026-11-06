#!/usr/bin/env python3
"""Static checks for the rendering experiment, not a GPU or Qt build test."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
required = [
    "CMakeLists.txt", "src/main.cpp", "src/render_lab.cpp", "src/render_lab.h",
    "src/gpu_scene_geometry.cpp", "src/gpu_scene_geometry.h",
    "src/gpu_scene_renderer.cpp", "src/gpu_scene_renderer.h",
    "src/map_server_settings.cpp", "src/map_server_settings.h",
    "src/appearance_settings.cpp", "src/appearance_settings.h",
    "src/language_settings.cpp", "src/language_settings.h", "i18n/translations.json",
    "tests/language_settings_tests.cpp",
    "tests/appearance_settings_tests.cpp",
    "src/unit_profile_settings.cpp", "src/unit_profile_settings.h",
    "src/unit_registry/aowis/model/units/unit_profile.h",
    "qml/UnitSettingsPage.qml", "tests/unit_profile_settings_tests.cpp",
    "tests/unit_registry_contract_tests.cpp", "tests/test_unit_settings.py",
    "tests/map_server_settings_tests.cpp",
    "tests/gpu_scene_geometry_tests.cpp", "tests/gpu_scene_stress_tests.cpp",
    "src/performance_samples.cpp", "src/performance_samples.h",
        "src/lifecycle_watchdog.cpp", "src/lifecycle_watchdog.h",
        "tests/lifecycle_watchdog_tests.cpp", "qml/Main.qml",
    "tools/init_bgfx.sh", "docs/renderer-lab.md",
]
for path in required:
    assert (root / path).is_file(), path

cmake = (root / "CMakeLists.txt").read_text()
source = (root / "src/render_lab.cpp").read_text()
scene = (root / "src/gpu_scene_renderer.cpp").read_text()
geometry = (root / "src/gpu_scene_geometry.cpp").read_text()
main = (root / "src/main.cpp").read_text()
qml = (root / "qml/Main.qml").read_text()
setup = (root / "tools/init_bgfx.sh").read_text()

checks = [
    ("qml-only baseline still available", "AOWIS_MOBILE_WITH_BGFX" in cmake and "Canvas {" in qml),
    ("no dependency fetching in CMake", "FetchContent_" not in cmake),
    ("single Qt-owned render thread", "BGFX_CONFIG_MULTITHREADED OFF" in cmake),
    ("Qt external GL state fence", "beginExternalCommands" in source and "endExternalCommands" in source),
    ("Qt Quick rendering callback", "beforeRenderPassRecording" in source),
    ("borrowed EGL context", "nativeInterface<QNativeInterface::QEGLContext>" in source),
    ("no bgfx swap", "bgfx::frame(BGFX_FRAME_FLUSH);" in source),
    ("current pinned bgfx init", "init.swapChain.width" in source and "init.reset" in source),
    ("current bgfx reset", "bgfx::reset(BGFX_RESET_NONE);" in source),
    ("pinned matching GL/GLES shaders", all(s in cmake for s in ["glsl/vs_cubes.bin", "essl/vs_cubes.bin", "glsl/fs_cubes.bin", "essl/fs_cubes.bin"])),
    ("shader binaries embedded in application", "qt_add_resources(aowis-mobile" in cmake),
    ("ordinary GPU shader program", "bgfx::createProgram" in scene and "bgfx::createShader" in scene),
    ("retained GPU vertex buffers", "bgfx::createVertexBuffer" in scene),
    ("GPU draw submissions", "bgfx::submit" in scene and "BGFX_STATE_PT_LINES" in scene),
    ("pan/zoom through GPU camera matrix", "setViewTransform" in scene and "makeCamera" in scene),
    ("no CPU regeneration in rendering callback", "createScene()" not in source and "createScene()" not in scene.split("GpuSceneMetrics GpuSceneRenderer::render")[1]),
    ("continuous 2-vertex line segments", "push(target, {coordinate, -6144.0f}" in geometry),
    ("mesh triangles for pipes and nodes", "scene.pipeTriangles" in geometry and "scene.nodeTriangles" in geometry),
    ("real CPU timings labeled correctly", "submitMicroseconds" in source and "frameIntervalMicroseconds" in source),
    ("GPU stats only if measured", "gpuTimerFreq > 0" in source and 'QStringLiteral("N/A")' in source),
    ("QML displays performance diagnostics", "lab.timings" in qml),
    ("no bgfx ASCII renderer remains", "dbgTextPrintf" not in source and "dbgTextClear" not in source),
    ("unit geometry test", "aowis-mobile-gpu-scene" in cmake),
    ("stress preset / determinism tests", "aowis-mobile-gpu-stress" in cmake and
     "createStressTile" in geometry and "visibleStressTiles" in geometry),
    ("bounded viewport tiles and LOD", "kMaximumCachedTiles" in scene and
     "kMaximumVisibleTiles" in geometry and "stride *= 2" in geometry),
    ("performance CSV and percentiles", "QSaveFile" in source and
     "percentile(intervals, 0.99)" in source and "frame_interval" not in source and
     "interval_us,submit_us,gpu_us" in source),
    ("QML presets and timed capture", "ComboBox {" in qml and
     "lab.setWorkload(index)" in qml and "lab.startBenchmark()" in qml),
    ("render-thread cleanup", "sceneGraphInvalidated" in source and "gpuScene->release()" in source),
    ("Android default test mode", "androidExperimentalDefault = true" in main),
    ("QML controls", "DragHandler {" in qml and "PinchHandler {" in qml and "TapHandler {" in qml and "WheelHandler {" in qml),
    ("window attached before first show", "visible: false" in qml and main.index("lab.attach(window)") < main.index("window->show()")),
    ("scene graph rebuild hints on Android", "setPersistentSceneGraph(false)" in main and "setPersistentGraphics(false)" in main),
    ("background pauses frame timer", "refresh.stop()" in main and "applicationStateChanged" in main),
    ("render lifecycle diagnostics", "sceneGraphStarts" in source and "sceneGraphStops" in source and "bgfxInitializations" in source),
    ("watchdog records first recovered frame", "lastCompletedFrameUs.store" in source and
     "recoveryWatchdog.poll" in source and "recoveryTimeouts" in source),
    ("explicit GPU resource reinitialization", "requestGpuRecreation()" in source and
     "recreateGpuRequested.exchange(false)" in source and "requestGpuRecreation()" in qml),
    ("real lifecycle ADB test", (root / "tools/test_android_lifecycle.sh").is_file() and
     "KEYCODE_HOME" in (root / "tools/test_android_lifecycle.sh").read_text() and
     "exec-out run-as" in (root / "tools/test_android_lifecycle.sh").read_text()),
    ("recovery event log and on-screen status", "events.csv" in source and
     "lab.recoveryStatus" in qml),
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
    ("MODEL-backed display unit settings", "unitProfileSettings" in main and
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
    ("same bgfx pin", "v1.159.9485-574" in setup),
    ("style: no auto type inference", all(not re.search(r"\bauto\b", (root / name).read_text()) for name in [
        "src/main.cpp", "src/map_server_settings.cpp", "src/map_server_settings.h",
        "tests/map_server_settings_tests.cpp", "src/render_lab.cpp", "src/render_lab.h",
        "src/gpu_scene_geometry.cpp", "src/gpu_scene_geometry.h",
        "src/gpu_scene_renderer.cpp", "src/gpu_scene_renderer.h",
    "src/map_server_settings.cpp", "src/map_server_settings.h",
    "src/appearance_settings.cpp", "src/appearance_settings.h",
    "src/language_settings.cpp", "src/language_settings.h", "i18n/translations.json",
    "tests/language_settings_tests.cpp",
    "tests/appearance_settings_tests.cpp",
    "src/unit_profile_settings.cpp", "src/unit_profile_settings.h",
    "src/unit_registry/aowis/model/units/unit_profile.h",
    "qml/UnitSettingsPage.qml", "tests/unit_profile_settings_tests.cpp",
    "tests/unit_registry_contract_tests.cpp", "tests/test_unit_settings.py",
    "tests/map_server_settings_tests.cpp",
        "src/performance_samples.cpp", "src/performance_samples.h",
        "src/lifecycle_watchdog.cpp", "src/lifecycle_watchdog.h",
        "tests/lifecycle_watchdog_tests.cpp",
        "src/compass_service.cpp", "src/compass_service.h",
        "src/compass_heading.h", "src/compass_heading_filter.h",
        "src/compass_sensor_source.cpp", "src/compass_sensor_source.h",
        "src/compass_fusion.h",
        "tests/compass_heading_tests.cpp",
        "tests/gpu_scene_geometry_tests.cpp", "tests/gpu_scene_stress_tests.cpp"
    ])),
]
for name, ok in checks:
    print(f"{'PASS' if ok else 'FAIL'}: {name}")
if not all(ok for _, ok in checks):
    sys.exit(1)
print("Static checks passed; native Qt/bgfx compilation and GPU test still required.")
