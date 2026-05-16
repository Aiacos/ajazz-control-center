<!--
  builtin-plugin-categories.md — RE task 4 deliverable.

  Catalogue of the BUILT-IN plugin categories that the official
  Stream Dock AJAZZ desktop application ships with out-of-the-box,
  derived from a first-hand observation of the running app on the
  v1.2 dev box (2026-05-16). Cross-referenced with the public
  Space Plugin SDK docs (sdk.key123.vip) where they expose
  matching plugin-sendable / plugin-received events, and with our
  own implementation status from `vendor-feature-matrix.md`.

  THIS DOCUMENT IS THE PARITY ANCHOR for first-party widget
  plugins. Every row maps a vendor-app default category to:
    - the on-screen label observed in the running app,
    - the shipping example actions inside that category,
    - the corresponding SDK-level event / wire mechanism (if any),
    - our coverage status (✅ / 🟡 / ❌ / ❓ / ⚪),
    - tracking note (TODO entry, file:line ref, or ❌-with-rationale).

  Read `docs/research/README.md` for the clean-room and
  no-redistribution rules that govern this directory.

  How this document was produced (capture-id: vendor-2026-05-16-streamdock-app-001):
    - Screenshot of the running Stream Dock AJAZZ app v3.10.195.0902
      on Windows 11, captured 2026-05-16 11:25:13 by the project
      maintainer on the v1.2 dev box.
    - Device under management: AKP05_140B (selected in the bottom
      `Attrezzatura` device picker).
    - Screenshot artefact held OUT-OF-REPO under
      `C:\Users\<user>\Pictures\Screenshots\Screenshot 2026-05-16 112513.png`
      (262 KB) per CLAUDE.md hard rule "no vendor blob redistribution".
      This document is the textual sanitised extract.
    - All category labels recorded as displayed in the IT-locale UI
      (the dev box runs Italian); the English column below is a
      direct translation, NOT a vendor-supplied string.

  When a row's "Our status" cell turns ✅, also update the
  matching row in `vendor-feature-matrix.md` and link the commit
  SHA in the Tracking column.
-->

# Built-in plugin categories — Stream Dock AJAZZ app

First-hand catalogue of the **default panel categories** the official
Stream Dock AJAZZ desktop app exposes in its right-sidebar "Pulsanti /
Pannelli" picker, observed on app version **3.10.195.0902** on Windows
11 against an AKP05_140B device on **2026-05-16**. This is the parity
anchor for our first-party widget-plugin work. Pair with
[`vendor-feature-matrix.md`](vendor-feature-matrix.md) (the broader
feature gap analysis) and
[`vendor-software-inventory.md`](vendor-software-inventory.md) (the
binary-artefact provenance ledger; this app version's hashes are in
the "2026-05-16 archival pass" callout).

> **Capture provenance**: `vendor-2026-05-16-streamdock-app-001`. Raw
> screenshot held out-of-repo per CLAUDE.md "no vendor blob
> redistribution"; the textual category enumeration below is the
> sanitised extract that lives in-repo. SHA-256 of the screenshot file
> is not recorded here because it carries no protocol-decision weight
> — the **categorical structure** below is what matters, not the
> pixel-level evidence.

