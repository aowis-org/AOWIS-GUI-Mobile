#include "mobile_embedded_map_backend.h"
#include "mobile_map_settings.h"
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

static bool checkSettings(const QString &root)
{
    const QString old_url = QStringLiteral("http://192.0.2.1:8122");
    const QString profile = QStringLiteral("0199cc11-0000-7000-8000-000000000001");
    QString provider;
    Aowis::Map::AvailableMapProvider inherited;
    inherited.inherited = true;
    inherited.provider.id = QStringLiteral("0199cc11-0000-7000-8000-000000000002");
    inherited.provider.name = QStringLiteral("Inherited imagery");
    inherited.provider.url_template = QStringLiteral("https://example.org/{z}/{x}/{y}.png");
    {
        MobileMapSettings settings(old_url, profile, nullptr, root);
        if (!settings.ready() || settings.upstreams().size() != 1 || settings.allowDirectFetch() ||
            settings.profileUsers(profile).size() != 1) return false;
        if (!settings.addUpstream(QStringLiteral("second"), QStringLiteral("https://192.0.2.2"), profile) ||
            settings.cascadeConfig().upstreams.last().url.port() != 8122 ||
            !settings.moveUpstream(QStringLiteral("second"), -1) ||
            settings.cascadeConfig().upstreams.first().name != QStringLiteral("second") ||
            settings.profileUsers(profile).size() != 2) return false;
        if (settings.addUpstream(QStringLiteral("duplicate"), QStringLiteral("https://192.0.2.2:8122"), {})) return false;
        settings.setAvailable({inherited});
        if (!settings.setProviderField(inherited.provider.id, QStringLiteral("enabled"), false) ||
            settings.setProviderField(inherited.provider.id, QStringLiteral("name"), QStringLiteral("Forbidden")) ||
            settings.setAcquisition(inherited.provider.id, QStringLiteral("direct"))) return false;
        provider = settings.addProvider(QStringLiteral("Test imagery"), QStringLiteral("https://example.org/{z}/{x}/{y}.png"));
        if (provider.isEmpty() || settings.addProvider(QStringLiteral("Invalid"), QStringLiteral("not-a-template")) != QString() ||
            !settings.setProviderField(provider, QStringLiteral("maxZoom"), 16) ||
            settings.setProviderField(provider, QStringLiteral("minZoom"), 17) ||
            !settings.setProviderField(provider, QStringLiteral("enabled"), false) ||
            !settings.setAcquisition(provider, QStringLiteral("cascade")) ||
            settings.setAcquisition(provider, QStringLiteral("direct")) ||
            !settings.setRoutes(provider, {QStringLiteral("second"), QStringLiteral("mobile-upstream")}) ||
            !settings.setPoorInternet(true) || !settings.setJpegQuality(65)) return false;
        if (!settings.setRoutes(inherited.provider.id, {QStringLiteral("mobile-upstream"), QStringLiteral("second")}) ||
            settings.setUpstreamField(QStringLiteral("second"), QStringLiteral("name"), QStringLiteral("mobile-upstream")) ||
            settings.setUpstreamField(QStringLiteral("second"), QStringLiteral("name"), QStringLiteral("bad name")) ||
            settings.setUpstreamField(QStringLiteral("second"), QStringLiteral("name"), QString()) ||
            settings.cascadeConfig().upstreams.first().name != QStringLiteral("second")) return false;

        // A stale cascade revision must not leave renamed provider routes behind.
        QString error, revision;
        std::unique_ptr<Aowis::Map::SettingsRepository> writer = MobileMapSettings::openRepository(root, &error);
        Aowis::Map::MapCascadeConfig concurrent;
        if (!writer || !writer->readCascade(&concurrent, &revision, &error)) return false;
        concurrent.jpeg_quality = 66;
        if (!writer->saveCascade(concurrent, revision, nullptr, &error) ||
            settings.setUpstreamField(QStringLiteral("second"), QStringLiteral("name"), QStringLiteral("site-map"))) return false;
        QHash<QString, QStringList> routes;
        if (!writer->readProviderUpstreamRoutes(&routes, nullptr, &error) ||
            routes.value(provider) != QStringList{QStringLiteral("second"), QStringLiteral("mobile-upstream")} ||
            routes.value(inherited.provider.id) != QStringList{QStringLiteral("mobile-upstream"), QStringLiteral("second")} ||
            settings.jpegQuality() != 66) return false;

        if (!settings.setUpstreamField(QStringLiteral("second"), QStringLiteral("name"), QStringLiteral(" site-map ")) ||
            !settings.setJpegQuality(65)) return false;
        const Aowis::Map::MapCascadeUpstream renamed = settings.cascadeConfig().upstreams.first();
        if (renamed.name != QStringLiteral("site-map") || renamed.url != QUrl(QStringLiteral("https://192.0.2.2:8122")) ||
            renamed.wireguard_profile_id != profile || !renamed.api_key.isEmpty() ||
            settings.allowDirectFetch() || !settings.profileUsers(profile).contains(QStringLiteral("Map server: site-map"))) return false;
    }
    {
        MobileMapSettings settings(old_url, profile, nullptr, root);
        if (!settings.ready() || settings.upstreams().size() != 2 || settings.jpegQuality() != 65 || !settings.poorInternet()) return false;
        settings.setAvailable({inherited});
        bool found = false;
        bool inherited_disabled = false;
        for (const QVariant &value : settings.providers())
        {
            const QVariantMap p = value.toMap();
            if (p.value(QStringLiteral("id")).toString() == inherited.provider.id)
                inherited_disabled = p.value(QStringLiteral("inherited")).toBool() &&
                    !p.value(QStringLiteral("enabled")).toBool() &&
                    p.value(QStringLiteral("routes")).toStringList() == QStringList{QStringLiteral("mobile-upstream"), QStringLiteral("site-map")};
            if (p.value(QStringLiteral("id")).toString() != provider) continue;
            found = p.value(QStringLiteral("maxZoom")).toInt() == 16 &&
                !p.value(QStringLiteral("enabled")).toBool() &&
                p.value(QStringLiteral("mode")).toString() == QStringLiteral("cascade") &&
                p.value(QStringLiteral("routes")).toStringList() == QStringList{QStringLiteral("site-map"), QStringLiteral("mobile-upstream")};
        }
        if (!found || !inherited_disabled || !settings.removeUpstream(QStringLiteral("site-map")) ||
            !settings.removeUpstream(QStringLiteral("mobile-upstream")) || settings.allowDirectFetch() ||
            !settings.profileUsers(profile).isEmpty()) return false;
    }
    MobileMapSettings restarted(old_url, profile, nullptr, root);
    return restarted.ready() && restarted.upstreams().isEmpty() && !restarted.allowDirectFetch() &&
        restarted.removeProvider(provider);
}

