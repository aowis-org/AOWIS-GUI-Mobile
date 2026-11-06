#include "unit_profile_settings.h"
#include "unit_display_text.h"
#include "unit_presented_quantities.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QStringList>
#include <QUuid>
#include <QVariantMap>
#include <cmath>
#include <string>
#include <string_view>

namespace {
QString asQString(std::string_view value)
{
    return QString::fromLatin1(value.data(), static_cast<qsizetype>(value.size()));
}

QString profileLabel(const aowis::units::UnitProfile &profile)
{
    const QString id = QString::fromStdString(profile.id);
    if (id == QStringLiteral("canonical"))
        return QStringLiteral("AOWIS Canonical — standard AOWIS units");
    static const QHash<QString, QString> descriptions = {
        {QStringLiteral("CMH"), QStringLiteral("cubic meters per hour")},
        {QStringLiteral("LPS"), QStringLiteral("liters per second")},
        {QStringLiteral("LPM"), QStringLiteral("liters per minute")},
        {QStringLiteral("MLD"), QStringLiteral("million liters per day")},
        {QStringLiteral("CMD"), QStringLiteral("cubic meters per day")},
        {QStringLiteral("CMS"), QStringLiteral("cubic meters per second")},
        {QStringLiteral("CFS"), QStringLiteral("cubic feet per second")},
        {QStringLiteral("GPM"), QStringLiteral("US gallons per minute")},
        {QStringLiteral("MGD"), QStringLiteral("million US gallons per day")},
        {QStringLiteral("IMGD"), QStringLiteral("million Imperial gallons per day")},
        {QStringLiteral("AFD"), QStringLiteral("acre-feet per day")}
    };
    if (profile.builtin && descriptions.contains(id))
        return id + QStringLiteral(" — ") + descriptions.value(id);
    return QString::fromStdString(profile.name);
}

QString translatable(const QString &source)
{
    const QByteArray key = source.toUtf8();
    return QCoreApplication::translate("UnitProfiles", key.constData());
}

QString groupLabel(std::string_view group)
{
    return translatable(asQString(group));
}
}

UnitProfileSettings::UnitProfileSettings(QObject *parent, const QString &settingsFile)
    : QObject(parent)
{
    if (settingsFile.isEmpty())
        this->settings = std::make_unique<QSettings>(
            QSettings::NativeFormat, QSettings::UserScope,
            QStringLiteral("AOWIS"), QStringLiteral("AOWIS-GUI-Mobile"));
    else
        this->settings = std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);

    const QJsonDocument document = QJsonDocument::fromJson(
        this->settings->value(QStringLiteral("units/custom_profiles")).toByteArray());
    if (document.isArray()) {
        for (const QJsonValue &value : document.array()) {
            if (!value.isObject()) continue;
            const QJsonObject item = value.toObject();
            aowis::units::UnitProfile profile;
            profile.id = item.value(QStringLiteral("id")).toString().toStdString();
            profile.name = item.value(QStringLiteral("name")).toString().trimmed().toStdString();
            const QJsonObject units = item.value(QStringLiteral("units")).toObject();
            bool valid = true;
            for (QJsonObject::const_iterator it = units.constBegin(); it != units.constEnd(); ++it) {
                if (!it.value().isString() ||
                    !aowis::units::isAllowedUnit(it.key().toStdString(), it.value().toString().toStdString())) {
                    valid = false;
                    break;
                }
                profile.overrides.emplace(it.key().toStdString(), it.value().toString().toStdString());
            }
            if (valid) (void)this->collection.add(std::move(profile));
        }
    }
    const QString saved = this->settings->value(
        QStringLiteral("units/selected_profile_id"), QStringLiteral("canonical")).toString();
    (void)this->collection.select(saved.toStdString());
}

UnitProfileSettings::~UnitProfileSettings() = default;

const aowis::units::UnitProfile *UnitProfileSettings::activeProfile() const
{
    return this->collection.find(this->collection.activeId());
}

