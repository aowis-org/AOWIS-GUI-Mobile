#!/usr/bin/env python3
"""Check installable icon artifacts without requiring Qt, Pillow, or Android SDK."""
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1]
def png_size(p: Path):
    data = p.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', p
    assert data[12:16] == b'IHDR', p
    return struct.unpack('>II', data[16:24])

master = root / 'assets/icons/aowis-source.png'
assert png_size(master) == (1171, 1153)
assert png_size(root / 'assets/icons/aowis.png') == (512, 512)
for density, pixels in [('mdpi', 48), ('hdpi', 72), ('xhdpi', 96),
                        ('xxhdpi', 144), ('xxxhdpi', 192)]:
    file = root / 'android/res' / ('mipmap-' + density) / 'ic_launcher.png'
    assert png_size(file) == (pixels, pixels), file
assert png_size(root / 'android/res/drawable-nodpi/ic_launcher_foreground.png') == (432, 432)
assert 'adaptive-icon' in (root / 'android/res/mipmap-anydpi-v26/ic_launcher.xml').read_text()
assert 'aowis_icon_background' in (root / 'android/res/values/colors.xml').read_text()
assert (root / 'assets/icons/aowis.ico').read_bytes().startswith(b'\x00\x00\x01\x00')
assert (root / 'assets/icons/aowis.icns').read_bytes().startswith(b'icns')
cmake = (root / 'CMakeLists.txt').read_text()
main = (root / 'src/main.cpp').read_text()
assert 'QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"' in cmake
assert 'QT_ANDROID_APP_ICON "@mipmap/ic_launcher"' in cmake
assert 'PREFIX "/aowis"' in cmake
assert 'QT_RESOURCE_ALIAS icon.png' in cmake
assert 'application.setWindowIcon(' in main and 'window->setIcon(' in main
assert 'MACOSX_BUNDLE_ICON_FILE aowis.icns' in cmake
assert 'packaging/windows/aowis.rc' in cmake
assert 'share/icons/hicolor/512x512/apps' in cmake
print('PASS: AOWIS icon assets, image sizes and build wiring')
