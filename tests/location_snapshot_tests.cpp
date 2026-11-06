#include "location_snapshot.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QGeoCoordinate>
#include <QGeoPositionInfo>

#include <cmath>
#include <cstdio>
#include <cstdlib>

// Test expectations must run in RelWithDebInfo too (NDEBUG disables assert).
#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        std::abort(); \
    } \
} while (false)

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    // No timestamp/provider sample must never create a fictional fix.
    CHECK(!LocationSnapshot::fromPosition(QGeoPositionInfo()).valid);
    QGeoPositionInfo noDate(QGeoCoordinate(12.0, 22.0), QDateTime());
    CHECK(!LocationSnapshot::fromPosition(noDate).valid);

    const QDateTime timestamp = QDateTime::fromString(
        QStringLiteral("2026-11-06T00:12:03.000Z"), Qt::ISODateWithMs);
    QGeoPositionInfo sample(QGeoCoordinate(50.822345, 9.023456, -21.5), timestamp);
    sample.setAttribute(QGeoPositionInfo::HorizontalAccuracy, 7.3);
    sample.setAttribute(QGeoPositionInfo::VerticalAccuracy, 14.5);
    sample.setAttribute(QGeoPositionInfo::GroundSpeed, 2.5);
    sample.setAttribute(QGeoPositionInfo::Direction, 275.0);
    const LocationSnapshot fix = LocationSnapshot::fromPosition(sample);
    CHECK(fix.valid);
    CHECK(std::abs(fix.latitude - 50.822345) < 0.00000001);
    CHECK(std::abs(fix.longitude - 9.023456) < 0.00000001);
    CHECK(fix.altitudeAvailable && fix.altitudeMeters == -21.5);
    CHECK(fix.horizontalAccuracyMeters == 7.3);
    CHECK(fix.verticalAccuracyMeters == 14.5);
    CHECK(fix.speedMetersPerSecond == 2.5);
    CHECK(fix.headingDegrees == 275.0);
    CHECK(fix.timestampUtc == QStringLiteral("2026-11-06T00:12:03.000Z"));

    // Missing measurements must remain missing; never show fake 0m accuracy.
    const QGeoPositionInfo bare(QGeoCoordinate(-33.0, 151.0), timestamp);
    const LocationSnapshot noOptional = LocationSnapshot::fromPosition(bare);
    CHECK(noOptional.valid);
    CHECK(!noOptional.altitudeAvailable);
    CHECK(noOptional.horizontalAccuracyMeters == -1.0);
    CHECK(noOptional.verticalAccuracyMeters == -1.0);
    CHECK(noOptional.speedMetersPerSecond == -1.0);
    CHECK(noOptional.headingDegrees == -1.0);

    // Accuracy cannot be negative or non-finite; do not expose it as valid.
    QGeoPositionInfo badAccuracy(QGeoCoordinate(1.0, 2.0), timestamp);
    badAccuracy.setAttribute(QGeoPositionInfo::HorizontalAccuracy, -3.0);
    badAccuracy.setAttribute(QGeoPositionInfo::VerticalAccuracy, INFINITY);
    CHECK(LocationSnapshot::fromPosition(badAccuracy).horizontalAccuracyMeters == -1.0);
    CHECK(LocationSnapshot::fromPosition(badAccuracy).verticalAccuracyMeters == -1.0);
    return 0;
}
