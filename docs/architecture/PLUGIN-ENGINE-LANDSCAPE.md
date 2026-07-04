# Plugin engine landscape

> **Status:** research / direction survey (2026-07-04). Complements
> [`PLUGIN-SYSTEM.md`](PLUGIN-SYSTEM.md) (first-party Python scripting host),
> [`PLUGIN-SDK.md`](PLUGIN-SDK.md) (aspirational language-agnostic SDK),
> [`PLUGIN-GAP-ANALYSIS.md`](PLUGIN-GAP-ANALYSIS.md), and
> [`STREAMCONTROLLER-EVALUATION.md`](STREAMCONTROLLER-EVALUATION.md). This doc
> answers a single question: **what open plugin engine should AJAZZ Control
> Center standardize on, and why?**

## TL;DR

- The engine we already embed — **OpenDeck**, implementing the **OpenAction
  API** — is the right long-term bet. It is the open, cross-platform, WebSocket,
  device-agnostic protocol that is *backwards-compatible with the Elgato Stream
  Deck SDK v2*, so it inherits the largest existing plugin pool while staying
  vendor-neutral.
- The guiding wish — *"for the plugin layer, something in the spirit of
  [`python-elgato-streamdeck`](https://github.com/abcminiuser/python-elgato-streamdeck)"*
  — is best read as **"a clean, open, minimal, well-maintained reference
  library, not a proprietary monolith."** At the plugin/engine layer that
  reference is **OpenAction + OpenDeck**; `python-elgato-streamdeck` itself is a
  *device* library and sits one layer below (see next section).
- Highest-value near-term work is **not** picking a new engine but **hardening
  the one we have**: surface silent plugin-load failures, officially endorse
  OpenAction as the recommended plugin SDK in our developer docs, and
  (optionally) ship a thin first-party Python authoring SDK to lower the barrier.

## Layer map — don't confuse the device library with the engine

A recurring confusion is worth naming up front, because it determines what
"something like `python-elgato-streamdeck`" actually points at.

| Layer | Responsibility | Reference implementations |
| --- | --- | --- |
| **Device / transport** | Enumerate hardware over HID; set button images; read raw button/encoder state. **No plugin loading.** | `python-elgato-streamdeck` (Python), [`mirajazz`](https://github.com/4ndv/mirajazz) (Rust, Stream Dock), our own C++ device backends |
| **Plugin engine / host** | Discover and launch plugins; speak the plugin wire protocol; route actions, settings, Property Inspector, device events | **OpenDeck** (OpenAction server), Elgato's proprietary Stream Deck app, StreamController |
| **Plugin protocol / SDK** | The contract a plugin implements + the client libraries that hide it | **OpenAction API** (WS), Elgato Stream Deck SDK v2, per-language SDKs |

`python-elgato-streamdeck` is squarely in the **device** row: it can drive a
panel directly (custom home-automation front-ends, `streamdeck_ui`'s
button→command UI) but has no concept of a `.sdPlugin`, no host, no marketplace.
Its virtues — open, minimal, dependency-light, well-maintained — are the virtues
we want *reproduced at the engine layer*. That is what OpenAction/OpenDeck
provides.

## Recommended engine: OpenAction + OpenDeck

| Aspect | Detail |
| --- | --- |
| Protocol | [OpenAction API](https://openaction.amankhanna.me/) — WebSocket, language- and device-agnostic, **backwards-compatible with Elgato Stream Deck SDK v2**. Works in any language with a WebSocket client. |
| Reference server | [OpenDeck](https://github.com/nekename/OpenDeck) — cross-platform (Linux / Windows / macOS); loads both legacy Elgato SDK plugins and modern OpenAction plugins. **Already embedded in this app** as the `.sdPlugin` host. |
| Authoring SDK | Official [OpenAction Rust crate](https://github.com/OpenActionAPI/rust); Svelte `svelte-pi` for Property Inspectors. |
| Ecosystem | [OpenAction marketplace](https://github.com/OpenActionAPI/marketplace) (marketplace.rivul.us) — curated open-source plugins, one-click install into OpenDeck via deeplink. |

Why this and not a bespoke engine: OpenAction gives us the *only* open path that
also inherits the Elgato plugin catalogue (the largest pool of deck plugins in
existence) without adopting Elgato's proprietary host. It is the engine-layer
analogue of what `python-elgato-streamdeck` is at the device layer — open,
minimal, cross-platform, no lock-in.

## Alternatives considered

| Engine / SDK | Verdict | Rationale |
| --- | --- | --- |
| **Elgato official** [`@elgato/streamdeck`](https://github.com/elgatosf/streamdeck) (Node/TS) | Reference only | The canonical SDK, but the host is proprietary and `com.elgato.*` plugins ship as **native Win/macOS binaries** that cannot run on Linux (tracked in issue #83). We stay *compatible* with its manifest, we do not adopt its host. |
| **StreamController** (Python / GTK4) | Rejected | No Elgato `.sdPlugin` support, GTK4 UI is non-embeddable, GPL-3.0, deck-only. Full write-up in [`STREAMCONTROLLER-EVALUATION.md`](STREAMCONTROLLER-EVALUATION.md). |
| **Bitfocus Companion** | Not applicable | Different model (TCP "modules", pro-AV control surface), not `.sdPlugin`. No compatibility path. |
| **Python plugin SDKs** — [`strohganoff/python-streamdeck-plugin-sdk`](https://github.com/strohganoff/python-streamdeck-plugin-sdk), [`gri-gus/streamdeck-python-sdk`](https://github.com/gri-gus/streamdeck-python-sdk) | Keep on radar | Client-side authoring SDKs (hide the WebSocket, "just define actions"), not hosts. They are the Python-flavored analogue of `python-elgato-streamdeck` at the *plugin* layer — useful as a template if we ship a first-party Python authoring SDK. |

## How this maps to what we already have

The app already runs **two** plugin runtimes behind `UnifiedPluginHost`:

1. **First-party Python out-of-process host** (`OutOfProcessPluginHost`, see
   [`PLUGIN-SYSTEM.md`](PLUGIN-SYSTEM.md)) — `bwrap`-sandboxed, JSON-over-pipes,
   for first-party scripting. This is already our clean,
   `python-elgato-streamdeck`-spirited host — but for *scripting*, not for the
   third-party `.sdPlugin` ecosystem.
2. **OpenDeck / OpenAction WebSocket host** — loads `.sdPlugin` and OpenAction
   plugins. **This is the layer to invest in** for third-party plugins.

So the direction is not a rewrite; it is doubling down on runtime #2.

## Recommended next steps

1. **Surface silent load failures.** Today a plugin that fails discovery prints
   only `plugin host ready: 0 loaded`. The real cause (unparsable manifest vs.
   unsupported platform vs. unresolved `CodePaths` target-triple) is swallowed.
   Make it user-visible. *(Tracked from the issue #85 triage.)*
2. **Officially endorse OpenAction.** Point [`docs/guides/PLUGIN_DEVELOPMENT.md`](../guides/PLUGIN_DEVELOPMENT.md)
   at the OpenAction Rust crate and the marketplace as the recommended path for
   third-party plugin authors.
3. **(Optional) First-party Python authoring SDK.** A thin "hide the WebSocket,
   just define actions" layer — the exact role `python-elgato-streamdeck` plays
   at the device layer, but for OpenAction plugins — modeled on the community
   Python SDKs above.

## Field note — issue #85

The `oadesktopentry` plugin under discussion in issue #85
(`me.amankhanna.oadesktopentry`) is authored by **Aman Khanna**, who is also the
author of the **OpenAction API** (`openaction.amankhanna.me`). It is therefore a
first-party OpenAction plugin — concrete evidence that a contributor is already
building inside exactly the ecosystem this doc recommends standardizing on. Its
failure to load was a host-side discovery issue (`CodePaths` target-triple / `OS`
resolution), not an ecosystem mismatch.

## Sources

- <https://github.com/abcminiuser/python-elgato-streamdeck>
- <https://openaction.amankhanna.me/>
- <https://github.com/nekename/OpenDeck>
- <https://github.com/OpenActionAPI/rust>
- <https://github.com/OpenActionAPI/marketplace>
- <https://github.com/elgatosf/streamdeck>
- <https://github.com/strohganoff/python-streamdeck-plugin-sdk>
- <https://github.com/gri-gus/streamdeck-python-sdk>
