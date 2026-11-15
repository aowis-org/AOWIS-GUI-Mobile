#pragma once

#include "wireguard_key_sealing.h"

// The Android Keystore as WireGuard secret sealer (Java KeystoreSealer,
// AES-256-GCM, key kept in the Keystore and excluded from backups). Empty
// functions on other platforms: secrets stay in plain text there, in the
// user's private configuration directory, as on the desktop GUI.
namespace AndroidKeystoreSealer
{
WireGuardKeySealing::Seal seal();
WireGuardKeySealing::Unseal unseal();
}
