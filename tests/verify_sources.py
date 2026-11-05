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
    "tests/gpu_scene_geometry_tests.cpp", "tests/gpu_scene_stress_tests.cpp",
    "src/performance_samples.cpp", "src/performance_samples.h", "qml/Main.qml",
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
    ("QML window and header follow application name", 'title: Qt.application.name' in qml and
     'Label { text: Qt.application.name;' in qml),
    ("Android launch package", 'shell monkey -p org.aowis.mobile -c android.intent.category.LAUNCHER 1'
     in (root / "compile_android.sh").read_text()),
    ("Linux executable", '--target aowis-mobile' in (root / "compile_linux.sh").read_text()),
    ("Linux desktop entry", 'Name=AOWIS Mobile\n' in (root / "packaging/linux/org.aowis.mobile.desktop").read_text() and
     'Exec=aowis-mobile\n' in (root / "packaging/linux/org.aowis.mobile.desktop").read_text()),
    ("Android Docker build tooling exists", (root / "compile_android.sh").is_file() and (root / "prepare_android_docker.sh").is_file() and (root / "tools/qt-android/Dockerfile").is_file()),
    ("same bgfx pin", "v1.159.9485-574" in setup),
    ("style: no auto type inference", all(not re.search(r"\bauto\b", (root / name).read_text()) for name in [
        "src/main.cpp", "src/render_lab.cpp", "src/render_lab.h",
        "src/gpu_scene_geometry.cpp", "src/gpu_scene_geometry.h",
        "src/gpu_scene_renderer.cpp", "src/gpu_scene_renderer.h",
        "src/performance_samples.cpp", "src/performance_samples.h",
        "tests/gpu_scene_geometry_tests.cpp", "tests/gpu_scene_stress_tests.cpp"
    ])),
]
for name, ok in checks:
    print(f"{'PASS' if ok else 'FAIL'}: {name}")
if not all(ok for _, ok in checks):
    sys.exit(1)
print("Static checks passed; native Qt/bgfx compilation and GPU test still required.")
