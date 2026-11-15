#!/usr/bin/env python3
"""Theme palette contrast/source smoke tests; graphical behavior still needs Qt."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
qml = (root / 'qml/Main.qml').read_text()
map_server_page = (root / 'qml/MapServerSettingsPage.qml').read_text()
settings = (root / 'src/appearance_settings.cpp').read_text()


def channel(hex_color):
    parts = [int(hex_color[index:index + 2], 16) / 255 for index in (1, 3, 5)]
    return [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in parts]


def contrast(foreground, background):
    lums = []
    for value in (foreground, background):
        r, g, b = channel(value)
        lums.append(0.2126 * r + 0.7152 * g + 0.0722 * b)
    return (max(lums) + 0.05) / (min(lums) + 0.05)


def palette(name):
    pattern = (r'readonly property color ' + name +
               r': darkTheme \? "(#[0-9a-fA-F]{6})" : "(#[0-9a-fA-F]{6})"')
    match = re.search(pattern, qml)
    assert match is not None, f'missing palette entry: {name}'
    return match.groups()


for mode in (0, 1):
    for foreground, background, min_ratio in (
        ('uiText', 'uiSurface', 7), ('uiText', 'uiInput', 7),
        ('uiText', 'uiRaised', 7), ('uiPlaceholder', 'uiInput', 4.5),
        ('uiAccent', 'uiSurface', 4.5), ('uiMuted', 'uiSurface', 4.5),
    ):
        ratio = contrast(palette(foreground)[mode], palette(background)[mode])
        assert ratio >= min_ratio, f'{foreground} / {background} ({mode}): {ratio:.2f}'

# The map server fields (own page) get the theme colors from Main.qml.
for field in ('mapServerUrl', 'mapServerPort'):
    assert re.search(r'id: ' + field + r'\b(?:(?!\n\s*}\n).){0,800}color: page.textColor',
                     map_server_page, re.S), field
assert map_server_page.count('placeholderTextColor: page.placeholderColor') == 2
assert 'placeholderColor: root.uiPlaceholder' in qml and 'textColor: root.uiText' in qml
assert qml.count('palette.buttonText: root.uiText') >= 8
assert qml.count('palette.highlightedText: root.uiSelectedText') >= 5
assert 'appearanceSettings.setMode' in qml
assert 'appearanceSettings.dark' in qml
assert 'colorSchemeChanged' in settings
assert 'appearance/theme' in settings
assert 'mapServerSettings.save' in map_server_page
assert 'root.navigateBack()' in qml
print('Appearance palette contrast/source tests passed for dark and light themes.')
