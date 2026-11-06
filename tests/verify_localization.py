#!/usr/bin/env python3
"""Check all current UI strings have complete runtime catalogs in EN/FR/AR/DE/ES.

This test is independent of Qt/Android so it also runs in CI and the Docker
build environment. Diagnostics CSV headers and stable event identifiers are
intentionally never translated.
"""
import ast
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
catalog = json.loads((ROOT / "i18n/translations.json").read_text(encoding="utf-8"))
assert set(catalog) == {"fr", "ar", "de", "es"}
strings = set()
for path in list((ROOT / "qml").glob("*.qml")) + list((ROOT / "src").glob("*.cpp")):
    source = path.read_text(encoding="utf-8")
    for match in re.finditer(r'\b(?:qsTr|tr)\(\s*((?:"(?:\\.|[^"\\])*"\s*)+)\)', source):
        quoted = re.findall(r'"(?:\\.|[^"\\])*"', match.group(1))
        strings.add("".join(ast.literal_eval(fragment) for fragment in quoted))
# Stored validation errors remain in English, but their getter returns the
# translated value through QCoreApplication::translate.
for line in (ROOT / "src/map_server_settings.cpp").read_text().splitlines():
    if '*error = QStringLiteral(' in line or 'setError(QStringLiteral(' in line:
        key = re.search(r'QStringLiteral\("([^"\\]*)"\)', line)
        assert key is not None, line
        strings.add(key.group(1))
for code, entries in catalog.items():
    missing = strings.difference(entries)
    assert not missing, f'{code}: missing {len(missing)} keys: {sorted(missing)!r}'
    for source in strings:
        translated = entries[source]
        assert isinstance(translated, str) and translated.strip(), (code, source)
        # Correct interpolation requires the same placeholder numbers, even
        # when grammar reorders them in right-to-left Arabic.
        src_placeholders = set(re.findall(r'%\d+', source))
        dst_placeholders = set(re.findall(r'%\d+', translated))
        assert src_placeholders == dst_placeholders, (code, source, translated)
        assert source.count('\n') == translated.count('\n'), (code, source)
assert any('\u0600' <= c <= '\u06ff' for c in catalog['ar']['Settings'])
qml = (ROOT / 'qml/Main.qml').read_text()
assert 'onActivated: function(index) {\n                            languageSettings.setLanguage(' in qml
assert 'LayoutMirroring.enabled: languageSettings.rightToLeft' in qml
assert 'root.activePage = ' not in qml  # prevent navigation destruction/recreation
assert 'qsTr("Settings")' in qml
assert 'languageSettings.rightToLeft' in qml
assert 'QQmlApplicationEngine::retranslate' in (ROOT / 'src/main.cpp').read_text()
for path in (ROOT / 'qml').glob('*.qml'):
    source = path.read_text()
    # User-facing QML text must not silently bypass the translation system.
    assert not re.search(r'\b(?:text|placeholderText|Accessible\.name):\s*"[A-Za-z]', source), path
print(f'PASS: {len(strings)} source strings covered in four catalogs; live QML / RTL contract verified')
