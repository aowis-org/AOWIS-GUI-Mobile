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
    "missing-first-sample diagnostic":
        "State::NoSamples" in service and "kFirstReadingTimeoutMs" in service and
        "compassService.sensorActive" in qml and "compassService.sampleCount" in qml,
    "screen orientation correction":
        "nativeOrientation()" in service and "angleBetween(" in service,
    "accessible vector compass with C++-driven continuous rotation":
        "Canvas {" in rose and "Behavior on rotation" not in rose and
        "RotationAnimation" not in rose and
        "Accessible.name:" in rose and "compassRose" in qml,
    "live heading directly drives rose and numeric display":
        "rotation: compass.hasHeading ? -compass.headingDegrees : 0" in rose and
        "Math.round(compassService.headingDegrees)" in qml and
        "compassService.direction" in qml and
        "displayInitialized" not in rose and "FrameAnimation" not in rose,
    "sensor prediction and Qt animation-clock repaint loop":
        "compass_heading_filter.h" in cmake and
        "AowisCompass::HeadingFilter m_headingFilter" in header and
        "m_headingFilter.sample" in service and
        "m_headingFilter.advanceDisplay" in service and
        "m_compass.setDataRate(60)" in service and
        "QAbstractAnimation" in service and
        "m_frameAnimation->start()" in service and
        "m_frameAnimation->stop()" in service and
        "m_displayTimer" not in service and
        "emit headingChanged()" in service and
        "sensorRateHz" in header and
        "displayRateHz" in header and
        "pageVisibleChanged" in main and
        "!compassService.pageVisible()" in main and
        "FrameAnimation" not in rose and "CompassMotion" not in rose,
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
