#pragma once

#include "map/core/map_provider_catalog.h"
#include <aowis/map/provider_catalog.h>

// Typed MAP-library catalog adapter implementing Render's provider directory.
class MobileMapProviderDirectory final : public QObject, public MapImageryProviderDirectory
{
    Q_OBJECT
public:
    const QVector<MapProviderEntry> &providers() const { return this->entries; }
    void replace(const QVector<Aowis::Map::AvailableMapProvider> &available);
    bool isProviderEnabled(const QString &id) const override;
    QString firstEnabledProviderId() const override;
    QString sourceForZoom(const QString &id, int zoom) const override;
    int maximumSourceZoom(const QString &id) const override;
    bool sourceZoomRange(const QString &id, int *minimum, int *maximum) const override;
    QString builtinProviderId(MapProvider provider) const override;
signals:
    void catalogChanged();
private:
    QVector<MapProviderEntry> entries;
};
