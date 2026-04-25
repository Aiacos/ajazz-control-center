# Privacy Policy

_Last updated: 2026-04-25_

AJAZZ Control Center (ACC) is a desktop application maintained by **Aiacos**
under the GPL-3.0-or-later licence. We take privacy seriously and design
the application so that **all user data stays on your computer** unless you
explicitly opt in to a network-aware feature.

## Data we do NOT collect

ACC does **not** collect, transmit, or store any of the following on remote
servers:

- Your profiles, key bindings, macros, encoder mappings, or RGB layouts.
- Captured macro keystrokes / mouse events.
- The serial numbers, firmware versions, or descriptors of your devices.
- Application focus history (used by per-app profile auto-switch).
- Crash dumps, telemetry pings, usage statistics, or analytics events.

There is **no telemetry endpoint, no opt-out telemetry, no anonymous
metrics**. ACC does not contact any Aiacos-controlled server during normal
operation.

## Data we keep on your machine

The application stores the following on your local filesystem:

| Data                           | Location (Linux)                                                  |
| ------------------------------ | ----------------------------------------------------------------- |
| Profile JSON files             | `~/.local/share/AjazzControlCenter/profiles/`                     |
| Macro recordings               | `~/.local/share/AjazzControlCenter/macros/`                       |
| Brand / theme overrides        | `~/.config/AjazzControlCenter/branding.json`                      |
| Light/dark theme preference    | `QSettings` under `Aiacos/AjazzControlCenter`                     |
| Window state and layout        | `QSettings` under `Aiacos/AjazzControlCenter`                     |
| Autostart entry (when enabled) | `~/.config/autostart/io.github.Aiacos.AjazzControlCenter.desktop` |

On macOS and Windows the equivalent platform locations are used
(`~/Library/Application Support/...` and `%APPDATA%\Aiacos\...`).

## Optional network features

These features are off by default and only contact the network when you
trigger them explicitly:

- **Plugin marketplace** — when you click "Browse plugins" the app fetches
  a curated index from the public GitHub Releases of third-party plugin
  packs. No identifying information is sent.
- **Update check** — disabled by default. When enabled in Settings, ACC
  fetches the latest release tag from the public GitHub API
  (`api.github.com/repos/Aiacos/ajazz-control-center/releases/latest`).

## Hardware permissions

ACC requires direct access to the USB HID interfaces of supported devices.
This is granted via:

- **Linux** — udev rules dropped in `/etc/udev/rules.d/` by the installer.
- **macOS** — Input Monitoring permission (system prompt).
- **Windows** — driverless via Microsoft's bundled HID driver; no admin
  prompt required.

ACC reads only the report descriptors and feature reports of the AJAZZ
devices it knows about. It does not access other USB peripherals.

## Macro recorder

The macro recorder captures keyboard and mouse events while it is running.
Captured events are stored locally in the active profile and never leave
the machine. The recorder is **off by default**, must be started manually
from the UI, and stops automatically when you save the macro.

## Plugins

Third-party plugins run inside a sandboxed plugin host. Each plugin
declares its required capabilities up-front (`network`, `filesystem`,
`device-write`) and the user must approve them on installation. ACC does
not prevent plugins from sending data over the network if the user grants
that capability — please review plugin permissions before installing.

## Third-party services

ACC has zero default third-party services. If you install a plugin
that talks to a cloud service (e.g. OBS Studio, Home Assistant, Discord)
the plugin's own privacy policy applies and is presented at install time.

## Children's privacy

ACC is a generic peripheral configurator and does not target users under
the age of 13. We do not knowingly collect any data from anyone, including
minors.

## Contact

Questions or concerns: open a GitHub issue at
<https://github.com/Aiacos/ajazz-control-center/issues> or contact the
maintainers at the email address listed in
[`AUTHORS`](../AUTHORS).

## Changes to this policy

Substantive changes to this document will be announced in the in-app
changelog viewer (Help → Release Notes) and via a new GitHub release.
The "Last updated" date at the top of this document always reflects the
most recent revision.
