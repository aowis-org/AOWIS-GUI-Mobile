#include "location_snapshot.h"

#include <QDateTime>
#include <QGeoCoordinate>
#include <QtNumeric>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

namespace
{
double availableMeasurement(const QGeoPositionInfo &position,
                            QGeoPositionInfo::Attribute attribute)
{
    if (!position.hasAttribute(attribute))
        return -1.0;
    const double value = position.attribute(attribute);
    return qIsFinite(value) && value >= 0.0 ? value : -1.0;
}
}

LocationSnapshot LocationSnapshot::fromPosition(const QGeoPositionInfo &position)
{
    LocationSnapshot snapshot;
    if (!position.isValid() || !position.coordinate().isValid() ||
        !position.timestamp().isValid())
        return snapshot;

    const QGeoCoordinate coordinate = position.coordinate();
    if (!qIsFinite(coordinate.latitude()) || !qIsFinite(coordinate.longitude()))
        return snapshot;

    snapshot.valid = true;
    snapshot.latitude = coordinate.latitude();
    snapshot.longitude = coordinate.longitude();
    if (qIsFinite(coordinate.altitude()))
    {
        snapshot.altitudeAvailable = true;
        snapshot.altitudeMeters = coordinate.altitude();
    }
    snapshot.horizontalAccuracyMeters = availableMeasurement(position, QGeoPositionInfo::HorizontalAccuracy);
    snapshot.verticalAccuracyMeters = availableMeasurement(position, QGeoPositionInfo::VerticalAccuracy);
    snapshot.speedMetersPerSecond = availableMeasurement(position, QGeoPositionInfo::GroundSpeed);
    snapshot.headingDegrees = availableMeasurement(position, QGeoPositionInfo::Direction);
    if (position.hasAttribute(QGeoPositionInfo::MagneticVariation))
    {
        const double variation = position.attribute(QGeoPositionInfo::MagneticVariation);
        if (qIsFinite(variation) && qAbs(variation) <= 180.0)
            snapshot.declinationDegrees = variation;
    }
#ifdef Q_OS_ANDROID
    if (!qIsFinite(snapshot.declinationDegrees))
    {
        const QJniObject field("android/hardware/GeomagneticField", "(FFFJ)V",
            static_cast<jfloat>(snapshot.latitude), static_cast<jfloat>(snapshot.longitude),
            static_cast<jfloat>(snapshot.altitudeAvailable ? snapshot.altitudeMeters : 0.0),
            static_cast<jlong>(position.timestamp().toMSecsSinceEpoch()));
        if (field.isValid())
            snapshot.declinationDegrees = field.callMethod<jfloat>("getDeclination", "()F");
    }
#endif
    snapshot.timestampUtc = position.timestamp().toUTC().toString(Qt::ISODateWithMs);
    return snapshot;
}
