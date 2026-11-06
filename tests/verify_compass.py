#!/usr/bin/env python3
"""Fast source contract checks for magnetic compass and renderer-independent UI."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
qml = (root / "qml/Main.qml").read_text(encoding="utf-8")
rose = (root / "qml/CompassRose.qml").read_text(encoding="utf-8")
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
main = (root / "src/main.cpp").read_text(encoding="utf-8")
service = (root / "src/compass_service.cpp").read_text(encoding="utf-8")
header = (root / "src/compass_service.h").read_text(encoding="utf-8")
docker = (root / "tools/qt-android/Dockerfile").read_text(encoding="utf-8")
checks = {
    "Qt Sensors linked and UI asset packaged":
        "Qt6::Sensors" in cmake and "qml/CompassRose.qml" in cmake,
    "Android Qt Sensors provisioned for host and ARM64":
        docker.count("--modules qtsensors") == 2 and
        "Qt6Sensors/Qt6SensorsConfig.cmake" in docker,
    "compass independent of GPS course and renderer":
        "QCompass" in header and "QGeoPositionInfoSource" not in service and
        "RenderLab" not in service and "QSettings" not in service,
    "start only when compass visible and app foreground":
        'onActivePageChanged: compassService.setPageVisible(activePage === "compass")' in qml and
        "setApplicationActive" in main and "this->m_compass.stop();" in service,
    "status for unavailable, stale and low calibration":
        "State::Unavailable" in service and "State::Stale" in service and
        "figure eight" in service and "kStaleAfterMs" in service,
    "screen orientation correction":
        "nativeOrientation()" in service and "angleBetween(" in service,
    "accessible polished vector compass and smooth rotation":
        "Canvas {" in rose and "RotationAnimation.Shortest" in rose and
        "Accessible.name:" in rose and "compassRose" in qml,
    "hamburger navigation and Android Back":
        'text: "Compass"' in qml and 'root.openPage("compass")' in qml and
        'activePage !== "map"' in qml,
    "live heading identified as magnetic, never true":
        "MAGNETIC" in qml and "not true north" in qml,
    "pure C++ heading regression test wired to ctest":
        "aowis-mobile-compass-heading-tests" in cmake and
        "aowis-mobile-compass-heading" in (root / "compile_linux.sh").read_text(),
}
for name, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {name}")
assert all(checks.values())
