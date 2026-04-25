# Plugin SDK

This document specifies the **public** plugin SDK for AJAZZ Control Center
(`acc`). It complements the [in-process Python host](PLUGIN-SYSTEM.md) —
which targets first-party scripting — by defining an **out-of-process,
language-agnostic** SDK aimed at third-party developers, the future
**Plugin Store**, and **OpenDeck / Stream Deck** ecosystem compatibility.

> Status: **draft / design**. The manifest schema lives at
> [`docs/schemas/plugin_manifest.schema.json`](../schemas/plugin_manifest.schema.json)
> (JSON Schema Draft 2020-12) and is the contract surface plugins must
> satisfy. Implementation tracking issues land under the
> [`area/plugin-sdk`](https://github.com/Aiacos/ajazz-control-center/labels/area%2Fplugin-sdk)
> label.

## Goals

1. **Drop-in compatibility with the Stream Deck SDK v2 manifest** so
   existing plugins (the largest pool of "deck" plugins in the wild) can
   be repackaged with only a thin shim.
2. **Drop-in compatibility with [OpenDeck](https://github.com/nekename/OpenDeck)
   plugins**, including its `CodePathLin` / `CodePaths` extensions for
   per-target Linux/macOS binaries.
3. **First-class native AJAZZ extensions** under an `Ajazz` namespace
   (sandboxing, signing, device-class scoping) without breaking strictness
   of the SDK-2 superset.
4. **Hard isolation by default**: every third-party plugin runs in its own
   subprocess under an OS-level sandbox; revoking a plugin's permissions
   never requires restarting `acc`.
5. **Stable transport**: plugins talk to `acc` over a documented
   WebSocket JSON protocol — the same wire shape Stream Deck uses — so
   third-party tooling, debug bridges and language ports stay simple.

## Non-goals

- We do **not** ship a closed app store; the catalogue is a signed JSON
  index hosted on GitHub Pages and is fully replaceable by the user.
- We do **not** auto-translate Stream Deck binaries; macOS `.app` /
  Windows `.exe` payloads from existing plugins still need their native
  binaries to run on the target OS. We provide a compatibility layer for
  the **manifest + protocol**, not for binaries.
- The in-process Python host is **not** going away — it remains the
  zero-friction path for power users writing personal automation; the
  out-of-process SDK is for redistributable plugins.

## Deployment shape

A plugin is a directory whose name ends in `.acplugin` (the
Stream Deck equivalent is `.sdPlugin`; both are accepted by the host):

```
spotify-now-playing.acplugin/
├── manifest.json                  ← validated against plugin_manifest.schema.json
├── icon@1x.png   icon@2x.png      ← 28x28 / 56x56, monochrome white-on-transparent
├── actions/
│   └── now-playing/
│       ├── icon@1x.png  icon@2x.png
│       └── property_inspector.html  ← optional UI, see PROPERTY-INSPECTOR
├── code/
│   ├── linux-x86_64/plugin        ← ELF, marked exec
│   ├── linux-aarch64/plugin
│   ├── darwin-arm64/plugin
│   ├── darwin-x86_64/plugin
│   └── windows-x86_64/plugin.exe
└── LICENSE
```

The `code/<target>/` layout matches OpenDeck's `CodePaths` and is
discovered by the host based on `runtime.platform.machine()` plus the
running OS. Manifests using the legacy Stream Deck `CodePath` /
`CodePathMac` / `CodePathWin` keys are also honoured (the Linux variant
is read from `CodePathLin`, OpenDeck's de-facto extension).

Plugins are distributed as zip archives renamed to
`<uuid>-<version>.acplugin.zip`. The Plugin Store ships a signed
catalogue index that lists download URL + sha256 + sigstore bundle for
each release.

## Discovery

`acc` scans the following directories at startup, in order:

| Order | Path                                                       | Purpose                                  |
| ----- | ---------------------------------------------------------- | ---------------------------------------- |
| 1     | `<XDG_CONFIG_HOME>/ajazz-control-center/plugins/`          | User-installed plugins (writable)        |
| 2     | `<XDG_DATA_HOME>/ajazz-control-center/plugins/`            | Plugin Store managed installs            |
| 3     | `${prefix}/share/ajazz-control-center/plugins/`            | System-wide / OEM-bundled                |
| 4     | `$ACC_PLUGIN_PATH` (colon-separated)                       | Developer override / CI                  |

Each candidate directory containing a valid `manifest.json` is loaded;
duplicate UUIDs resolve to the **highest semver**. Disabled plugins (per
user setting) are scanned but never spawned.

## Lifecycle

```
        ┌─────────────────────────────────────────────────────┐
        │                  PluginHost (acc)                   │
        │  ┌─────────────┐                ┌────────────────┐  │
        │  │  Catalogue  │                │  WS server     │  │
        │  │  scanner    │ spawn ────────▶│  ws://127.0.0.1:│  │
        │  └─────────────┘                │  <ephemeral>    │  │
        │         │                       └────────────────┘  │
        │         ▼                                ▲          │
        │  ┌─────────────┐  fork / exec            │          │
        │  │ Sandbox     │ ───────────────────────►│          │
        │  │ launcher    │                         │          │
        │  └─────────────┘                         │          │
        └──────────┬─────────────────────────────────────────-┘
                   ▼                                ▲
        ┌──────────────────────┐                    │
        │   plugin subprocess  │── WebSocket ──────-┘
        │ (any language)       │   (JSON, line-delimited)
        └──────────────────────┘
```

**Phases**

1. **Discovery** — manifest.json validated against the JSON Schema. Any
   error on a third-party plugin is non-fatal: the plugin is skipped and
   surfaced in the Settings → Plugins page with the failing JSON Pointer.
2. **Negotiation** — host reserves an ephemeral WS port on `127.0.0.1`,
   generates a per-launch random `pluginUUID` register token, and starts
   listening **before** spawning the plugin so the child cannot race past
   `connect`.
3. **Spawn** — sandbox launcher (see below) execs `code/<target>/plugin`
   with these flags, identical to Stream Deck's:
   ```
   -port <port> -pluginUUID <uuid> -registerEvent registerPlugin -info <json>
   ```
   `<info>` is a JSON blob containing host version, application info,
   device list (filtered to those the plugin's manifest is compatible
   with) and current language. The exact schema lives in
   [`docs/schemas/plugin_register_info.schema.json`](../schemas/plugin_register_info.schema.json)
   (TBD, mirrors Stream Deck's `info` payload).
4. **Registration** — within 5 s the plugin must send
   `{"event":"registerPlugin","uuid":"<token>"}`. Missing / wrong token
   ⇒ host disconnects, plugin marked broken.
5. **Steady state** — bidirectional events flow over the same socket
   (see [Wire protocol](#wire-protocol)).
6. **Shutdown** — host sends `applicationDidTerminate` and SIGTERM
   (`TerminateProcess` on Windows). After a 2 s grace period the
   subprocess is force-killed via SIGKILL / `TerminateProcess` with
   `-9` and its sandbox unit is torn down.

## Wire protocol

The transport is **line-delimited JSON over a single WebSocket** —
identical at the framing level to Stream Deck's plugin protocol. A
plugin written against `@elgato/streamdeck` should run unchanged once
its manifest is rewritten; a plugin written against
`openaction.openOpenDeckSocket(port, uuid, registerEvent, info)` runs
unchanged with no rewrite.

### Plugin → host events (subset)

| Event                       | Purpose                                                |
| --------------------------- | ------------------------------------------------------ |
| `registerPlugin`            | Initial handshake; presents the launch token.          |
| `registerPropertyInspector` | Sent by the embedded HTML PI when it opens.            |
| `setSettings`               | Persist per-instance settings.                         |
| `setGlobalSettings`         | Persist plugin-wide settings (encrypted at rest).      |
| `setTitle`                  | Update the on-key title.                               |
| `setImage`                  | Update the on-key image (base64 PNG/JPEG/SVG).         |
| `setState`                  | Switch a multi-state action's state.                   |
| `showOk` / `showAlert`      | Transient on-key feedback.                             |
| `openUrl`                   | Ask the host to open a URL in the user's browser.      |
| `logMessage`                | Forward a structured log line to `acc`'s log pipeline. |
| `sendToPropertyInspector`   | RPC to the open property inspector for this action.    |

### Host → plugin events (subset)

| Event                          | Purpose                                                 |
| ------------------------------ | ------------------------------------------------------- |
| `keyDown` / `keyUp`            | Hardware key event.                                     |
| `dialDown` / `dialUp` / `dialRotate` | Encoder events.                                   |
| `touchTap`                     | Touch-strip / display tap event (AKP-class devices).    |
| `willAppear` / `willDisappear` | Action becomes / leaves visible (folder navigation).    |
| `propertyInspectorDidAppear`   | PI was opened in the host UI.                           |
| `applicationDidLaunch` / `applicationDidTerminate` | OS process events for `ApplicationsToMonitor`. |
| `deviceDidConnect` / `deviceDidDisconnect` | Hot-plug events.                            |
| `systemDidWakeUp`              | OS wake-from-sleep notification.                        |
| `didReceiveSettings` / `didReceiveGlobalSettings` | Persistence read-backs.              |

The full event vocabulary will be enumerated in
[`docs/schemas/plugin_protocol.schema.json`](../schemas/plugin_protocol.schema.json)
(TBD). For now the source of truth is Stream Deck's published vocabulary
plus AJAZZ extensions namespaced under `ajazz/*` (e.g.
`ajazz/setBacklight`).

### Compatibility modes

`Ajazz.Compatibility` in the manifest selects how strictly the host
emulates a foreign protocol when speaking to the plugin:

| Mode         | Behaviour                                                                   |
| ------------ | --------------------------------------------------------------------------- |
| `native`     | Full vocabulary including `ajazz/*` extensions. Default for new plugins.    |
| `opendeck`   | OpenDeck-flavoured event names (`openContextMenu`, `getContextSettings` …). |
| `streamdeck` | Strict Stream Deck SDK-2 vocabulary; `ajazz/*` events are filtered out.     |

This lets us host the long tail of existing plugins **without** forcing
either ecosystem to converge on AJAZZ-specific event names.

## Sandboxing

Every third-party plugin runs as a separate OS process. The launcher
hardens that process per-platform; `Ajazz.Sandbox` and `Ajazz.Permissions`
in the manifest declare the privileges the plugin actually needs, and
the launcher refuses to grant anything not declared.

| Platform | Mechanism                                                                                                 |
| -------- | --------------------------------------------------------------------------------------------------------- |
| Linux    | `bwrap` (bubblewrap) with `--unshare-user --unshare-pid --unshare-ipc --unshare-uts`, RO `/usr` overlay,  |
|          | private `$HOME` mapped to a per-plugin data dir, `/dev` empty, `seccomp-bpf` allowlist. systemd-run scope |
|          | when available so resource limits and journal isolation are free.                                         |
| macOS    | `sandbox-exec` with a generated profile derived from declared `Permissions`. Hardened-runtime entitlements|
|          | optional (signed builds only).                                                                            |
| Windows  | AppContainer + restricted token via `CreateProcessAsUser`; named pipe / loopback TCP only network.        |
| Flatpak  | When `acc` itself runs inside Flatpak, plugins are spawned with `flatpak-spawn --sandbox` and inherit the |
|          | host's portal model. Network access requires a Flatpak hold via `--talk-name=org.freedesktop.NetworkManager`.|

The sandbox launcher and seccomp profile generator live in
`src/plugins/sandbox/` (TBD); the design document is
[SANDBOX.md](SANDBOX.md) (TBD).

### Permission model

`Ajazz.Permissions` is a closed enum set; declaring nothing means the
plugin is fully isolated (no network, no filesystem outside its private
dir, no global hotkeys, no clipboard). The user is asked to grant
permissions on first launch; revocation is live (the plugin is
restarted with the new policy).

| Permission        | Effect                                                              |
| ----------------- | ------------------------------------------------------------------- |
| `network`         | Outbound TCP/UDP to public IPs. No loopback to host services.       |
| `clipboard`       | Read/write the system clipboard via `acc`'s broker.                 |
| `filesystem-read` | RO access to user-selected paths (broker, never raw FS access).     |
| `filesystem-write`| RW access to user-selected paths (broker).                          |
| `hotkeys`         | Register global hotkeys via `acc`'s broker.                         |
| `notifications`   | Post desktop notifications (rate-limited to 1/s).                   |
| `audio-capture`   | Capture audio frames (PortAudio broker, opt-in per device).         |
| `video-capture`   | Capture camera frames (gated by OS camera consent).                 |

## Signing & supply chain

`Ajazz.Signing` declares a [Sigstore](https://www.sigstore.dev/) bundle
URL + Fulcio identity (e.g. a GitHub OIDC subject like
`https://github.com/<owner>/<repo>/.github/workflows/release.yml@refs/tags/v1.2.3`).
The Plugin Store's catalogue index pins both `sha256` and the Sigstore
bundle path; the host verifies the bundle against the declared identity
**before** unpacking the archive.

Unsigned plugins still install (after an explicit user opt-in) but are
flagged in the Settings page and never auto-update.

The full chain — Sigstore identity, Rekor inclusion proof, transparency
log entry — is documented in [SUPPLY-CHAIN.md](SUPPLY-CHAIN.md) (TBD).

## Plugin Store

The Store is a **read model** over a signed JSON catalogue:

```json
{
  "schemaVersion": 1,
  "updatedAt": "2026-04-25T12:00:00Z",
  "plugins": [
    {
      "uuid": "com.aiacos.spotify-now-playing",
      "name": "Spotify Now Playing",
      "version": "1.2.0",
      "author": "Aiacos",
      "description": "Show current track on a key.",
      "icon": "https://store.aiacos.dev/icons/spotify@2x.png",
      "downloadUrl": "https://store.aiacos.dev/plugins/com.aiacos.spotify-now-playing-1.2.0.acplugin.zip",
      "sha256": "...",
      "sigstoreBundle": "https://store.aiacos.dev/plugins/com.aiacos.spotify-now-playing-1.2.0.acplugin.zip.sigstore",
      "sigstoreIdentity": "https://github.com/Aiacos/spotify-now-playing/.github/workflows/release.yml@refs/tags/v1.2.0",
      "compatibility": "native",
      "supportedDevices": ["AKP153", "AKP153E", "AKP815"],
      "tags": ["streaming", "music"]
    }
  ]
}
```

The catalogue lives at
`https://store.aiacos.dev/catalogue/v1/index.json` (or any URL set in
`Settings → Plugins → Catalogue URL`) and is itself sigstore-signed by
the same identity that publishes `acc` releases. Mirrors are explicitly
supported: power users can host a private catalogue for an internal
plugin set.

The QML side renders the catalogue through `PluginCatalogModel`
(C++/Qt), with grid virtualisation, filter chips by category and device,
and an install / disable / update toggle column. The model exposes the
same shape whether it's reading the signed remote catalogue, a local
mirror or a mock fixture for development.

## Development workflow

1. `acc plugin scaffold --uuid com.acme.foo --lang ts` — generates a
   ready-to-build plugin directory with manifest, TypeScript bindings,
   sample action and a `vite` watcher.
2. `acc plugin run ./foo.acplugin` — sideloads + spawns the plugin in
   debug mode (no sandbox, all events traced to stderr, source-maps
   enabled).
3. `acc plugin pack ./foo.acplugin` — produces the signed
   `.acplugin.zip` ready to publish. CI helpers for GitHub Actions
   (`Aiacos/setup-acc-plugin@v1`) ship the same step.
4. `acc plugin lint ./foo.acplugin` — validates the manifest against
   `plugin_manifest.schema.json`, checks icons, runs a smoke
   registration against a headless host.

The CLI is `src/cli/` (binary `acc`); subcommands are stable from
`acc 1.0`.

## Compatibility matrix

| Plugin source                   | Manifest needs           | Code needs                | Status            |
| ------------------------------- | ------------------------ | ------------------------- | ----------------- |
| Stream Deck SDK-2 plugin        | rename `.sdPlugin` → `.acplugin`; add `Ajazz.Compatibility: streamdeck` | none      | Phase 1 (target)  |
| OpenDeck plugin                 | none (`CodePathLin` honoured) | none                      | Phase 1 (target)  |
| Native AJAZZ plugin             | as per schema            | as per `Ajazz.SupportedDevices` | Phase 1 (target)  |
| Stream Deck Mini SDK-1 plugin   | manual port              | manual port               | Out of scope      |

"Phase 1" lands behind the `--enable-plugin-sdk` feature flag in `acc`
1.x and graduates to default-on once the WS protocol is stable in 2.0.

## Open issues

- [ ] Per-target sigstore identities (one per CI job vs one per repo).
- [ ] How to surface live-revocation UX without disrupting active
      profiles.
- [ ] Property-inspector embedded HTML: QtWebEngine vs WebView2 vs
      WKWebView trade-offs.
- [ ] Catalogue mirror discovery (DNS TXT vs static URL list).
- [ ] Linux-only: do we ship a portable bwrap or rely on the system one?

## See also

- [PLUGIN-SYSTEM.md](PLUGIN-SYSTEM.md) — first-party in-process Python
  host (complementary, not replaced).
- [`docs/schemas/plugin_manifest.schema.json`](../schemas/plugin_manifest.schema.json)
  — authoritative manifest contract.
- [`docs/wiki/Plugin-Development.md`](../wiki/Plugin-Development.md) —
  user-facing tutorial.
- Stream Deck SDK reference:
  <https://docs.elgato.com/streamdeck/sdk/introduction/getting-started/>.
- OpenDeck:
  <https://github.com/nekename/OpenDeck>.
