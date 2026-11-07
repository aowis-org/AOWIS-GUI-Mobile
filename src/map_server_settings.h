#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QSettings;

// Stores the AOWIS-SERVER-MAP endpoint; this class does not initiate network
// requests. MobileMapView reads the URL and loads tiles and terrain from it.
class MapServerSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY endpointChanged)
    Q_PROPERTY(int serverPort READ serverPort NOTIFY endpointChanged)
    Q_PROPERTY(QString endpointUrl READ endpointUrl NOTIFY endpointChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY endpointChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit MapServerSettings(QObject *parent = nullptr,
                               const QString &settingsFile = QString());
    ~MapServerSettings() override;

    QString serverUrl() const;
    int serverPort() const;
    QString endpointUrl() const;
    bool configured() const;
    QString error() const;

    Q_INVOKABLE bool save(const QString &url, const QString &portText);

signals:
    void endpointChanged();
    void errorChanged();

private:
    void setError(const QString &message);
    static bool validate(const QString &url, const QString &portText,
                         QString *normalizedUrl, int *port, QString *error);

    std::unique_ptr<QSettings> settings;
    QString savedUrl;
    int savedPort = 0;
    QString validationError;
};
