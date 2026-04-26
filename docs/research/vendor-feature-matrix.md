<!--
  vendor-feature-matrix.md — RE task 3 deliverable.

  Gap analysis: every user-facing feature of the AJAZZ first-party
  desktop apps, with our coverage status. Updated as recon (task 2)
  and our own implementation work progresses. Read
  `docs/research/README.md` for the clean-room rules.

  Cell legend:
    ✅ done       — fully implemented and shipped on the indicated platforms.
    🟡 partial    — basic path works, missing some features (column note explains).
    ❌ missing    — not implemented at all (column note links the issue or the TODO entry tracking the gap).
    ❓ unknown    — the vendor app exposes this but we have not yet captured / decoded it; recon pending.
    ⚪ n/a        — feature does not apply to this device class.

  Status of each row is set goal-backwards: only mark ✅ when the
  matching code in `src/devices/...` is shipped to `main`, not when
  a branch implements it. ❌ entries should always link a TODO entry
  so the gap is scheduled work, not orphan work.
-->

# Vendor feature matrix

Per-feature gap analysis: what the official AJAZZ desktop apps do,
versus what AJAZZ Control Center does. Aggregated across the
Stream Dock, keyboard and mouse product lines. **Read
[`docs/research/README.md`](README.md) before contributing** —
clean-room rules apply: every "vendor app does" claim must trace to
an observation captured in
[`vendor-protocol-notes.md`](vendor-protocol-notes.md), not to
re-typed vendor source.

> **Coverage today**: rows below are seeded from our existing
> `docs/_data/devices.yaml` and from the public AJAZZ feature
> sheets. The matrix is **scaffold-only** until USB / WebSocket
> captures (RE task 2) populate the "Vendor" column with verified
> behavior. Until then, the "Vendor" column reflects what the
> public marketing claims, NOT what the protocol shows. Update each
> row with a `[capture-id]` link once recon lands.

## Stream Dock — keys, encoders, screens

| Feature                                                | Vendor (claimed)                                                                                                                           | Our status                                                                                           | Tracking                                                                                                                 |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| Per-key LCD render (PNG / JPEG)                        | ✅ ships in stock app                                                                                                                      | ✅ done                                                                                              | AKP153 / AKP153E / AKP03 / AKP05 backends, JPEG 85×85 + PNG 72×72.                                                       |
| Multi-page profiles                                    | ✅                                                                                                                                         | 🟡 partial                                                                                           | Folder navigation lands via `OpenFolder` / `BackToParent` (`#28`); per-page assets store still scoped to single profile. |
| Encoder / dial bindings (cw / ccw / press)             | ✅ on AKP03, AKP05                                                                                                                         | ✅ done                                                                                              | `EncoderBinding` (`#29`) — three chains per encoder.                                                                     |
| Touch-strip events (AKP05)                             | ✅                                                                                                                                         | ✅ done                                                                                              | streamdeck/akp05 backend captures the report.                                                                            |
| Per-encoder display (AKP05 ring screen)                | ✅                                                                                                                                         | ✅ done                                                                                              | per-encoder screen I/O wired.                                                                                            |
| Per-key haptics                                        | ❓ — vendor SDK exposes a `streamdock/haptic` event but no AKP-class hardware in our catalogue surfaces it; AKP815 + AK980 PRO unverified. | ❌ missing                                                                                           | —                                                                                                                        |
| Lock-screen image                                      | ❓ vendor sets a still image when the host suspends.                                                                                       | ❌ missing                                                                                           | —                                                                                                                        |
| Hardware firmware update (over-the-app DFU)            | ✅ — vendor app downloads firmware blobs from `cdn1.key123.vip` and pushes them via a vendor-specific HID report.                          | ❌ missing                                                                                           | Track once captured.                                                                                                     |
| Live device-status overlay (e.g. CPU/RAM tile widgets) | ✅ via plugin                                                                                                                              | 🟡 partial — Plugin SDK in design (`docs/architecture/PLUGIN-SDK.md`); built-in widgets not shipped. | "Plugin SDK + Store" in TODO.                                                                                            |
| Stream-deck SDK-2 plugin compat                        | ✅ via Mirabox compat shim                                                                                                                 | 🟡 partial — manifest parses (M1-M4 of PI), wire bridge pending M5.                                  | "Property Inspector M5" in TODO.                                                                                         |
| OBS / Streamlabs / Discord triggers                    | ✅ via plugin                                                                                                                              | ❌ missing                                                                                           | Tracked under Plugin SDK.                                                                                                |

