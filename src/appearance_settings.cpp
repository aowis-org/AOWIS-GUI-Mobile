#include "appearance_settings.h"

#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>

AppearanceSettings::AppearanceSettings(QObject *parent, const QString &settingsFile)
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

    const QString restored = this->settings->value(
        QStringLiteral("appearance/theme"), QStringLiteral("system")).toString();
    this->selectedMode = (restored == QStringLiteral("dark") ||
                          restored == QStringLiteral("light"))
                             ? restored : QStringLiteral("system");

    // Changing the OS theme updates the UI when following the system. Manual
    // choices remain stable until changed in Settings.
    QStyleHints *hints = QGuiApplication::styleHints();
    QObject::connect(hints, &QStyleHints::colorSchemeChanged, this,
                     [this](Qt::ColorScheme) {
        if (this->selectedMode == QStringLiteral("system"))
            emit themeChanged();
    });
}

AppearanceSettings::~AppearanceSettings() = default;

QString AppearanceSettings::mode() const
{
    return this->selectedMode;
}

bool AppearanceSettings::dark() const
{
    if (this->selectedMode == QStringLiteral("dark"))
        return true;
    if (this->selectedMode == QStringLiteral("light"))
        return false;

    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark)
        return true;
    if (scheme == Qt::ColorScheme::Light)
        return false;
    // Some Linux themes do not advertise a scheme. Use their window palette.
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

bool AppearanceSettings::setMode(const QString &mode)
{
    if (mode != QStringLiteral("system") &&
        mode != QStringLiteral("dark") &&
        mode != QStringLiteral("light"))
        return false;

    if (this->selectedMode == mode)
        return true;

    this->settings->setValue(QStringLiteral("appearance/theme"), mode);
    this->settings->sync();
    if (this->settings->status() != QSettings::NoError)
        return false;

    this->selectedMode = mode;
    emit themeChanged();
    return true;
}
