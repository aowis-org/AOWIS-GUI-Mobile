#include "location_service.h"

#include <QCoreApplication>
#include <QTemporaryDir>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir storage;
    if (!storage.isValid())
        return 1;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, storage.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::SystemScope, storage.path());
    {
        LocationService service;
        if (!service.enabled() || service.follow())
            return 2;
        service.setFollow(true);
        service.setEnabled(false);
    }
    LocationService restored;
    restored.setApplicationActive(true);
    restored.setApplicationActive(false);
    restored.setApplicationActive(true);
    if (restored.enabled() || !restored.follow() || restored.hasFix()
        || restored.fresh() || restored.status() != QStringLiteral("GPS off"))
        return 3;
    restored.setApplicationActive(false);
    restored.setEnabled(true);
    return restored.enabled() && !restored.hasFix() ? 0 : 4;
}
