# Spike — Extracting the OpenDeck backend into a headless sidecar

> **Branch:** `spike/opendeck-backend-extraction` · **Date:** 2026-06-26 ·
> **Verdict:** 🟢 **GREEN — feasible with a mechanical refactor**
>
> Analysis only (read-only). No OpenDeck code was imported into the repo; the
> clone lived in the session scratchpad. This document is the deliverable.

## Spike question

> Can OpenDeck's plugin manager (`plugins/` + `events/`) be decoupled from Tauri
> and run headless, so we can drive it from a Qt UI instead of its Svelte
> frontend?

The answer hinges on a single measurement: **how much, and in what way, the
reusable code is coupled to Tauri.**

## Architectural context (confirmed)

OpenDeck's "Rust backend" is **not a separate crate or daemon**: it is code
compiled into the Tauri binary (`src-tauri/src/`). Upstream reference: commit
`af29a43` (shallow clone taken 2026-06-26).

`events/` splits into three concerns, and that split is the key to everything:

| Subtree            | What it is                                            | For us                       |
| ------------------ | ----------------------------------------------------- | ---------------------------- |
| `events/frontend/` | `#[tauri::command]` handlers → the `invoke()` surface | **DISCARD** (Qt replaces it) |
| `events/inbound/`  | plugin → host messages (Elgato WS protocol)           | **PORT**                     |
| `events/outbound/` | host → plugin messages (Elgato WS protocol)           | **PORT**                     |

## Tauri dependency count

Patterns searched: `tauri::`, `#[tauri::command]`, `AppHandle`, `WebviewWindow`,
`Manager`, `.emit()`, `APP_HANDLE`, `tauri_plugin`, `use tauri`.

### Whole backend

- **74** total Tauri references across all of `src-tauri/src/`.
- **31** of those are the single global pattern `APP_HANDLE`
  (`OnceLock<AppHandle>`, declared at `main.rs:30`). That is ~42% of the
  coupling concentrated in *one* indirection.

### Per concern (LOC and coupling references)

| Area                | LOC | Tauri refs          | Reuse?                                      |
| ------------------- | --- | ------------------- | ------------------------------------------- |
| `plugins/`          | 933 | 9 (all in `mod.rs`) | ✅ port                                     |
| `events/inbound/`   | 721 | 17                  | ✅ port                                     |
| `events/outbound/`  | 860 | 5                   | ✅ port                                     |
| `store/` (persist.) | 800 | **0**               | ✅ port verbatim                            |
| `events/frontend/`  | 890 | 30                  | ❌ discard (= UI control surface, Qt's job) |
| `elgato.rs` (wire)  | 299 | —                   | ❌ discard (mirajazz already does it)       |

**Reuse target (plugins + inbound + outbound + store) ≈ 3,314 LOC with just
31 coupling sites.** `manifest.rs` / `webserver.rs` / `info_param.rs` and all of
`store/` are at **zero** references → port verbatim.

## Nature of the coupling (the qualitative fact that decides it)

Nearly every site is `crate::APP_HANDLE.get().unwrap()` threaded into a function
that then does one of three things, each with a direct replacement in our
sidecar model:

1. **Emit to the UI** — e.g. `inbound/misc.rs`:
   `app.get_webview_window("main").unwrap().emit("show_alert", ctx)` → in the
   sidecar this becomes one JSON line on stdout (`{"event":"show_alert",...}`)
   already consumed by the Qt proxy. Events involved: `show_alert`, `show_ok`,
   `switch_profile`, `device_brightness`.
1. **Render + emit a key image** — `update_state(APP_HANDLE, ...)` in
   `states.rs`/`keypad.rs`/`encoder.rs`: the `AppHandle` is threaded in purely to
   reach the emit + rendering. Replaced by passing our own context.
1. **Path resolution** — `APP_HANDLE.path().resolve("plugins", Resource)`
   (`plugins/mod.rs:415`) → a path from config.

### Irreducible Tauri-specific sites (rewrite, not port)

Few and localised:

- `plugins/mod.rs:216` — `tauri::WebviewWindowBuilder` opens the **Property
  Inspector** window. In our architecture the PI is already handled by Qt + the
  WS bootstrap → we **discard** this site rather than port it.
- `plugins/mod.rs:454` — `APP_HANDLE.manage(tx)` stores a channel in Tauri's
  state container → a field on a struct of ours.
- `events/inbound/devices.rs:41` — `tauri_plugin_aptabase` (telemetry) → **drop**.

The Tauri crates in `Cargo.toml` (`tray-icon`, `autostart`, `dialog`,
`deep-link`, `single-instance`, `aptabase`, `log`) are **all app-shell
concerns**: none is needed by the plugin engine.

## Extraction strategy (one mechanical indirection)

Replace the global `static APP_HANDLE: OnceLock<AppHandle>` with
`static HOST: OnceLock<HostContext>`, where `HostContext` exposes:

- `emit(event: &str, payload: Value)` → writes one JSON line to stdout (existing
  sidecar protocol) instead of `webview.emit()`;
- `plugins_dir()` / paths from config instead of `BaseDirectory::Resource`;
- the `tx` channel as a struct field instead of `.manage()`.

Once that is done, `plugins/` + `events/inbound|outbound/` + `store/` compile
**without `tauri`** as a dependency. The PI window and telemetry are removed.

## Verdict

🟢 **GREEN.** The coupling is shallow and uniform (one pattern, 31 sites across
~3.3k reusable LOC), not structural. The extraction is a mechanical refactor
around a single indirection, NOT a rewrite.

### Comparison with the alternatives (for the decision)

- **Fork the whole OpenDeck app** (keep the Tauri monolith): rejected — perpetual
  maintenance cost, inherits the Svelte UI.
- **Extract the logic into our own Rust sidecar** (`streamdock-host` pattern):
  **recommended** — reuses OpenDeck's mature plugin engine (GPL ↔ GPL, license
  compatible), keeps the Qt UI, speaks the Elgato WS protocol we already
  implement.

### Caveat (boundary with our existing C++ host)

We already have an Elgato/OpenAction plugin host in C++ (Spec Kit 001). Porting
the OpenDeck engine creates **two plugin systems** until one is retired. The
decision — "OpenDeck-derived sidecar **replaces** the C++ host" vs. "port only
the missing pieces" — must be made before writing production code, not during.

## Reproducibility

```bash
git clone --depth 1 https://github.com/nekename/OpenDeck.git   # commit af29a43
cd OpenDeck/src-tauri/src
PAT='tauri::|#\[tauri::command\]|AppHandle|WebviewWindow|Manager|\.emit\(|APP_HANDLE|tauri_plugin|use tauri'
grep -rnE "$PAT" . | wc -l            # 74 total
grep -rn 'APP_HANDLE' . | wc -l       # 31 (the dominant pattern)
```