## Keyboards — RGB, macros, layers

> Captures pending: AK980 PRO is the only keyboard already
> enumerated in our HID layer. AK820 / AK820 Pro / AK680 Max are in
> the inventory and reachable via the proprietary backend (clean-
> room from VIA + USB capture work).

| Feature                                                       | Vendor (claimed)                   | Our status                                                                                                   | Tracking                                         |
| ------------------------------------------------------------- | ---------------------------------- | ------------------------------------------------------------------------------------------------------------ | ------------------------------------------------ |
| Per-key RGB (static / breathing / wave / custom curves)       | ✅ on every magnetic-switch model. | 🟡 partial — static + zone supported via VIA / proprietary. Custom curves (HE-style analog ramp) unverified. | Backlog under "AK980 PRO scaffold → functional". |
| Layer-aware keymap (Fn + dual-layer)                          | ✅                                 | ✅ done                                                                                                      | VIA backend.                                     |
| Macros (record + replay)                                      | ✅                                 | 🟡 partial — `MacroRecorder` scaffold (`#30`), no UI for assignment yet.                                     | "MacroRecorder UI" follow-up.                    |
| Hall-Effect analog actuation point (AK820 Max HE / AK680 Max) | ✅                                 | ❓ unknown                                                                                                   | —                                                |
| Bluetooth / 2.4 GHz / wired tri-mode toggle                   | ✅ on tri-mode SKUs.               | ❓ unknown — our backend speaks USB-HID only today.                                                          | —                                                |
| Battery level + charging status                               | ✅                                 | ❌ missing                                                                                                   | —                                                |
| Wireless dongle pairing flow                                  | ✅                                 | ❌ missing                                                                                                   | —                                                |
| Firmware update                                               | ✅                                 | ❌ missing                                                                                                   | —                                                |

## Mice — DPI, RGB, buttons

| Feature                                                | Vendor (claimed)            | Our status                                               | Tracking                                          |
| ------------------------------------------------------ | --------------------------- | -------------------------------------------------------- | ------------------------------------------------- |
| DPI stages (cycle on side button)                      | ✅ on every PAW-class SKU   | ✅ done                                                  | `IMouseCapable::setDpiStage(idx, stage)` (`#31`). |
| RGB backlight effects                                  | ✅                          | ✅ done                                                  | aj_series backend.                                |
| Polling rate selector (125 / 500 / 1000 / 4000 / 8000) | ✅ on AJ159 Pro / AJ199 Max | ❓ unknown                                               | —                                                 |
| Button remap / macros                                  | ✅                          | ❌ missing                                               | —                                                 |
| Lift-off distance                                      | ✅                          | ❌ missing                                               | —                                                 |
| Sensor calibration / sleep timeout                     | ✅                          | ❌ missing                                               | —                                                 |
| Wireless tri-mode (USB-C / 2.4 GHz / BT)               | ✅ on tri-mode SKUs         | ❌ missing — we currently expose only the USB transport. | —                                                 |
| Battery level + charging status                        | ✅                          | ❌ missing                                               | —                                                 |
| Firmware update                                        | ✅                          | ❌ missing                                               | —                                                 |

## Vendor-app Qt module dependencies (architectural parity)

Surfaced by the static-analysis pass on the Stream Dock AJAZZ Mac
bundle (`static-2026-04-26-streamdock-mac-001` in
[`vendor-protocol-notes.md`](vendor-protocol-notes.md)). Each row
is a Qt module the vendor links that we either also link, or do
NOT link — making the row a parity gap.

