# OpenDeck UI integration

Reimplement the OpenDeck control-center UX in this project, driving our backend
(mirajazz sidecar + Elgato-SDK plugin host + profile model), with **two
interchangeable UI implementations selected by a config file**.

| Doc                                        | Contents                                                                                                                                      |
| ------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------- |
| [`00-research.md`](00-research.md)         | Synthesis of the research: OpenDeck (UX/build/tokens), our backend singletons, Mirabox⇄AJAZZ OEM, MiraboxSpace repos & plugin-layer adoption. |
| [`01-contract.md`](01-contract.md)         | The IPC contract (35 commands + 10 events) and OpenDeck data types, mapped to our backend — the shared seam both UIs target.                  |
| [`02-architecture.md`](02-architecture.md) | Layering, the two implementations (`qml` / `webui`), and the **config-file / `ui.mode` settings design**.                                     |
| [`03-dev-plan.md`](03-dev-plan.md)         | Phased plan, the verification system (functional via debug-channel + visual via `screenshot` vs OpenDeck references), and the status log.     |

## TL;DR architecture

- **Core (unchanged):** mirajazz wire layer + our services/singletons.
- **Plugin layer:** Elgato-SDK host + Mirabox manifest extensions + Space /
  StreamDock-Plugins catalog (MiraboxSpace = plugin-layer core).
- **UI (config-selected):**
  - `ui.mode = qml` → native QML OpenDeck-style UI (cleaner, native; likely
    default).
  - `ui.mode = webui` → OpenDeck's real Svelte SPA embedded in QtWebEngine via a
    Tauri-compat shim + `OpenDeckBridge` (pixel-1:1, max reuse).
- Selection: env `AJAZZ_UI_MODE` → `ui.conf` (`[ui] mode=…`) → default `qml`.

## License

OpenDeck and the MiraboxSpace plugin repos we reuse are GPL-compatible
(GPL-3.0 / GPL-2.0 / MIT); this project is GPL-3.0-or-later. Vendored frontend
keeps its upstream `LICENSE.md` + a `NOTICE` recording the source commit. The
Mirabox **Device SDK** (closed `transport` blob) is **not** adopted — mirajazz
remains the open device core.
