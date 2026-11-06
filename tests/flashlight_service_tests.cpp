#include "flashlight_service.h"

#include <QCoreApplication>
#include <QString>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    FlashlightService service;
#ifndef Q_OS_ANDROID
    // Desktop must not emulate flashlight hardware or falsely report ON.
    if (service.available() || service.enabled() || service.busy())
        return 1;
    service.setApplicationActive(true);
    service.toggle();
    if (service.enabled() || service.error().isEmpty())
        return 2;
    service.setApplicationActive(false);
    service.toggle();
    if (service.enabled())
        return 3;
#endif
    return 0;
}
