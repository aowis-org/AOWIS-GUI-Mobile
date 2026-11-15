#pragma once

#include <QString>

#include <functional>

// The secrets of a stored WireGuard .conf (PrivateKey and PresharedKey
// values) as sealed tokens, everything else unchanged: names, addresses,
// endpoints, comments and line ends stay readable in AOWIS-CONFIG. On
// Android the sealer is the Android Keystore (android_keystore_sealer.h).
namespace WireGuardKeySealing
{

// Prefix of a sealed value ("aowis-keystore-v1:..."); never a valid key.
QString sealedPrefix();

// seal(plain) returns the sealed token or an empty string on failure.
using Seal = std::function<QString(const QString &plain)>;
// unseal(token, &plain) returns false when the token cannot be opened.
using Unseal = std::function<bool(const QString &token, QString *plain)>;

// True when a PrivateKey/PresharedKey value is stored in plain text.
bool hasPlainSecrets(const QString &text);
// True when the text contains sealed values.
bool hasSealedSecrets(const QString &text);
// Seals every plain secret value. False (out untouched) if one cannot be
// sealed: a secret is then never stored in plain text instead.
bool sealSecrets(const QString &text, const Seal &seal, QString *out);
// Opens every sealed value. False if one cannot be opened (another device,
// app reinstalled, damaged); *out is then the text with the tokens left in.
bool unsealSecrets(const QString &text, const Unseal &unseal, QString *out);

} // namespace WireGuardKeySealing
