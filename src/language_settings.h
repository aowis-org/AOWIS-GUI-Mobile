#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTranslator>

#include <memory>

class QSettings;

// Runtime Qt translator backed by the application's bundled UTF-8 catalog.
// No external translation tool is needed in the Android cross-toolchain.
class MobileCatalogTranslator final : public QTranslator
{
public:
    explicit MobileCatalogTranslator(QObject *parent = nullptr);
    bool isEmpty() const override;
    QString translate(const char *context, const char *sourceText,
                      const char *disambiguation = nullptr, int n = -1) const override;
    void setEntries(const QHash<QString, QString> &entries);

private:
    QHash<QString, QString> entries;
};

class LanguageSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(bool rightToLeft READ rightToLeft NOTIFY languageChanged)

public:
    explicit LanguageSettings(QObject *parent = nullptr,
                              const QString &settingsFile = QString());
    ~LanguageSettings() override;

    QString language() const;
    bool rightToLeft() const;
    Q_INVOKABLE bool setLanguage(const QString &code);

signals:
    void languageChanged();

private:
    void applyLanguage(const QString &code);

    std::unique_ptr<QSettings> settings;
    MobileCatalogTranslator translator;
    QHash<QString, QHash<QString, QString>> catalogs;
    QString selectedLanguage = QStringLiteral("en");
    Qt::LayoutDirection initialLayoutDirection = Qt::LeftToRight;
    bool translatorInstalled = false;
};
