#pragma once

#include <QGeoPositionInfo>
#include <QString>
#include <limits>

// A renderer-independent, validated positioning sample. Missing optional
// measurements use -1, never a fabricated zero.
struct LocationSnapshot
{
    bool valid = false;
    double latitude = 0.0;
    double longitude = 0.0;
    bool altitudeAvailable = false;
    double altitudeMeters = 0.0;
    double horizontalAccuracyMeters = -1.0;
    double verticalAccuracyMeters = -1.0;
    double speedMetersPerSecond = -1.0;
    double headingDegrees = -1.0;
    // Signed degrees east of geographic north. Zero is a valid measurement.
    double declinationDegrees = std::numeric_limits<double>::quiet_NaN();
    QString timestampUtc;

    static LocationSnapshot fromPosition(const QGeoPositionInfo &position);
};
