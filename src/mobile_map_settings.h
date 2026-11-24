#pragma once

#include <aowis/map/settings_repository.h>
#include <aowis/map/provider_catalog.h>
#include <QObject>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

// Typed frontend to MAP-owned settings. Only runtime proxy information is
// supplied by the view; it is never written to the repository.
class MobileMapSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList providers READ providers NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList upstreams READ upstreams NOTIFY changed)
    Q_PROPERTY(bool allowDirectFetch READ allowDirectFetch NOTIFY changed)
    Q_PROPERTY(bool poorInternet READ poorInternet NOTIFY changed)
    Q_PROPERTY(int jpegQuality READ jpegQuality NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit MobileMapSettings(const QString &legacy_url = {}, const QString &legacy_profile = {},
                               QObject *parent = nullptr, const QString &root = {});
    static std::unique_ptr<Aowis::Map::SettingsRepository> openRepository(const QString &root, QString *error);
    bool ready() const { return this->valid; }
    QString error() const { return this->last_error; }
    bool allowDirectFetch() const { return this->cascade.allow_direct_fetch; }
    bool poorInternet() const { return this->cascade.poor_internet_mode; }
    int jpegQuality() const { return this->cascade.jpeg_quality; }
    Aowis::Map::MapCascadeConfig cascadeConfig() const { return this->cascade; }
    QVariantList providers() const;
    QVariantList upstreams() const;
    QHash<QString, QString> personalFallbacks() const;
    Q_INVOKABLE bool setFallback(const QString &id, const QString &target);
    void setAvailable(const QVector<Aowis::Map::AvailableMapProvider> &entries);
    QStringList profileUsers(const QString &id) const;
    Q_INVOKABLE QString addProvider(const QString &name, const QString &url);
    Q_INVOKABLE bool setProviderField(const QString &id, const QString &field, const QVariant &value);
    Q_INVOKABLE bool removeProvider(const QString &id);
    Q_INVOKABLE bool setAcquisition(const QString &id, const QString &mode);
    Q_INVOKABLE bool setRoutes(const QString &id, const QStringList &names);
    Q_INVOKABLE bool setAllowDirectFetch(bool enabled);
    Q_INVOKABLE bool setPoorInternet(bool enabled);
    Q_INVOKABLE bool setJpegQuality(int quality);
    Q_INVOKABLE bool addUpstream(const QString &name, const QString &url, const QString &profile, const QString &key);
    Q_INVOKABLE bool setUpstreamField(const QString &name, const QString &field, const QString &value);
    Q_INVOKABLE bool moveUpstream(const QString &name, int step);
    Q_INVOKABLE bool removeUpstream(const QString &name);
signals:
    void changed();
    void catalogChanged();
    void fallbacksChanged();
    void errorChanged();
private:
    bool load();
    bool finish(bool ok, const QString &error);
    bool fail(const QString &error);
    bool saveProviders(const QVector<Aowis::Map::MapProvider> &value);
    bool saveCascade(const Aowis::Map::MapCascadeConfig &value);
    bool knownProvider(const QString &id) const;
    QVariantList fallbackChoices(const QString &id) const;
    bool validFallback(const QString &id, const QString &target) const;
    std::unique_ptr<QSettings> personal_settings;
    QHash<QString, QString> saved_fallbacks;
    std::unique_ptr<Aowis::Map::SettingsRepository> repository;
    Aowis::Map::MapCascadeConfig cascade;
    QVector<Aowis::Map::MapProvider> local;
    QVector<Aowis::Map::AvailableMapProvider> available;
    QSet<QString> disabled;
    QHash<QString, Aowis::Map::MapProviderAcquisitionMode> modes;
    QHash<QString, QStringList> routes;
    QString cascade_revision, provider_revision, disabled_revision, mode_revision, route_revision;
    QString last_error;
    bool valid = false;
};