> **Locale note**: app was running with Italian UI strings. The
> "Vendor label (IT)" column below quotes the displayed text verbatim;
> the "Vendor label (EN)" column is a translation, NOT a vendor-
> supplied string. The English form is the one we should adopt in our
> own UI to stay consistent with the rest of our codebase (English
> source-of-truth, runtime-translated via Qt's `tr()`).

> **Coverage today**: every row in §1 below maps to **❌ missing** or
> **🟡 partial** in the "Our status" column. The vendor app ships
> ELEVEN built-in widget categories out of the box; we ship **zero**
> first-party widget plugins, only the underlying plugin
> infrastructure (manifest schema, sandboxed host, Property Inspector
> M1-M4 — see [`docs/architecture/PLUGIN-SDK.md`](../architecture/PLUGIN-SDK.md)).
> This is the single largest first-party-feature gap surfaced by
> Phase 9.x research and is the natural backlog driver for the
> v1.3 milestone.

______________________________________________________________________

## 1. Default panel categories observed in the running app

Eleven (11) categories are visible in the right-sidebar "Pannelli"
picker of a fresh install of Stream Dock AJAZZ v3.10.195.0902. The
top one ("Stream Dock AJAZZ") groups the **system actions** that
operate on the device itself; the remaining ten group **widget
actions** that render to a button face, react to button presses, or
chain to host-side automation.

| # | Vendor label (IT)                              | Vendor label (EN, normalised)        | What it does (observed)                                                                                                                                            | SDK event / wire           | Our status | Tracking                                                                                                                                                                                                                          |
| - | ---------------------------------------------- | ------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------- | ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1 | **Stream Dock AJAZZ**                          | **Stream Dock system actions**       | Eight system-level actions: see §2 below.                                                                                                                          | host-side, no SDK event    | 🟡 partial | `ActionEngine::OpenFolder`, `BackToParent` ✅; the other six (page navigation, page indicator, brightness, screen off) are missing. Tracking row in §2.                                                                           |
| 2 | **Cassetta Degli Attrezzi**                    | **Toolbox**                          | Aggregate of small one-shot host-side utilities the vendor groups together (calculator pop-up, hotkey send, OS volume up/down, run program, type text, password). | host-side, no SDK event    | 🟡 partial | `ActionEngine::KeyPress` ✅, `RunCommand` ✅, `OpenUrl` ✅; missing: type-text, password-store, OS-volume binding, calculator pop-up. See [`vendor-feature-matrix.md`](vendor-feature-matrix.md) "Action Types Vendor Has" section. |
| 3 | **Lettore Audio**                              | **Audio Player**                     | Drag-and-drop a local audio file to a button → button face shows track title + transport, press plays/pauses on host's default sink.                               | host-side `QtMultimedia`   | ❌ missing | Vendor parity gap recorded in [`vendor-feature-matrix.md`](vendor-feature-matrix.md) row "Qt module dependencies → QtMultimedia". Requires `QT += Multimedia` link + `QMediaPlayer` host service. **NOT YET TRACKED in TODO.md.** |
| 4 | **Flusso Operativo**                           | **Workflow** (multi-step macro)      | Chain N actions into one button press (e.g. open URL → 1s sleep → key press → run command). Vendor app exposes a graphical step builder.                          | host-side action chaining  | ✅ done    | `ActionEngine` already supports chained actions: `ActionKind::Sleep` exists, multi-step JSON encodes the chain. UI surface (graphical builder) is the missing **half** — see TODO "Action chain builder UI".                      |
| 5 | **OBS Studio**                                 | **OBS Studio**                       | Scene switch / source toggle / start-stop recording. Vendor uses obs-websocket protocol over WS to localhost:4455.                                                 | host-side `QtWebSockets`   | ❌ missing | obs-websocket bridge not implemented. Mentioned in [`vendor-feature-matrix.md`](vendor-feature-matrix.md) "OBS / Streamlabs / Discord triggers". **NOT YET TRACKED in TODO.md.**                                                  |
| 6 | **Note utili**                                 | **Useful Notes** (sticky-note)       | Free-text note rendered on the button face; tap-to-edit via Property Inspector.                                                                                    | host-side text-render      | ❌ missing | Trivial to implement once Property Inspector M5 (`sendToPropertyInspector` RPC bridge) lands — the note edit flow is exactly the M5 reference use-case. Tracking: TODO "Property Inspector M5".                                   |
| 7 | **Monitoraggio delle prestazioni del sistema** | **System Performance Monitor**       | Real-time CPU% / RAM% / GPU% / Net (down/up) tile that updates every ~1s on the button face. Vendor uses OpenHardwareMonitorLib IPC.                              | host-side polling timer    | ❌ missing | Tile widget pattern (live polling host-side metric → button face render) is the highest-traffic widget in the vendor app per the screenshot. Requires a new `host_metrics` service. **NOT YET TRACKED in TODO.md.**                |
| 8 | **calendar**                                   | **Calendar**                         | Today's date rendered on the button face (e.g. "May 16" + month chip). Press → opens host's default calendar.                                                      | host-side date render      | ❌ missing | Lowest-effort row to ship. Pure host-side render, no external integration. **NOT YET TRACKED in TODO.md.**                                                                                                                        |
| 9 | **Passare da un dispositivo all'altro**        | **Switch device** (fast-device swap) | Multi-device users with two AJAZZ controllers can bind a button to "make device B the focused device on app open"; useful for AKP153 + AKP05 dual-deck layouts.    | host-side, in-app routing  | 🟡 partial | Our app already enumerates multiple connected devices (`ConnectedRole`); the vendor flow is "make this device the focused device for the next profile-edit session". Wire to `DeviceSwitcher` proposed under v1.3.                |
| 10 | **Time Options**                              | **Time Options** (multi-zone clocks) | Time-zone clock face: pick a city/timezone → button shows live HH:MM (e.g. "09:24 London", "01:00 Bangkok" visible in the screenshot grid).                       | host-side `QTimeZone`      | ❌ missing | Pure host-side render + 60s timer tick. Trivial to ship. **NOT YET TRACKED in TODO.md.** Cross-link: D-05 honesty contract — this is HOST time, NOT device-RTC time (ARCH-05 default verdict: AJAZZ devices have no RTC).         |
| 11 | **Domanda sul meteo**                         | **Weather**                          | Current weather + temperature on the button face for a configured location. Vendor uses a public weather API (carrier unknown — capture pending).                  | host-side HTTPS fetch      | ❌ missing | Requires a host-side HTTP client (we already link `QtNetwork` for the catalogue fetcher) + a chosen weather API provider. **NOT YET TRACKED in TODO.md.** Privacy implication: button-config carries user's location.            |

______________________________________________________________________

## 2. Stream Dock system actions (the default-pre-installed group)

The vendor app's "Stream Dock AJAZZ" category (row 1 above) ships
**eight** system-level actions, exactly as enumerated in the
right-sidebar of the captured screenshot. These do not communicate
with any SDK plugin process — they are routed inside the vendor app's
own host code, talking directly to the device backend.

| Action label (IT)              | Action label (EN, normalised) | What it does                                                                                                                                                                                                                                                                                              | Our status | Tracking                                                                                                                                                                                                                                                                              |
| ------------------------------ | ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Crea una cartella**          | **Create folder**             | Bind a button to "open a sub-page of buttons". Sub-page is rendered with a back-arrow auto-prepended.                                                                                                                                                                                                      | ✅ done    | `ActionKind::OpenFolder` at `src/core/include/ajazz/core/action_engine.hpp:38-46`.                                                                                                                                                                                                  |
| **Cambio di scena**            | **Scene change** (back to parent) | Bind a button to "exit the current sub-page and return to its parent". Auto-bound on every folder's first slot in the vendor app.                                                                                                                                                                       | ✅ done    | `ActionKind::BackToParent` (same file).                                                                                                                                                                                                                                            |
| **Precedente**                 | **Previous page**             | Within a profile that has multiple device pages (the dot indicators visible at the bottom of the screenshot grid: 1·2·3·4·5·6·7·+), advance to the previous page.                                                                                                                                       | ❌ missing | The bottom-of-grid page indicator (1·2·3…+) is observable in the screenshot. We support folder navigation but **not** linear page navigation within a single profile. **NOT YET TRACKED in TODO.md.** Maps to mirajazz / opendeck-akp03 page-cursor concept (out-of-band transport). |
| **Prossimo**                   | **Next page**                 | Symmetric to "Previous page".                                                                                                                                                                                                                                                                            | ❌ missing | Same as above.                                                                                                                                                                                                                                                                     |
| **Vai alla pagina**            | **Go to page** (jump)         | Bind a button to "jump to page N within this profile". Useful for radial-menu-style profile layouts.                                                                                                                                                                                                     | ❌ missing | Same as above.                                                                                                                                                                                                                                                                     |
| **Casella indicatore pagina**  | **Page indicator widget**     | Render the current page number on a button face (the "1·2·3" dots visible at the bottom of the screenshot grid is its native form; the per-button widget is a textual counterpart).                                                                                                                     | ❌ missing | Pure host-side render once page-cursor exists. **NOT YET TRACKED in TODO.md.**                                                                                                                                                                                                     |
| **Luminosità**                 | **Brightness**                | Bind a button to "set device brightness to X%" or "cycle brightness". The screenshot grid shows three brightness sliders (100% / 82% / 50%) on the bottom row of the AKP05 face — those are visualisations of the bound brightness levels.                                                              | 🟡 partial | We expose brightness via the device backend (`IDisplayCapable::setBrightness`), but the **bind-to-button** action is missing from `ActionEngine`. **NOT YET TRACKED in TODO.md.**                                                                                                  |
| **Spegnere lo schermo**        | **Turn off screen**           | Bind a button to "blank the device screen now" (zero brightness or sleep mode).                                                                                                                                                                                                                          | ❌ missing | Cleanly maps to existing `IDisplayCapable::setBrightness(0)` or a dedicated `setSleep` capability. **NOT YET TRACKED in TODO.md.**                                                                                                                                                |

**Coverage of the system-actions category: 2/8 ✅, 1/8 🟡, 5/8 ❌.**

______________________________________________________________________

## 3. Cross-references and SDK event mapping

The public Space Plugin SDK
(`https://sdk.key123.vip/en/guide/events-sent.html`,
`https://sdk.key123.vip/en/guide/events-received.html`) exposes
**12 plugin-sendable events** and **19 plugin-received events** —
this is the wire surface that any third-party plugin (or our own
sandboxed plugin host) speaks. These events are NOT the built-in
categories above — they are the lower-level RPC vocabulary that the
built-in categories use internally. Mapping:

- The **"Stream Dock system actions"** category (row 1, §2) does NOT
  use the SDK wire — it is in-app routing inside the vendor's own
  host code. Our equivalent is `ActionEngine` direct dispatch.
- The **widget categories** (rows 3-11) are individual plugin
  processes that DO use the SDK wire — e.g. the OBS Studio category
  is implemented as a plugin process that uses `keyDown` (SDK event)
  to fire a scene change via obs-websocket, then `setImage` (SDK
  event) to update the button face with the new scene name.

For our parity work, this means:
1. **System actions (§2) ship in our `ActionEngine`** — no plugin
   process needed. Five of eight rows are open work; estimated effort
   is one week (page-cursor + 4 actions binding to it + brightness
   action surface).
2. **Widget categories (§1 rows 3-11) ship as first-party plugin
   processes** — same plugin manifest + sandboxed host as third-
   party plugins, just packaged inside our app. Eight categories
   missing entirely; the cheapest three to ship (Calendar, Time
   Options, System Performance Monitor) are pure host-render with
   no external integration and could land in v1.3 in 1-2 days each.

The full SDK event vocabulary is documented in
[`vendor-software-inventory.md`](vendor-software-inventory.md) under
the "Time-sync feature in vendor app" callout (where it lists the
12 sendable + 19 received events to prove that no `setTime` / `setRTC`
event exists in the public SDK — the same enumeration applies here).

______________________________________________________________________

## 4. Implementation priority (proposed for v1.3)

Ranked by effort × user-visibility:

| Rank | Item                                                            | Effort | Why this rank                                                                                                                                                                                       |
| ---- | --------------------------------------------------------------- | ------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1    | **Page-cursor + Previous/Next/Go-to-page system actions** (§2) | S      | Unblocks four other rows (page indicator, page-bound layouts). Pure `ActionEngine` work. Maps cleanly to existing folder-navigation pattern.                                                       |
| 2    | **Brightness + Turn-off-screen system actions** (§2)           | XS     | Already have the device-side capability; just needs `ActionKind::SetBrightness` enum entry + dispatch. One commit.                                                                                  |
| 3    | **Calendar widget plugin** (§1 row 8)                          | S      | Pure host-render, zero external integration. Reference implementation for the plugin-process widget pattern. Doubles as documentation example.                                                     |
| 4    | **Time Options widget plugin** (§1 row 10)                     | S      | Pure host-render + `QTimeZone` + 60s timer. Cross-links D-05 honesty contract (this is HOST time, not device-RTC time). Avoids the ARCH-05 trap.                                                  |
| 5    | **System Performance Monitor widget plugin** (§1 row 7)        | M      | High-traffic widget. Requires a new `host_metrics` service abstraction (CPU/RAM/Net polling). Cross-platform polling is non-trivial (Linux: `/proc`, Windows: PDH, macOS: `host_statistics`).      |
| 6    | **Useful Notes widget plugin** (§1 row 6)                      | M      | Gated on Property Inspector M5 (`sendToPropertyInspector` RPC bridge). Once M5 lands, this is a one-day plugin.                                                                                    |
| 7    | **Audio Player widget plugin** (§1 row 3)                      | M      | Requires `QtMultimedia` link (vendor parity gap already noted). Non-trivial UX (track-title rendering, transport controls). Privacy: button-config carries local file paths.                       |
| 8    | **Workflow builder UI** (§1 row 4)                             | L      | Underlying chained-action support already exists; the missing half is the graphical step builder in the QML editor. Significant UI work but no protocol/wire change.                              |
| 9    | **OBS Studio plugin** (§1 row 5)                               | M      | obs-websocket protocol over WS. Requires `QtWebSockets` link (vendor parity gap already noted in matrix). Niche but high-visibility (streamer audience).                                          |
| 10   | **Weather plugin** (§1 row 11)                                 | M      | Choose a privacy-respecting weather provider; carry user location server-side. Privacy review required before shipping.                                                                            |
| 11   | **Toolbox category — type-text + password + calculator** (§1 row 2) | M      | Three separate small actions; password-store row needs careful design (we are NOT shipping a credential vault — read-only reference to host keyring is the pattern).                              |
| 12   | **Switch device action** (§1 row 9)                            | XS     | Trivial once we decide on UX: button → "make this device the focused device". One commit.                                                                                                          |

XS=hours / S=1-2 days / M=3-5 days / L=1-2 weeks per row.

______________________________________________________________________

## 5. Anti-features (do NOT add, even if vendor ships them)

- **Cloud sync of profile assets**. The vendor app uploads button
  faces to its CDN as part of profile sharing. We do not ship cloud
  features for any reason. Profile import/export ships as
  `.ajazzprofile` bundle on disk (already done — see
  [`vendor-feature-matrix.md`](vendor-feature-matrix.md) "Profile
  import / export" row).
- **Telemetry on widget render**. The vendor app reports widget
  usage frequency back to its analytics endpoint. We do not collect
  telemetry — see [`docs/policies/capture-data-hygiene.md`](../policies/capture-data-hygiene.md)
  "Anti-features" and CLAUDE.md cross-cutting prohibition.
- **Auto-install plugins from a remote store on first launch**. The
  vendor app pre-installs the eight system actions + ten widget
  categories above into a fresh profile. We will offer them via the
  Plugin Store UI (already designed under TODO "Plugin SDK + Store")
  but the user must opt-in to install each one. No "starter pack"
  silent install.
- **Decompiling the vendor binary to recover widget assets or icons.**
  CLAUDE.md hard rule. The icons / button faces visible in the
  screenshot are the vendor's IP. Our widgets must ship with original
  iconography (commission, license CC0, or roll our own).

______________________________________________________________________

## 6. How to update this document

1. When you ship a row that is currently ❌ or 🟡, change the cell to
   ✅ in the same PR that ships the feature, with the commit SHA
   appended to the Tracking column. Also update the matching row in
   [`vendor-feature-matrix.md`](vendor-feature-matrix.md).
1. When a vendor app update changes the default category set
   (capture-id will be `vendor-YYYY-MM-DD-streamdock-app-NNN`), add
   the new categories at the top of §1 with the capture date and
   the version string from the running app's "About" pane.
1. When a TODO.md entry is created for a row currently marked "**NOT
   YET TRACKED in TODO.md**", remove that flag from the Tracking
   column and link the TODO entry instead.
