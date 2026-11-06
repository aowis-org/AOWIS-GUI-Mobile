#include "map_server_settings.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

MapServerSettings::MapServerSettings(QObject *parent, const QString &settingsFile)
    : QObject(parent)
{
    if (settingsFile.isEmpty())
    {
        this->settings = std::make_unique<QSettings>(
            QSettings::NativeFormat, QSettings::UserScope,
            QStringLiteral("AOWIS"), QStringLiteral("AOWIS-GUI-Mobile"));
    }
    else
    {
        this->settings = std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);
    }

    const QString restoredUrl = this->settings->value(QStringLiteral("mapServer/url")).toString();
    const QString restoredPort = this->settings->value(QStringLiteral("mapServer/port")).toString();
    QString normalized;
    QString ignoredError;
    int port = 0;
    if (validate(restoredUrl, restoredPort, &normalized, &port, &ignoredError))
    {
        this->savedUrl = normalized;
        this->savedPort = port;
    }
}

MapServerSettings::~MapServerSettings() = default;

QString MapServerSettings::serverUrl() const
{
    return this->savedUrl;
}

int MapServerSettings::serverPort() const
{
    return this->savedPort;
}

QString MapServerSettings::endpointUrl() const
{
    if (!configured())
        return QString();
    QUrl endpoint(this->savedUrl);
    endpoint.setPort(this->savedPort);
    return endpoint.toString(QUrl::FullyEncoded);
}

bool MapServerSettings::configured() const
{
    return !this->savedUrl.isEmpty() && this->savedPort > 0;
}

QString MapServerSettings::error() const
{
    if (this->validationError.isEmpty())
        return QString();
    const QByteArray source = this->validationError.toUtf8();
    return QCoreApplication::translate("MapServerSettings", source.constData());
}

void MapServerSettings::setError(const QString &message)
{
    if (this->validationError == message)
        return;
    this->validationError = message;
    emit errorChanged();
}

bool MapServerSettings::validate(const QString &url, const QString &portText,
                                 QString *normalizedUrl, int *port, QString *error)
{
    const QString address = url.trimmed();
    const QUrl parsed(address, QUrl::StrictMode);
    if (address.isEmpty() || !parsed.isValid() || parsed.isRelative() ||
        (parsed.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0 &&
         parsed.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) ||
        parsed.host().isEmpty())
    {
        *error = QStringLiteral("Enter a full HTTP or HTTPS URL, for example http://192.168.2.10");
        return false;
    }
    if (parsed.port(-1) != -1 || !parsed.userInfo().isEmpty() ||
        parsed.hasQuery() || parsed.hasFragment())
    {
        *error = QStringLiteral("Put the port in the separate Port field; URL credentials, queries and fragments are not supported.");
        return false;
    }

    static const QRegularExpression portPattern(QStringLiteral("^[0-9]{1,5}$"));
    const QString digits = portText.trimmed();
    bool converted = false;
    const int parsedPort = digits.toInt(&converted, 10);
    if (!portPattern.match(digits).hasMatch() || !converted ||
        parsedPort < 1 || parsedPort > 65535)
    {
        *error = QStringLiteral("Port must be a number from 1 to 65535.");
        return false;
    }
    *normalizedUrl = parsed.toString(QUrl::FullyEncoded);
    *port = parsedPort;
    error->clear();
    return true;
}

bool MapServerSettings::save(const QString &url, const QString &portText)
{
    QString normalized;
    QString message;
    int port = 0;
    if (!validate(url, portText, &normalized, &port, &message))
    {
        setError(message);
        return false;
    }
    this->settings->setValue(QStringLiteral("mapServer/url"), normalized);
    this->settings->setValue(QStringLiteral("mapServer/port"), port);
    this->settings->sync();
    if (this->settings->status() != QSettings::NoError)
    {
        setError(QStringLiteral("Could not save map server settings to local storage."));
        return false;
    }
    const bool changed = (this->savedUrl != normalized || this->savedPort != port);
    this->savedUrl = normalized;
    this->savedPort = port;
    setError(QString());
    if (changed)
        emit endpointChanged();
    return true;
}
