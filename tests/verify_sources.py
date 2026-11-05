#!/usr/bin/env python3
"""Dependency-free structural checks for the experimental Qt Quick lab.

These tests cannot replace compiling against Qt/bgfx or testing on a GPU.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
required = [
    "CMakeLists.txt",
    "src/main.cpp",
    "src/render_lab.cpp",
    "src/render_lab.h",
    "qml/Main.qml",
    "tools/init_bgfx.sh",
    "docs/renderer-lab.md",
]
for path in required:
    assert (root / path).is_file(), path

cmake = (root / "CMakeLists.txt").read_text()
source = (root / "src/render_lab.cpp").read_text()
main = (root / "src/main.cpp").read_text()
qml = (root / "qml/Main.qml").read_text()
setup = (root / "tools/init_bgfx.sh").read_text()

checks = [
    ("explicit no-bgfx baseline option", "AOWIS_MOBILE_WITH_BGFX" in cmake),
    ("qml resource type alias", "QT_RESOURCE_ALIAS Main.qml" in cmake),
    ("no implicit git fetching from cmake", "FetchContent_" not in cmake),
    ("no bgfx render thread", "BGFX_CONFIG_MULTITHREADED OFF" in cmake),
    ("Qt Quick rendering callback", "beforeRenderPassRecording" in source),
    ("Qt GL state fence", "beginExternalCommands" in source and "endExternalCommands" in source),
    ("borrowed Qt EGL context", "nativeInterface<QNativeInterface::QEGLContext>" in source),
    ("bgfx submits without swapping Qt's surface", "bgfx::frame(BGFX_FRAME_FLUSH);" in source),
    ("current bgfx init API", "init.swapChain.width" in source and "init.swapChain.height" in source and "init.reset" in source),
    ("current bgfx reset API", "bgfx::reset(BGFX_RESET_NONE);" in source),
    ("old bgfx resolution API absent", "init.resolution" not in source and not re.search(r"bgfx::reset\s*\([^)]*,[^)]*,[^)]*\)", source)),
    ("scenegraph shutdown", "sceneGraphInvalidated" in source and "bgfx::shutdown()" in source),
    ("Android chooses test on launch", "androidExperimentalDefault = true" in main),
    ("QML baseline graphic", "Canvas {" in qml),
    ("QML interactive map", "MouseArea {" in qml),
    ("QML status and controls", "lab.status" in qml and "onClicked:" in qml),
    ("known git pin", "v1.159.9485-574" in setup),
    ("gitlink committed", "add .gitmodules external/bgfx.cmake" in setup),
    ("no auto in new C++", all(not re.search(r"\bauto\b", (root / n).read_text()) for n in ["src/main.cpp", "src/render_lab.cpp", "src/render_lab.h"])),
]
for name, result in checks:
    print(f"{'PASS' if result else 'FAIL'}: {name}")
if not all(result for _, result in checks):
    sys.exit(1)
print("Structural checks passed (not a Qt/bgfx compile or GPU test).")
