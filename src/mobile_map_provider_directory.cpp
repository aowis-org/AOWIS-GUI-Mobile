#include "mobile_map_provider_directory.h"
#include "map/core/map_provider_fallback_resolution.h"

void MobileMapProviderDirectory::replace(const QVector<Aowis::Map::AvailableMapProvider> &available)
{
    QVector<MapProviderEntry> entries;
    for (const Aowis::Map::AvailableMapProvider &available_entry : available)
    {
        const Aowis::Map::MapProvider &provider = available_entry.provider;
        MapProviderEntry entry;
        entry.id = provider.id;
        entry.name = provider.name;
        entry.url = provider.url_template;
        entry.coverage = provider.coverage;
        entry.enabled = provider.enabled;
        entry.remote_fetch_enabled = provider.remote_fetch_enabled;
        entry.minimum_zoom = provider.minimum_zoom;
        entry.maximum_zoom = provider.maximum_zoom;
        entries.append(entry);
    }
    bool changed = entries.size() != this->entries.size();
    for (qsizetype i = 0; !changed && i < entries.size(); ++i)
    {
        const MapProviderEntry &a = entries.at(i);
        const MapProviderEntry &b = this->entries.at(i);
        changed = a.id != b.id || a.name != b.name || a.url != b.url ||
            a.coverage != b.coverage || a.enabled != b.enabled ||
            a.remote_fetch_enabled != b.remote_fetch_enabled ||
            a.minimum_zoom != b.minimum_zoom || a.maximum_zoom != b.maximum_zoom;
    }
    if (!changed) return;
    this->entries = entries;
    emit catalogChanged();
}

bool MobileMapProviderDirectory::isProviderEnabled(const QString &id) const
{
    const MapProviderEntry *entry = MapProviderFallbackResolution::find(this->entries, id);
    return entry != nullptr && entry->enabled;
}

QString MobileMapProviderDirectory::firstEnabledProviderId() const
{
    for (const MapProviderEntry &entry : this->entries)
        if (entry.enabled) return entry.id;
    return {};
}

QString MobileMapProviderDirectory::sourceForZoom(const QString &id, int zoom) const
{
    return MapProviderFallbackResolution::sourceForZoom(this->entries, {}, id, zoom);
}

int MobileMapProviderDirectory::maximumSourceZoom(const QString &id) const
{
    return MapProviderFallbackResolution::maximumSourceZoom(this->entries, {}, id);
}

bool MobileMapProviderDirectory::sourceZoomRange(const QString &id, int *minimum, int *maximum) const
{
    const MapProviderEntry *entry = MapProviderFallbackResolution::find(this->entries, id);
    if (entry == nullptr || minimum == nullptr || maximum == nullptr) return false;
    *minimum = entry->minimum_zoom;
    *maximum = entry->maximum_zoom;
    return true;
}

QString MobileMapProviderDirectory::builtinProviderId(MapProvider provider) const
{
    return MapProviderCatalog::builtinId(static_cast<int>(provider));
}