static bool checkDeprecatedKeys(const QString &root)
{
    const QString profile = QStringLiteral("0199cc11-0000-7000-8000-000000000001");
    const QString name = QStringLiteral("legacy-map");
    const QUrl url(QStringLiteral("https://192.0.2.2:8122"));
    QString error, revision;
    {
        std::unique_ptr<Aowis::Map::SettingsRepository> repository = MobileMapSettings::openRepository(root, &error);
        Aowis::Map::MapCascadeConfig cascade;
        if (!repository || !repository->readCascade(&cascade, &revision, &error)) return false;
        Aowis::Map::MapCascadeUpstream upstream;
        upstream.name = name;
        upstream.url = url;
        upstream.wireguard_profile_id = profile;
        upstream.api_key = QByteArrayLiteral("deprecated-test-key");
        cascade.upstreams.append(upstream);
        cascade.allow_direct_fetch = false;
        if (!repository->saveCascade(cascade, revision, nullptr, &error)) return false;
    }
    MobileMapSettings settings({}, {}, nullptr, root);
    if (!settings.ready() || settings.upstreams().size() != 1 || settings.allowDirectFetch() ||
        settings.upstreams().first().toMap().contains(QStringLiteral("key")) ||
        settings.setUpstreamField(name, QStringLiteral("key"), QStringLiteral("rejected"))) return false;
    const Aowis::Map::MapCascadeUpstream loaded = settings.cascadeConfig().upstreams.first();
    if (!loaded.api_key.isEmpty() || loaded.name != name || loaded.url != url ||
        loaded.wireguard_profile_id != profile || !settings.setJpegQuality(65)) return false;
    std::unique_ptr<Aowis::Map::SettingsRepository> repository = MobileMapSettings::openRepository(root, &error);
    Aowis::Map::MapCascadeConfig saved;
    return repository && repository->readCascade(&saved, nullptr, &error) &&
        saved.upstreams.size() == 1 && saved.upstreams.first().api_key.isEmpty() &&
        saved.upstreams.first().wireguard_profile_id == profile && saved.jpeg_quality == 65;
}

