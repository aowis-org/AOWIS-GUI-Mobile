#!/usr/bin/env python3
"""Gradienter integration contract checks without a Qt/Android SDK."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
qml = (root / "qml/Main.qml").read_text()
vial = (root / "qml/GradienterVial.qml").read_text()
service = (root / "src/gradienter_service.cpp").read_text()
header = (root / "src/gradienter_service.h").read_text()
math = (root / "src/gradienter_math.h").read_text()
main = (root / "src/main.cpp").read_text()
cmake = (root / "CMakeLists.txt").read_text()
build = (root / "compile_linux.sh").read_text()
checks = {
    "service registered with QML": 'setContextProperty(QStringLiteral("gradienterService")' in main,
    "navigation and in-app Back": 'text: "Gradienter"' in qml and
         'root.openPage("gradienter")' in qml and 'activePage !== "map"' in qml,
    "Qt QML component packaged": "qml/GradienterVial.qml" in cmake and "GradienterVial {" in qml,
    "native Qt Sensors acceleration service": "QAccelerometer" in header and
         "QAccelerometer::Gravity" in service and "Qt6::Sensors" in cmake,
    "page and foreground lifecycle": "gradienterService.setPageVisible(activePage === \"gradienter\")" in qml and
         "gradienterService.setApplicationActive" in main and "this->m_accelerometer.stop()" in service,
    "frame clock owns visual update, no JS filtering": "QAbstractAnimation" in service and
         "m_filter.advance" in service and "Behavior on" not in vial and
         "onPaint:" not in vial and "Timer {" not in vial and "requestPaint" not in vial,
    "false reading states": "State::NoSamples" in service and "State::Unavailable" in service and
         "State::Stale" in service and "kFirstReadingTimeoutMs" in service,
    "screen orientation compensation": "angleBetween(" in service and
         "screenAxes" in service,
    "zero reference explicit and reversible": "setReference()" in service and
         "resetReference()" in service and "gradienterService.referenceSet" in qml,
    "conditional valid readings": "gradienterService.hasReading" in qml and
         "gradienterService.level" in qml and "gradienterService.tiltDegrees" in qml,
    "no new location permissions or data storage": "QSettings" not in service and
         "QGeoPosition" not in service and "RenderLab" not in service and
         "QNetwork" not in service,
    "hardware independent tests built": "aowis-mobile-gradienter-math-tests" in cmake and
         "aowis-mobile-gradienter-math-tests" in build and "gradienter-math" in build,
    "no type inferred in C++": "auto " not in service and "auto " not in header and
         "auto " not in math,
}
for description, good in checks.items():
    print(("PASS" if good else "FAIL") + ": " + description)
assert all(checks.values())
