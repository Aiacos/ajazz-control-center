# StreamController vs. OpenDeck — Evaluation

> **Status:** Evaluation only — no code change, no fork.
> **Date:** 2026-06-30.
> **Question asked:** Should we replace OpenDeck with
> [StreamController](https://github.com/StreamController/StreamController),
> rebasing it on our `mirajazz` sidecar while keeping the rest so plugins
> work correctly?
> **Short answer:** **No — not for this project's goals.** StreamController is
> a well-built application, and the `mirajazz` integration seam is genuinely
> clean, but its plugin ecosystem is **incompatible with the Elgato
> `.sdPlugin` plugins this project requires.** That single fact disqualifies
> it as an OpenDeck replacement here. Details, evidence, and the few ideas
> worth borrowing are below.

______________________________________________________________________

## 1. The decision in one paragraph

The project needs to host **Elgato `.sdPlugin` plugins** (the existing
marketplace: HWiNFO, Weather, System Monitor, …) over the Elgato Stream Deck
WebSocket SDK protocol. OpenDeck does exactly this. **StreamController cannot
run Elgato plugins at all** — it has its own, separate, native-Python plugin
system and store, which does not speak the Elgato protocol and cannot load
`.sdPlugin` packages. Because the Elgato plugin requirement is a hard
requirement for us, StreamController is the wrong tool, independent of how
clean its code is. We are therefore keeping the current stack (OpenDeck web UI
in a `WebEngineView` + `mirajazz` Rust sidecar + Elgato `.sdPlugin` host on
port 57116).

## 2. The three plugin ecosystems (the real fork in the road)

Most of the "OpenDeck vs StreamController" confusion dissolves once you see
that **three different plugin systems** are in play, and only one of them is
the hardware transport:

| System             | Plugin format                            | UI model                       | Relationship to our goal                  |
| ------------------ | ---------------------------------------- | ------------------------------ | ----------------------------------------- |
| **OpenDeck**       | Elgato `.sdPlugin` (WebSocket SDK)       | Web SPA, embeddable in WebView | **Hosts the Elgato marketplace we need**  |
| **StreamController** | Native Python `PluginBase`/`ActionBase` | GTK4 app, standalone           | Different, incompatible plugin ecosystem  |
| **mirajazz**       | *(none — it is byte transport)*          | Headless Rust sidecar          | Drives AJAZZ hardware under *either* UI   |

There is **no option that gives you StreamController's UI *and* the existing
Elgato plugins at the same time** — the two ecosystems do not interoperate.
`mirajazz` sits underneath both and is agnostic; it is not the thing being
chosen between.

## 3. StreamController — what it actually is

Researched from the upstream repo, the developer docs
(`streamcontroller.core447.com`), and a file-level read of `src/`.

- **Language / UI:** Python (~99%), **GTK4 + libadwaita**. A native
  GNOME-style **Linux desktop app**. There is no HTML/JS frontend, no WebView,
  no localhost web UI. The editor is compiled GTK widgets rendered in-process.
- **Not embeddable in Qt.** GTK4 and Qt are incompatible widget toolkits with
  separate event loops and rendering. You cannot reparent a GTK4 window into a
  QML `WebEngineView` the way OpenDeck's SPA drops in. "Using it whole" would
  mean shipping it as a **separate standalone application**, not a pane inside
  the AJAZZ Control Center.
- **Device backend:** their own fork of `python-elgato-streamdeck`
  (Python + hidapi). Elgato decks are first-class; non-Elgato clones are added
  per-PID (e.g. Mirabox 293S landed via issue #404) — but coverage lags and is
  **behind our `4ndv/mirajazz` Rust sidecar** for AKP05E/N4.
- **Plugin system:** native Python packages (`PluginBase` + `ActionBase`
  subclasses, `manifest.json`/`about.json` metadata), with an optional
  per-plugin out-of-process backend over **rpyc** (TCP RPC). Its own plugin
  store. **Not Elgato `.sdPlugin`; not the Elgato WebSocket SDK.**
- **License:** **GPL-3.0.** Forking and shipping it makes the distributed work
  GPL-3.0. Manageable for a *separate* app; a real constraint if linked
  in-process.
- **Maturity:** ~1.1k stars, 39 releases, but still **beta** (latest
  `1.5.0-beta.x`, May 2026 — never a stable 1.0), single-maintainer-dominated.
  Healthy and maintained, but not an API-stable platform to build a product
  backend on.

## 4. The one genuinely good finding: the `mirajazz` seam is clean

If we *had* chosen StreamController, rebasing it on our sidecar would have been
**low effort**. Recording this because it is reusable knowledge and it
validates our own sidecar architecture.

StreamController's device I/O is isolated behind a **single adapter**,
`BetterDeck`, with the raw `StreamDeck` object referenced in only
**~60 call-sites across 2 files** (`BetterDeck.py`, `DeckController.py`) plus
**one enumeration site** in `DeckManager.py`. The contract is **duck-typed** —
no base class to inherit — and StreamController *already ships two non-Elgato
backends that prove it*:

- `Subclasses/FakeDeck.py` — a pure mock implementing the method surface as
  no-ops.
- `Subclasses/RemoteDeck.py` (+ `RemoteDeckManager.py`) — a deck **driven over
  the network**: `set_key_image()` converts to PIL, rotates 180°, and ships the
  image out of process; input arrives from the manager and fires the stored
  `key_callback`.

A `mirajazz` adapter would be *"`RemoteDeck`, but the transport is a stdio pipe
to the Rust sidecar"* — roughly **2-3 new/modified files**
(`Subclasses/MirajazzDeck.py` implementing the ~33-method duck-typed interface,
an enumeration hook in `DeckManager.py`, and image-format alignment), with **no
changes** to the page model, rendering composition, plugin host, or
input-dispatch layers.

> **Architectural takeaway:** that StreamController already has a
> "deck over the network" backend is strong evidence its device contract is a
> clean port target. It is the same shape as our `SidecarStreamDockDevice`
> seam — independent confirmation that **the out-of-process sidecar boundary is
> the right place to abstract Stream Deck hardware.** This is the durable
> lesson even though we are not switching.

## 5. What OpenDeck gives us that we would lose

Current integration (mapped from `src/app/src/`):

- **Embeddable web UI:** OpenDeck is a Svelte SPA
  (`src/app/webui/opendeck`, from `nekename/OpenDeck`) loaded into a
  `WebEngineView` via the `opendeck://` scheme, bridged to C++ over QWebChannel
  (`OpenDeckBridge`, ~40 commands + 10 events; `opendeck_bridge.cpp`). It lives
  *inside* the Qt control center.
- **Elgato plugin host:** `SdPluginServer` (`sd_plugin_server.cpp`) speaks the
  Elgato Stream Deck v6 WebSocket protocol on `127.0.0.1:57116`, including the
  Property Inspector second-connection relay. **This is what runs the Elgato
  marketplace plugins.**
- **One app, all devices:** the same Qt app also owns mouse (AJ-series) and
  keyboard (AK980 RGB/settings/firmware) as native QML. StreamController does
  **only** stream decks, so switching would split the product into **two apps**
  (StreamController for decks + the Qt app for mouse/keyboard).
- **Cleaner license boundary:** an arm's-length web-UI-in-WebView is legally
  cleaner than pulling a GPL-3.0 GTK app in-process.

Switching to StreamController would trade all of the above for a cleaner-looking
GTK codebase whose plugins we cannot use.

## 6. Scorecard

| Axis                          | OpenDeck (current)                    | StreamController                          | Winner for us |
| ----------------------------- | ------------------------------------- | ----------------------------------------- | ------------- |
| Elgato `.sdPlugin` support    | ✅ Yes (the whole point)              | ❌ No — own Python ecosystem               | **OpenDeck**  |
| Embeddable in Qt app          | ✅ Web SPA in WebView                 | ❌ Standalone GTK4 app                     | **OpenDeck**  |
| `mirajazz` integration        | ✅ Already wired                      | ✅ Clean seam (~2-3 files) but not wired   | OpenDeck      |
| AJAZZ/AKP05E device coverage  | ✅ via `mirajazz` Rust sidecar        | ⚠️ Python lib, behind `mirajazz`          | **OpenDeck**  |
| Mouse + keyboard in one app   | ✅ Same Qt app                        | ❌ Decks only → two apps                   | **OpenDeck**  |
| License for our distribution  | ✅ Arm's-length WebView boundary      | ⚠️ GPL-3.0 if bundled                      | **OpenDeck**  |
| Code cleanliness / UX polish  | ⚠️ Web SPA + Tauri shim               | ✅ Polished native GTK                      | StreamController |
| Stability / API maturity      | ⚠️ Both moving targets               | ⚠️ Perpetual beta, single maintainer       | tie           |

The two axes StreamController wins (polish, native feel) do not outweigh the
hard requirement it fails (Elgato plugins) plus the structural costs
(non-embeddable, two apps, GPL).

## 7. Ideas worth borrowing (without switching)

Even though we keep OpenDeck, StreamController's source is worth mining:

1. **Validate our sidecar boundary.** Its `RemoteDeck` confirms that
   "Stream Deck as an out-of-process device" is a sound abstraction — exactly
   our `SidecarStreamDockDevice` design. Keep it.
2. **`MediaPlayerThread` frame pacing.** It runs ~30 FPS active / ~2 FPS idle
   for animated keys. If we ever add animated key media, that adaptive-rate
   model is a good reference (and a reminder our sidecar protocol must sustain
   ~30 FPS, which the persistent-handle design already does).
3. **Page JSON model.** Its hardware-agnostic, input-type-keyed page schema
   (`keys`/`dials`/`touchscreens`/`background`) is a clean reference if we
   revisit our profile format.

## 8. If the Elgato requirement ever changes

The only scenario where StreamController becomes attractive is if the project
**drops the Elgato `.sdPlugin` requirement** and is willing to adopt
StreamController's Python plugin ecosystem instead. In that (different) world,
the path would be: fork StreamController, add `Subclasses/MirajazzDeck.py` + an
enumeration hook, ship it as the standalone deck app, and accept GPL-3.0 +
two-app split. That is a product-direction decision, not a technical blocker —
the technical part (mirajazz) is the easy ~2-3-file piece documented in §4.

A theoretical third path — teaching StreamController to *also* host Elgato
`.sdPlugin` plugins via a WebSocket SDK server — is a large new feature
(re-implementing our `SdPluginServer` inside StreamController's Python world),
not "using it whole," and is not recommended.

______________________________________________________________________

## Appendix — sources

- StreamController repo, docs (`streamcontroller.core447.com`), and a
  file-level read of `src/backend/DeckManagement/` (`DeckManager.py`,
  `DeckController.py`, `BetterDeck.py`, `Subclasses/FakeDeck.py`,
  `Subclasses/RemoteDeck.py`), `src/backend/PageManagement/Page.py`,
  `src/backend/PluginManager/{PluginBase,ActionBase}.py`.
- StreamController device library fork:
  `StreamController/streamcontroller-python-elgato-streamdeck`.
- Mirabox/clone support: upstream issue #404.
- Our integration: `src/app/src/opendeck_bridge.{hpp,cpp}`,
  `sd_plugin_server.{hpp,cpp}`, `sidecar_stream_dock_device.{hpp,cpp}`,
  `stream_dock_control_service.hpp`, `docs/opendeck-ui/01-contract.md`.
