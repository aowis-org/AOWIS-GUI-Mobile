#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QSettings;

// Stores the AOWIS-SERVER-MAP endpoint and the WireGuard profile it is
// reached through (empty: direct); this class does not initiate network
// requests. MobileMapView reads both and loads tiles and terrain, through
// that profile's tunnel only when one is set.
class MapServerSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY endpointChanged)
    Q_PROPERTY(int serverPort READ serverPort NOTIFY endpointChanged)
    Q_PROPERTY(QString endpointUrl READ endpointUrl NOTIFY endpointChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY endpointChanged)
    Q_PROPERTY(QString wireguardProfileId READ wireguardProfileId NOTIFY endpointChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit MapServerSettings(QObject *parent = nullptr,
                               const QString &settingsFile = QString());
    ~MapServerSettings() override;

    QString serverUrl() const;
    int serverPort() const;
    QString endpointUrl() const;
    bool configured() const;
    QString wireguardProfileId() const;
    QString error() const;

    // wireguardProfileId: AOWIS-CONFIG id of a WireGuard profile, or empty.
    Q_INVOKABLE bool save(const QString &url, const QString &portText,
                          const QString &wireguardProfileId = QString());

signals:
    void endpointChanged();
    void errorChanged();

private:
    void setError(const QString &message);
    static bool validate(const QString &url, const QString &portText,
                         QString *normalizedUrl, int *port, QString *error);

    std::unique_ptr<QSettings> settings;
    QString savedUrl;
    static constexpr int DefaultMapPort = 8122;
    int savedPort = DefaultMapPort;
    QString savedProfile;
    QString validationError;
};
