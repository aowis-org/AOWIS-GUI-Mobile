#include "map_server_settings.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>

static bool check(bool condition, const char *message)
{
    if (!condition)
        qCritical() << "FAILED:" << message;
    return condition;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!check(directory.isValid(), "temporary settings directory"))
        return 1;
    const QString file = directory.filePath(QStringLiteral("settings.ini"));
    {
        MapServerSettings settings(nullptr, file);
        if (!check(!settings.configured() && settings.endpointUrl().isEmpty(), "new installation is unconfigured")) return 1;
        if (!check(!settings.save(QStringLiteral("ftp://example.org"), QStringLiteral("80")), "reject unsupported scheme")) return 1;
        if (!check(!settings.save(QStringLiteral("example.org"), QStringLiteral("80")), "require absolute URL")) return 1;
        if (!check(!settings.save(QStringLiteral("http://example.org:8080"), QStringLiteral("80")), "reject embedded port")) return 1;
        if (!check(!settings.save(QStringLiteral("http://example.org?token=a"), QStringLiteral("80")), "reject query")) return 1;
        if (!check(!settings.save(QStringLiteral("http://user:pw@example.org"), QStringLiteral("80")), "reject credentials")) return 1;
        if (!check(!settings.save(QStringLiteral("http://example.org"), QStringLiteral("0")), "reject port zero")) return 1;
        if (!check(!settings.save(QStringLiteral("http://example.org"), QStringLiteral("65536")), "reject port too large")) return 1;
        if (!check(!settings.save(QStringLiteral("http://example.org"), QStringLiteral("8x80")), "reject nonnumeric port")) return 1;
        if (!check(!settings.configured(), "invalid configurations are not saved")) return 1;
        if (!check(settings.save(QStringLiteral(" https://maps.example.org/tiles "), QStringLiteral("443")), "save valid settings")) return 1;
        if (!check(settings.endpointUrl() == QStringLiteral("https://maps.example.org:443/tiles"), "construct endpoint with separate port")) return 1;
        if (!check(!settings.save(QStringLiteral("invalid"), QStringLiteral("5000")), "reject invalid replacement")) return 1;
        if (!check(settings.endpointUrl() == QStringLiteral("https://maps.example.org:443/tiles"), "invalid replacement preserves previous endpoint")) return 1;
    }
    {
        MapServerSettings reloaded(nullptr, file);
        if (!check(reloaded.configured(), "configuration restored after restart")) return 1;
        if (!check(reloaded.serverUrl() == QStringLiteral("https://maps.example.org/tiles"), "URL restored")) return 1;
        if (!check(reloaded.serverPort() == 443, "port restored")) return 1;
        if (!check(reloaded.save(QStringLiteral("http://192.168.2.102"), QStringLiteral("8080")), "IPv4 address accepted")) return 1;
        if (!check(reloaded.endpointUrl() == QStringLiteral("http://192.168.2.102:8080"), "IPv4 endpoint formed")) return 1;
    }
    qInfo() << "Map server settings persistence and validation passed.";
    return 0;
}
