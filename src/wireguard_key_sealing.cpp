#include "wireguard_key_sealing.h"

#include <QRegularExpression>
#include <QStringList>

namespace
{

// key = value, the value ending at blanks or a '#' comment (wg-quick syntax).
const QRegularExpression &secretLine()
{
    static const QRegularExpression pattern(
        QStringLiteral("^(\\s*(?:PrivateKey|PresharedKey)\\s*=\\s*)([^\\s#]+)"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// Applies change(value) to each secret value; false when change fails.
bool rewrite(const QString &text, const std::function<bool(const QString &, QString *)> &change, QString *out)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    bool all = true;
    for (QString &line : lines)
    {
        const QRegularExpressionMatch match = secretLine().match(line);
        if (!match.hasMatch())
            continue;
        QString replaced;
        if (!change(match.captured(2), &replaced))
        {
            all = false;
            continue;
        }
        line = match.captured(1) + replaced + line.mid(match.capturedEnd(2));
    }
    *out = lines.join(QLatin1Char('\n'));
    return all;
}

bool anySecret(const QString &text, bool sealed)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines)
    {
        const QRegularExpressionMatch match = secretLine().match(line);
        if (match.hasMatch() && match.captured(2).startsWith(WireGuardKeySealing::sealedPrefix()) == sealed)
            return true;
    }
    return false;
}

} // namespace

namespace WireGuardKeySealing
{

QString sealedPrefix()
{
    return QStringLiteral("aowis-keystore-v1:");
}

bool hasPlainSecrets(const QString &text)
{
    return anySecret(text, false);
}

bool hasSealedSecrets(const QString &text)
{
    return anySecret(text, true);
}

bool sealSecrets(const QString &text, const Seal &seal, QString *out)
{
    QString result;
    const bool ok = rewrite(text, [&seal](const QString &value, QString *replaced) {
        if (value.startsWith(sealedPrefix()))
        {
            *replaced = value;
            return true;
        }
        *replaced = seal(value);
        return replaced->startsWith(sealedPrefix());
    }, &result);
    if (ok)
        *out = result;
    return ok;
}

bool unsealSecrets(const QString &text, const Unseal &unseal, QString *out)
{
    return rewrite(text, [&unseal](const QString &value, QString *replaced) {
        if (!value.startsWith(sealedPrefix()))
        {
            *replaced = value;
            return true;
        }
        return unseal(value, replaced) && !replaced->isEmpty();
    }, out);
}

} // namespace WireGuardKeySealing
