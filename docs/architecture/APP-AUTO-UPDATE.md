# App auto-update — architecture

This document records the design decision for how AJAZZ Control
Center notifies users that a new version of **the app** is available.
This is distinct from [FIRMWARE-UPDATES.md](FIRMWARE-UPDATES.md) which
covers device-firmware updates.

Status: **DECIDED 2026-05-18**. Scaffold landing in a follow-up commit.

## Decision

**Notify-only, GitHub-Releases-driven, Windows + macOS only.** Linux
distributions delegate entirely to their package manager (Flatpak ↔
Flathub, `.deb` ↔ `apt`, `.rpm` ↔ `dnf`).

Pattern adopted: **KeePassXC's `UpdateChecker`** — `QNetworkAccessManager`
hits `https://api.github.com/repos/Aiacos/ajazz-control-center/releases[/latest]`,
parses JSON, compares semver, opens a release-notes banner. The user
clicks "Download" → `QDesktopServices::openUrl(...)` → OS browser →
OS-native installer.

The opposite patterns — **Sparkle / WinSparkle delta updates** (OBS,
Nextcloud), or **Velopack-style replace-pipeline** — are rejected
for v1 because:

1. We're a one-maintainer alpha; the silent-install bug surface is
   the largest in any auto-update system.
2. QtSparkle is unmaintained (last commit 2019).
3. Sparkle.framework needs Obj-C wrapping for Qt; WinSparkle is
   Win-only; neither covers Linux.
4. Our existing GitHub Releases pipeline is already the source of
   truth — no need for a separate appcast server.

If we outgrow notify-only later, the path forward is to plug in
**Sparkle on macOS** + **WinSparkle on Windows** behind the same
`AppUpdateService` API. That migration is incremental, not a rewrite.

## What we ship (MVP)

### `AppUpdateService` QML singleton

Mirrors the shape of `LightingService` / `BatteryService` /
`SettingsService` already in the app.

- `Q_PROPERTY` exposed to QML:
  - `bool autoCheckEnabled` — default `true`. User can disable in
    Settings.
  - `bool includeNightly` — default `false`. When `true`, the API
    query also considers `prerelease == true` releases.
  - `QString currentVersion` — read from `AJAZZ_PRODUCT_VERSION` at
    compile time (already set in `CMakeLists.txt`).
  - `QString latestVersion` — what we found upstream; empty until the
    first successful check.
  - `QString latestReleaseNotes` — markdown body of the latest
    release, rendered in the banner.
  - `QUrl latestReleaseUrl` — link to the GitHub release page; what
    the "Download" button opens.
  - `enum Status { Idle, Checking, UpToDate, UpdateAvailable, Error }` —
    drives the banner visibility.

- `Q_INVOKABLE`:
  - `void checkNow()` — manual trigger from a Settings button.
  - `void dismissCurrentUpdate()` — user clicked "Later"; we suppress
    the banner until the next launch.

- Auto-check cadence: **24 hours** + a **5 s delay after first
  launch** so we don't block the splash. Pattern stolen from
  KeePassXC's `Config::GUI_CheckForUpdatesNextCheck`.

### GitHub Releases query

- Endpoint: `GET /repos/Aiacos/ajazz-control-center/releases/latest`
  (returns 1 record with `tag_name`, `body`, `html_url`,
  `assets[].browser_download_url`, `prerelease`).
- When `includeNightly == true`, also query `GET /releases/tags/nightly`
  (our rolling pre-release tag per `.github/workflows/nightly.yml`)
  and surface whichever is newer.
- Use `If-None-Match` (ETag) on subsequent polls to stay inside the
  60 req/h unauthenticated rate limit.
- Honor `X-RateLimit-Reset` on 403; back off until the timestamp.

### Per-OS gating

The check runs on Linux + Windows + macOS. The **download UX** differs:

