#include "language_settings.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QTemporaryDir>

#include <cassert>

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    QTemporaryDir temporary;
    assert(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("language.ini"));
    const Qt::LayoutDirection oldDirection = QGuiApplication::layoutDirection();

    {
        LanguageSettings language(nullptr, path);
        assert(language.language() == QStringLiteral("en"));
        assert(!language.rightToLeft());
        assert(QCoreApplication::translate("Main", "Settings") == QStringLiteral("Settings"));

        assert(language.setLanguage(QStringLiteral("fr")));
        assert(language.language() == QStringLiteral("fr"));
        assert(QCoreApplication::translate("Main", "Settings") == QStringLiteral("Paramètres"));
        assert(QCoreApplication::translate("LocationService", "Position available") == QStringLiteral("Position disponible"));

        assert(language.setLanguage(QStringLiteral("ar")));
        assert(language.rightToLeft());
        assert(QGuiApplication::layoutDirection() == Qt::RightToLeft);
        assert(QCoreApplication::translate("Main", "Settings") == QString::fromUtf8("الإعدادات"));
        assert(QCoreApplication::translate("Main", "Map") == QString::fromUtf8("الخريطة"));

        assert(language.setLanguage(QStringLiteral("de")));
        assert(!language.rightToLeft());
        assert(QGuiApplication::layoutDirection() == Qt::LeftToRight);
        assert(QCoreApplication::translate("Main", "Settings") == QStringLiteral("Einstellungen"));

        assert(language.setLanguage(QStringLiteral("es")));
        assert(QCoreApplication::translate("Main", "Settings") == QStringLiteral("Ajustes"));
        assert(!language.setLanguage(QStringLiteral("invalid")));
        assert(language.language() == QStringLiteral("es"));
    }
    {
        LanguageSettings restored(nullptr, path);
        assert(restored.language() == QStringLiteral("es"));
        assert(QCoreApplication::translate("Main", "Settings") == QStringLiteral("Ajustes"));
        assert(restored.setLanguage(QStringLiteral("en")));
        assert(QCoreApplication::translate("Main", "Settings") == QStringLiteral("Settings"));
    }
    assert(QGuiApplication::layoutDirection() == oldDirection);
    return 0;
}
