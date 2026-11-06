#include "language_settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

MobileCatalogTranslator::MobileCatalogTranslator(QObject *parent)
    : QTranslator(parent)
{
}

bool MobileCatalogTranslator::isEmpty() const
{
    return this->entries.isEmpty();
}

QString MobileCatalogTranslator::translate(const char *context, const char *sourceText,
                                           const char *disambiguation, int n) const
{
    Q_UNUSED(context)
    Q_UNUSED(disambiguation)
    Q_UNUSED(n)
    if (sourceText == nullptr)
        return QString();
    return this->entries.value(QString::fromUtf8(sourceText));
}

void MobileCatalogTranslator::setEntries(const QHash<QString, QString> &items)
{
    this->entries = items;
}

LanguageSettings::LanguageSettings(QObject *parent, const QString &settingsFile)
    : QObject(parent)
    , translator(this)
{
    this->initialLayoutDirection = QGuiApplication::layoutDirection();
    if (settingsFile.isEmpty())
    {
        this->settings = std::make_unique<QSettings>(
            QSettings::NativeFormat, QSettings::UserScope,
            QStringLiteral("AOWIS"), QStringLiteral("AOWIS-GUI-Mobile"));
    }
    else
        this->settings = std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);

    QFile file(QStringLiteral(":/i18n/translations.json"));
    if (file.open(QIODevice::ReadOnly))
    {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
        {
            const QJsonObject languages = document.object();
            for (const QString &code : {QStringLiteral("fr"), QStringLiteral("ar"),
                                         QStringLiteral("de"), QStringLiteral("es")})
            {
                QHash<QString, QString> entries;
                const QJsonObject messages = languages.value(code).toObject();
                for (QJsonObject::const_iterator it = messages.constBegin(); it != messages.constEnd(); ++it)
                {
                    if (it.value().isString() && !it.value().toString().isEmpty())
                        entries.insert(it.key(), it.value().toString());
                }
                this->catalogs.insert(code, entries);
            }
        }
    }

    QString restored = this->settings->value(
        QStringLiteral("interface/language"), QStringLiteral("en")).toString();
    if (restored != QStringLiteral("en") && !this->catalogs.contains(restored))
        restored = QStringLiteral("en");
    this->applyLanguage(restored);
}

LanguageSettings::~LanguageSettings()
{
    if (this->translatorInstalled)
        QCoreApplication::removeTranslator(&this->translator);
    QGuiApplication::setLayoutDirection(this->initialLayoutDirection);
}

QString LanguageSettings::language() const
{
    return this->selectedLanguage;
}

bool LanguageSettings::rightToLeft() const
{
    return this->selectedLanguage == QStringLiteral("ar");
}

void LanguageSettings::applyLanguage(const QString &code)
{
    if (this->translatorInstalled)
    {
        QCoreApplication::removeTranslator(&this->translator);
        this->translatorInstalled = false;
    }
    this->translator.setEntries(this->catalogs.value(code));
    this->selectedLanguage = code;
    if (code != QStringLiteral("en"))
        this->translatorInstalled = QCoreApplication::installTranslator(&this->translator);
    QGuiApplication::setLayoutDirection(this->rightToLeft()
        ? Qt::RightToLeft : Qt::LeftToRight);
    emit languageChanged();
}

bool LanguageSettings::setLanguage(const QString &code)
{
    if (code != QStringLiteral("en") && code != QStringLiteral("fr") &&
        code != QStringLiteral("ar") && code != QStringLiteral("de") &&
        code != QStringLiteral("es"))
        return false;
    if (code != QStringLiteral("en") && this->catalogs.value(code).isEmpty())
        return false;
    if (code == this->selectedLanguage)
        return true;

    this->settings->setValue(QStringLiteral("interface/language"), code);
    this->settings->sync();
    if (this->settings->status() != QSettings::NoError)
        return false;
    this->applyLanguage(code);
    return true;
}
