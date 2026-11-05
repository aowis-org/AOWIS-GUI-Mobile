#include "appearance_settings.h"

#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "temporary settings directory");
    const QString path = directory.filePath(QStringLiteral("appearance.ini"));

    {
        AppearanceSettings settings(nullptr, path);
        require(settings.mode() == QStringLiteral("system"), "default follows system");
        require(!settings.setMode(QStringLiteral("purple")), "unknown modes rejected");
        require(settings.mode() == QStringLiteral("system"), "invalid selection unchanged");
        require(settings.setMode(QStringLiteral("dark")), "save dark theme");
        require(settings.dark(), "dark overrides system");
    }
    {
        AppearanceSettings settings(nullptr, path);
        require(settings.mode() == QStringLiteral("dark"), "dark restored");
        require(settings.setMode(QStringLiteral("light")), "save light theme");
        require(!settings.dark(), "light overrides system");
    }
    {
        AppearanceSettings settings(nullptr, path);
        require(settings.mode() == QStringLiteral("light"), "light restored");
        require(settings.setMode(QStringLiteral("system")), "restore system choice");
    }
    {
        AppearanceSettings settings(nullptr, path);
        require(settings.mode() == QStringLiteral("system"), "system restored");
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QStringLiteral("appearance/theme"), QStringLiteral("invalid"));
        settings.sync();
        AppearanceSettings appearance(nullptr, path);
        require(appearance.mode() == QStringLiteral("system"), "corrupt choice falls back");
    }
    std::cout << "Appearance settings tests passed\n";
    return 0;
}
