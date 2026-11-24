#include "mobile_embedded_map_backend.h"
#include "mobile_map_provider_directory.h"
#include "mobile_embedded_map_transport.h"
#include <aowis/map/settings_repository.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryDir>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const QDir root(temporary.path());
    const QString settings = root.filePath(QStringLiteral("settings"));
    const QString data = root.filePath(QStringLiteral("data"));
    QString error;
    std::unique_ptr<Aowis::Map::SettingsRepository> repository =
        Aowis::Map::SettingsRepository::openAtOwnerRoot(QStringLiteral("mobile-map"), settings, &error);
    if (!repository) { qCritical() << error; return 1; }
    Aowis::Map::MapCascadeConfig cascade;
    QString revision;
    if (!repository->readCascade(&cascade, &revision, &error)) return 1;
    cascade.allow_direct_fetch = false;
    if (!repository->saveCascade(cascade, revision, nullptr, &error)) return 1;
    repository.reset();

    // An upstream with no WireGuard transport must not be reached directly.
    Aowis::Map::MapCascadeUpstream upstream;
    upstream.name = QStringLiteral("mobile-upstream");
    upstream.url = QUrl(QStringLiteral("http://192.0.2.1:8122"));
    upstream.wireguard_profile_id = QStringLiteral("unavailable-test-tunnel");
    QString provider;
    // Valid tiny PNG; renderer transports cached data without modifying it.
    const QByteArray png = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aA9sAAAAASUVORK5CYII=");
    {
        MobileEmbeddedMapBackend backend(nullptr, data, settings);
        backend.configure(upstream);
        if (!backend.settingsError().isEmpty() || !backend.terrainReady()) return 1;
        MobileMapProviderDirectory directory;
        directory.replace(backend.availableProviders());
        provider = directory.firstEnabledProviderId();
        if (provider.isEmpty() || !directory.isProviderEnabled(provider)) return 1;
        int minimum = 0, maximum = 0;
        if (!directory.sourceZoomRange(provider, &minimum, &maximum) || minimum > maximum ||
            directory.sourceForZoom(provider, minimum) != provider) return 1;
        const MapTiles::TileRequestResult miss = backend.tiles()->getTile(provider, minimum, 0, 0,
                                                                         QStringLiteral("offline-miss"));
        if (miss.status != MapTiles::TileRequestStatus::Unavailable &&
            miss.status != MapTiles::TileRequestStatus::InvalidRequest) return 1;
        if (backend.tiles()->upstreamActivity().active != 0 || backend.tiles()->upstreamActivity().queued != 0)
            return 1;
        const QString cache = QDir(data).filePath(QStringLiteral("raster/maptiles/%1/%2/0").arg(provider).arg(minimum));
        if (!QDir().mkpath(cache)) return 1;
        QFile tile(QDir(cache).filePath(QStringLiteral("0.png")));
        if (!tile.open(QIODevice::WriteOnly) || tile.write(png) != png.size()) return 1;
    }
    // A new backend, no in-memory tile cache, same on-disk storage.
    MobileEmbeddedMapBackend restarted(nullptr, data, settings);
    restarted.configure(upstream);
    if (!restarted.settingsError().isEmpty() || !restarted.terrainReady()) return 1;
    MobileMapProviderDirectory directory;
    directory.replace(restarted.availableProviders());
    int minimum = 0, maximum = 0;
    if (!directory.sourceZoomRange(provider, &minimum, &maximum)) return 1;
    MobileEmbeddedMapTransport transport(restarted);
    QEventLoop loop;
    QByteArray received;
    QObject::connect(&transport, &InterfaceServerMap::signalTileDataReceived, &loop,
                     [&received, &loop](const QString &key, const QByteArray &bytes)
    {
        if (key == QStringLiteral("offline-hit")) received = bytes;
        loop.quit();
    });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    transport.requestTile(QStringLiteral("/%1/%2/0/0.png").arg(provider).arg(minimum),
                          QStringLiteral("offline-hit"), 0, 0);
    loop.exec();
    if (received != png || restarted.tiles()->upstreamActivity().active != 0 ||
        restarted.tiles()->upstreamActivity().queued != 0) return 1;
    qInfo() << "Embedded MAP direct provider directory and offline raster restart passed";
    return 0;
}
