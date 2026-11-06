#include "location_snapshot.h"

#include <QDateTime>
#include <QGeoCoordinate>
#include <QtNumeric>

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
    snapshot.timestampUtc = position.timestamp().toUTC().toString(Qt::ISODateWithMs);
    return snapshot;
}
