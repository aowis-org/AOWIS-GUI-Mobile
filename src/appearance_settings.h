#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QSettings;
class QStyleHints;

// Persistent interface appearance. "system" follows Android/desktop color
// scheme changes; "dark" and "light" override the interface palette only.
// The map renderer is intentionally independent of this setting.
class AppearanceSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode NOTIFY themeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY themeChanged)

public:
    explicit AppearanceSettings(QObject *parent = nullptr,
                                const QString &settingsFile = QString());
    ~AppearanceSettings() override;

    QString mode() const;
    bool dark() const;
    Q_INVOKABLE bool setMode(const QString &mode);

signals:
    void themeChanged();

private:
    std::unique_ptr<QSettings> settings;
    QString selectedMode;
};
