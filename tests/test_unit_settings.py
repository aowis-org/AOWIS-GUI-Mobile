#!/usr/bin/env python3
"""Source-level acceptance for mobile units without Qt or external submodules."""
from pathlib import Path
import json
import re
import sys

root = Path(__file__).resolve().parents[1]
main = (root / 'qml/Main.qml').read_text()
page = (root / 'qml/UnitSettingsPage.qml').read_text()
cpp = (root / 'src/unit_profile_settings.cpp').read_text()
cmake = (root / 'CMakeLists.txt').read_text()
model = (root / 'src/unit_registry/aowis/model/units/unit_profile.h').read_text()
translations = json.loads((root / 'i18n/translations.json').read_text())

checks = {
    'Language precedes Units': main.index('text: qsTr("Language")') < main.index('text: qsTr("Units")'),
    'Appearance is last settings section': main.index('text: qsTr("Appearance")') > main.index('text: qsTr("Map server")'),
    'Units selector and detail page': 'id: unitProfileSelector' in main and 'root.openPage("units")' in main and 'UnitSettingsPage {' in main,
    'Android Back from Units returns Settings': 'activePage === "units"' in main and 'openPage("settings")' in main,
    'Same model registry profiles as Desktop': len(re.findall(r'"(?:CMH|LPS|LPM|MLD|CMD|CMS|CFS|GPM|MGD|IMGD|AFD)"',model.split('flow_template_names')[1].split('};')[0])) == 11,
    'Live profile signals': 'Q_PROPERTY(int revision READ revision NOTIFY changed)' in (root/'src/unit_profile_settings.h').read_text() and 'unitProfileSettings.revision' in main,
    'Persistence and custom editing': all(text in cpp for text in ('units/custom_profiles','units/selected_profile_id','collection.add','collection.rename','collection.remove','collection.setUnit')),
    'No changes to canonical values': 'setProfileUnit' in model and 'resolvedUnit' in model and 'aowis-mobile-unit-profile-settings' in cmake,
    'Unit settings QML embedded': 'qml/UnitSettingsPage.qml' in cmake,
    'Search filters quantity rows': 'unitProfileSettings.fields(unitSearch.text, unitProfileSettings.revision, languageSettings.language)' in page,
    'Custom profile editor and confirmation': 'profileDialog' in page and 'deleteDialog' in page,
}
for name, success in checks.items():
    print(('PASS' if success else 'FAIL')+': '+name)
if not all(checks.values()):
    sys.exit(1)

# Each QML string in the edited pages must exist in all the language catalogs.
# QML escape sequences are replaced, not decoded as raw UTF-8 unicode escapes.
strings = set()
for source in (main,page):
    strings.update(value.replace('\\n','\n').replace('\\"','"') for value in
                   re.findall(r'qsTr\("((?:\\.|[^"\\])*)"\)', source))
for code in ('fr','ar','de','es'):
    missing = sorted(strings.difference(translations[code]))
    if missing:
        print('FAIL: missing '+code+' translations: '+repr(missing[:12]))
        sys.exit(1)
    print('PASS: '+code+' catalog covers '+str(len(strings))+' current QML strings')
print('Unit settings source acceptance passed.')