static bool checkFallbacks(const QString &root)
{
    QString a, b, c, gap;
    const QString url = QStringLiteral("https://example.org/%1/{z}/{x}/{y}.png");
    {
        MobileMapSettings settings({}, {}, nullptr, root);
        if (!settings.ready()) return false;
        a = settings.addProvider(QStringLiteral("Fallback A"), url.arg(QStringLiteral("a")));
        b = settings.addProvider(QStringLiteral("Fallback B"), url.arg(QStringLiteral("b")));
        c = settings.addProvider(QStringLiteral("Fallback C"), url.arg(QStringLiteral("c")));
        gap = settings.addProvider(QStringLiteral("Fallback gap"), url.arg(QStringLiteral("gap")));
        if (a.isEmpty() || b.isEmpty() || c.isEmpty() || gap.isEmpty() ||
            !settings.setProviderField(a, QStringLiteral("maxZoom"), 10) ||
            !settings.setProviderField(b, QStringLiteral("maxZoom"), 15) ||
            !settings.setProviderField(c, QStringLiteral("maxZoom"), 20) ||
            !settings.setProviderField(gap, QStringLiteral("minZoom"), 12)) return false;
        int backend_changes = 0;
        QObject::connect(&settings, &MobileMapSettings::changed, &settings, [&backend_changes]() { ++backend_changes; });
        if (!settings.setFallback(a, b) || !settings.setFallback(b, c) ||
            settings.setFallback(a, a) || settings.setFallback(b, a) || settings.setFallback(a, gap) ||
            backend_changes != 0) return false;
    }
    MobileMapSettings settings({}, {}, nullptr, root);
    if (settings.personalFallbacks().value(a) != b || settings.personalFallbacks().value(b) != c) return false;
    QString error;
    std::unique_ptr<Aowis::Map::SettingsRepository> repository = MobileMapSettings::openRepository(root, &error);
    QVector<Aowis::Map::MapProvider> local;
    if (!repository || !repository->readProviders(&local, nullptr, &error)) return false;
    QVector<Aowis::Map::AvailableMapProvider> available;
    for (const Aowis::Map::MapProvider &provider : local) available.append({provider, false, {}});
    MobileMapProviderDirectory directory;
    directory.replace(available, settings.personalFallbacks());
    if (directory.sourceForZoom(a, 10) != a || directory.sourceForZoom(a, 11) != b ||
        directory.sourceForZoom(a, 16) != c || directory.maximumSourceZoom(a) != 20) return false;
    int notifications = 0;
    QObject::connect(&directory, &MobileMapProviderDirectory::catalogChanged, &directory, [&notifications]() { ++notifications; });
    QObject::connect(&settings, &MobileMapSettings::fallbacksChanged, &directory, [&]() {
        directory.setFallbacks(settings.personalFallbacks());
    });
    if (!settings.setFallback(b, {}) || directory.maximumSourceZoom(a) != 15 || notifications != 1 ||
        !settings.setFallback(b, c) || directory.maximumSourceZoom(a) != 20 || notifications != 2) return false;
    // An inherited provider cannot enter the chain even if its UUID was once local.
    for (Aowis::Map::AvailableMapProvider &entry : available)
        if (entry.provider.id == b) entry.inherited = true;
    directory.replace(available, settings.personalFallbacks());
    if (directory.maximumSourceZoom(a) != 10) return false;
    if (!settings.setProviderField(b, QStringLiteral("enabled"), false) ||
        settings.personalFallbacks().contains(a) || settings.setFallback(a, b) ||
        !settings.setProviderField(b, QStringLiteral("enabled"), true) ||
        settings.personalFallbacks().value(a) != b || !settings.removeProvider(b) ||
        settings.personalFallbacks().contains(a)) return false;
    return settings.setFallback(a, {}) && !settings.personalFallbacks().contains(a);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const QDir root(temporary.path());
    if (!checkSettings(root.filePath(QStringLiteral("settings-ui"))))
    { qCritical() << "MAP settings migration/persistence failed"; return 1; }
    if (!checkDeprecatedKeys(root.filePath(QStringLiteral("deprecated-keys"))))
    { qCritical() << "Deprecated MAP key migration failed"; return 1; }
    if (!checkFallbacks(root.filePath(QStringLiteral("fallback-settings"))))
    { qCritical() << "Personal imagery fallback regression failed"; return 1; }
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
    cascade.upstreams.append(upstream);
    QString provider;
    // Valid tiny PNG; renderer transports cached data without modifying it.
    const QByteArray png = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aA9sAAAAASUVORK5CYII=");
    {
        MobileEmbeddedMapBackend backend(nullptr, data, settings);
        backend.configure(cascade);
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
    restarted.configure(cascade);
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
