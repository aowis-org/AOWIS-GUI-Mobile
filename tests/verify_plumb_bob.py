#!/usr/bin/env python3
"""Pure source checks for Plumb Bob integration and non-invasive ADB launch."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
qml = (root / "qml/Main.qml").read_text()
component = (root / "qml/PlumbBobInstrument.qml").read_text()
cpp = (root / "src/plumb_bob_service.cpp").read_text()
header = (root / "src/plumb_bob_service.h").read_text()
math = (root / "src/plumb_bob_math.h").read_text()
main = (root / "src/main.cpp").read_text()
cmake = (root / "CMakeLists.txt").read_text()
build = (root / "compile_linux.sh").read_text()
android = (root / "compile_android.sh").read_text()
lifecycle = (root / "tools/test_android_lifecycle.sh").read_text()
checks = {
    "native C++ sensor": "QAccelerometer" in header and
        "QAccelerometer::Gravity" in cpp and "QAbstractAnimation" in cpp,
    "page scoped sensor, lifecycle": "this->m_accelerometer.stop()" in cpp and
        "plumbBobService.setPageVisible" in qml and
        "plumbBobService.setApplicationActive" in main,
    "false reading and missing-sensor states": all(x in cpp for x in
        ("State::TooFlat", "State::Unavailable", "State::NoSamples", "State::Stale")),
    "screen compensated without system setting": "angleBetween" in cpp and
        "screenDirection" in math and "accelerometer_rotation" not in cpp,
    "declarative graphic, no JavaScript motion": "PlumbBobInstrument {" in qml and
        "Rotation {" in component and "onPaint:" not in component and
        "Timer {" not in component and "Behavior on" not in component,
    "navigation and Android Back": 'root.openPage("plumbbob")' in qml and
        'text: qsTr("Plumb Bob")' in qml and 'activePage !== "map"' in qml,
    "Qt module and context registration": "qml/PlumbBobInstrument.qml" in cmake and
        'setContextProperty(QStringLiteral("plumbBobService")' in main,
    "native regression tests in builds": "aowis-mobile-plumb-bob-math-tests" in cmake and
        "aowis-mobile-plumb-bob-math-tests" in build,
    "single edge deviation only": "faceDegrees" not in qml + cpp + header and
        "FACE LEAN" not in qml and "signedFaceLean" not in math and
        "faceLean" not in math and "plumbBobService.edgeDegrees.toFixed" in qml and
        "plumb(this->edgeDegrees())" in cpp,
    "help explains plumb only": "not magnetic north" not in qml and
        "phone's edge departs from vertical" in qml,
    "no C++ auto": all("auto " not in t for t in (cpp, header, math)),
    "launch without monkey or system changes":
        'tools/launch_android_app.sh' in android and
        'tools/launch_android_app.sh' in lifecycle and
        'shell am start -W' in (root / 'tools/launch_android_app.sh').read_text() and
        'shell monkey ' not in android and 'shell monkey ' not in lifecycle and
        'settings put' not in android and 'settings put' not in lifecycle,
}
for label, ok in checks.items():
    print(("PASS" if ok else "FAIL") + ": " + label)
assert all(checks.values())