| OS / Format          | What we do                                                                                                                                              |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **macOS .dmg**       | Banner shows "Update X.Y.Z available". "Download" → open release page in browser. v2: integrate Sparkle for in-place install.                            |
| **Windows .msi**     | Same as macOS. v2: integrate WinSparkle.                                                                                                                |
| **Linux .flatpak**   | **Self-disable**: detect `qEnvironmentVariableIsSet("FLATPAK_ID")` and return early from `checkNow()`. Flathub manages updates via GNOME Software/KDE Discover.    |
| **Linux .deb/.rpm**  | Banner shows "Update available — please run `sudo apt upgrade` (or your system updater)". No download attempt.                                            |
| **Source build**     | Detect via `AJAZZ_BUILT_FROM_SOURCE=ON` CMake option (off by default). When ON, banner says "Update available — git pull + rebuild".                       |

### UI surface

- **Banner** at the top of the main window when `Status == UpdateAvailable`.
  Two buttons: "Open release page" (primary, opens browser) +
  "Later" (dismisses).
- **Settings → About** section: 3 elements:
  1. `SwitchDelegate` "Check for updates on launch" bound to
     `autoCheckEnabled`.
  2. `SwitchDelegate` "Include pre-release / nightly builds" bound to
     `includeNightly`.
  3. `Button` "Check now" → `AppUpdateService.checkNow()`.

### Signing posture

- v1: trust GitHub TLS chain only. We already publish `SHA256SUMS`
  + SLSA Sigstore attestation per `.github/workflows/release.yml` —
  users can verify manually via `gh attestation verify`.
- v2: ship a detached `.minisig` next to each artifact, embed the
  public key under `resources/release-keys/ajazz-release.pub`. The
  service verifies before opening the OS installer.

## Non-goals (avoid scope creep)

| Non-goal                                              | Why                                                                                                                            |
| ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| **Delta updates** (binary patching)                    | Veloren's Airshipper still doesn't have these after 5 years of trying. Save the complexity.                                    |
| **Silent download + auto-install**                    | Data-loss risk during an active edit session. Standard FOSS practice (OBS, Audacity, KeePassXC) is consent-gated.              |
| **Appcast XML server**                                 | GitHub Releases JSON is the canonical source of truth. No separate infrastructure to maintain.                                 |
| **Auto-restart + apply on exit**                       | v2 if we ever integrate WinSparkle/Sparkle. Not v1.                                                                            |
| **Linux .deb/.rpm in-app update**                      | Requires `sudo` — bad UX. Use system updater.                                                                                  |
| **Flatpak/Snap/distro override**                       | Sandboxed; `flatpak update` from inside the sandbox is anti-pattern. GNOME Software / KDE Discover own that flow.              |
| **In-app installer launcher** (macOS DMG mount + drag) | v2. v1 just opens the release page; user does the drag.                                                                        |
| **Cross-channel switching** (stable ↔ nightly mid-session) | User edits `includeNightly` in Settings, next check picks it up. No live re-query in the same session.                         |

## Privacy

- The HTTP request to `api.github.com` reveals the user's IP +
  approximate version cadence to GitHub. Acceptable for an
  open-source app — same posture as KeePassXC / Audacity / OBS.
- **No telemetry pings.** The check is purely "is there a newer tag";
  we send no user identifier, no platform breakdown, no enabled-feature
  list.
- `autoCheckEnabled = false` in Settings turns off ALL outbound
  network from this service. The manual "Check now" button still
  works, but only when clicked.

## Comparison with peer FOSS Qt apps

| Project              | Linux                   | Windows                          | macOS                | Channels                 | Notes                                                                                       |
| -------------------- | ----------------------- | -------------------------------- | -------------------- | ------------------------ | ------------------------------------------------------------------------------------------- |
| **KeePassXC**        | distro / Snap / AppImage | GitHub Releases API + notify     | same                 | stable / beta toggle     | Single Qt6 impl, no Sparkle. **Our pattern.**                                                |
| **OBS Studio**       | Flatpak / PPA / AUR     | Custom delta-patch updater + UAC | Sparkle              | stable / beta / RC       | Heaviest. Has its own manifest server + binary-patch toolchain.                              |
| **Audacity**         | distro                  | wxWidgets banner + browser open  | same                 | alpha / beta / stable    | wx not Qt but pattern identical.                                                            |
| **Nextcloud Desktop**| OCUpdater notify-only   | MSI fetch + Authenticode + relaunch | Sparkle              | server-side branching    | Two-layer dispatch. Reference for "v2 with native installers".                              |
| **qBittorrent**      | (no code path)          | Notify + browser                 | same                 | regex on version string  | Hard `#ifdef Q_OS_WIN \|\| Q_OS_MACOS`. Linux never checks.                                  |
| **Krita / Inkscape** | distro                  | (no in-app updater)              | (none)               | N/A                      | Punt entirely.                                                                              |