QVariantList UnitProfileSettings::listProfiles(const QString &languageCode, int reactiveRevision) const
{
    Q_UNUSED(languageCode)
    Q_UNUSED(reactiveRevision)
    QVariantList result = this->profiles();
    for (QVariant &entry : result) {
        QVariantMap profile = entry.toMap();
        if (profile.value(QStringLiteral("builtin")).toBool())
            profile.insert(QStringLiteral("label"), translatable(profile.value(QStringLiteral("label")).toString()));
        entry = profile;
    }
    return result;
}

QVariantList UnitProfileSettings::profiles() const
{
    QVariantList result;
    for (const aowis::units::UnitProfile &profile : this->collection.profiles()) {
        result.append(QVariantMap{{QStringLiteral("id"), QString::fromStdString(profile.id)},
                                  {QStringLiteral("name"), QString::fromStdString(profile.name)},
                                  {QStringLiteral("label"), profileLabel(profile)},
                                  {QStringLiteral("builtin"), profile.builtin}});
    }
    return result;
}

int UnitProfileSettings::currentIndex() const
{
    const std::vector<aowis::units::UnitProfile> &items = this->collection.profiles();
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].id == this->collection.activeId()) return static_cast<int>(i);
    return 0;
}

QString UnitProfileSettings::activeName() const
{
    return QString::fromStdString(this->activeProfile()->name);
}

bool UnitProfileSettings::editable() const
{
    return !this->activeProfile()->builtin;
}

int UnitProfileSettings::revision() const
{
    return this->changeRevision;
}

void UnitProfileSettings::persist()
{
    QJsonArray custom;
    for (const aowis::units::UnitProfile &profile : this->collection.profiles()) {
        if (profile.builtin) continue;
        QJsonObject overrides;
        for (const aowis::units::UnitSelections::value_type &item : profile.overrides)
            overrides.insert(QString::fromStdString(item.first), QString::fromStdString(item.second));
        custom.append(QJsonObject{
            {QStringLiteral("id"), QString::fromStdString(profile.id)},
            {QStringLiteral("name"), QString::fromStdString(profile.name)},
            {QStringLiteral("units"), overrides}
        });
    }
    this->settings->setValue(QStringLiteral("units/custom_profiles"),
                             QJsonDocument(custom).toJson(QJsonDocument::Compact));
    this->settings->setValue(QStringLiteral("units/selected_profile_id"),
                             QString::fromStdString(this->collection.activeId()));
    this->settings->sync();
}

void UnitProfileSettings::publish()
{
    ++this->changeRevision;
    emit changed();
}

bool UnitProfileSettings::selectProfile(int index)
{
    if (index < 0 || index >= static_cast<int>(this->collection.profiles().size())) return false;
    if (!this->collection.select(this->collection.profiles()[static_cast<std::size_t>(index)].id)) return false;
    this->persist();
    this->publish();
    return true;
}

bool UnitProfileSettings::createProfile(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return false;
    aowis::units::UnitProfile profile = *this->activeProfile();
    profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    profile.name = trimmed.toStdString();
    profile.builtin = false;
    if (!this->collection.add(profile) || !this->collection.select(profile.id)) return false;
    this->persist();
    this->publish();
    return true;
}

bool UnitProfileSettings::renameProfile(const QString &name)
{
    if (!this->editable() || name.trimmed().isEmpty()) return false;
    if (!this->collection.rename(this->collection.activeId(), name.trimmed().toStdString())) return false;
    this->persist();
    this->publish();
    return true;
}

bool UnitProfileSettings::removeProfile()
{
    if (!this->editable() || !this->collection.remove(this->collection.activeId())) return false;
    this->persist();
    this->publish();
    return true;
}

bool UnitProfileSettings::setUnit(const QString &quantity, const QString &unit)
{
    if (!this->editable() ||
        !this->collection.setUnit(this->collection.activeId(), quantity.toStdString(), unit.toStdString()))
        return false;
    this->persist();
    this->publish();
    return true;
}

