# AJAZZ Control Center — Competitive Feature-Gap Analysis

_Companion document to [`feature-gaps.csv`](feature-gaps.csv) (63 rows: 15 P0, 30 P1, 18 P2)._

## Executive summary

AJAZZ Control Center has the right architectural bones — Qt 6 / QML UI, modular C++ device backends for the AKP03/AKP05/AKP153 stream-deck-likes, VIA + proprietary keyboards, and AJ-series mice, plus an embedded Python plugin host — but its **action engine, profile system, and integration surface are still placeholders**. Every mainstream competitor ships a richer experience on those three axes, and the gap is largely about turning the existing scaffolding into a usable feature set rather than adding new device backends.

The single biggest functional gap is the **action engine**. Elgato's flagship feature is the [Multi-Action](https://help.elgato.com/hc/en-us/articles/360027960912-Elgato-Stream-Deck-Multi-Actions), which lets a key fire a chain of steps with per-step delays, and [Bitfocus Companion](https://bitfocus.io/companion) builds an entire pro-broadcast control surface on top of stacked actions, latching, and per-button feedback. iCUE adds [secondary actions, timers, and conditional triggers](https://www.youtube.com/watch?v=T5WzjbfbGk0) to the same idea. ajazz-control-center today only has a single action slot per key (`profile.hpp`), so the AKP devices effectively run as glorified shortcut buttons. Closing this gap also unlocks [encoder rotate/push actions](https://help.elgato.com/hc/en-us/articles/10567379685901-Elgato-Stream-Deck-Technical-Specifications), [touch-strip widgets](https://community.folivora.ai/t/stream-deck-plus-touchscreen/30041), and [tap/hold dual-function keys](https://x-bows.com/blogs/engineering-innovation/definitive-guide-qmk-via-vial-xbows-keyboards), which are individual rows in the CSV but share the same composite-action primitive.