We sit closest to KeePassXC + qBittorrent: a small `UpdateChecker`
QObject that hits GitHub Releases and opens the browser on consent.

## Implementation map

When the scaffold lands:

- `src/app/src/app_update_service.{hpp,cpp}` — new QML singleton
- `src/app/qml/components/UpdateBanner.qml` — new banner widget
- `src/app/qml/SettingsPage.qml` — add the 3-element About-section
- `src/app/src/application.{hpp,cpp}` — construct + register the
  service (mirrors LightingService / SettingsService)
- `tests/unit/test_app_update_service.cpp` — unit tests for the
  semver compare + Flatpak self-disable + asset-name OS detection

The full scope is ~600 LOC + ~150 LOC of tests. ETA one commit.

## When this revisits

- **AJAZZ Control Center hits v1.0 stable** → consider integrating
  Sparkle on macOS + WinSparkle on Windows for in-place install.
- **A user files an issue asking for delta updates** → cost/benefit
  re-check; almost certainly still defer.
- **Flathub publishes the project** → ensure the Flatpak self-disable
  gate actually works (test under flatpak-spawn).
- **GitHub Releases API rate limits become a problem** → revisit
  with an authenticated token in a small Cloudflare Worker proxy.

______________________________________________________________________

## References

Source material from the 2026-05-18 research agents:

### Per-OS canonical mechanisms

- [Flathub maintenance docs](https://docs.flathub.org/docs/for-app-authors/maintenance/)
- [Flatpak distributing applications](https://docs.flatpak.org/en/latest/distributing-applications.html)
- [AppImage update mechanisms](https://docs.appimage.org/packaging-guide/optional/updates.html)
- [winget upgrade command](https://learn.microsoft.com/en-us/windows/package-manager/winget/upgrade)
- [microsoft/winget-cli auto-update issue #6146](https://github.com/microsoft/winget-cli/issues/6146)
- [Sparkle](https://sparkle-project.org/)
- [WinSparkle](https://github.com/vslavik/winsparkle)
- [Velopack](https://github.com/velopack/velopack)
- [GitHub REST rate limits](https://docs.github.com/en/rest/using-the-rest-api/rate-limits-for-the-rest-api)

### Peer-app source pointers

- [KeePassXC UpdateChecker.cpp](https://github.com/keepassxreboot/keepassxc/blob/develop/src/networking/UpdateChecker.cpp) — our reference implementation
- [OBS feature-sparkle.cmake](https://github.com/obsproject/obs-studio/blob/master/frontend/cmake/feature-sparkle.cmake)
- [OBS AutoUpdateThread.cpp](https://github.com/obsproject/obs-studio/blob/master/frontend/utility/AutoUpdateThread.cpp)
- [OBS updater binary](https://github.com/obsproject/obs-studio/tree/master/frontend/updater)
- [Audacity UpdateManager.cpp](https://github.com/audacity/audacity/blob/master/au3/src/update/UpdateManager.cpp)
- [Nextcloud Desktop updater](https://github.com/nextcloud/desktop/tree/master/src/gui/updater)
- [qBittorrent programupdater.cpp](https://github.com/qbittorrent/qBittorrent/blob/master/src/gui/programupdater.cpp)

### Qt-native libraries considered

- [QtSparkle (unmaintained)](https://github.com/davidsansome/qtsparkle)
- [flaviotordini/updater](https://github.com/flaviotordini/updater)
- [Skycoder42/QtAutoUpdater](https://github.com/Skycoder42/QtAutoUpdater)
- [Qt Installer Framework](https://doc.qt.io/qtinstallerframework/index.html)

### Licensing

- [GPL-3.0 vs App Store (FSF)](https://www.fsf.org/blogs/licensing/more-about-the-app-store-gpl-enforcement)
- [Sparkle EdDSA docs](https://sparkle-project.org/documentation/publishing/)
