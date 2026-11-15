# WireGuard VPN

Settings → **WireGuard VPN** manages WireGuard profiles for servers that are
only reachable over a VPN. Tunnels run inside AOWIS: AOWIS-NETWORK's
userspace engine (wireguard-go with gVisor's netstack) behind an
authenticated loopback SOCKS5 proxy. There is no Android `VpnService`, no
system VPN icon, and nothing else on the phone is routed through a tunnel.

## Profiles

- **Add profile** opens the editor: a name and the configuration text
  (wg-quick `.conf`, pasted). **Import .conf file…** opens the system file
  picker and puts the file into the same editor for review before saving.
- The editor checks the text live with AOWIS-NETWORK's parser, the same one
  the desktop GUI and the engine use: errors and warnings are shown with
  line numbers, translated, never quoting keys or values. Supported: one
  `[Interface]` (PrivateKey, one Address, DNS, MTU) and one `[Peer]`
  (PublicKey, PresharedKey, Endpoint, AllowedIPs, PersistentKeepalive).
  `PostUp`/`PreDown` and similar hooks are ignored with a warning and never
  run.
- Profiles are stored verbatim in the app's private settings
  (`wireguard/profiles` in `AOWIS-GUI-Mobile` QSettings; on Android inside
  the app's data directory). **The private key is stored unencrypted there**,
  as on the desktop; Android Keystore encryption is not implemented yet.
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

No app connection uses a tunnel yet; starting one verifies the handshake
with the server. The map server connection is next (roadmap). Tunnels keep
running while AOWIS is in the background and stop when it exits.

## Build

`AOWIS_MOBILE_WITH_WIREGUARD` (default ON) links AOWIS-NETWORK; see
[dependencies](dependencies.md) for the Android engine library. With it OFF
the Settings button is hidden.

Tests: `aowis-mobile-wireguard-settings` (`./compile_linux.sh --tests`)
covers diagnostics, profile persistence, name rules, file import, the
traffic formatting and a real engine start/stop against a peer that never
answers.