Profile management is the next visible gap. [Stream Deck Smart Profiles](https://help.elgato.com/hc/en-us/articles/360053419071-Elgato-Stream-Deck-Smart-Profiles), [iCUE per-game profiles](https://www.corsair.com/us/en/explorer/diy-builder/how-tos/how-to-create-an-icue-profile-for-your-favorite-game/), [Armoury Crate Scenario Profiles](https://rog.asus.com/articles/guides/guide-scenario-profiles-make-games-and-apps-run-with-your-preferred-system-settings-every-time/), and [Logitech G HUB game profiles](https://www.youtube.com/watch?v=nLN5VWtvds4) all auto-switch when an app gains focus. Even [StreamController on Linux](https://flathub.org/en/apps/com.core447.StreamController) does this. Without focus-watching, every other RGB/lighting and mouse-DPI feature in the CSV is half-broken — none of them can adapt automatically. The fix lives in `src/app` and depends on per-OS focus APIs.

Plugin ecosystem is the area where the project can either become a credible alternative or fade into per-vendor obscurity. The [Stream Deck SDK](https://docs.elgato.com/streamdeck/sdk/introduction/plugin-environment/) and its [Marketplace](https://www.elgato.com/us/en/explorer/products/stream-deck/how-to-make-your-own-stream-deck-plugin/) are why creators tolerate Elgato's Windows/macOS-only software at all. ajazz-control-center already embeds a Python interpreter (`src/plugins/src/plugin_host.cpp`); shipping the missing pieces — a property-inspector schema, a community plugin store, sandboxing, and signed packages — is mostly UI and infrastructure work, not new C++ device code.

RGB lighting trails by an entire generation: [iCUE Murals](https://www.corsair.com/us/en/s/icue-murals-lighting), [Aura Creator](https://www.asus.com/us/support/faq/1042503/), and [Razer Chroma Studio/Visualizer](https://www.upgrade-my-laptop.com/guides/laptop-support/razer-chroma-explained-a-deep-dive-into-rgb-profiles-studio-connect-visualizer/) all offer layered timelines, audio-reactive effects, screen-sampling, and cross-vendor sync via Hue/Nanoleaf/Govee. The pragmatic shortcut is to **federate with [OpenRGB](https://openrgb.org)** — speak its SDK on port 6742 and inherit dozens of community effect tools — instead of building a Murals clone from scratch.

Keyboard and mouse coverage looks broad on paper (VIA, proprietary backends, PAW3395 sensors) but lacks editor surfaces. [Vial offers live remap with no flash](https://svalboard.substack.com/p/svalboard-now-with-real-time-keyboard); [Piper covers Linux mouse config](https://itsfoss.com/piper-configure-gaming-mouse-linux/) end-to-end. ajazz needs to close P0 gaps around the keymap editor, DPI stages, lift-off distance, and per-app mouse profiles to feel competitive with both proprietary stacks and the open-source baseline.

Finally, **trust and distribution polish** quietly determine adoption. Code-signed `.msi`, notarized `.dmg`, signed plugins, signed firmware, and a published "no-telemetry" stance are competitive moats against Razer/Corsair (account-bound, telemetry-on) — and they map directly to existing CI in `.github/workflows/release.yml`.

The recommended sequencing reflects this: build the action engine + profile auto-switch + an OBS/Spotify plugin pair first to be **demoable** against Stream Deck on day one, then layer on the marketplace, signed binaries, and OpenRGB federation. Larger items (cloud sync, Aura-Creator-class timeline, firmware UI) can wait for community traction.

## Top 10 P0 gaps

- **Multi-action bundles with per-step delays** — turns the action engine from one-shortcut-per-key into a real macro pad ([Stream Deck Multi-Actions](https://help.elgato.com/hc/en-us/articles/360027960912-Elgato-Stream-Deck-Multi-Actions)).
- **Profile auto-switch on focused application** — every competitor has it; without it, lighting / DPI / layouts can never adapt automatically ([Smart Profiles](https://help.elgato.com/hc/en-us/articles/360053419071-Elgato-Stream-Deck-Smart-Profiles), [Scenario Profiles](https://rog.asus.com/articles/guides/guide-scenario-profiles-make-games-and-apps-run-with-your-preferred-system-settings-every-time/)).
- **Live per-key remapping editor for VIA + proprietary keyboards** — the single feature that justifies installing a keyboard utility ([Vial real-time remap](https://svalboard.substack.com/p/svalboard-now-with-real-time-keyboard)).
- **Folders / nested folders on the AKPs** — without them AKP153's 15-key ceiling stays a hard limit ([Stream Deck folders](https://www.elgato.com/us/en/explorer/products/stream-deck/how-to-use-folders-stream-deck/)).
- **Encoder / dial action bindings** — AKP03/AKP05 already expose the events; the action engine ignores them.
- **Macro recorder with editable timing** — table-stakes on every gaming peripheral ([iCUE](https://www.corsair.com/us/en/explorer/gamer/keyboards/how-to-add-macros-and-remap-keys-in-corsair-icue/), [Synapse 4](https://www.razer.com/synapse-4)).
- **DPI stage editor with per-stage colour for AJ-series mice** — already advertised but no UI ([Piper](https://itsfoss.com/piper-configure-gaming-mouse-linux/)).
- **Profile import/export to a single shareable file** — community sharing is impossible without it ([Vial layout files](https://x-bows.com/blogs/engineering-innovation/definitive-guide-qmk-via-vial-xbows-keyboards)).
- **First-party OBS and media-control plugins shipping with the app** — the most-installed Stream Deck plugins; absence is conspicuous ([SD top plugins](https://www.elgato.com/us/en/explorer/products/stream-deck/stream-deck-plugins-for-streaming/)).
- **Code-signed Windows installer and notarized macOS DMG** — Gatekeeper / SmartScreen block unsigned builds, killing first-run conversion.

## Quick-win P1 features (small effort, high impact)

- **Battery indicator in tray** for wireless AJ339/AJ380 — hours of work, immediate visible value ([Logitech battery UX](https://hub.sync.logitech.com/mx-master-3s/post/how-do-i-check-the-battery-level-on-my-mice-and-keyboard-WkcnD9dnohFqEyM)).
- **Lift-off distance + sniper button** — single sensor command in `aj_series.cpp`; closes a gap competitive FPS players notice instantly ([Razer Synapse 4](https://www.razer.com/synapse-4)).
- **Polling-rate / debounce / NKRO toggles** — capability flags + sliders, no new protocol work needed.
- **OS notifications** for device connect / firmware available / battery low — small `NotificationService` wraps QSystemTrayIcon + libnotify.
- **Cross-platform autostart on login** — XDG `.desktop` + Win Run key + macOS LaunchAgent; one screen of code.
- **Privacy / no-telemetry policy doc** — write-only effort, real differentiator versus [Razer/Corsair account requirements](https://openrgb.org).
- **Home Assistant / Philips Hue Python plugins** — REST + `phue` library; ships as `python/ajazz_plugins/*` without core changes ([iCUE Hue/Nanoleaf](https://www.corsair.com/us/en/s/icue-murals-lighting)).
- **In-app changelog viewer** — reads `CHANGELOG.md` already in repo; signals product polish.
- **OpenRGB SDK server adapter** — exposes AJAZZ devices to dozens of existing community effect tools without re-implementing them ([OpenRGB](https://openrgb.org)).
- **Property-Inspector JSON schema for plugins** — unlocks third-party plugin UI without each author touching QML ([Stream Deck SDK](https://docs.elgato.com/streamdeck/sdk/introduction/plugin-environment/)).
