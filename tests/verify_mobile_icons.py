#!/usr/bin/env python3
"""Ensure mobile UI action icons do not depend on Android Unicode glyph fonts."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
qml = (root / 'qml/Main.qml').read_text(encoding='utf-8')
icons = (root / 'qml/UiIcon.qml').read_text(encoding='utf-8')
cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')

assert 'QML_FILES qml/Main.qml qml/UiIcon.qml' in cmake
assert 'QT_RESOURCE_ALIAS UiIcon.qml' in cmake
assert 'Canvas {' in icons and 'context.stroke()' in icons
for kind in ('menu', 'down', 'location'):
    assert f'icon.kind === "{kind}"' in icons, f'missing icon rendering: {kind}'
    assert re.search(r'UiIcon\s*{\s*kind:\s*"' + kind + r'"\s*\n\s*strokeColor: root.uiText', qml), kind
# Still drawn for the instrument pages and future controls.
for kind in ('plus', 'minus', 'reset'):
    assert f'icon.kind === "{kind}"' in icons, f'missing icon rendering: {kind}'

# Menu, dropdown arrows and math controls must not require missing font glyphs.
for glyph in ('☰', '▾', '−'):
    assert glyph not in qml, f'font-dependent symbol remains: {glyph}'
assert qml.count('kind: "down"') == 3
assert qml.count('kind: "menu"') == 1
assert 'id: hamburgerButton' in qml
assert 'anchors.right: parent.right' in qml
assert 'edge: Qt.RightEdge' in qml
assert 'QT_ANDROID_APP_ICON "@mipmap/ic_launcher"' in cmake
print('PASS: packaged font-independent UI icons for menu, selectors and location')
