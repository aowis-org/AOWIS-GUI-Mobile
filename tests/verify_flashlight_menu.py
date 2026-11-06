#!/usr/bin/env python3
"""Check portable tool menu, Android flashlight permissions and packaging."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
qml = (root / 'qml/Main.qml').read_text()
icons = (root / 'qml/UiIcon.qml').read_text()
menu = (root / 'qml/ToolMenuButton.qml').read_text()
cmake = (root / 'CMakeLists.txt').read_text()
main = (root / 'src/main.cpp').read_text()
service = (root / 'src/flashlight_service.cpp').read_text()
bridge = (root / 'android/src/org/aowis/mobile/TorchBridge.java').read_text()

drawer = qml[qml.index('    Drawer {'):]
assert drawer.index('text: qsTr("Map")') < drawer.index('text: qsTr("Settings")')
assert drawer.index('text: qsTr("Settings")') < drawer.index('text: qsTr("Tools")')
assert drawer.index('text: qsTr("Tools")') < drawer.index('text: qsTr("Diagnostics")')
assert drawer.count('Rectangle { Layout.fillWidth: true; height: 1; color: root.uiBorder }') >= 2
assert 'GridLayout {' in drawer and 'columns: 2' in drawer
# Longer translated labels must not make the first column narrower than the second.
tool_grid = drawer[drawer.index('            GridLayout {'):drawer.index('            Label {', drawer.index('            GridLayout {'))]
assert 'uniformCellWidths: true' in tool_grid
assert 'uniformCellHeights: true' in tool_grid
assert tool_grid.count('ToolMenuButton {') == 4
assert tool_grid.count('Layout.fillWidth: true') == 5  # grid + four buttons
assert tool_grid.count('Layout.preferredWidth: 0') == 4
for tool in ('compass', 'gradienter', 'plumbbob', 'flashlight'):
    assert f'iconKind: "{tool}"' in drawer
    assert f'icon.kind === "{tool}"' in icons
assert drawer.index('iconKind: "compass"') < drawer.index('iconKind: "gradienter"')
assert drawer.index('iconKind: "gradienter"') < drawer.index('iconKind: "plumbbob"')
assert drawer.index('iconKind: "plumbbob"') < drawer.index('iconKind: "flashlight"')
assert 'lit: flashlightService.enabled' in drawer
assert 'onClicked: flashlightService.toggle()' in drawer
assert 'UiIcon {' in menu and 'Text.WordWrap' in menu
assert 'QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"' in cmake
assert 'qt_add_android_permission(aowis-mobile NAME android.permission.CAMERA)' in cmake
assert 'src/flashlight_service.cpp' in cmake and 'qml/ToolMenuButton.qml' in cmake
assert 'android.hardware.camera2' in bridge and 'getCameraIdList()' in bridge
assert 'CameraCharacteristics.FLASH_INFO_AVAILABLE' in bridge
assert 'manager.setTorchMode(id, enabled)' in bridge
assert 'QCameraPermission' in service and 'requestPermission' in service
# Both Android JNI bridge calls must pass the jobject value, not a member-function pointer.
assert service.count("context.object<jobject>()") == 2
assert "context.object<jobject>," not in service
assert 'setApplicationActive(false)' not in service or 'setTorch(false)' in service
assert 'flashlightService.setApplicationActive(active)' in main
assert 'QQmlApplicationEngine::retranslate' in main
assert not re.search(r'\bauto\b', service)
print('PASS: 2x2 tools, permission-gated Android torch, lifecycle, and localization wiring')
