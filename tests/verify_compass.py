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
rv = (root / "src/compass_sensor_source.cpp").read_text(encoding="utf-8")
fusion = (root / "src/compass_fusion.h").read_text(encoding="utf-8")
filt = (root / "src/compass_heading_filter.h").read_text(encoding="utf-8")
checks = {
    "Qt Sensors linked and UI asset packaged":
        "Qt6::Sensors" in cmake and "qml/CompassRose.qml" in cmake,
    "Android Qt Sensors provisioned for host and ARM64":
        docker.count("--modules qtsensors") == 2 and
        "Qt6Sensors/Qt6SensorsConfig.cmake" in docker,
    "compass independent of GPS course, renderer and Qt Sensors":
        "QCompass" not in header and "QCompass" not in service and
        "QSensor" not in service and "CompassSensorSource m_source" in header and
        "QGeoPositionInfoSource" not in service and
        "RenderLab" not in service and "QSettings" not in service,
    "start only when compass visible and app foreground":
        'compassService.setPageVisible(activePage === "compass")' in qml and
        "setApplicationActive" in main and "this->m_source.stop();" in service,
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
        "headingDegrees: compassService.roseHeadingDegrees" in qml and
        "Q_PROPERTY(double roseHeadingDegrees" in header and
        "continuousDisplayDegrees()" in service and
        "displayInitialized" not in rose and "FrameAnimation" not in rose,
    "sensor prediction and Qt animation-clock repaint loop":
        "compass_heading_filter.h" in cmake and
        "AowisCompass::HeadingFilter m_headingFilter" in header and
        "m_headingFilter.sample" in service and
        "m_headingFilter.advanceDisplay" in service and
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
    "NDK rotation vector preferred, own fusion fallback":
        "kRotationVectorType = 11" in rv and "kGyroscopeType = 4" in rv and
        "ALooper_prepare" in rv and
        "ASensorManager_getInstanceForPackage" in rv and
        "AowisCompass::AttitudeFusion fusion" in rv and
        "compass_sensor_source.cpp" in cmake and "compass_fusion.h" in cmake and
        "target_link_libraries(aowis-mobile PRIVATE android)" in cmake and
        "startSource(CompassSensorSource::Mode::RotationVector)" in service and
        "startSource(CompassSensorSource::Mode::FusionWithGyroscope)" in service and
        "headingFromRotationVector(" in service and
        "kFusedSensorTuning" in service and "kRawCompassTuning" in service and
        "compassService.sourceDescription" in qml and
        "compassService.hardwareDescription" in qml,
    "own fusion: gyroscope integration, TRIAD, disturbance rejection":
        "addGyroscope" in fusion and "orientationFromGravityAndField" in fusion and
        "fieldError" in fusion and "gravityError" in fusion and "Qt" not in fusion.split("namespace")[1],
    "index triangle on all four sides":
        "for (let side = 0; side < 4; ++side)" in rose,
    "no stillness gate freezing the heading":
        "m_motionConfirmations" not in filt and "realTurn" not in filt and
        "minCutoffHz" in filt,
    "hamburger navigation and Android Back":
        'text: qsTr("Compass")' in qml and 'root.openPage("compass")' in qml and
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
