#pragma once

#include <aowis/model/units/unit_profile.h>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <memory>

class QSettings;

// Qt Quick adapter for the exact MODEL unit-profile registry used in Desktop.
// All selections are display preferences; canonical MODEL values never change.
class UnitProfileSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed)
    Q_PROPERTY(QString activeName READ activeName NOTIFY changed)
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit UnitProfileSettings(QObject *parent = nullptr, const QString &settingsFile = QString());
    ~UnitProfileSettings() override;

    QVariantList profiles() const;
    Q_INVOKABLE QVariantList listProfiles(const QString &languageCode, int reactiveRevision) const;
    int currentIndex() const;
    QString activeName() const;
    bool editable() const;
    int revision() const;

    Q_INVOKABLE bool selectProfile(int index);
    Q_INVOKABLE bool createProfile(const QString &name);
    Q_INVOKABLE bool renameProfile(const QString &name);
    Q_INVOKABLE bool removeProfile();
    Q_INVOKABLE bool setUnit(const QString &quantity, const QString &unit);
    Q_INVOKABLE QVariantList fields(const QString &query, int reactiveRevision, const QString &languageCode) const;
    Q_INVOKABLE QString formatDistance(double meters, int reactiveRevision) const;
    Q_INVOKABLE QString formatAltitude(double meters, int reactiveRevision) const;
    Q_INVOKABLE QString formatSpeed(double metersPerSecond, int reactiveRevision) const;

signals:
    void changed();

private:
    const aowis::units::UnitProfile *activeProfile() const;
    void persist();
    void publish();
    QString formatLength(double meters, const char *quantity) const;

    std::unique_ptr<QSettings> settings;
    aowis::units::UnitProfileCollection collection;
    int changeRevision = 0;
};