| Qt module                                                                                         | Vendor links | We link                              | Implication                                                                                                                                                             |
| ------------------------------------------------------------------------------------------------- | ------------ | ------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `QtCore`, `QtGui`, `QtWidgets`, `QtNetwork`, `QtQml`, `QtQuick`, `QtSvg`, `QtConcurrent`, `QtXml` | ✅           | ✅                                   | Aligned — same baseline.                                                                                                                                                |
| `QtWebEngineCore`, `QtWebEngineWidgets`, `QtWebChannel`                                           | ✅           | ✅ (gated by `AJAZZ_HAVE_WEBENGINE`) | The vendor uses the same Property Inspector embedding mechanism we ship in M1-M4. PI HTML pages should run in our embedding modulo the host-side method surface.        |
| `QtWebSockets`                                                                                    | ✅           | ❌ — pending plugin host wire        | Validates the design in `docs/architecture/PLUGIN-SDK.md`. Wire up alongside the Plugin process spawner / WebSocket bridge milestones in TODO.md.                       |
| `QtSerialPort`                                                                                    | ✅           | ❌                                   | New gap. Most likely a USB-CDC bootloader handoff for firmware update. Track as a recon target for the firmware-update flow (`vendor-techniques.md` § 2).               |
| `QtMultimedia`, `QtMultimediaWidgets`                                                             | ✅           | ❌                                   | New gap. Likely "play sound" actions and / or audio-level mapping (the AKP153 manual mentions per-button audio levels). Feeds the action-engine action-kind backlog.    |
| `QtPdf`                                                                                           | ✅           | ❌                                   | Lower priority. Possibly in-app help / changelog rendering or PDF profile export. Confirm via runtime capture before duplicating.                                       |
| `QtVirtualKeyboard`                                                                               | ✅           | ❌                                   | Probably bundled by `macdeployqt` defaults, not actively used. Confirm by searching the vendor app's Resources for QML on-screen keyboard usage in a future recon pass. |
| `QtPrintSupport`                                                                                  | ✅           | ❌                                   | Probably print-to-paper export of profile sheets. Niche feature, not blocking.                                                                                          |
| `QtCore5Compat`                                                                                   | ✅           | ❌                                   | Indicates the vendor codebase migrated from Qt 5 → Qt 6 without dropping the compat module. No protocol implication; we are Qt-6-native and should stay there.          |
| `QtPositioning`, `QtOpenGL`, `QtDBus`, `QtQmlModels`, `QtQuickWidgets`                            | ✅           | ⚪ partial / framework-default       | Likely auto-bundled by `macdeployqt` rather than actively used. No action needed.                                                                                       |

## Cross-cutting infrastructure

| Capability                                       | Vendor (claimed)                                          | Our status                                                                                                                        | Tracking                                             |
| ------------------------------------------------ | --------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| Single-instance gate (re-raise on second launch) | ✅                                                        | ✅ done                                                                                                                           | `SingleInstanceGuard` (`f7c4847`).                   |
| System-tray indicator                            | ✅                                                        | ✅ done                                                                                                                           | `TrayController`.                                    |
| Autostart on login                               | ✅ on Windows; macOS via `launchd`.                       | ✅ done                                                                                                                           | `AutostartService` (Linux + macOS + Windows; `#35`). |
| Profile import / export                          | ✅ as `.streamdockprofile`                                | ✅ done                                                                                                                           | `.ajazzprofile` bundle (`#32`).                      |
| Plugin Store (in-app catalogue + install)        | ✅ via `space.key123.vip/StreamDock/plugins`.             | 🟡 partial — UI ships in `PluginStore.qml`; live catalogue fetcher (`StreamdockCatalogFetcher`) ships, lifecycle manager pending. | "Plugin SDK + Store" in TODO.                        |
| Crash reporting / telemetry                      | ✅ (opt-out)                                              | ❌ missing — iceboxed (no opt-in).                                                                                                | —                                                    |
| In-app changelog viewer                          | ✅                                                        | ✅ done                                                                                                                           | `BrandingService::changelogText()` (`#37`).          |
| Light / dark theme switch                        | 🟡 vendor app uses a single theme aligned to OS dark mode | ✅ done                                                                                                                           | `ThemeService` (`6072265`).                          |
| Per-OS sandboxed plugin host                     | ❌ — vendor runs plugins in-process.                      | 🟡 in design (`A4 — PluginHost out-of-process` in TODO).                                                                          | A4.                                                  |

## How to update this matrix

1. When you ship a feature whose row says ❌ or 🟡, change the cell
   to ✅ or 🟡 in the same PR that ships the feature, with the
   commit SHA appended to the "Tracking" column.
1. When recon (RE task 2) confirms or refutes a vendor "claimed"
   capability, change the "Vendor" column from ❓ to ✅ / ❌ and
   link the capture id.
1. When a new device family is added to `docs/_data/devices.yaml`,
   add the rows here at the same time so coverage gaps are visible
   from day one.