QVariantList UnitProfileSettings::fields(const QString &query, int reactiveRevision, const QString &languageCode) const
{
    Q_UNUSED(reactiveRevision)
    Q_UNUSED(languageCode)
    QVariantList result;
    const QString needle = query.trimmed();
    QString previousGroup;
    const aowis::units::UnitProfile *profile = this->activeProfile();
    // Desktop displays electrical quantities in addition to hydraulic MODEL fields.
    // Keep dimensionless and percentage fields together at the end, as in Desktop.
    for (int pass = 0; pass < 2; ++pass) {
        for (const aowis::units::UnitField &field : aowis::units::unit_fields) {
            if (!UnitPresentedQuantities::isPresented(field)) continue;
            const bool bottom = field.group == "Dimensionless" || field.group == "Percentages";
            if (bottom != (pass == 1)) continue;
            const QString key = asQString(field.key);
            const QString label = translatable(UnitDisplayText::quantity(field.key));
            const QString group = groupLabel(field.group);
            const QStringList allowed = asQString(field.choices).split(QLatin1Char(';'));
            if (!needle.isEmpty() && !key.contains(needle, Qt::CaseInsensitive) &&
                !label.contains(needle, Qt::CaseInsensitive) &&
                !group.contains(needle, Qt::CaseInsensitive) &&
                !allowed.join(QLatin1Char(' ')).contains(needle, Qt::CaseInsensitive)) continue;
            const QString selected = asQString(aowis::units::resolvedUnit(*profile, field.key));
            QVariantList choices;
            int selectedIndex = 0;
            for (int index = 0; index < allowed.size(); ++index) {
                choices.append(QVariantMap{
                    {QStringLiteral("label"), UnitDisplayText::symbol(allowed.at(index))},
                    {QStringLiteral("unit"), allowed.at(index)}
                });
                if (allowed.at(index) == selected) selectedIndex = index;
            }
            result.append(QVariantMap{
                {QStringLiteral("key"), key},
                {QStringLiteral("label"), label},
                {QStringLiteral("group"), group},
                {QStringLiteral("groupStart"), group != previousGroup},
                {QStringLiteral("canonical"), UnitDisplayText::symbol(asQString(aowis::units::canonicalUnit(field.key)))},
                {QStringLiteral("selected"), selectedIndex},
                {QStringLiteral("choices"), choices}
            });
            previousGroup = group;
        }
    }
    return result;
}

QString UnitProfileSettings::formatLength(double meters, const char *quantity) const
{
    if (!std::isfinite(meters)) return QString();
    const QString unit = asQString(aowis::units::resolvedUnit(*this->activeProfile(), quantity));
    double value = meters;
    int decimals = 1;
    if (unit == QStringLiteral("ft")) value *= 3.280839895013123;
    else if (unit == QStringLiteral("km")) { value /= 1000.0; decimals = 3; }
    else if (unit == QStringLiteral("mi")) { value /= 1609.344; decimals = 3; }
    return QLocale().toString(value, 'f', decimals) + QLatin1Char(' ') + UnitDisplayText::symbol(unit);
}

QString UnitProfileSettings::formatDistance(double meters, int reactiveRevision) const
{
    Q_UNUSED(reactiveRevision)
    return this->formatLength(meters, "distance");
}

QString UnitProfileSettings::formatAltitude(double meters, int reactiveRevision) const
{
    Q_UNUSED(reactiveRevision)
    return this->formatLength(meters, "altitude");
}

QString UnitProfileSettings::formatSpeed(double metersPerSecond, int reactiveRevision) const
{
    Q_UNUSED(reactiveRevision)
    if (!std::isfinite(metersPerSecond)) return QString();
    const QString unit = asQString(aowis::units::resolvedUnit(*this->activeProfile(), "velocity"));
    const double converted = unit == QStringLiteral("ft/s") ? metersPerSecond * 3.280839895013123 : metersPerSecond;
    return QLocale().toString(converted, 'f', 1) + QLatin1Char(' ') + UnitDisplayText::symbol(unit);
}
