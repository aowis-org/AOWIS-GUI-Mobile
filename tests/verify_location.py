#!/usr/bin/env python3
"""Fast GPS integration checks without installed Qt or Android hardware."""
from pathlib import Path
root = Path(__file__).resolve().parent.parent
main = (root / 'src/main.cpp').read_text()
cmake = (root / 'CMakeLists.txt').read_text()
qml = (root / 'qml/Main.qml').read_text()
source = (root / 'src/location_service.cpp').read_text()
docker = (root / 'tools/qt-android/Dockerfile').read_text()
checks = {
    'location linked to application and standalone geometry tests':
        'Qt6::Positioning' in cmake and 'aowis-mobile-location-snapshot-tests' in cmake and
        'locationService' in main,
    'foreground Android permission declarations':
        all(name in cmake for name in ['ACCESS_FINE_LOCATION', 'ACCESS_COARSE_LOCATION']) and
        'ACCESS_BACKGROUND_LOCATION' not in cmake,
    'automatic foreground permission':
        'setApplicationActive(active)' in main and 'startIfAllowed();' in source and
        'QLocationPermission::WhenInUse' in source and
        'requestPermission(permission, this,' in source and
        'if (!this->m_enabled || !this->m_applicationActive)' in source,
    'background pauses provider and foreground resumes':
        'this->m_source->stopUpdates();' in source and 'State::Paused' in source and
        'setApplicationActive' in main,
    'stale fix detection and valid optional measurements':
        'kStaleAfterMilliseconds' in source and 'LocationSnapshot::fromPosition' in source and
        (root/'src/location_snapshot.cpp').is_file(),
    'no persistence / network transfer of coordinates':
        set(line.strip() for line in source.splitlines() if '.setValue(' in line) == {
            'this->m_settings.setValue(QStringLiteral("gps/enabled"), enabled);',
            'this->m_settings.setValue(QStringLiteral("gps/follow"), follow);'} and 'QSaveFile' not in source and
        'QNetworkAccessManager' not in source,
    'map menu GPS control, follow, marker and settings display':
        'id: locationChip' not in qml and 'locationService.enabled = checked' in qml and
        'locationService.follow = checked' in qml and 'mapView.setLocationMarker(' in qml and
        'locationService.follow && locationService.fresh' in qml and
        'locationService.latitude.toFixed(6)' in qml and 'locationService.altitudeAvailable' in qml and
        'visible: root.activePage === "map"' in qml,
    'pinned Android module addon':
        '--modules qtpositioning' in docker and
        'Qt6Positioning/Qt6PositioningConfig.cmake' in docker,
}
for description, passed in checks.items():
    print(f'{"PASS" if passed else "FAIL"}: {description}')
assert all(checks.values())
