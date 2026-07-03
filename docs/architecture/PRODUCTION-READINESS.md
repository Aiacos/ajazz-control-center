# Production readiness — plugin system vs Elgato Stream Deck SDK

> Audit date: 2026-07-03. Method: official SDK docs (docs.elgato.com) checked
> feature-by-feature against the implementation, plus live driving of the app
> on real hardware (AKP05E + AK980 PRO + AJ159) with screenshot verification
> per surface, plus packaging-artifact inspection of the freshly published
> nightly. Update this file whenever a blocker below changes state.

## Verdict

**NOT-READY** for the bar "a random plugin from the Elgato store just works";
**READY-WITH-CAVEATS** for the curated set (sysmon, starterpack, OBS
StreamDock, Weather, oasystem — all live-verified working end-to-end).

The conformant core is broad and well-defended: registration handshake +
`-info`, dual-WS Property Inspector with context canonicalisation and
ownership checks, bidirectional settings with the `id` correlator, per-state
visuals (`setImage`/`setTitle` with `state`/`target` — fixed 2026-07-03),
automatic 2-state cycle honouring `DisableAutomaticStates`, dial events with
signed ticks + exactly-once `dialUp` (fixed 2026-07-03), `touchTap`, built-in
encoder layouts `$X1..$C1` with `setFeedback`/`setFeedbackLayout`,
`switchToProfile`/`switchProfile`, app-monitor events with
`ApplicationsToMonitor` filtering, `systemDidWakeUp`, `openUrl`/`logMessage`,
and the OS/version manifest gates.

## Open blockers (ordered by severity)

| # | Gap | Impact | Where |
|---|-----|--------|-------|
| 1 | ~~Manifest `Profiles[]` ignored~~ | REDUCED to a documented limitation: `Profiles[]` is parsed, empty-token "back to previous" works (d330360f), and a shipped profile name now lazily materializes as a real (empty) profile on first `switchToProfile` — but the bundled `.streamDeckProfile` LAYOUT content is still not imported (ZIP-internal format undocumented in our RE corpus; needs a real artifact to RE first — no installed plugin ships one, and OpenDeck upstream does not implement it either) | **PARTIAL 2026-07-03** — `plugin_manifest.cpp` `Profiles[]` parser + `matchShippedProfileName`; `PluginManager::shippedProfileName`; `application.cpp` lazy materialization |
| 2 | ~~No `streamdeck://` deep-link~~ | Linux DONE (handler + single-instance hand-off + x-scheme-handler); Windows now self-registers HKCU `Software\Classes\streamdeck` at startup (covers MSI + ZIP); macOS ships `CFBundleURLTypes` via `resources/macos/Info.plist.in` + a `QFileOpenEvent` filter. Win/mac legs are code-complete but not yet verified on real Win/mac hosts | **FIXED (Linux) 2026-07-03; Win/macOS registration landed 2026-07-03 (verification pending on those OSes)** — `Application::handleDeepLink`, `main.cpp` |
| 3 | ~~`setImage`/`setTitle` `state`/`target` ignored~~ | — | **FIXED 2026-07-03** (`plugin_device_bridge.cpp`, per-state override maps) |
| 4 | ~~Multi-Action cannot host plugin actions~~ | userDesiredState still needs a per-child state field | **FIXED (core) 2026-07-03** — engine fallback delivers keyDown/keyUp with `isInMultiAction:true` to the owning plugin; mounted-action double-fire guard |
| 5 | ~~Custom JSON encoder layouts not loaded~~ | — | **FIXED 2026-07-03** (`renderCustomEncoderLayout`, cached file loader in the bridge) |

Degradations that are acceptable to document rather than fix: `touchTap.hold`
always false; `titleParametersDidChange` carries defaults instead of the
user's title styling; `switchToProfile` `page` field ignored.

## Distribution packaging (Linux)

- **Flatpak is the self-contained artifact** (org.kde 6.8 runtime bundles Qt,
  QML, WebEngine; sidecar + sysmon + SPA included). Green in CI.
- deb/rpm are built against aqt Qt 6.8.3, so `dpkg-shlibdeps` cannot map the
  Qt sonames and generated a Depends with **no Qt at all** (verified on the
  published nightly .deb). Explicit runtime deps are now declared (QML
  modules, Widgets/Svg/WebSockets libs, python3, Recommends nodejs) — these
  packages require a distro with Qt >= 6.8 and fail cleanly elsewhere.
- Windows MSI bundles Qt via windeployqt with a CI gate asserting
  Qt6Widgets.dll + qwindows.dll; macOS DMG is a universal bundle. Both also
  embed the OpenDeck SPA since 2026-07-03 (the qtwebengine aqt module was
  missing from every packaging job before — silently shipping the native
  fallback UI).

## Live-verification status (2026-07-03 UI tour, screenshots on file)

All six surfaces pass: sidebar/devices, keyboard panel (AK980), mouse panel
(AJ159 DPI/polling/battery), Plugin Store (258 icons, 0 broken), app
settings, AKP05E editor + PI (including the new sysmon "Sample every (s)"),
action search. Known cosmetic issues: overlapping state texts on multi-state
dial slots; empty space above "RGB effect" in the mouse RGB tab.
