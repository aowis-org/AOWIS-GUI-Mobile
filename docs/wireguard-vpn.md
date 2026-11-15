# WireGuard VPN

Settings → **WireGuard VPN** manages WireGuard profiles for servers that are
only reachable over a VPN. Tunnels run inside AOWIS: AOWIS-NETWORK's
userspace engine (wireguard-go with gVisor's netstack) behind an
authenticated loopback SOCKS5 proxy. There is no Android `VpnService`, no
system VPN icon, and nothing else on the phone is routed through a tunnel.

## Profiles

- **Add profile** opens the editor: a name, an optional description and the
  configuration text (wg-quick `.conf`, pasted). **Import .conf file…** opens the system file
  picker and puts the file into the same editor for review before saving.
- The editor checks the text live with AOWIS-NETWORK's parser, the same one
  the desktop GUI and the engine use: errors and warnings are shown with
  line numbers, translated, never quoting keys or values. Supported: one
  `[Interface]` (PrivateKey, one Address, DNS, MTU) and one `[Peer]`
  (PublicKey, PresharedKey, Endpoint, AllowedIPs, PersistentKeepalive).
  `PostUp`/`PreDown` and similar hooks are ignored with a warning and never
  run.
- Profiles are stored with **AOWIS-CONFIG** (`WireGuardProfiles`, collection
  `wireguard`) in the owner root `gui-mobile`, like the desktop GUI's `gui`
  root: on Android below the app's private configuration directory
  (`QStandardPaths::AppConfigLocation`/aowis/gui-mobile, as CONFIG requires
  there), on desktop Linux `~/.config/aowis/gui-mobile`. CONFIG keeps the
  exact text plus name and description, revision-checks every update and
  delete (a change from elsewhere is a conflict, never overwritten), and
  reports damaged records instead of failing the whole list.
- **Secrets on Android are sealed with the Android Keystore:** the values of
  `PrivateKey` and `PresharedKey` are stored as
  `aowis-keystore-v1:<base64url(IV ‖ ciphertext ‖ tag)>` (AES-256-GCM, the
  prefix authenticated; `android/src/org/aowis/mobile/KeystoreSealer.java`);
  everything else in the `.conf` stays readable. The Keystore key never
  leaves the Keystore and is not part of backups. If sealing fails the
  profile is not saved (never a plain fallback). Profiles stored in plain
  text earlier are sealed on the next start. On desktop Linux secrets stay
  plain in `~/.config/aowis/gui-mobile` (private directory), as in the GUI.
- **Locked profiles:** when sealed values cannot be opened (app reinstalled,
  backup restored to another phone, Keystore reset) the profile is shown
  with a notice and never started; its tunnel consumers (the map server)
  are blocked with the same reason. Edit it and paste the keys again, or
  import it again. Tests: `tests/test_android_keystore_sealer.sh` (the Java
  sealer on the host JDK, stub Android classes) and
  `aowis-mobile-wireguard-settings` (only secret values change, migration,
  locked profiles, no plain fallback).
- Deletion goes through CONFIG's reference guard: the profile the map server
  uses cannot be deleted (the page says "Used by: Map server").
- Saving a changed configuration stops that profile's running tunnel.
  Deleting a profile stops its tunnel and removes its keys from the device.

## Status and Start/Stop

Selecting a profile shows its state, last handshake, traffic (largest
binary unit, three significant digits) and the resolved peer endpoint,
refreshed every second while a tunnel runs. **Start tunnel** starts the
engine off the GUI thread (the endpoint's host name may need resolving);
**Stop tunnel** stops it, also while it is still starting. States:
Stopped · Starting… · Running, waiting for the first handshake · Connected ·
Last handshake older than 180 s (check endpoint, keys, network) · Could not
start (engine message).

Tunnels keep running while AOWIS is in the background and stop when it
exits.

## Map server through WireGuard

Settings → Network & servers → Map server → **Connection**: *Direct (no VPN)* or one of the
WireGuard profiles (saved with the map server's URL and port, as
`mapServer/wireguardProfile`). With a profile:

- The map view requires that profile's tunnel; it starts on demand (no
  Start needed), is retried after 5, 10, 20, 40, then every 60 seconds while
  it fails, and stops again when no longer needed (unless started by hand).
- Tiles, terrain and the provider catalog use the tunnel's authenticated
  SOCKS5 proxy (host names resolved inside the tunnel) on their own
  QNetworkAccessManagers; nothing else in the app is proxied.
- **Fail closed:** while the tunnel is not running nothing is requested,
  never directly. The map shows why (starting, failed and retrying, stopped
  by the user, profile missing) with a tap through to WireGuard VPN.
- **Stop tunnel** is respected: a stopped profile stays stopped also for the
  map server until it is started again (or AOWIS restarts).
- A changed configuration of the used profile restarts its tunnel and the
  map reconnects through the new one.
- URL, port and profile change together: property changes of one event loop
  pass are coalesced, so the URL is never used before its profile is known.

## Build

`AOWIS_MOBILE_WITH_WIREGUARD` (default ON) links AOWIS-NETWORK; see
[dependencies](dependencies.md) for the Android engine library. With it OFF
the Settings button is hidden.

Tests: `aowis-mobile-wireguard-settings` (`./compile_linux.sh --tests`)
covers diagnostics, profile persistence, name rules, file import, the
traffic formatting and a real engine start/stop against a peer that never
answers.
